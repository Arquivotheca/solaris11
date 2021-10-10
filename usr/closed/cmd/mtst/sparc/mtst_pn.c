/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_jg.h>
#include <sys/memtestio_pn.h>
#include "mtst.h"

/*
 * Panther specific routines located in this file.
 */
void	pn_init(mdata_t *);
int	pn_pre_test(mdata_t *);

/*
 * External global variables (declared in mtst.c).
 */
extern	cpu_info_t	*cip_arrayp;

/*
 * These Panther errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t panther_cmds[] = {

	/*
	 * Bus error(s).
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

	/*
	 * L2$ tag errors.  Index bits are tag[42:19] = PA[42:19],
	 * ECC bits are tag[14:6], then LRU, and State are 3-bits each.
	 */

	"kdthce",		do_k_err,		CHP_KD_ETHCE,
	MASK(0x7fffff8003full), BIT(30),
	MASK(0x1ff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data hw corrected L2-cache tag error.",

	"kithce",		do_k_err,		CHP_KI_ETHCE,
	MASK(0x7fffff8003full), BIT(31),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr hw corrected L2-cache tag error.",

	"udthce",		do_u_cp_err,		CHP_UD_ETHCE,
	MASK(0x7fffff8003full), BIT(32),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw corrected L2-cache tag error.",

	"uithce",		do_u_cp_err,		CHP_UI_ETHCE,
	MASK(0x7fffff8003full), BIT(33),
	MASK(0x1ff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr hw corrected L2-cache tag error.",

	"kdthue",		do_k_err,		CHP_KD_ETHUE,
	MASK(0x7fffff8003full), BIT(34)|BIT(33),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data hw uncorrectable L2-cache tag error.",

	"kithue",		do_k_err,		CHP_KI_ETHUE,
	MASK(0x7fffff8003full), BIT(35)|BIT(34),
	MASK(0x1ff),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr hw uncorrectable L2-cache tag error.",

	"udthue",		do_u_cp_err,		CHP_UD_ETHUE,
	MASK(0x7fffff8003full), BIT(36)|BIT(35),
	MASK(0x1ff),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw uncorrectable L2-cache tag error.",

	"uithue",		do_u_cp_err,		CHP_UI_ETHUE,
	MASK(0x7fffff8003full), BIT(37)|BIT(36),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr hw uncorrectable L2-cache tag error.",

	/*
	 * D$ errors.  Note that the 8-bit ecc on the data is later shifted
	 * so that a common dcache injection routine can be used (in
	 * memtest_chp.c file).
	 */

	"kddspel",		do_k_err,		CHP_KD_DDSPEL,
	MASK(ALL_BITS), 	BIT(0),
	MASK(0xff00), 		BIT(8),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable D-cache data parity error\n"
	"\t\t\tdue to a load.",

	"kddspeltl1",		do_k_err,		CHP_KD_DDSPELTL1,
	MASK(ALL_BITS), 	BIT(2),
	MASK(0xff00), 		BIT(10),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable D-cache data parity error\n"
	"\t\t\tdue to a load at trap level 1.",

	"kddspes",		do_notimp,		CHP_KD_DDSPES,
	MASK(ALL_BITS), 	BIT(1),
	MASK(0xff00), 		BIT(9),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable D-cache data parity error\n"
	"\t\t\tdue to a store.",

	"uddspel",		do_notimp,		CHP_UD_DDSPEL,
	MASK(ALL_BITS), 	BIT(3),
	MASK(0xff00), 		BIT(11),
	OFFSET(0), 		OFFSET(0),
	"Cause a user sw correctable D-cache data parity error\n"
	"\t\t\tdue to a load.",

	"uddspes",		do_notimp,		CHP_UD_DDSPES,
	MASK(ALL_BITS), 	BIT(4),
	MASK(0xff00), 		BIT(12),
	OFFSET(0), 		OFFSET(0),
	"Cause a user sw correctable D-cache data parity error\n"
	"\t\t\tdue to a store.",

	"kdtspel",		do_k_err,		CHP_KD_DTSPEL,
	MASK(0x3ffffffe), 	BIT(5),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable D-cache tag parity error\n"
	"\t\t\tdue to a load.",

	"kdtspeltl1",		do_k_err,		CHP_KD_DTSPELTL1,
	MASK(0x3ffffffe), 	BIT(7),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable D-cache tag parity error\n"
	"\t\t\tdue to a load at trap level 1.",

	"kdtspes",		do_notimp,		CHP_KD_DTSPES,
	MASK(0x3ffffffe), 	BIT(6),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable D-cache tag parity error\n"
	"\t\t\tdue to a store.",

	"udtspel",		do_notimp,		CHP_UD_DTSPEL,
	MASK(0x3ffffffe), 	BIT(8),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user sw correctable D-cache tag parity error\n"
	"\t\t\tdue to a load.",

	"udtspes",		do_notimp,		CHP_UD_DTSPES,
	MASK(0x3ffffffe), 	BIT(9),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user sw correctable D-cache tag parity error\n"
	"\t\t\tdue to a store.",

	"kdthpel",		do_k_err,		CHP_KD_DTHPEL,
	MASK(0x3ffffffe),	BIT(10),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel hw correctable D-cache tag parity error\n"
	"\t\t\tdue to a load.",

	"kdthpes",		do_notimp,		CHP_KD_DTHPES,
	MASK(0x3ffffffe),	BIT(11),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel hw correctable D-cache tag parity error\n"
	"\t\t\tdue to a store.",

	"udthpel",		do_notimp,		CHP_UD_DTHPEL,
	MASK(0x3ffffffe),	BIT(12),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user hw correctable D-cache tag parity error\n"
	"\t\t\tdue to a load.",

	"udthpes",		do_notimp,		CHP_UD_DTHPES,
	MASK(0x3ffffffe),	BIT(13),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user hw correctable D-cache tag parity error\n"
	"\t\t\tdue to a store.",

	"dtphys=addr,xor,data",	do_k_err,		G4U_DTPHYS,
	MASK(0x3ffffffe), 	BIT(14),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		NULL,
	"Insert an error into the D-cache tag at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.\n",

	/*
	 * I$ errors.
	 */

	"kidspe",		do_k_err,		CHP_KI_IDSPE,
	MASK(0xffffffff),	BIT(0),
	MASK(0x40000000000ull),	BIT(42),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable I-cache data parity error.",

	"kidspetl1",		do_k_err,		CHP_KI_IDSPETL1,
	MASK(0xffffffff),	BIT(1),
	MASK(0x40000000000ull),	BIT(42),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable I-cache data parity error\n"
	"\t\t\tat trap level 1.",

	"kidspepcr",		do_k_err,		CHP_KI_IDSPEPCR,
	MASK(0xfffff800),	BIT(11),
	MASK(0x40000000000ull),	BIT(42),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable I-cache data (pc-rel) parity error.",

	"uidspe",		do_notimp,		CHP_UI_IDSPE,
	MASK(0xffffffff),	BIT(2),
	MASK(0x40000000000ull),	BIT(42),
	OFFSET(0),		NULL,
	"Cause a user sw correctable I-cache data parity error.",

	"kitspe",		do_k_err,		CHP_KI_ITSPE,
	MASK(0x1fffffff00ull),	BIT(8),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable I-cache tag parity error.",

	"kitspetl1",		do_k_err,		CHP_KI_ITSPETL1,
	MASK(0x1fffffff00ull),	BIT(9),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable I-cache tag parity error\n"
	"\t\t\tat trap level 1.",

	"uitspe",		do_notimp,		CHP_UI_ITSPE,
	MASK(0x1fffffff00ull),	BIT(10),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a user sw correctable I-cache tag parity error.",

	"kithpe",		do_k_err,		CHP_KI_ITHPE,
	MASK(0x1fffffff00ull),	BIT(11),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a kernel hw correctable I-cache tag parity error.",

	"uithpe",		do_notimp,		CHP_UI_ITHPE,
	MASK(0x1fffffff00ull),	BIT(12),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a user hw correctable I-cache tag parity error.",

	"itphys=addr,xor,data",	do_k_err,		G4U_ITPHYS,
	MASK(0x1fffffff00ull), 	BIT(13),
	MASK(0x2000000000ull), 	BIT(37),
	NULL,			NULL,
	"Insert an error into the I-cache tag at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.",

	/*
	 * L2/L3$ Internal Processor errors.
	 */

	"l2_mh",		do_k_err,		PN_L2_MH,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a L2-cache multitag-way hit error (IERR).",

	"l2_ill_state",		do_k_err,		PN_L2_ILLSTATE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a L2-cache Illegal State error (IERR).",

	"l3_mh",		do_k_err,		PN_L3_MH,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a L3-cache multitag-way hit error (IERR).",

	"l3_ill_state",		do_k_err,		PN_L3_ILLSTATE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a L3-cache Illegal State error (IERR).",

	/*
	 * L3$ data (UCx) errors.
	 */

	"kdl3ucu",		do_k_err,		PN_KD_L3UCU,
	MASK(ALL_BITS),		BIT(18)|BIT(17),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data sw uncorrectable L3-cache error.",

	"copyinl3ucu",		do_k_err,		PN_KU_L3UCUCOPYIN,
	MASK(ALL_BITS),		BIT(19)|BIT(18),
	MASK(0x1ff),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern/user copyin uncorrectable L3-cache error.",

	"kdl3ucutl1",		do_k_err,		PN_KD_L3UCUTL1,
	MASK(ALL_BITS),		BIT(20)|BIT(19),
	MASK(0x1ff),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data uncorrectable L3-cache error at\n"
	"\t\t\ttrap level 1.",

	"kdl3oucu",		do_notimp,		PN_KD_L3OUCU,
	MASK(ALL_BITS),		BIT(21)|BIT(20),
	MASK(0x1ff),		BIT(4)|BIT(3),
	NULL,			NULL,
	"Cause a kern data sw uncorrectable L3-cache error\n"
	"\t\t\tthat is orphaned.",

	"kil3ucu",		do_k_err,		PN_KI_L3UCU,
	MASK(ALL_BITS),		BIT(22)|BIT(21),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr sw uncorrectable L3-cache error.",

	"kil3ucutl1",		do_k_err,		PN_KI_L3UCUTL1,
	MASK(ALL_BITS),		BIT(23)|BIT(22),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr uncorrectable L3-cache error\n"
	"\t\t\tat trap level 1.",

	"kil3oucu",		do_k_err,		PN_KI_L3OUCU,
	MASK(ALL_BITS),		BIT(24)|BIT(23),
	MASK(0x1ff),		BIT(7)|BIT(6),
	NULL,			NULL,
	"Cause a kern instr sw uncorrectable L3-cache error\n"
	"\t\t\tthat is orphaned.",

	"udl3ucu",		do_u_err,		PN_UD_L3UCU,
	MASK(ALL_BITS),		BIT(25)|BIT(24),
	MASK(0x1ff),		BIT(8)|BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a user data sw uncorrectable L3-cache error.",

	"uil3ucu",		do_u_err,		PN_UI_L3UCU,
	MASK(ALL_BITS),		BIT(26)|BIT(25),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr sw uncorrectable L3-cache error.",

	"obpdl3ucu",		do_k_err,		PN_OBPD_L3UCU,
	MASK(ALL_BITS),		BIT(27)|BIT(26),
	MASK(0x1ff),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an OBP data uncorrectable L3-cache error.",

	"kdl3ucc",		do_k_err,		PN_KD_L3UCC,
	MASK(ALL_BITS),		BIT(27),
	MASK(0x1ff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data sw correctable L3-cache error.",

	"copyinl3ucc",		do_k_err,		PN_KU_L3UCCCOPYIN,
	MASK(ALL_BITS),		BIT(28),
	MASK(0x1ff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern/user copyin correctable L3-cache error.",

	"kdl3ucctl1",		do_k_err,		PN_KD_L3UCCTL1,
	MASK(ALL_BITS), 	BIT(29),
	MASK(0x1ff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data sw correctable L3-cache error at\n"
	"\t\t\ttrap level 1.",

	"kdl3oucc",		do_notimp,		PN_KD_L3OUCC,
	MASK(ALL_BITS), 	BIT(30),
	MASK(0x1ff), 		BIT(6),
	NULL,			NULL,
	"Cause a kern data sw correctable L3-cache error\n"
	"\t\t\tthat is orphaned.",

	"kil3ucc",		do_k_err,		PN_KI_L3UCC,
	MASK(ALL_BITS), 	BIT(31),
	MASK(0x1ff), 		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr sw correctable L3-cache error.",

	"kil3ucctl1",		do_k_err,		PN_KI_L3UCCTL1,
	MASK(ALL_BITS), 	BIT(32),
	MASK(0x1ff), 		BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr correctable L3-cache error at\n"
	"\t\t\ttrap level 1.",

	"kil3oucc",		do_notimp,		PN_KI_L3OUCC,
	MASK(ALL_BITS),		BIT(33),
	MASK(0x1ff),		BIT(0),
	NULL,			NULL,
	"Cause a kern instr sw correctable L3-cache error\n"
	"\t\t\tthat is orphaned.",

	"udl3ucc",		do_u_err,		PN_UD_L3UCC,
	MASK(ALL_BITS),		BIT(34),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data sw correctable L3-cache error.",

	"uil3ucc",		do_u_err,		PN_UI_L3UCC,
	MASK(ALL_BITS),		BIT(35),
	MASK(0x1ff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr sw correctable L3-cache error.",

	"obpdl3ucc",		do_k_err,		PN_OBPD_L3UCC,
	MASK(ALL_BITS),		BIT(36),
	MASK(0x1ff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause an OBP data sw correctable L3-cache error.",

	/*
	 * L3$ block-load and store-merge errors.
	 */

	"kdl3edul",		do_k_err,		PN_KD_L3EDUL,
	MASK(ALL_BITS),		BIT(37)|BIT(36),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw uncorrectable L3-cache error due to\n"
	"\t\t\ta block load.",

	"kdl3edus",		do_k_err,		PN_KD_L3EDUS,
	MASK(ALL_BITS),		BIT(38)|BIT(37),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data hw uncorrectable L3-cache error due to\n"
	"\t\t\ta store merge.",

	"kdl3edupr",		do_k_err,		PN_KD_L3EDUPR,
	MASK(ALL_BITS),		BIT(38)|BIT(37),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw uncorrectable L3-cache error due to\n"
	"\t\t\ta prefetch.",

	"udl3edul",		do_u_err,		PN_UD_L3EDUL,
	MASK(ALL_BITS),		BIT(39)|BIT(38),
	MASK(0x1ff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw uncorrectable L3-cache error due to\n"
	"\t\t\ta block load.",

	"udl3edus",		do_u_err,		PN_UD_L3EDUS,
	MASK(ALL_BITS),		BIT(40)|BIT(39),
	MASK(0x1ff),		BIT(8)|BIT(7),
	OFFSET(8),		OFFSET(0),
	"Cause a user data hw uncorrectable L3-cache error due to\n"
	"\t\t\ta store merge.",

	"kdl3edcl",		do_k_err,		PN_KD_L3EDCL,
	MASK(ALL_BITS),		BIT(40),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw corrected L3-cache error due to\n"
	"\t\t\ta block load.",

	"kdl3edcs",		do_k_err,		PN_KD_L3EDCS,
	MASK(ALL_BITS),		BIT(41),
	MASK(0x1ff),		BIT(1),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data hw corrected L3-cache error due to\n"
	"\t\t\ta store merge.",

	"kdl3edcpr",		do_k_err,		PN_KD_L3EDCPR,
	MASK(ALL_BITS),		BIT(41),
	MASK(0x1ff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw corrected L3-cache error due to\n"
	"\t\t\ta prefetch.",

	"udl3edcl",		do_u_err,		PN_UD_L3EDCL,
	MASK(ALL_BITS),		BIT(42),
	MASK(0x1ff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw corrected L3-cache error due to\n"
	"\t\t\ta block load.",

	"udl3edcs",		do_u_err,		PN_UD_L3EDCS,
	MASK(ALL_BITS),		BIT(43),
	MASK(0x1ff),		BIT(4),
	OFFSET(8),		OFFSET(0),
	"Cause a user data hw corrected L3-cache error due to\n"
	"\t\t\ta store merge.",

	/*
	 * L3$ data and tag errors injected by address/offset/index.
	 */

	"l3phys=offset,xor",	do_k_err,		PN_L3PHYS,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1ff),		BIT(0),
	NULL,			NULL,
	"Insert an error into the L3-cache at byte\n"
	"\t\t\t\toffset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l3tphys=offset,xor",	do_k_err,		PN_L3TPHYS,
	MASK(0xfffff00003full),	BIT(30),
	MASK(0x1ff),		BIT(1),
	NULL,			NULL,
	"Insert an error into the L3-cache tag at byte\n"
	"\t\t\t\toffset \"offset\".\n"
	"\t\t\t\tThis may modify the cache line state.",

	/*
	 * L3$ write-back errors.
	 */

	"kdl3wdu",		do_k_err,		PN_KD_L3WDU,
	MASK(ALL_BITS),		BIT(44)|BIT(43),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable L3-cache\n"
	"\t\t\twrite-back error.",

	"kil3wdu",		do_k_err,		PN_KI_L3WDU,
	MASK(ALL_BITS),		BIT(45)|BIT(44),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable L3-cache\n"
	"\t\t\twrite-back error.",

	"udl3wdu",		do_ud_wb_err,		PN_UD_L3WDU,
	MASK(ALL_BITS),		BIT(46)|BIT(45),
	MASK(0x1ff),		BIT(7)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a user data uncorrectable L3-cache\n"
	"\t\t\twrite-back error.",

	"uil3wdu",		do_notimp,		PN_UI_L3WDU,
	MASK(ALL_BITS),		BIT(47)|BIT(46),
	MASK(0x1ff),		BIT(8)|BIT(7),
	NULL,			NULL,
	"Cause a user instr uncorrectable L3-cache\n"
	"\t\t\twrite-back error.",

	"kdl3wdc",		do_k_err,		PN_KD_L3WDC,
	MASK(ALL_BITS),		BIT(47),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		NULL,
	"Cause a kern data correctable L3-cache\n"
	"\t\t\twrite-back error.",

	"kil3wdc",		do_k_err,		PN_KI_L3WDC,
	MASK(ALL_BITS),		BIT(48),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable L3-cache\n"
	"\t\t\twrite-back error.",

	"udl3wdc",		do_ud_wb_err,		PN_UD_L3WDC,
	MASK(ALL_BITS), 	BIT(49),
	MASK(0x1ff), 		BIT(2),
	OFFSET(0),		NULL,
	"Cause a user data correctable L3-cache\n"
	"\t\t\twrite-back error.",

	"uil3wdc",		do_notimp,		PN_UI_L3WDC,
	MASK(ALL_BITS), 	BIT(50),
	MASK(0x1ff), 		BIT(3),
	NULL,			NULL,
	"Cause a user instr correctable L3-cache\n"
	"\t\t\twrite-back error.",

	/*
	 * L3$ copy-back errors.
	 */

	"kdl3cpu",		do_k_err,		PN_KD_L3CPU,
	MASK(ALL_BITS),		BIT(51)|BIT(50),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data uncorrectable L3-cache\n"
	"\t\t\tcopy-back error.",

	"kil3cpu",		do_k_err,		PN_KI_L3CPU,
	MASK(ALL_BITS),		BIT(52)|BIT(51),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable L3-cache\n"
	"\t\t\tcopy-back error.",

	"udl3cpu",		do_u_cp_err,		PN_UD_L3CPU,
	MASK(ALL_BITS),		BIT(53)|BIT(52),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a user data uncorrectable L3-cache\n"
	"\t\t\tcopy-back error.",

	"uil3cpu",		do_notimp,		PN_UI_L3CPU,
	MASK(ALL_BITS),		BIT(54)|BIT(53),
	MASK(0x1ff),		BIT(7)|BIT(6),
	NULL,			NULL,
	"Cause a user instr uncorrectable L3-cache\n"
	"\t\t\tcopy-back error.",

	"kdl3cpc",		do_k_err,		PN_KD_L3CPC,
	MASK(ALL_BITS),		BIT(54),
	MASK(0x1ff),		BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable L3-cache copy-back error.",

	"kil3cpc",		do_k_err,		PN_KI_L3CPC,
	MASK(ALL_BITS),		BIT(55),
	MASK(0x1ff), 		BIT(0),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable L3-cache copy-back error.",

	"udl3cpc",		do_u_cp_err,		PN_UD_L3CPC,
	MASK(ALL_BITS), 	BIT(56),
	MASK(0x1ff), 		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data correctable L3-cache copy-back error.",

	"uil3cpc",		do_notimp,		PN_UI_L3CPC,
	MASK(ALL_BITS), 	BIT(57),
	MASK(0x1ff), 		BIT(2),
	NULL,			NULL,
	"Cause a user instr correctable L3-cache copy-back error.",

	/*
	 * L3$ tag errors.
	 * Note: Panther TSxE commands like TSCE, TSUE are covered
	 * by the THxE cases.
	 */

	"kdl3thce",		do_k_err,		PN_KD_L3ETHCE,
	MASK(0xfffff00003full),	BIT(30),
	MASK(0x1ff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data hw corrected L3-cache tag error.",

	"kil3thce",		do_k_err,		PN_KI_L3ETHCE,
	MASK(0xfffff00003full),	BIT(31),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr hw corrected L3-cache tag error.",

	"udl3thce",		do_u_cp_err,		PN_UD_L3ETHCE,
	MASK(0xfffff00003full),	BIT(32),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw corrected L3-cache tag error.",

	"uil3thce",		do_u_cp_err,		PN_UI_L3ETHCE,
	MASK(0xfffff00003full),	BIT(33),
	MASK(0x1ff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr hw corrected L3-cache tag error.",

	"kdl3thue",		do_k_err,		PN_KD_L3ETHUE,
	MASK(0xfffff00003full),	BIT(34)|BIT(33),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data hw uncorrectable L3-cache\n"
	"\t\t\ttag error.",

	"kil3thue",		do_k_err,		PN_KI_L3ETHUE,
	MASK(0xfffff00003full),	BIT(35)|BIT(34),
	MASK(0x1ff),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr hw uncorrectable L3-cache\n"
	"\t\t\ttag error.",

	"udl3thue",		do_u_cp_err,		PN_UD_L3ETHUE,
	MASK(0xfffff00003full),	BIT(36)|BIT(35),
	MASK(0x1ff),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw uncorrectable L3-cache tag error.",

	"uil3thue",		do_u_cp_err,		PN_UI_L3ETHUE,
	MASK(0xfffff00003full),	BIT(37)|BIT(36),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr hw uncorrectable L3-cache tag error.",

	/*
	 * L2/L3$ data and tag sticky errors.
	 */

	"kdedcsticky",		do_k_err,		PN_KD_EDC_STKY,
	MASK(ALL_BITS),		BIT(40),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a sticky kern data hw corrected L2-cache\n"
	"\t\t\terror due to a block load.",

	"kdl3edcsticky",	do_k_err,		PN_KD_L3EDC_STKY,
	MASK(ALL_BITS),		BIT(41),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a sticky kern data hw corrected L3-cache\n"
	"\t\t\terror due to a block load.",

	"kdthcesticky",		do_k_err,		PN_KD_THCE_STKY,
	MASK(0x7fffff8003full), BIT(30),
	MASK(0x1ff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a sticky kernel data hw corrected L2-cache\n"
	"\t\t\ttag error.",

	"kdl3thcesticky",	do_k_err,		PN_KD_L3THCE_STKY,
	MASK(0xfffff00003full),	BIT(31),
	MASK(0x1ff),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a sticky kernel data hw corrected L3-cache\n"
	"\t\t\ttag error.",

	/*
	 * {I-D}TLB errors.
	 */

	"kddtlbp",		do_k_err,		PN_KD_TLB,
	MASK(ALL_BITS),		BIT(47),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kernel data D-TLB parity error.",

	"kddtlbptl1",		do_k_err,		PN_KD_TLBTL1,
	MASK(ALL_BITS),		BIT(47),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kernel data D-TLB parity error\n"
	"\t\t\tat trap level 1.",

	"kditlbp",		do_k_err,		PN_KI_TLB,
	MASK(ALL_BITS),		BIT(47),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kernel instruction I-TLB parity error.",

	"uddtlbp",		do_u_err,		PN_UD_TLB,
	MASK(ALL_BITS),		BIT(47),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a user data D-TLB parity error.",

	"uditlbp",		do_u_err,		PN_UI_TLB,
	MASK(ALL_BITS),		BIT(47),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a user instruction I-TLB parity error.",

	/*
	 * P$(P-Cache) errors.
	 */

	"kdpcp",		do_k_err,		PN_KD_PC,
	MASK(ALL_BITS),		BIT(57),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kernel data P-Cache parity error.",

	/*
	 * IPB(Instr. Prefetch Buffer) errors.
	 */

	"kipbp",		do_k_err,		PN_KI_IPB,
	MASK(ALL_BITS),		BIT(7),
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kernel data I-Cache Prefetch parity error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

/*
 * Note that the Cheetah+ command set (cheetahp_cmds) is not included
 * below because the commands are all copied above in the Panther list.
 * This is due to the L2$ having different size and ASI access layout
 * which affects the data and check-bit masks in the command list.
 */
static cmd_t *pn_commands[] = {
	panther_cmds,
	jaguar_cmds,
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static  opsvec_t operations = {
	gen_flushall_l2,		/* flush entire L2$/L3$ */
	pn_pre_test,			/* pre-test routine */
	NULL				/* no post-test routine */
};

/*
 * Note that this routine (and the other cpu specific xx_init() routines)
 * is called by cpu_init() and run AFTER the cpu specific pre_test routines
 * (such as the one below) as well as by the init() routine near the
 * start of proceedings.
 *
 * The code that checks if the primary thread is bound to a suitable core
 * is only run the second time this routine is called (from the init_thread()
 * routine) by checking that the thread is not already bound.  If the binding
 * code run before this routine is changed, it could break the thread
 * handling code here.
 */
void
pn_init(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	system_info_t	*sip = mdatap->m_sip;
	cpu_info_t	*cip = mdatap->m_cip;
	uint_t		cache_way_mask;
	int		cpuid = cip->c_cpuid;
	int		i, found = 0;
	int		core_id = mdatap->m_cip->c_core_id;
	char		str[40];
	char		*fname = "pn_init";

	/*
	 * Provide pointers to the comamnd and ops structs.
	 */
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = pn_commands;

	/*
	 * Since each core can only allocate in specific ways in the
	 * L2 and L3 caches (when in split cache mode), ensure the
	 * primary thread is bound to a cpu that will use the requested
	 * way if one was specified by the user.
	 *	core0 -> ways 0 and 1
	 *	core1 -> ways 2 and 3
	 */
	if ((mdatap->m_bound == 0) && F_CACHE_WAY(iocp)) {

		/*
		 * First check that the requested way is valid, note that both
		 * L2 and L3 in Panther have the same associativity (4-way).
		 */
		cache_way_mask = cip->c_l2_assoc - 1;
		if (iocp->ioc_cache_way >= cip->c_l2_assoc) {
			msg(MSG_WARN, "%s: specified cache way is "
			    "out of range, masking it to %d\n", fname,
			    iocp->ioc_cache_way & cache_way_mask);
			iocp->ioc_cache_way &= cache_way_mask;
		}

		/*
		 * If the current binding is on a cpu that uses the correct
		 * ways in the cache, then there is no work to do. Otherwise
		 * must find a suitable cpuid to bind to.
		 */
		if ((core_id << 1) != (iocp->ioc_cache_way & 2)) {
			/*
			 * Check if the user specified binding criteria,
			 * if so we must try to honour both it and the
			 * requested way.
			 *
			 * Because the choose_thr_cpu() routine marks all
			 * found cpus as not usable again, we can call it
			 * multiple times until we get a cpu that matches
			 * the criteria or until we run out of cpus.
			 */
			if (iocp->ioc_bind_thr_criteria[mdatap->m_threadno]
			    != NULL) {

				/*
				 * Looking for a cpu this way is inefficient
				 * but that's ok for this rarely run case.
				 */
				for (i = 0; (i < sip->s_ncpus_online) && !found;
				    i++) {
					if (choose_thr_cpu(mdatap) == -1) {
						/*
						 * Break to the found check.
						 */
						break;
					}

					/*
					 * Check if the chosen cpu's core_id
					 * is compatible with requested way.
					 */
					if ((mdatap->m_cip->c_core_id << 1) ==
					    (iocp->ioc_cache_way & 2)) {
						found++;
					}
				}
			} else {
				/*
				 * Search through the cpu list for one with
				 * a core_id that corresponds to the cache way.
				 */
				for (i = 0;
				    (i < sip->s_ncpus_online) && !found; i++) {
					cip = (cip_arrayp + i);
					(void) snprintf(str, sizeof (str),
					    "%s[i=%d,cpuid=%d]", fname,
					    i, cip->c_cpuid);

					/*
					 * If this cpu has already been chosen,
					 * skip it, otherwise check it.
					 */
					if (cip->c_already_chosen != 0) {
						msg(MSG_DEBUG3, "%s: cpu is "
						    "already chosen, "
						    "skipping\n", str);
						continue;
					}

					/*
					 * Check if core_id works with way.
					 */
					if ((cip->c_core_id << 1) ==
					    (iocp->ioc_cache_way & 2)) {
						found++;
					}
				}
			}

			if (found) {
				/*
				 * Indicate that this cpu has been chosen and
				 * should not be chosen again.
				 */
				cip->c_already_chosen = 1;
				cpuid = cip->c_cpuid;
			} else {
				msg(MSG_ERROR, "%s: could not find a "
				    "cpu that matches binding criteria "
				    "and cache way %d!\n", fname,
				    iocp->ioc_cache_way);
				msg(MSG_ERROR, "%s: Please re-run command "
				    "with different binding "
				    "and/or way options\n", fname);
				/*
				 * Set the struct pointers to NULL so the
				 * caller will fail/exit.
				 */
				mdatap->m_opvp = NULL;
				mdatap->m_cmdpp = NULL;
			}

			/*
			 * Update the structs with the found cpu info,
			 * note that the thread is bound after this routine.
			 */
			cip->c_cpuid = cpuid;
			mdatap->m_cip = cip;
			iocp->ioc_thr2cpu_binding[mdatap->m_threadno] = cpuid;
		}
	}
	msg(MSG_DEBUG2, "%s: init finished with primary thread to be "
	    "bound to cpuid=%d\n", fname, cpuid);
}

int
pn_pre_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;

	switch (IOC_COMMAND(iocp)) {
		case CHP_UD_ETHCE:
		case CHP_UI_ETHCE:
		case CHP_UD_ETHUE:
		case CHP_UI_ETHUE:
		case CHP_KD_DTHPEL:
		case PN_UD_L3ETHCE:
		case PN_UI_L3ETHCE:
		case PN_UD_L3ETHUE:
		case PN_UI_L3ETHUE:
			iocp->ioc_nthreads = 2;
			break;
		default:
			break;
	}

	return (0);
}
