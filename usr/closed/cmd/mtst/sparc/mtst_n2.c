/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains sun4v Niagara-II (US-T2) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtestio_n2.h>
#include "mtst.h"

/*
 * Niagara-II specific routines located in this file, and externs.
 */
	void	n2_init(mdata_t *);
extern	int	ni_pre_test(mdata_t *);

/*
 * These Niagara-II errors are grouped according to the definitions
 * in the Niagara-I and II header files.
 *
 * Note that ALL the tests available for Niagara-II are listed in this
 * file including the ones that exist for Niagara-I.  This way similar
 * tests can be given new command names (to match PRM), without having
 * all the Niagara-I tests listed as available since not all are applicable.
 *
 * NOTE: could add the concept of common NI tests, maybe with
 *	 a cputype field defined like: GNI = (NI1 | N12), but that
 *	 means the cputype will have to be changed to a bitfield...
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t niagara2_cmds[] = {

	/*
	 * Memory (DRAM) uncorrectable errors.
	 */
	"hddau",		do_k_err,		NI_HD_DAU,
	MASK(0xffff),		BIT(15)|BIT(0),
	MASK(0xffff),		BIT(15)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory uncorrectable ecc error.",

	"hddauma",		do_k_err,		NI_HD_DAUMA,
	MASK(0xffff),		BIT(14)|BIT(1),
	MASK(0xffff),		BIT(14)|BIT(1),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hddaucwq",		do_k_err,		N2_HD_DAUCWQ,
	MASK(0xffff),		BIT(13)|BIT(2),
	MASK(0xffff),		BIT(13)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta control word queue operation.",

	"hidau",		do_k_err,		NI_HI_DAU,
	MASK(0xffff),		BIT(10)|BIT(5),
	MASK(0xffff),		BIT(10)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instr memory uncorrectable ecc error.",

	"kddau",		do_k_err,		NI_KD_DAU,
	MASK(0xffff),		BIT(12)|BIT(3),
	MASK(0xffff),		BIT(12)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable ecc error.",

	"kddaudtlb",		do_k_err,		N2_KD_DAUDTLB,
	MASK(0xffff),		BIT(11)|BIT(4),
	MASK(0xffff),		BIT(11)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable memory error via\n"
	"\t\t\ta data TLB fill operation.",

	"kidauitlb",		do_k_err,		N2_KI_DAUITLB,
	MASK(0xffff),		BIT(10)|BIT(5),
	MASK(0xffff),		BIT(10)|BIT(5),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable memory error via\n"
	"\t\t\tan instr TLB fill operation.",

	"kddautl1",		do_k_err,		NI_KD_DAUTL1,
	MASK(0xffff),		BIT(9)|BIT(6),
	MASK(0xffff),		BIT(9)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"kddaupr",		do_k_err,		NI_KD_DAUPR,
	MASK(0xffff),		BIT(8)|BIT(7),
	MASK(0xffff),		BIT(8)|BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data uncorrectable memory error via\n"
	"\t\t\ta prefetch.",

	"kidau",		do_k_err,		NI_KI_DAU,
	MASK(0xffff),		BIT(7)|BIT(8),
	MASK(0xffff),		BIT(7)|BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr uncorrectable memory error.",

	"kidautl1",		do_k_err,		NI_KI_DAUTL1,
	MASK(0xffff),		BIT(6)|BIT(9),
	MASK(0xffff),		BIT(6)|BIT(9),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"uddau",		do_u_err,		NI_UD_DAU,
	MASK(0xffff),		BIT(5)|BIT(10),
	MASK(0xffff),		BIT(5)|BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a user data uncorrectable memory error.",

	"uidau",		do_u_err,		NI_UI_DAU,
	MASK(0xffff),		BIT(4)|BIT(11),
	MASK(0xffff),		BIT(4)|BIT(11),
	OFFSET(0),		NULL,
	"Cause a user instr uncorrectable memory error.",

	"kddsu",		do_k_err,		NI_KD_DSU,
	MASK(0xffff),		BIT(3)|BIT(12),
	MASK(0xffff),		BIT(3)|BIT(12),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable ecc error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"kddbu",		do_k_err,		NI_KD_DBU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable out of range error.",

	"iodru",		do_io_err,		NI_IO_DRU,
	MASK(0xffff),		BIT(2)|BIT(13),
	MASK(0xffff),		BIT(2)|BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) uncorrectable memory error.",

	/*
	 * Memory (DRAM) correctable errors.
	 */
	"hddac",		do_k_err,		NI_HD_DAC,
	MASK(0xffff),		BIT(2)|BIT(1),
	MASK(0xffff),		BIT(9),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory correctable ecc error.",

	"hddacma",		do_k_err,		NI_HD_DACMA,
	MASK(0xffff),		BIT(6),
	MASK(0xffff),		BIT(11),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hddaccwq",		do_k_err,		N2_HD_DACCWQ,
	MASK(0xffff),		BIT(7),
	MASK(0xffff),		BIT(11),
	OFFSET(0),		NULL,
	"Cause a hyper data correctable memory error via\n"
	"\t\t\ta control word queue operation.",

	"hidac",		do_k_err,		NI_HI_DAC,
	MASK(0xffff),		BIT(1)|BIT(0),
	MASK(0xffff),		BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instr memory correctable ecc error.",

	"kddac",		do_k_err,		NI_KD_DAC,
	MASK(0xffff),		BIT(3)|BIT(2),
	MASK(0xffff),		BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory correctable ecc error.",

	"kddacdtlb",		do_k_err,		N2_KD_DACDTLB,
	MASK(0xffff),		BIT(8),
	MASK(0xffff),		BIT(12),
	OFFSET(0),		NULL,
	"Cause a kern data correctable memory error via\n"
	"\t\t\ta data TLB fill operation.",

	"kidacitlb",		do_k_err,		N2_KI_DACITLB,
	MASK(0xffff),		BIT(9),
	MASK(0xffff),		BIT(13),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable memory error via\n"
	"\t\t\tan instr TLB fill operation.",

	"kddactl1",		do_k_err,		NI_KD_DACTL1,
	MASK(0xffff),		BIT(8),
	MASK(0xffff),		BIT(12),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"kddacpr",		do_k_err,		NI_KD_DACPR,
	MASK(0xffff),		BIT(9),
	MASK(0xffff),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error via\n"
	"\t\t\ta prefetch.",

	"kddacstorm",		do_k_err,		NI_KD_DACSTORM,
	MASK(0xffff),		BIT(10),
	MASK(0xffff),		BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error storm\n"
	"\t\t\t(use misc1 for count, default is 64 errors).",

	"kidac",		do_k_err,		NI_KI_DAC,
	MASK(0xffff),		BIT(11),
	MASK(0xffff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr correctable memory error.",

	"kidactl1",		do_k_err,		NI_KI_DACTL1,
	MASK(0xffff),		BIT(12),
	MASK(0xffff),		BIT(2),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"uddac",		do_u_err,		NI_UD_DAC,
	MASK(0xffff),		BIT(13),
	MASK(0xffff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user data correctable memory error.",

	"uidac",		do_u_err,		NI_UI_DAC,
	MASK(0xffff),		BIT(14),
	MASK(0xffff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr correctable memory error.",

	"kddsc",		do_k_err,		NI_KD_DSC,
	MASK(0xffff),		BIT(15),
	MASK(0xffff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory correctable ecc error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"iodrc",		do_io_err,		NI_IO_DRC,
	MASK(0xffff),		BIT(13),
	MASK(0xffff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) correctable memory error.",

	/*
	 * L2 cache data uncorrectable errors.
	 */
	"hdldau",		do_k_err,		NI_HD_LDAU,
	MASK(ALL_BITS),		BIT(30)|BIT(21),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data uncorrectable error.",

	"hdldauma",		do_k_err,		NI_HD_LDAUMA,
	MASK(ALL_BITS),		BIT(29)|BIT(20),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache data uncorrectable error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdldaucwq",		do_k_err,		N2_HD_LDAUCWQ,
	MASK(ALL_BITS),		BIT(28)|BIT(19),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache data uncorrectable error via\n"
	"\t\t\ta control word queue operation.",

	"hildau",		do_k_err,		NI_HI_LDAU,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instr uncorrectable error.",

	"kdldau",		do_k_err,		NI_KD_LDAU,
	MASK(ALL_BITS),		BIT(27)|BIT(18),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error.",

	"kdldaudtlb",		do_k_err,		N2_KD_LDAUDTLB,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\ta data TLB fill operation.",

	"kildauitlb",		do_k_err,		N2_KI_LDAUITLB,
	MASK(ALL_BITS),		BIT(27)|BIT(18),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache instr uncorrectable error via\n"
	"\t\t\tan instr TLB fill operation.",

	"copyinldau",		do_k_err,		NI_LDAUCOPYIN,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern/user L2 cache data uncorrectable error\n"
	"\t\t\tvia copyin.",

	"kdldautl1",		do_k_err,		NI_KD_LDAUTL1,
	MASK(ALL_BITS),		BIT(25)|BIT(16),
	MASK(0x7f),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error at\n"
	"\t\t\ttrap level 1.",

	"kdldaupr",		do_k_err,		NI_KD_LDAUPR,
	MASK(ALL_BITS),		BIT(24)|BIT(15),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\ta prefetch.",

	"hdldaupri",		do_k_err,		N2_HD_LDAUPRI,
	MASK(ALL_BITS),		BIT(23)|BIT(14),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\ta prefetch ICE.",

	"obpldau",		do_k_err,		NI_OBP_LDAU,
	MASK(ALL_BITS),		BIT(22)|BIT(13),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\tan OBP access.",

	"kildau",		do_k_err,		NI_KI_LDAU,
	MASK(ALL_BITS),		BIT(21)|BIT(12),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data uncorrectable\n"
	"\t\t\terror.",

	"kildautl1",		do_k_err,		NI_KI_LDAUTL1,
	MASK(ALL_BITS),		BIT(20)|BIT(11),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data uncorrectable\n"
	"\t\t\terror at trap level 1.",

	"udldau",		do_u_err,		NI_UD_LDAU,
	MASK(ALL_BITS),		BIT(19)|BIT(10),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache data uncorrectable error.",

	"uildau",		do_u_err,		NI_UI_LDAU,
	MASK(ALL_BITS),		BIT(18)|BIT(9),
	MASK(0x7f),		BIT(0)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction data uncorrectable\n"
	"\t\t\terror.",

	"kdldsu",		do_k_err,		NI_KD_LDSU,
	MASK(ALL_BITS),		BIT(17)|BIT(8),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"ioldru",		do_io_err,		NI_IO_LDRU,
	MASK(ALL_BITS),		BIT(16)|BIT(7),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) L2 cache uncorrectable error.",

	/*
	 * L2 cache data and tag correctable errors.
	 */
	"hdldac",		do_k_err,		NI_HD_LDAC,
	MASK(ALL_BITS),		BIT(31),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data correctable error.",

	"hdldacma",		do_k_err,		NI_HD_LDACMA,
	MASK(ALL_BITS),		BIT(30),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache data correctable error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdldaccwq",		do_k_err,		N2_HD_LDACCWQ,
	MASK(ALL_BITS),		BIT(29),
	MASK(0x7f),		BIT(6),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache data correctable error via\n"
	"\t\t\ta control word queue operation.",

	"hildac",		do_k_err,		NI_HI_LDAC,
	MASK(ALL_BITS),		BIT(33),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instr correctable error.",

	"kdldac",		do_k_err,		NI_KD_LDAC,
	MASK(ALL_BITS),		BIT(28),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error.",

	"kdldacdtlb",		do_k_err,		N2_KD_LDACDTLB,
	MASK(ALL_BITS),		BIT(27),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache data correctable error via\n"
	"\t\t\ta data TLB fill operation.",

	"kildacitlb",		do_k_err,		N2_KI_LDACITLB,
	MASK(ALL_BITS),		BIT(26),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache instr correctable error via\n"
	"\t\t\tan instr TLB fill operation.",

	"copyinldac",		do_k_err,		NI_LDACCOPYIN,
	MASK(ALL_BITS),		BIT(25),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern/user L2 cache data correctable error\n"
	"\t\t\tvia copyin.",

	"kdldactl1",		do_k_err,		NI_KD_LDACTL1,
	MASK(ALL_BITS),		BIT(24),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error at\n"
	"\t\t\ttrap level 1.",

	"kdldacpr",		do_k_err,		NI_KD_LDACPR,
	MASK(ALL_BITS),		BIT(23),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error via\n"
	"\t\t\ta prefetch.",

	"hdldacpri",		do_k_err,		N2_HD_LDACPRI,
	MASK(ALL_BITS),		BIT(22),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error via\n"
	"\t\t\ta prefetch ICE.",

	"obpldac",		do_k_err,		NI_OBP_LDAC,
	MASK(ALL_BITS),		BIT(21),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error via\n"
	"\t\t\tan OBP access.",

	"kildac",		do_k_err,		NI_KI_LDAC,
	MASK(ALL_BITS),		BIT(20),
	MASK(0x7f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data correctable\n"
	"\t\t\terror.",

	"kildactl1",		do_k_err,		NI_KI_LDACTL1,
	MASK(ALL_BITS),		BIT(19),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data correctable\n"
	"\t\t\terror at trap level 1.",

	"udldac",		do_u_err,		NI_UD_LDAC,
	MASK(ALL_BITS),		BIT(18),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache data correctable error.",

	"uildac",		do_u_err,		NI_UI_LDAC,
	MASK(ALL_BITS),		BIT(17),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction data correctable error.",

	"kdldsc",		do_k_err,		NI_KD_LDSC,
	MASK(ALL_BITS),		BIT(16),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"ioldrc",		do_io_err,		NI_IO_LDRC,
	MASK(ALL_BITS),		BIT(15),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) L2 cache correctable error.",

	/* tag - ce only */
	"hdldtc",		do_k_err,		NI_HD_LDTC,
	MASK(0x3fffff),		BIT(15),
	MASK(0x3f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache tag correctable error.",

	"hildtc",		do_k_err,		NI_HI_LDTC,
	MASK(0x3fffff),		BIT(14),
	MASK(0x3f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction tag correctable error.",

	"kdldtc",		do_k_err,		NI_KD_LDTC,
	MASK(0x3fffff),		BIT(13),
	MASK(0x3f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag correctable error.",

	"kdldtctl1",		do_k_err,		NI_KD_LDTCTL1,
	MASK(0x3fffff),		BIT(12),
	MASK(0x3f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag correctable error\n"
	"\t\t\tat trap level 1.",

	"kildtc",		do_k_err,		NI_KI_LDTC,
	MASK(0x3fffff),		BIT(11),
	MASK(0x3f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag correctable error.",

	"kildtctl1",		do_k_err,		NI_KI_LDTCTL1,
	MASK(0x3fffff),		BIT(10),
	MASK(0x3f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag correctable error\n"
	"\t\t\tat trap level 1.",

	"udldtc",		do_u_err,		NI_UD_LDTC,
	MASK(0x3fffff),		BIT(9),
	MASK(0x3f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache tag correctable error.",

	"uildtc",		do_u_err,		NI_UI_LDTC,
	MASK(0x3fffff),		BIT(8),
	MASK(0x3f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction tag correctable error.",

	/*
	 * L2 cache data and tag errors injected by address.
	 */
	"l2phys=offset,xor",	do_k_err,		NI_L2PHYS,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x7f),		BIT(0),
	NULL,			NULL,
	"Insert an error into the L2 cache at byte\n"
	"\t\t\t\toffset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"kl2virt=kvaddr,xor",	do_k_err,		NI_K_L2VIRT,
	MASK(ALL_BITS),		BIT(2),
	MASK(0x7f),		BIT(2),
	NULL,			NULL,
	"Insert an error into the L2 cache data at kernel\n"
	"\t\t\t\tvirtual address \"kvaddr\".\n"
	"\t\t\t\tThis may modify the cache line state.",

	/*
	 * XXX	Note that if the kvaddr is not mapped in (TLB) Niagara
	 * 	panics.  This may be due to TLB errata or is perhaps a
	 * 	kernel bug, must be debugged once kernel is further along.
	 */
	"ul2virt=uvaddr,xor",	do_k_err,		NI_K_L2VIRT,
	MASK(ALL_BITS),		BIT(3),
	MASK(0x7f),		BIT(3),
	NULL,			NULL,
	"Insert an error into the L2 cache data at user\n"
	"\t\t\t\tvirtual address \"uvaddr\".\n"
	"\t\t\t\tThis may modify the cache line state.",

	"l2tphys=offset,xor,data", do_k_err,		NI_L2TPHYS,
	MASK(0x3fffff),		BIT(1),
	MASK(0x3f), 		BIT(1),
	NULL,			NULL,
	"Insert an error into the L2 cache tag\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l2scrubphys=offset,xor", do_k_err,		NI_L2SCRUBPHYS,
	MASK(ALL_BITS),		BIT(2),
	MASK(0x7f),		BIT(2),
	NULL,			NULL,
	"Insert an error into the L2 cache at byte offset\n"
	"\t\t\t\t\"offset\" to be hit by the L2 scrubber.",

	/*
	 * L2 cache NotData errors.
	 */
	"hdl2nd",		do_k_err,		N2_HD_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData data access error.",

	"hdl2ndma",		do_k_err,		N2_HD_L2NDMA,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdl2ndcwq",		do_k_err,		N2_HD_L2NDCWQ,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData error via\n"
	"\t\t\ta control word queue operation.",

	"hil2nd",		do_k_err,		N2_HI_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache instruction NotData\n"
	"\t\t\terror.",

	"kdl2nd",		do_k_err,		N2_KD_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData data access error.",

	"kdl2nddtlb",		do_k_err,		N2_KD_L2NDDTLB,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\ta data TLB fill operation.",

	"kil2nditlb",		do_k_err,		N2_KI_L2NDITLB,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\tan instr TLB fill operation.",

	"l2ndcopyin",		do_k_err,		N2_L2NDCOPYIN,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern/user L2 cache NotData error\n"
	"\t\t\tvia copyin.",

	"kdl2ndtl1",		do_k_err,		N2_KD_L2NDTL1,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error at\n"
	"\t\t\ttrap level 1.",

	"kdl2ndpr",		do_k_err,		N2_KD_L2NDPR,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\ta prefetch.",

	"hdl2ndpri",		do_k_err,		N2_HD_L2NDPRI,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\ta prefetch ICE.",

	"obpl2nd",		do_k_err,		N2_OBP_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\tan OBP access.",

	"kil2nd",		do_k_err,		N2_KI_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache instruction NotData\n"
	"\t\t\terror.",

	"kil2ndtl1",		do_k_err,		N2_KI_L2NDTL1,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a kern L2 cache instruction NotData\n"
	"\t\t\terror at trap level 1.",

	"udl2nd",		do_u_err,		N2_UD_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a user L2 cache NotData data access error.",

	"uil2nd",		do_u_err,		N2_UI_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a user L2 cache NotData instruction\n"
	"\t\t\taccess error.",

	"hdl2ndwb",		do_k_err,		N2_HD_L2NDWB,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData write-back, note that\n"
	"\t\t\tno error is generated.",

	"hil2ndwb",		do_k_err,		N2_HI_L2NDWB,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData write-back, note that\n"
	"\t\t\tno error is generated.",

	"iol2nd",		do_io_err,		N2_IO_L2ND,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		NULL,
	"Cause an IO (DMA) L2 cache NotData error.",

	"l2ndphys=offset",	do_k_err,		N2_L2NDPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert NotData into the L2 cache at byte\n"
	"\t\t\t\toffset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	/*
	 * L2 cache V(U)AD uncorrectable (fatal) errors.
	 */
	"hdlvfvd",		do_k_err,		N2_HD_LVF_VD,
	MASK(0xffffffff),	BIT(22)|BIT(23),
	MASK(0xff00000000ULL),	BIT(37)|BIT(38),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache VD field fatal data error.",

	"hilvfvd",		do_k_err,		N2_HI_LVF_VD,
	MASK(0xffffffff),	BIT(21)|BIT(22),
	MASK(0xff00000000ULL),	BIT(36)|BIT(37),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache VD field fatal instruction error.",

	"kdlvfvd",		do_k_err,		N2_KD_LVF_VD,
	MASK(0xffffffff),	BIT(20)|BIT(21),
	MASK(0xff00000000ULL),	BIT(35)|BIT(36),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal data error.",

	"kilvfvd",		do_k_err,		N2_KI_LVF_VD,
	MASK(0xffffffff),	BIT(19)|BIT(20),
	MASK(0xff00000000ULL),	BIT(34)|BIT(35),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal instruction error.",

	"udlvfvd",		do_u_err,		N2_UD_LVF_VD,
	MASK(0xffffffff),	BIT(18)|BIT(19),
	MASK(0xff00000000ULL),	BIT(33)|BIT(34),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal data error.",

	"uilvfvd",		do_u_err,		N2_UI_LVF_VD,
	MASK(0xffffffff),	BIT(17)|BIT(18),
	MASK(0xff00000000ULL),	BIT(32)|BIT(33),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal instruction error.",

	"hdlvfua",		do_k_err,		N2_HD_LVF_UA,
	MASK(0xffff),		BIT(12)|BIT(13),
	MASK(0xff00000000ULL),	BIT(37)|BIT(38),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache UA field fatal data error.",

	"hilvfua",		do_k_err,		N2_HI_LVF_UA,
	MASK(0xffff),		BIT(11)|BIT(12),
	MASK(0xff00000000ULL),	BIT(36)|BIT(37),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache UA field fatal instruction error.",

	"kdlvfua",		do_k_err,		N2_KD_LVF_UA,
	MASK(0xffff),		BIT(10)|BIT(11),
	MASK(0xff00000000ULL),	BIT(35)|BIT(36),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal data error.",

	"kilvfua",		do_k_err,		N2_KI_LVF_UA,
	MASK(0xffff),		BIT(9)|BIT(10),
	MASK(0xff00000000ULL),	BIT(34)|BIT(35),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal instruction error.",

	"udlvfua",		do_u_err,		N2_UD_LVF_UA,
	MASK(0xffff),		BIT(8)|BIT(9),
	MASK(0xff00000000ULL),	BIT(33)|BIT(34),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal data error.",

	"uilvfua",		do_u_err,		N2_UI_LVF_UA,
	MASK(0xffff),		BIT(7)|BIT(8),
	MASK(0xff00000000ULL),	BIT(32)|BIT(33),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal instruction error.",

	/*
	 * L2 cache V(U)AD correctable errors.
	 */
	"hdlvcvd",		do_k_err,		N2_HD_LVC_VD,
	MASK(0xffffffff),	BIT(22),
	MASK(0xff00000000ULL),	BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache VD field correctable data\n"
	"\t\t\terror.",

	"hilvcvd",		do_k_err,		N2_HI_LVC_VD,
	MASK(0xffffffff),	BIT(21),
	MASK(0xff00000000ULL),	BIT(33),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache VD field correctable instruction\n"
	"\t\t\terror.",

	"kdlvcvd",		do_k_err,		N2_KD_LVC_VD,
	MASK(0xffffffff),	BIT(20),
	MASK(0xff00000000ULL),	BIT(34),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field correctable data\n"
	"\t\t\terror.",

	"kilvcvd",		do_k_err,		N2_KI_LVC_VD,
	MASK(0xffffffff),	BIT(19),
	MASK(0xff00000000ULL),	BIT(35),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field correctable instruction\n"
	"\t\t\terror.",

	"udlvcvd",		do_u_err,		N2_UD_LVC_VD,
	MASK(0xffffffff),	BIT(18),
	MASK(0xff00000000ULL),	BIT(36),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field correctable data\n"
	"\t\t\terror.",

	"uilvcvd",		do_u_err,		N2_UI_LVC_VD,
	MASK(0xffffffff),	BIT(17),
	MASK(0xff00000000ULL),	BIT(36),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field correctable instruction\n"
	"\t\t\terror.",

	"hdlvcua",		do_k_err,		N2_HD_LVC_UA,
	MASK(0xffff),		BIT(12),
	MASK(0xff00000000ULL),	BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache UA field correctable data\n"
	"\t\t\terror.",

	"hilvcua",		do_k_err,		N2_HI_LVC_UA,
	MASK(0xffff),		BIT(11),
	MASK(0xff00000000ULL),	BIT(33),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache UA field correctable instruction\n"
	"\t\t\terror.",

	"kdlvcua",		do_k_err,		N2_KD_LVC_UA,
	MASK(0xffff),		BIT(10),
	MASK(0xff00000000ULL),	BIT(34),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field correctable data\n"
	"\t\t\terror.",

	"kilvcua",		do_k_err,		N2_KI_LVC_UA,
	MASK(0xffff),		BIT(9),
	MASK(0xff00000000ULL),	BIT(35),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field correctable instruction\n"
	"\t\t\terror.",

	"udlvcua",		do_u_err,		N2_UD_LVC_UA,
	MASK(0xffff),		BIT(8),
	MASK(0xff00000000ULL),	BIT(33),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field correctable data\n"
	"\t\t\terror.",

	"uilvcua",		do_u_err,		N2_UI_LVC_UA,
	MASK(0xffff),		BIT(7),
	MASK(0xff00000000ULL),	BIT(36),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field correctable instruction\n"
	"\t\t\terror.",

	/*
	 * L2 cache V(U)AD errors injected by address.
	 */
	"l2vdphys=offset,xor",	do_k_err,		NI_L2VDPHYS,
	MASK(0xffffffff),	BIT(6),
	MASK(0xff00000000ULL),	BIT(36),
	NULL,			NULL,
	"Insert an error into the L2 cache VD field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l2uaphys=offset,xor",	do_k_err,		NI_L2UAPHYS,
	MASK(0xffff),		BIT(5),
	MASK(0xff00000000ULL),	BIT(35),
	NULL,			NULL,
	"Insert an error into the L2 cache UA field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	/*
	 * L2 cache directory errors.
	 */
	"kdlrf",		do_k_err,		NI_KD_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache directory fatal data error.",

	"kilrf",		do_k_err,		NI_KI_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache directory fatal instruction error.",

	"udlrf",		do_u_err,		NI_UD_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache directory fatal data error.",

	"uilrf",		do_u_err,		NI_UI_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache directory fatal instruction error.",

	"l2dirphys=offset",	do_k_err,		NI_L2DIRPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert an error into the L2 cache directory\n"
	"\t\t\t\tat byte offset \"offset\".",

	/*
	 * L2 cache write back errors.
	 *
	 * NOTE: there are two HV level NotData write-back errors in
	 *	 the NotData section above.
	 */
	"hdldwu",		do_k_err,		NI_HD_LDWU,
	MASK(ALL_BITS),		BIT(42)|BIT(41),
	MASK(0x7f),		BIT(3)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper L2 data uncorrectable write-back error.",

	"hildwu",		do_k_err,		NI_HI_LDWU,
	MASK(ALL_BITS),		BIT(43)|BIT(42),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a hyper L2 instruction uncorrectable write-back\n"
	"\t\t\terror via a prefetch ICE.",

	"hdldwupri",		do_k_err,		N2_HD_LDWUPRI,
	MASK(ALL_BITS),		BIT(42)|BIT(41),
	MASK(0x7f),		BIT(3)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper L2 data uncorrectable write-back error\n"
	"\t\t\tvia a prefetch ICE.",

	"hildwupri",		do_k_err,		N2_HI_LDWUPRI,
	MASK(ALL_BITS),		BIT(43)|BIT(42),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a hyper L2 instruction uncorrectable write-back\n"
	"\t\t\terror using .",

	"kdldwu",		do_k_err,		NI_KD_LDWU,
	MASK(ALL_BITS),		BIT(44)|BIT(43),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern L2 data uncorrectable write-back error.",

	"kildwu",		do_k_err,		NI_KI_LDWU,
	MASK(ALL_BITS),		BIT(45)|BIT(44),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		NULL,
	"Cause a kern L2 instruction uncorrectable write-back\n"
	"\t\t\terror.",

	"udldwu",		do_ud_wb_err,		NI_UD_LDWU,
	MASK(ALL_BITS),		BIT(46)|BIT(45),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		NULL,
	"Cause a user L2 data uncorrectable write-back error.",

	"uildwu",		do_ud_wb_err,		NI_UI_LDWU,
	MASK(ALL_BITS),		BIT(47)|BIT(46),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		NULL,
	"Cause a user L2 instruction uncorrectable write-back\n"
	"\t\t\terror.",

	"hdldwc",		do_k_err,		NI_HD_LDWC,
	MASK(ALL_BITS),		BIT(46),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		NULL,
	"Cause a hyper L2 data correctable write-back error.",

	"hildwc",		do_k_err,		NI_HI_LDWC,
	MASK(ALL_BITS),		BIT(47),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper L2 instruction correctable write-back\n"
	"\t\t\terror.",

	"hdldwcpri",		do_k_err,		N2_HD_LDWCPRI,
	MASK(ALL_BITS),		BIT(46),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		NULL,
	"Cause a hyper L2 data correctable write-back error\n"
	"\t\t\tvia a prefetch ICE.",

	"hildwcpri",		do_k_err,		N2_HI_LDWCPRI,
	MASK(ALL_BITS),		BIT(47),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper L2 instruction correctable write-back\n"
	"\t\t\terror via a prefetch ICE.",

	"kdldwc",		do_k_err,		NI_KD_LDWC,
	MASK(ALL_BITS),		BIT(48),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		NULL,
	"Cause a kern L2 data correctable write-back error.",

	"kildwc",		do_k_err,		NI_KI_LDWC,
	MASK(ALL_BITS),		BIT(49),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern L2 instruction correctable write-back\n"
	"\t\t\terror.",

	"udldwc",		do_ud_wb_err,		NI_UD_LDWC,
	MASK(ALL_BITS),		BIT(50),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		NULL,
	"Cause a user L2 data correctable write-back error.",

	"uildwc",		do_ud_wb_err,		NI_UI_LDWC,
	MASK(ALL_BITS),		BIT(51),
	MASK(0x7f),		BIT(6),
	OFFSET(0),		NULL,
	"Cause a user L2 instruction correctable write-back\n"
	"\t\t\terror.",

	/*
	 * L1 data cache data and tag correctable errors.
	 */
	"hddcdp",		do_k_err,		NI_HD_DDC,
	MASK(ALL_BITS),		BIT(27)|BIT(2),
	MASK(0x1fe000),		BIT(19)|BIT(18),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 data cache data correctable error.",

	"kddcdp",		do_k_err,		NI_KD_DDC,
	MASK(ALL_BITS),		BIT(26)|BIT(3),
	MASK(0x1fe000),		BIT(18)|BIT(17),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache data correctable error.",

	"kddcdptl1",		do_k_err,		NI_KD_DDCTL1,
	MASK(ALL_BITS),		BIT(25)|BIT(4),
	MASK(0x1fe000),		BIT(17)|BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache data correctable error\n"
	"\t\t\tat trap level 1.",

	"hddctp",		do_k_err,		NI_HD_DTC,
	MASK(0x7ffffffc),	BIT(24)|BIT(5),
	BIT(13),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 data cache tag correctable error.",

	"kddctp",		do_k_err,		NI_KD_DTC,
	MASK(0x7ffffffc),	BIT(23)|BIT(6),
	BIT(13),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache tag correctable error.",

	"kddctptl1",		do_k_err,		NI_KD_DTCTL1,
	MASK(0x7ffffffc),	BIT(22)|BIT(7),
	BIT(13),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache tag correctable error\n"
	"\t\t\tat trap level 1.",

	"hddcvp",		do_k_err,		N2_HD_DCVP,
	BIT(1),			BIT(1),
	BIT(14),		BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 data cache tag valid bit error.",

	"kddcvp",		do_k_err,		N2_KD_DCVP,
	BIT(1),			BIT(1),
	BIT(14),		BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache tag valid bit error.",

	"kddcvptl1",		do_k_err,		N2_KD_DCVPTL1,
	BIT(1),			BIT(1),
	BIT(14),		BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache tag valid bit error\n"
	"\t\t\tat trap level 1.",

	"hddctm",		do_k_err,		N2_HD_DCTM,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper L1 data cache tag multi-hit error.",

	"kddctm",		do_k_err,		N2_KD_DCTM,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern L1 data cache tag multi-hit error.",

	"kddctmtl1",		do_k_err,		N2_KD_DCTMTL1,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern L1 data cache tag multi-hit error\n"
	"\t\t\tat trap level 1.",

	"dphys=offset,xor,data", do_k_err,		NI_DPHYS,
	MASK(ALL_BITS),		BIT(4)|BIT(5),
	MASK(0x1fe000),		BIT(15)|BIT(16),
	NULL,			NULL,
	"Insert an error into the L1 data cache\n"
	"\t\t\t\tdata at byte offset \"offset\".",

	"dtphys=offset,xor",	do_k_err,		NI_DTPHYS,
	MASK(0x7ffffffc),	BIT(5)|BIT(6),
	BIT(13),		BIT(13),
	NULL,			NULL,
	"Insert an error into the L1 data cache\n"
	"\t\t\t\ttag at byte offset \"offset\".",

	/*
	 * L1 instruction cache data and tag correctable errors.
	 */
	"hiicdp",		do_k_err,		NI_HI_IDC,
	MASK(0xffffffff),	BIT(25)|BIT(9),
	BIT(32),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 instruction cache data correctable\n"
	"\t\t\terror.",

	"kiicdp",		do_k_err,		NI_KI_IDC,
	MASK(0xffffffff),	BIT(24)|BIT(8),
	BIT(32),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache data correctable\n"
	"\t\t\terror.",

	"kiicdptl1",		do_k_err,		NI_KI_IDCTL1,
	MASK(0xffffffff),	BIT(23)|BIT(7),
	BIT(32),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache data correctable\n"
	"\t\t\terror at trap level 1.",

	"hiictp",		do_k_err,		NI_HI_ITC,
	MASK(0xfffffff),	BIT(22)|BIT(6),
	BIT(16),		BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 instruction cache tag correctable\n"
	"\t\t\terror.",

	"kiictp",		do_k_err,		NI_KI_ITC,
	MASK(0xfffffff),	BIT(21)|BIT(5),
	BIT(16),		BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag correctable\n"
	"\t\t\terror.",

	"kiictptl1",		do_k_err,		NI_KI_ITCTL1,
	MASK(0xfffffff),	BIT(20)|BIT(4),
	BIT(16),		BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag correctable\n"
	"\t\t\terror at trap level 1.",

	"hiicvp",		do_k_err,		N2_HI_ICVP,
	BIT(1),			BIT(1),
	BIT(15),		BIT(15),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 instruction cache tag valid bit error.",

	"kiicvp",		do_k_err,		N2_KI_ICVP,
	BIT(1),			BIT(1),
	BIT(15),		BIT(15),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag valid bit error.",

	"kiicvptl1",		do_k_err,		N2_KI_ICVPTL1,
	BIT(1),			BIT(1),
	BIT(15),		BIT(15),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag valid bit error\n"
	"\t\t\tat trap level 1.",

	"hiictm",		do_k_err,		N2_HI_ICTM,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper L1 instruction cache tag multi-hit error.",

	"kiictm",		do_k_err,		N2_KI_ICTM,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern L1 instruction cache tag multi-hit error.",

	"kiictmtl1",		do_k_err,		N2_KI_ICTMTL1,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern L1 instruction cache tag multi-hit error\n"
	"\t\t\tat trap level 1.",

	"iphys=offset,xor,data", do_k_err,		NI_IPHYS,
	MASK(0xffffffff),	BIT(9)|BIT(3),
	BIT(32),		BIT(32),
	NULL,			NULL,
	"Insert an error into the L1\n"
	"\t\t\t\tinstruction cache data at byte offset \"offset\".",

	"itphys=offset,xor",	do_k_err,		NI_ITPHYS,
	MASK(0xfffffff),	BIT(8)|BIT(2),
	BIT(16),		BIT(16),
	NULL,			NULL,
	"Insert an error into the L1\n"
	"\t\t\t\tinstruction cache tag at byte offset \"offset\".",

	/*
	 * Instruction and data TLB data and tag (CAM) errors.
	 */
	"kddtdp",		do_k_err,		N2_KD_DTDP,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB data parity error.",

	"kddtdpv",		do_k_err,		N2_KD_DTDPV,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB data parity error (alt method).",

	"kddttp",		do_k_err,		N2_KD_DTTP,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB tag parity error.",

	"uddtdp",		do_u_err,		NI_UD_DMDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user data D-TLB data parity error.",

	"uddttp",		do_u_err,		NI_UD_DMTU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user data D-TLB tag parity error.",

	"kddttm",		do_k_err,		N2_KD_DTTM,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB tag multi-hit error.",

	"kddtmu",		do_k_err,		N2_KD_DTMU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB miss MRA parity error.",

	"dtdprand",		do_k_err,		NI_DMDURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random D-TLB data parity error.",

	"dttprand",		do_k_err,		NI_DMTURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random D-TLB tag parity error.",

	"kiitdp",		do_k_err,		N2_KI_ITDP,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern instruction I-TLB data parity error.",

	"kiitdpv",		do_k_err,		N2_KI_ITDPV,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern instruction I-TLB data parity error\n"
	"\t\t\t(alt method).",

	"kiittp",		do_k_err,		N2_KI_ITTP,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern instruction I-TLB tag parity error.",

	"uiitdp",		do_u_err,		NI_UI_IMDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user instruction I-TLB data parity error.",

	"uiittp",		do_u_err,		NI_UI_IMTU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user instruction I-TLB tag parity error.",

	"kiittm",		do_k_err,		N2_KI_ITTM,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern instruction I-TLB tag multi-hit error.",

	"kiitmu",		do_k_err,		N2_KI_ITMU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern instruction I-TLB miss MRA parity error.",

	"itdprand",		do_k_err,		NI_IMDURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random I-TLB data parity error.",

	"ittprand",		do_k_err,		NI_IMTURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random I-TLB tag parity error.",

	/*
	 * Integer register file (SPARC Internal) errors.
	 */
	"hdirful",		do_k_err,		NI_HD_IRUL,
	MASK(0xff),		BIT(4)|BIT(3),
	MASK(0xff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file uncorrectable ecc\n"
	"\t\t\terror on load access.",

	"hdirfus",		do_k_err,		NI_HD_IRUS,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"hdirfuo",		do_k_err,		NI_HD_IRUO,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* kern */
	"kdirful",		do_k_err,		NI_KD_IRUL,
	MASK(0xff),		BIT(7)|BIT(6),
	MASK(0xff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file uncorrectable ecc\n"
	"\t\t\terror on load access.",

	"kdirfus",		do_k_err,		NI_KD_IRUS,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"kdirfuo",		do_k_err,		NI_KD_IRUO,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* correctable */
	"hdirfcl",		do_k_err,		NI_HD_IRCL,
	MASK(0xff),		BIT(3),
	MASK(0xff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file correctable ecc\n"
	"\t\t\terror on load access.",

	"hdirfcs",		do_k_err,		NI_HD_IRCS,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file correctable ecc\n"
	"\t\t\terror on store access.",

	"hdirfco",		do_k_err,		NI_HD_IRCO,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file correctable ecc\n"
	"\t\t\terror on op access.",

	/* ce kern */
	"kdirfcl",		do_k_err,		NI_KD_IRCL,
	MASK(0xff),		BIT(6),
	MASK(0xff),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file correctable ecc\n"
	"\t\t\terror on load access.",

	"kdirfcs",		do_k_err,		NI_KD_IRCS,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file correctable ecc\n"
	"\t\t\terror on store access.",

	"kdirfco",		do_k_err,		NI_KD_IRCO,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file correctable ecc\n"
	"\t\t\terror on op access.",

	/*
	 * Floating-point register file (SPARC Internal) errors.
	 */
	"hdfrful",		do_k_err,		NI_HD_FRUL,
	MASK(0xff),		BIT(4)|BIT(3),
	MASK(0xff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file uncorrectable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"hdfrfus",		do_k_err,		NI_HD_FRUS,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"hdfrfuo",		do_k_err,		NI_HD_FRUO,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* kern */
	"kdfrful",		do_k_err,		NI_KD_FRUL,
	MASK(0xff),		BIT(7)|BIT(6),
	MASK(0xff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file uncorrectable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"kdfrfus",		do_k_err,		NI_KD_FRUS,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"kdfrfuo",		do_k_err,		NI_KD_FRUO,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* correctable */
	"hdfrfcl",		do_k_err,		NI_HD_FRCL,
	MASK(0xff),		BIT(3),
	MASK(0xff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file correctable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"hdfrfcs",		do_k_err,		NI_HD_FRCS,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file correctable ecc\n"
	"\t\t\terror on store access.",

	"hdfrfco",		do_k_err,		NI_HD_FRCO,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file correctable ecc\n"
	"\t\t\terror on op access.",

	/* ce kern */
	"kdfrfcl",		do_k_err,		NI_KD_FRCL,
	MASK(0xff),		BIT(6),
	MASK(0xff),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file correctable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"kdfrfcs",		do_k_err,		NI_KD_FRCS,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file correctable ecc\n"
	"\t\t\terror on store access.",

	"kdfrfco",		do_k_err,		NI_KD_FRCO,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file correctable ecc\n"
	"\t\t\terror on op access.",

	/*
	 * Store Buffer (SPARC Internal) errors.
	 */
	"hdsbdlu",		do_k_err,		N2_HD_SBDLU,
	MASK(0x7f),		BIT(6)|BIT(2),
	MASK(0x7f),		BIT(6)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on load access.",

	"hdsbdpu",		do_k_err,		N2_HD_SBDPU,
	MASK(0x7f),		BIT(5)|BIT(1),
	MASK(0x7f),		BIT(5)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on PCX access.",

	"hdsbdpuasi",		do_k_err,		N2_HD_SBDPUASI,
	MASK(0x7f),		BIT(4)|BIT(0),
	MASK(0x7f),		BIT(4)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on asi access.",

	"kdsbdlu",		do_k_err,		N2_KD_SBDLU,
	MASK(0x7f),		BIT(6)|BIT(3),
	MASK(0x7f),		BIT(6)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on load access.",

	"kdsbdpu",		do_k_err,		N2_KD_SBDPU,
	MASK(0x7f),		BIT(5)|BIT(2),
	MASK(0x7f),		BIT(5)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on PCX access.",

	"hdsbapp",		do_k_err,		N2_HD_SBAPP,
	MASK(0x7f),		BIT(6),
	MASK(0x7f),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array tag (address) parity\n"
	"\t\t\terror on PCX access.",

	"hdsbappasi",		do_k_err,		N2_HD_SBAPPASI,
	MASK(0x7f),		BIT(5),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array tag (address) parity\n"
	"\t\t\terror on asi access.",

	"kdsbapp",		do_k_err,		N2_KD_SBAPP,
	MASK(0x7f),		BIT(4),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern Store Buffer array tag (address) parity\n"
	"\t\t\terror on PCX access.",

	"kdsbappasi",		do_k_err,		N2_KD_SBAPPASI,
	MASK(0x7f),		BIT(3),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern Store Buffer array tag (address) parity\n"
	"\t\t\terror on asi access.",

	"iosbdiou",		do_k_err,		N2_IO_SBDIOU,
	MASK(0x7f),		BIT(5)|BIT(2),
	MASK(0x7f),		BIT(5)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause an IO Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on PCX access.",

	"iosbdiouasi",		do_k_err,		N2_IO_SBDIOUASI,
	MASK(0x7f),		BIT(6)|BIT(1),
	MASK(0x7f),		BIT(6)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an IO Store Buffer array data uncorrectable\n"
	"\t\t\tecc error on asi access.",

	/* correctable */
	"hdsbdlc",		do_k_err,		N2_HD_SBDLC,
	MASK(0x7f),		BIT(5),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array data correctable\n"
	"\t\t\tecc error on load access.",

	"hdsbdpc",		do_k_err,		N2_HD_SBDPC,
	MASK(0x7f),		BIT(4),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array data correctable\n"
	"\t\t\tecc error on PCX access.",

	"hdsbdpcasi",		do_k_err,		N2_HD_SBDPCASI,
	MASK(0x7f),		BIT(3),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Store Buffer array data correctable\n"
	"\t\t\tecc error on asi access.",

	"kdsbdlc",		do_k_err,		N2_KD_SBDLC,
	MASK(0x7f),		BIT(2),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern Store Buffer array data correctable\n"
	"\t\t\tecc error on load access.",

	"kdsbdpc",		do_k_err,		N2_KD_SBDPC,
	MASK(0x7f),		BIT(1),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern Store Buffer array data correctable\n"
	"\t\t\tecc error on PCX access.",

	"iosbdpc",		do_k_err,		N2_IO_SBDPC,
	MASK(0x7f),		BIT(0),
	MASK(0x7f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause an IO Store Buffer array data correctable\n"
	"\t\t\tecc error on PCX access.",

	"iosbdpcasi",		do_k_err,		N2_IO_SBDPCASI,
	MASK(0x7f),		BIT(1),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an IO Store Buffer array data correctable\n"
	"\t\t\tecc error on asi access.",

	/*
	 * Internal register array (SPARC Internal) errors.
	 */
	"hdscau",		do_k_err,		N2_HD_SCAU,
	MASK(0xff),		BIT(7)|BIT(3),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Scratchpad register array uncorrectable\n"
	"\t\t\tecc error.",

	"hdscac",		do_k_err,		N2_HD_SCAC,
	MASK(0xff),		BIT(2),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Scratchpad register array correctable\n"
	"\t\t\tecc error.",

	"kdscau",		do_k_err,		N2_KD_SCAU,
	MASK(0xff),		BIT(6)|BIT(2),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern Scratchpad register array uncorrectable\n"
	"\t\t\tecc error.",

	"kdscac",		do_k_err,		N2_KD_SCAC,
	MASK(0xff),		BIT(1),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern register array correctable\n"
	"\t\t\tecc error.",

	/* tick compare errors */
	"hdtcup",		do_k_err,		N2_HD_TCUP,
	MASK(0xff),		BIT(5)|BIT(1),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Tick Compare register array\n"
	"\t\t\tuncorrectable ecc error via asr access.",

	"hdtccp",		do_k_err,		N2_HD_TCCP,
	MASK(0xff),		BIT(3),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Tick Compare register array\n"
	"\t\t\tcorrectable ecc error via asr access.",

	"hdtcud",		do_k_err,		N2_HD_TCUD,
	MASK(0xff),		BIT(4)|BIT(0),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Tick Compare register array\n"
	"\t\t\tuncorrectable ecc error via HW access.",

	"hdtccd",		do_k_err,		N2_HD_TCCD,
	MASK(0xff),		BIT(2),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Tick Compare register array\n"
	"\t\t\tcorrectable ecc error via HW access.",

	/* trap stack array errors */
	"hdtsau",		do_k_err,		N2_HD_TSAU,
	MASK(0xff),		BIT(7)|BIT(3),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Trap Stack register array uncorrectable\n"
	"\t\t\tecc error.",

	"hdtsac",		do_k_err,		N2_HD_TSAC,
	MASK(0xff),		BIT(2),
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper Trap Stack register array correctable\n"
	"\t\t\tecc error.",

	/* MMU register array errors */
	"hddtmu",		do_k_err,		N2_HD_MRAU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper MMU register array uncorrectable\n"
	"\t\t\tparity error via HW access.",

	"hdmrauasi",		do_k_err,		N2_HD_MRAUASI,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper MMU register array uncorrectable\n"
	"\t\t\tparity error via ASI access.",

	/*
	 * Modular Arithmetic Unit and CWQ (SPARC Internal) errors.
	 */
	"hdmamul",		do_k_err,		NI_HD_MAUL,
	BIT(25)|BIT(24),	BIT(24),
	BIT(25)|BIT(24),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Insert a modular arithmetic parity error with load\n"
	"\t\t\taccess (no error detected by HW on load access).",

	"hdmamus",		do_k_err,		NI_HD_MAUS,
	BIT(25)|BIT(24),	BIT(25),
	BIT(25)|BIT(24),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper modular arithmetic parity error on store.",

	"hdmamuo",		do_k_err,		NI_HD_MAUO,
	BIT(25)|BIT(24),	BIT(24),
	BIT(25)|BIT(24),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper modular arithmetic parity error on op.",

	"hdcwqp",		do_k_err,		N2_HD_CWQP,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper control word queue protocol error.",

	/*
	 * System on Chip (SOC) MCU errors.
	 */
	"iomcu0ecc",		do_k_err,		N2_HD_MCUECC,
	BIT(N2_SOC_MCU0ECC_SHIFT),	BIT(N2_SOC_MCU0ECC_SHIFT),
	BIT(N2_SOC_MCU0ECC_SHIFT),	BIT(N2_SOC_MCU0ECC_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 0 exceeded CE threshold\n"
	"\t\t\terror signal.",

	"iomcu1ecc",		do_k_err,		N2_HD_MCUECC,
	BIT(N2_SOC_MCU1ECC_SHIFT),	BIT(N2_SOC_MCU1ECC_SHIFT),
	BIT(N2_SOC_MCU1ECC_SHIFT),	BIT(N2_SOC_MCU1ECC_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 1 exceeded CE threshold\n"
	"\t\t\terror signal.",

	"iomcu2ecc",		do_k_err,		N2_HD_MCUECC,
	BIT(N2_SOC_MCU2ECC_SHIFT),	BIT(N2_SOC_MCU2ECC_SHIFT),
	BIT(N2_SOC_MCU2ECC_SHIFT),	BIT(N2_SOC_MCU2ECC_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 2 exceeded CE threshold\n"
	"\t\t\terror signal.",

	"iomcu3ecc",		do_k_err,		N2_HD_MCUECC,
	BIT(N2_SOC_MCU3ECC_SHIFT),	BIT(N2_SOC_MCU3ECC_SHIFT),
	BIT(N2_SOC_MCU3ECC_SHIFT),	BIT(N2_SOC_MCU3ECC_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 3 exceeded CE threshold\n"
	"\t\t\terror signal.",

	"iomcu0fbr",		do_k_err,		N2_HD_MCUFBR,
	BIT(N2_SOC_MCU0FBR_SHIFT),	BIT(N2_SOC_MCU0FBR_SHIFT),
	BIT(N2_SOC_MCU0FBR_SHIFT),	BIT(N2_SOC_MCU0FBR_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 0 exceeded FBDIMM recoverable\n"
	"\t\t\tthreshold error signal.",

	"iomcu1fbr",		do_k_err,		N2_HD_MCUFBR,
	BIT(N2_SOC_MCU1FBR_SHIFT),	BIT(N2_SOC_MCU1FBR_SHIFT),
	BIT(N2_SOC_MCU1FBR_SHIFT),	BIT(N2_SOC_MCU1FBR_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 1 exceeded FBDIMM recoverable\n"
	"\t\t\tthreshold error signal.",

	"iomcu2fbr",		do_k_err,		N2_HD_MCUFBR,
	BIT(N2_SOC_MCU2FBR_SHIFT),	BIT(N2_SOC_MCU2FBR_SHIFT),
	BIT(N2_SOC_MCU2FBR_SHIFT),	BIT(N2_SOC_MCU2FBR_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 2 exceeded FBDIMM recoverable\n"
	"\t\t\tthreshold error signal.",

	"iomcu3fbr",		do_k_err,		N2_HD_MCUFBR,
	BIT(N2_SOC_MCU3FBR_SHIFT),	BIT(N2_SOC_MCU3FBR_SHIFT),
	BIT(N2_SOC_MCU3FBR_SHIFT),	BIT(N2_SOC_MCU3FBR_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 3 exceeded FBDIMM recoverable\n"
	"\t\t\tthreshold error signal.",

	"iomcu0fbu",		do_k_err,		N2_HD_MCUFBU,
	BIT(N2_SOC_MCU0FBU_SHIFT),	BIT(N2_SOC_MCU0FBU_SHIFT),
	BIT(N2_SOC_MCU0FBU_SHIFT),	BIT(N2_SOC_MCU0FBU_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 0 FBDIMM unrecoverable link\n"
	"\t\t\terror.",

	"iomcu1fbu",		do_k_err,		N2_HD_MCUFBU,
	BIT(N2_SOC_MCU1FBU_SHIFT),	BIT(N2_SOC_MCU1FBU_SHIFT),
	BIT(N2_SOC_MCU1FBU_SHIFT),	BIT(N2_SOC_MCU1FBU_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 1 FBDIMM unrecoverable link\n"
	"\t\t\terror.",

	"iomcu2fbu",		do_k_err,		N2_HD_MCUFBU,
	BIT(N2_SOC_MCU2FBU_SHIFT),	BIT(N2_SOC_MCU2FBU_SHIFT),
	BIT(N2_SOC_MCU2FBU_SHIFT),	BIT(N2_SOC_MCU2FBU_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 2 FBDIMM unrecoverable link\n"
	"\t\t\terror.",

	"iomcu3fbu",		do_k_err,		N2_HD_MCUFBU,
	BIT(N2_SOC_MCU3FBU_SHIFT),	BIT(N2_SOC_MCU3FBU_SHIFT),
	BIT(N2_SOC_MCU3FBU_SHIFT),	BIT(N2_SOC_MCU3FBU_SHIFT),
	NULL,				NULL,
	"Cause an MCU branch 3 FBDIMM unrecoverable link\n"
	"\t\t\terror.",

	/*
	 * System on Chip (SOC) Internal errors.
	 */
	"ioncudmucredit",	do_k_err,		N2_IO_NCUDMUC,
	BIT(N2_SOC_NCUDMUCREDIT_SHIFT),	BIT(N2_SOC_NCUDMUCREDIT_SHIFT),
	BIT(N2_SOC_NCUDMUCREDIT_SHIFT),	BIT(N2_SOC_NCUDMUCREDIT_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) parity error on the credit\n"
	"\t\t\ttoken bus to NCU for DMU PIO write credits.",

	"ioniudpar",		do_k_err,		N2_IO_NIUDPAR,
	BIT(N2_SOC_NIUDATAPARITY_SHIFT), BIT(N2_SOC_NIUDATAPARITY_SHIFT),
	BIT(N2_SOC_NIUDATAPARITY_SHIFT), BIT(N2_SOC_NIUDATAPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an NIU (SOC) data parity error in the DMA\n"
	"\t\t\tread return from the SIO.",

	"ioniuctague",		do_k_err,		N2_IO_NIUCTAGUE,
	BIT(N2_SOC_NIUCTAGUE_SHIFT),	BIT(N2_SOC_NIUCTAGUE_SHIFT),
	BIT(N2_SOC_NIUCTAGUE_SHIFT),	BIT(N2_SOC_NIUCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an NIU (SOC) CTAG uncorrectable error in the\n"
	"\t\t\tDMA read return from the SIO.",

	"ioniuctagce",		do_k_err,		N2_IO_NIUCTAGCE,
	BIT(N2_SOC_NIUCTAGCE_SHIFT),	BIT(N2_SOC_NIUCTAGCE_SHIFT),
	BIT(N2_SOC_NIUCTAGCE_SHIFT),	BIT(N2_SOC_NIUCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an NIU (SOC) CTAG correctable error in the\n"
	"\t\t\tDMA read return from the SIO.",

	"iosioctague",		do_k_err,		N2_IO_SIOCTAGUE,
	BIT(N2_SOC_SIOCTAGUE_SHIFT),	BIT(N2_SOC_SIOCTAGUE_SHIFT),
	BIT(N2_SOC_SIOCTAGUE_SHIFT),	BIT(N2_SOC_SIOCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an SIO (SOC) CTAG uncorrectable error from\n"
	"\t\t\tthe old FIFO.",

	"iosioctagce",		do_k_err,		N2_IO_SIOCTAGCE,
	BIT(N2_SOC_SIOCTAGCE_SHIFT),	BIT(N2_SOC_SIOCTAGCE_SHIFT),
	BIT(N2_SOC_SIOCTAGCE_SHIFT),	BIT(N2_SOC_SIOCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an SIO (SOC) CTAG correctable error from\n"
	"\t\t\tthe old FIFO.",

	/* NCU errors */
	"ioncuctague",		do_k_err,		N2_IO_NCUCTAGUE,
	BIT(N2_SOC_NCUCTAGUE_SHIFT),	BIT(N2_SOC_NCUCTAGUE_SHIFT),
	BIT(N2_SOC_NCUCTAGUE_SHIFT),	BIT(N2_SOC_NCUCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) CTAG uncorrectable error on an\n"
	"\t\t\tinterrupt write or a PIO read return.",

	"ioncuctagce",		do_k_err,		N2_IO_NCUCTAGCE,
	BIT(N2_SOC_NCUCTAGCE_SHIFT),	BIT(N2_SOC_NCUCTAGCE_SHIFT),
	BIT(N2_SOC_NCUCTAGCE_SHIFT),	BIT(N2_SOC_NCUCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) CTAG correctable error on an\n"
	"\t\t\tinterrupt write or a PIO read return.",

	"ioncudmuue",		do_k_err,		N2_IO_NCUDMUUE,
	BIT(N2_SOC_NCUDMUUE_SHIFT),	BIT(N2_SOC_NCUDMUUE_SHIFT),
	BIT(N2_SOC_NCUDMUUE_SHIFT),	BIT(N2_SOC_NCUDMUUE_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) parity error in the NCU DMU\n"
	"\t\t\tPIO Req FIFO.",

	"ioncucpxue",		do_k_err,		N2_IO_NCUCPXUE,
	BIT(N2_SOC_NCUCPXUE_SHIFT),	BIT(N2_SOC_NCUCPXUE_SHIFT),
	BIT(N2_SOC_NCUCPXUE_SHIFT),	BIT(N2_SOC_NCUCPXUE_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) error in the output\n"
	"\t\t\tFIFO to the crossbar.",

	"ioncupcxue",		do_k_err,		N2_IO_NCUPCXUE,
	BIT(N2_SOC_NCUPCXUE_SHIFT),	BIT(N2_SOC_NCUPCXUE_SHIFT),
	BIT(N2_SOC_NCUPCXUE_SHIFT),	BIT(N2_SOC_NCUPCXUE_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) detected PIO/CSR command error from\n"
	"\t\t\tthe processors.",

	"ioncupcxd",		do_k_err,		N2_IO_NCUPCXD,
	BIT(N2_SOC_NCUPCXDATA_SHIFT),	BIT(N2_SOC_NCUPCXDATA_SHIFT),
	BIT(N2_SOC_NCUPCXDATA_SHIFT),	BIT(N2_SOC_NCUPCXDATA_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) detected PIO/CSR data error from\n"
	"\t\t\tthe processors.",

	"ioncuint",		do_k_err,		N2_IO_NCUINT,
	BIT(N2_SOC_NCUINTTABLE_SHIFT),	BIT(N2_SOC_NCUINTTABLE_SHIFT),
	BIT(N2_SOC_NCUINTTABLE_SHIFT),	BIT(N2_SOC_NCUINTTABLE_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) interrupt table read error.",

	"ioncumondofifo",	do_k_err,		N2_IO_NCUMONDOF,
	BIT(N2_SOC_NCUMONDOFIFO_SHIFT),	BIT(N2_SOC_NCUMONDOFIFO_SHIFT),
	BIT(N2_SOC_NCUMONDOFIFO_SHIFT),	BIT(N2_SOC_NCUMONDOFIFO_SHIFT),
	NULL,				NULL,
	"Cause an NCU (SOC) mondo FIFO read error.",

	"ioncumondotable",	do_k_err,		N2_IO_NCUMONDOT,
	BIT(N2_SOC_NCUMONDOTABLE_SHIFT), BIT(N2_SOC_NCUMONDOTABLE_SHIFT),
	BIT(N2_SOC_NCUMONDOTABLE_SHIFT), BIT(N2_SOC_NCUMONDOTABLE_SHIFT),
	NULL,				 NULL,
	"Cause an NCU (SOC) mondo table read error.",

	"ioncudpar",		do_k_err,		N2_IO_NCUDPAR,
	BIT(N2_SOC_NCUDATAPARITY_SHIFT), BIT(N2_SOC_NCUDATAPARITY_SHIFT),
	BIT(N2_SOC_NCUDATAPARITY_SHIFT), BIT(N2_SOC_NCUDATAPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an NCU (SOC) data parity error for\n"
	"\t\t\tinterrupt write or a PIO read return.",

	/* DMU errors */
	"iodmudpar",		do_k_err,		N2_IO_DMUDPAR,
	BIT(N2_SOC_DMUDATAPARITY_SHIFT), BIT(N2_SOC_DMUDATAPARITY_SHIFT),
	BIT(N2_SOC_DMUDATAPARITY_SHIFT), BIT(N2_SOC_DMUDATAPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an DMU (SOC) data parity error in the DMA read\n"
	"\t\t\treturn from the SIO.",

	"iodmusiicredit",	do_k_err,		N2_IO_DMUSIIC,
	BIT(N2_SOC_DMUSIICREDIT_SHIFT), BIT(N2_SOC_DMUSIICREDIT_SHIFT),
	BIT(N2_SOC_DMUSIICREDIT_SHIFT), BIT(N2_SOC_DMUSIICREDIT_SHIFT),
	NULL,				NULL,
	"Cause an DMU (SOC) parity error in the DMA write\n"
	"\t\t\tacknowledge credit from the SII.",

	"iodmuctague",		do_k_err,		N2_IO_DMUCTAGUE,
	BIT(N2_SOC_DMUCTAGUE_SHIFT),	BIT(N2_SOC_DMUCTAGUE_SHIFT),
	BIT(N2_SOC_DMUCTAGUE_SHIFT),	BIT(N2_SOC_DMUCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an DMU (SOC) CTAG uncorrectable error in the DMA\n"
	"\t\t\tread return from the SIO.",

	"iodmuctagce",		do_k_err,		N2_IO_DMUCTAGCE,
	BIT(N2_SOC_DMUCTAGCE_SHIFT),	BIT(N2_SOC_DMUCTAGCE_SHIFT),
	BIT(N2_SOC_DMUCTAGCE_SHIFT),	BIT(N2_SOC_DMUCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an DMU (SOC) CTAG correctable error in the DMA\n"
	"\t\t\tread return from the SIO.",

	"iodmuncucredit",	do_k_err,		N2_IO_DMUNCUC,
	BIT(N2_SOC_DMUNCUCREDIT_SHIFT), BIT(N2_SOC_DMUNCUCREDIT_SHIFT),
	BIT(N2_SOC_DMUNCUCREDIT_SHIFT), BIT(N2_SOC_DMUNCUCREDIT_SHIFT),
	NULL,				NULL,
	"Cause an DMU (SOC) parity error in the mondo\n"
	"\t\t\tacknowledge credit from NCU.",

	"iodmuint",		do_k_err,		N2_IO_DMUINT,
	BIT(N2_SOC_DMUINTERNAL_SHIFT),	BIT(N2_SOC_DMUINTERNAL_SHIFT),
	BIT(N2_SOC_DMUINTERNAL_SHIFT),	BIT(N2_SOC_DMUINTERNAL_SHIFT),
	NULL,				NULL,
	"Cause an DMU (SOC) internal error",

	/* SII errors */
	"iosiidmuapar",		do_k_err,		N2_IO_SIIDMUAP,
	BIT(N2_SOC_SIIDMUAPARITY_SHIFT), BIT(N2_SOC_SIIDMUAPARITY_SHIFT),
	BIT(N2_SOC_SIIDMUAPARITY_SHIFT), BIT(N2_SOC_SIIDMUAPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an SII (SOC) parity error on address field\n"
	"\t\t\tfor DMA transactions from DMU FIFO.",

	"iosiidmudpar",		do_k_err,		N2_IO_SIIDMUDP,
	BIT(N2_SOC_SIIDMUDPARITY_SHIFT), BIT(N2_SOC_SIIDMUDPARITY_SHIFT),
	BIT(N2_SOC_SIIDMUDPARITY_SHIFT), BIT(N2_SOC_SIIDMUDPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an SII (SOC) parity error on data\n"
	"\t\t\tfor DMA transactions from DMU FIFO.",

	"iosiiniuapar",		do_k_err,		N2_IO_SIINIUAP,
	BIT(N2_SOC_SIINIUAPARITY_SHIFT), BIT(N2_SOC_SIINIUAPARITY_SHIFT),
	BIT(N2_SOC_SIINIUAPARITY_SHIFT), BIT(N2_SOC_SIINIUAPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an SII (SOC) parity error on address field\n"
	"\t\t\tfor DMA transactions from NIU FIFO.",

	"iosiiniudpar",		do_k_err,		N2_IO_SIINIUDP,
	BIT(N2_SOC_SIINIUDPARITY_SHIFT), BIT(N2_SOC_SIINIUDPARITY_SHIFT),
	BIT(N2_SOC_SIINIUDPARITY_SHIFT), BIT(N2_SOC_SIINIUDPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an SII (SOC) parity error on data\n"
	"\t\t\tfor DMA transactions from NIU FIFO.",

	"iosiidmuctague",	do_k_err,		N2_IO_SIIDMUCTU,
	BIT(N2_SOC_SIIDMUCTAGUE_SHIFT), BIT(N2_SOC_SIIDMUCTAGUE_SHIFT),
	BIT(N2_SOC_SIIDMUCTAGUE_SHIFT), BIT(N2_SOC_SIIDMUCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an SII (SOC) CTAG uncorrectable error on\n"
	"\t\t\ta transaction from the DMU FIFO.",

	"iosiidmuctagce",	do_k_err,		N2_IO_SIIDMUCTC,
	BIT(N2_SOC_SIIDMUCTAGCE_SHIFT), BIT(N2_SOC_SIIDMUCTAGCE_SHIFT),
	BIT(N2_SOC_SIIDMUCTAGCE_SHIFT), BIT(N2_SOC_SIIDMUCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an SII (SOC) CTAG correctable error on\n"
	"\t\t\ta transaction from the DMU FIFO.",

	"iosiiniuctague",	do_k_err,		N2_IO_SIINIUCTU,
	BIT(N2_SOC_SIINIUCTAGUE_SHIFT), BIT(N2_SOC_SIINIUCTAGUE_SHIFT),
	BIT(N2_SOC_SIINIUCTAGUE_SHIFT), BIT(N2_SOC_SIINIUCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an SII (SOC) CTAG uncorrectable error on\n"
	"\t\t\ta transaction from the NIU FIFO.",

	"iosiiniuctagce",	do_k_err,		N2_IO_SIINIUCTC,
	BIT(N2_SOC_SIINIUCTAGCE_SHIFT), BIT(N2_SOC_SIINIUCTAGCE_SHIFT),
	BIT(N2_SOC_SIINIUCTAGCE_SHIFT), BIT(N2_SOC_SIINIUCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an SII (SOC) CTAG correctable error on\n"
	"\t\t\ta transaction from the NIU FIFO.",

	/*
	 * SSI (bootROM interface) errors.
	 */
	"hdssipar",		do_notimp,		NI_HD_SSIPAR,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data SSI parity error on read.",

	"hdssipars",		do_notimp,		NI_HD_SSIPARS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data SSI parity error on write.",

	"hdssito",		do_k_err,		NI_HD_SSITO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data SSI timeout error on read.",

	"hdssitos",		do_k_err,		NI_HD_SSITOS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data SSI timeout error on write.",

	/*
	 * DEBUG test case to ensure framework calls HV correctly.
	 */
	"n2test",		do_k_err,		N2_TEST,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to ensure mtst framework can execute\n"
	"\t\t\tin HV mode correctly.",

	/*
	 * DEBUG print case to check the processors ESR values.
	 */
	"n2printesrs",		do_k_err,		N2_PRINT_ESRS,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG print case to check the processors ESR values.",

	/*
	 * DEBUG test case to clear the contents of certain ESRs.
	 */
	"n2clearesrs",		do_k_err,		N2_CLEAR_ESRS,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to clear the contents of certain ESRs.",

	/*
	 * End of list.
	 */
	NULL,			NULL,		NULL,
	NULL,			NULL,		NULL,
	NULL};

static cmd_t *commands[] = {
	niagara2_cmds,
	sun4v_generic_cmds,
	NULL
};

static  opsvec_t operations = {
	gen_flushall_l2,	/* requires L2$ to be in DM mode first */
	ni_pre_test,		/* pre-test routine (using N1 routine) */
	NULL			/* no post-test routine */

};

void
n2_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}
