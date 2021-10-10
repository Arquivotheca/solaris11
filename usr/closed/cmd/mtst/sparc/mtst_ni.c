/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains sun4v Niagara (US-T1) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include "mtst.h"

/*
 * Niagara specific routines located in this file.
 */
void	ni_init(mdata_t *);
int	ni_pre_test(mdata_t *);

/*
 * These Niagara errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t niagara_cmds[] = {

	/*
	 * Memory (DRAM) uncorrectable errors.
	 */
	"hddau",		do_k_err,		NI_HD_DAU,
	MASK(0xffff),		BIT(15)|BIT(0),
	MASK(0xffff),		BIT(15)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory uncorrectable ecc error.",

	"hidau",		do_k_err,		NI_HI_DAU,
	MASK(0xffff),		BIT(11)|BIT(4),
	MASK(0xffff),		BIT(11)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory uncorrectable ecc error.",

	"kddau",		do_k_err,		NI_KD_DAU,
	MASK(0xffff),		BIT(14)|BIT(1),
	MASK(0xffff),		BIT(14)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable ecc error.",

	"hddauma",		do_k_err,		NI_HD_DAUMA,
	MASK(0xffff),		BIT(13)|BIT(2),
	MASK(0xffff),		BIT(13)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"kddautl1",		do_k_err,		NI_KD_DAUTL1,
	MASK(0xffff),		BIT(12)|BIT(3),
	MASK(0xffff),		BIT(12)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"kddaupr",		do_k_err,		NI_KD_DAUPR,
	MASK(0xffff),		BIT(11)|BIT(4),
	MASK(0xffff),		BIT(11)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data uncorrectable memory error via\n"
	"\t\t\ta prefetch.",

	"kidau",		do_k_err,		NI_KI_DAU,
	MASK(0xffff),		BIT(10)|BIT(5),
	MASK(0xffff),		BIT(10)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr uncorrectable memory error.",

	"kidautl1",		do_k_err,		NI_KI_DAUTL1,
	MASK(0xffff),		BIT(9)|BIT(6),
	MASK(0xffff),		BIT(9)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"uddau",		do_u_err,		NI_UD_DAU,
	MASK(0xffff),		BIT(8)|BIT(7),
	MASK(0xffff),		BIT(8)|BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a user data uncorrectable memory error.",

	"uidau",		do_u_err,		NI_UI_DAU,
	MASK(0xffff),		BIT(7)|BIT(8),
	MASK(0xffff),		BIT(7)|BIT(8),
	OFFSET(0),		NULL,
	"Cause a user instr uncorrectable memory error.",

	"kddsu",		do_k_err,		NI_KD_DSU,
	MASK(0xffff),		BIT(6)|BIT(9),
	MASK(0xffff),		BIT(6)|BIT(9),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable ecc error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"kddbu",		do_k_err,		NI_KD_DBU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable out of range error.",

	"iodru",		do_io_err,		NI_IO_DRU,
	MASK(0xffff),		BIT(4)|BIT(11),
	MASK(0xffff),		BIT(4)|BIT(11),
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

	"hidac",		do_k_err,		NI_HI_DAC,
	MASK(0xffff),		BIT(1)|BIT(0),
	MASK(0xffff),		BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instr memory correctable ecc error.",

	"kddac",		do_k_err,		NI_KD_DAC,
	MASK(0xffff),		BIT(3)|BIT(2),
	MASK(0xffff),		BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory correctable ecc error.",

	"hddacma",		do_k_err,		NI_HD_DACMA,
	MASK(0xffff),		BIT(7),
	MASK(0xffff),		BIT(11),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

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

	"hildau",		do_k_err,		NI_HI_LDAU,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instr uncorrectable error.",

	"kdldau",		do_k_err,		NI_KD_LDAU,
	MASK(ALL_BITS),		BIT(29)|BIT(20),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error.",

	"copyinldau",		do_k_err,		NI_LDAUCOPYIN,
	MASK(ALL_BITS),		BIT(28)|BIT(19),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern/user L2 cache data uncorrectable error\n"
	"\t\t\tvia copyin.",

	"hdldauma",		do_k_err,		NI_HD_LDAUMA,
	MASK(ALL_BITS),		BIT(27)|BIT(18),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\ta modular arithmetic operation.",

	"kdldautl1",		do_k_err,		NI_KD_LDAUTL1,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error at\n"
	"\t\t\ttrap level 1.",

	"kdldaupr",		do_k_err,		NI_KD_LDAUPR,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\ta prefetch.",

	"obpldau",		do_k_err,		NI_OBP_LDAU,
	MASK(ALL_BITS),		BIT(25)|BIT(16),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\tan OBP access.",

	"kildau",		do_k_err,		NI_KI_LDAU,
	MASK(ALL_BITS),		BIT(24)|BIT(15),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data uncorrectable\n"
	"\t\t\terror.",

	"kildautl1",		do_k_err,		NI_KI_LDAUTL1,
	MASK(ALL_BITS),		BIT(23)|BIT(14),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data uncorrectable\n"
	"\t\t\terror at trap level 1.",

	"kdldsu",		do_k_err,		NI_KD_LDSU,
	MASK(ALL_BITS),		BIT(22)|BIT(13),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"ioldru",		do_io_err,		NI_IO_LDRU,
	MASK(ALL_BITS),		BIT(20)|BIT(11),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) L2 cache uncorrectable error.",

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

	/*
	 * L2 cache data and tag correctable errors.
	 */
	"hdldac",		do_k_err,		NI_HD_LDAC,
	MASK(ALL_BITS),		BIT(31),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data correctable error.",

	"hildac",		do_k_err,		NI_HI_LDAC,
	MASK(ALL_BITS),		BIT(33),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instr correctable error.",

	"kdldac",		do_k_err,		NI_KD_LDAC,
	MASK(ALL_BITS),		BIT(30),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error.",

	"copyinldac",		do_k_err,		NI_LDACCOPYIN,
	MASK(ALL_BITS),		BIT(29),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern/user L2 cache data correctable error\n"
	"\t\t\tvia copyin.",

	"hdldacma",		do_k_err,		NI_HD_LDACMA,
	MASK(ALL_BITS),		BIT(28),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache data correctable error via\n"
	"\t\t\ta modular arithmetic operation.",

	"kdldactl1",		do_k_err,		NI_KD_LDACTL1,
	MASK(ALL_BITS),		BIT(27),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error at\n"
	"\t\t\ttrap level 1.",

	"kdldacpr",		do_k_err,		NI_KD_LDACPR,
	MASK(ALL_BITS),		BIT(26),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data uncorrectable error via\n"
	"\t\t\ta prefetch.",

	"obpldac",		do_k_err,		NI_OBP_LDAC,
	MASK(ALL_BITS),		BIT(25),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error via\n"
	"\t\t\tan OBP access.",

	"kildac",		do_k_err,		NI_KI_LDAC,
	MASK(ALL_BITS),		BIT(24),
	MASK(0x7f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data correctable\n"
	"\t\t\terror.",

	"kildactl1",		do_k_err,		NI_KI_LDACTL1,
	MASK(ALL_BITS),		BIT(23),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction data correctable\n"
	"\t\t\terror at trap level 1.",

	"kdldsc",		do_k_err,		NI_KD_LDSC,
	MASK(ALL_BITS),		BIT(22),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache data correctable error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"ioldrc",		do_io_err,		NI_IO_LDRC,
	MASK(ALL_BITS),		BIT(20),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) L2 cache correctable error.",

	/* tag - ce only */
	"hdldtc",		do_k_err,		NI_HD_LDTC,
	MASK(0x3fffff),		BIT(18),
	MASK(0x3f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache tag correctable error.",

	"hildtc",		do_k_err,		NI_HI_LDTC,
	MASK(0x3fffff),		BIT(17),
	MASK(0x3f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction tag correctable error.",

	"kdldtc",		do_k_err,		NI_KD_LDTC,
	MASK(0x3fffff),		BIT(16),
	MASK(0x3f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag correctable error.",

	"kdldtctl1",		do_k_err,		NI_KD_LDTCTL1,
	MASK(0x3fffff),		BIT(15),
	MASK(0x3f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag correctable error\n"
	"\t\t\tat trap level 1.",

	"kildtc",		do_k_err,		NI_KI_LDTC,
	MASK(0x3fffff),		BIT(14),
	MASK(0x3f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag correctable error.",

	"kildtctl1",		do_k_err,		NI_KI_LDTCTL1,
	MASK(0x3fffff),		BIT(13),
	MASK(0x3f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag correctable error\n"
	"\t\t\tat trap level 1.",

	"udldac",		do_u_err,		NI_UD_LDAC,
	MASK(ALL_BITS),		BIT(23),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache data correctable error.",

	"uildac",		do_u_err,		NI_UI_LDAC,
	MASK(ALL_BITS),		BIT(22),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction data correctable error.",

	"udldtc",		do_u_err,		NI_UD_LDTC,
	MASK(0x3fffff),		BIT(14),
	MASK(0x3f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache tag correctable error.",

	"uildtc",		do_u_err,		NI_UI_LDTC,
	MASK(0x3fffff),		BIT(13),
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
	"Insert an error into the L2 cache at kernel\n"
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
	"Insert an error into the L2 cache at user\n"
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
	 * L2 cache write back errors.
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
	"\t\t\terror.",

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
	 * L2 cache VA(U)D errors.
	 */
	"kdlvuvd",		do_k_err,		NI_KD_LVU_VD,
	MASK(0xffffff),		BIT(20),
	BIT(25)|BIT(24),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal data error.",

	"kilvuvd",		do_k_err,		NI_KI_LVU_VD,
	MASK(0xffffff),		BIT(19),
	BIT(25)|BIT(24),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal instruction error.",

	"udlvuvd",		do_u_err,		NI_UD_LVU_VD,
	MASK(0xffffff),		BIT(18),
	BIT(25)|BIT(24),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal data error.",

	"uilvuvd",		do_u_err,		NI_UI_LVU_VD,
	MASK(0xffffff),		BIT(17),
	BIT(25)|BIT(24),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache VD field fatal instruction error.",

	"kdlvuua",		do_k_err,		NI_KD_LVU_UA,
	MASK(0xfff),		BIT(10),
	BIT(24),		BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal data error.",

	"kilvuua",		do_k_err,		NI_KI_LVU_UA,
	MASK(0xfff),		BIT(9),
	BIT(24),		BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal instruction error.",

	"udlvuua",		do_u_err,		NI_UD_LVU_UA,
	MASK(0xfff),		BIT(8),
	BIT(24),		BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal data error.",

	"uilvuua",		do_u_err,		NI_UI_LVU_UA,
	MASK(0xfff),		BIT(7),
	BIT(24),		BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache UA field fatal instruction error.",

	"l2vdphys=offset,xor",	do_k_err,		NI_L2VDPHYS,
	MASK(0xffffff),		BIT(6),
	BIT(25)|BIT(24),	BIT(24),
	NULL,			NULL,
	"Insert an error into the L2 cache VD field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l2uaphys=offset,xor",	do_k_err,		NI_L2UAPHYS,
	MASK(0xfff),		BIT(5),
	BIT(24),		BIT(24),
	NULL,			NULL,
	"Insert an error into the L2 cache UA field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	/*
	 * L2 cache directory errors.
	 */
	"kdlru",		do_k_err,		NI_KD_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache directory fatal data error.",

	"kilru",		do_k_err,		NI_KI_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache directory fatal instruction error.",

	"udlru",		do_u_err,		NI_UD_LRU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache directory fatal data error.",

	"uilru",		do_u_err,		NI_UI_LRU,
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
	 * L1 data cache data and tag correctable errors.
	 */
	"hdddc",		do_k_err,		NI_HD_DDC,
	MASK(ALL_BITS),		BIT(27)|BIT(2),
	MASK(0x1fe000),		BIT(19)|BIT(18),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 data cache data correctable error.",

	"kdddc",		do_k_err,		NI_KD_DDC,
	MASK(ALL_BITS),		BIT(26)|BIT(3),
	MASK(0x1fe000),		BIT(18)|BIT(17),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache data correctable error.",

	"kdddctl1",		do_k_err,		NI_KD_DDCTL1,
	MASK(ALL_BITS),		BIT(25)|BIT(4),
	MASK(0x1fe000),		BIT(17)|BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache data correctable error\n"
	"\t\t\tat trap level 1.",

	"hddtc",		do_k_err,		NI_HD_DTC,
	MASK(0x3ffffffe),	BIT(24)|BIT(5),
	BIT(13),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 data cache tag correctable error.",

	"kddtc",		do_k_err,		NI_KD_DTC,
	MASK(0x3ffffffe),	BIT(23)|BIT(6),
	BIT(13),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache tag correctable error.",

	"kddtctl1",		do_k_err,		NI_KD_DTCTL1,
	MASK(0x3ffffffe),	BIT(22)|BIT(7),
	BIT(13),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 data cache tag correctable error\n"
	"\t\t\tat trap level 1.",

	"dphys=offset,xor,data", do_k_err,		NI_DPHYS,
	MASK(ALL_BITS),		BIT(4)|BIT(5),
	MASK(0x1fe000),		BIT(15)|BIT(16),
	NULL,			NULL,
	"Insert a simulated error into the L1 data cache\n"
	"\t\t\t\tdata at byte offset \"offset\".",

	"dtphys=offset,xor",	do_k_err,		NI_DTPHYS,
	MASK(0x3ffffffe),	BIT(5)|BIT(6),
	BIT(13),		BIT(13),
	NULL,			NULL,
	"Insert a simulated error into the L1 data cache\n"
	"\t\t\t\ttag at byte offset \"offset\".",

	/*
	 * L1 instruction cache data and tag correctable errors.
	 */
	"hiidc",		do_k_err,		NI_HI_IDC,
	MASK(0xffffffff),	BIT(25)|BIT(9),
	MASK(BIT(32)),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 instruction cache data correctable\n"
	"\t\t\terror.",

	"kiidc",		do_k_err,		NI_KI_IDC,
	MASK(0xffffffff),	BIT(24)|BIT(8),
	MASK(BIT(32)),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache data correctable\n"
	"\t\t\terror.",

	"kiidctl1",		do_k_err,		NI_KI_IDCTL1,
	MASK(0xffffffff),	BIT(23)|BIT(7),
	MASK(BIT(32)),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache data correctable\n"
	"\t\t\terror at trap level 1.",

	"hiitc",		do_k_err,		NI_HI_ITC,
	MASK(0xfffffff),	BIT(22)|BIT(6),
	MASK(BIT(32)),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 instruction cache tag correctable\n"
	"\t\t\terror.",

	"kiitc",		do_k_err,		NI_KI_ITC,
	MASK(0xfffffff),	BIT(21)|BIT(5),
	MASK(BIT(32)),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag correctable\n"
	"\t\t\terror.",

	"kiitctl1",		do_k_err,		NI_KI_ITCTL1,
	MASK(0xfffffff),	BIT(20)|BIT(4),
	MASK(BIT(32)),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag correctable\n"
	"\t\t\terror at trap level 1.",

	"iphys=offset,xor,data", do_k_err,		NI_IPHYS,
	MASK(0xffffffff),	BIT(9)|BIT(3),
	MASK(BIT(32)),		BIT(32),
	NULL,			NULL,
	"Insert a simulated error into the L1\n"
	"\t\t\t\tinstruction cache data at byte offset \"offset\".",

	"itphys=offset,xor",	do_k_err,		NI_ITPHYS,
	MASK(0xfffffff),	BIT(8)|BIT(2),
	MASK(BIT(32)),		BIT(32),
	NULL,			NULL,
	"Insert a simulated error into the L1\n"
	"\t\t\t\tinstruction cache tag at byte offset \"offset\".",

	/*
	 * Instruction and data TLB data and tag (CAM) errors.
	 */
	"kddmdu",		do_k_err,		NI_KD_DMDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB data parity error.",

	"hddmtu",		do_k_err,		NI_HD_DMTU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper data D-TLB tag parity error.",

	"uddmdu",		do_u_err,		NI_UD_DMDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user data D-TLB data parity error.",

	"uddmtu",		do_u_err,		NI_UD_DMTU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user data D-TLB tag parity error.",

	"hddmduasi",		do_k_err,		NI_HD_DMDUASI,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper data D-TLB tag parity error via ASI\n"
	"\t\t\taccess.",

	"uddmduasi",		do_u_err,		NI_UD_DMDUASI,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user data D-TLB data parity error via ASI\n"
	"\t\t\taccess.",

	"kddmsu",		do_k_err,		NI_KD_DMSU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern data D-TLB data parity error on store.",

	"dmdurand",		do_k_err,		NI_DMDURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random D-TLB data parity error.",

	"dmturand",		do_k_err,		NI_DMTURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random D-TLB tag parity error, note that tag\n"
	"\t\t\terrors are not normally detected.",

	"kiimdu",		do_k_err,		NI_KI_IMDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern instruction I-TLB data parity error.",

	"hiimtu",		do_k_err,		NI_HI_IMTU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instruction I-TLB tag parity error.",

	"uiimdu",		do_u_err,		NI_UI_IMDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user instruction I-TLB data parity error.",

	"uiimtu",		do_u_err,		NI_UI_IMTU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user instruction I-TLB tag parity error.",

	"hiimduasi",		do_k_err,		NI_HI_IMDUASI,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instruction I-TLB data parity error via\n"
	"\t\t\tASI access.",

	"uiimduasi",		do_u_err,		NI_UI_IMDUASI,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user instruction I-TLB data parity error via\n"
	"\t\t\tASI access.",

	"imdurand",		do_k_err,		NI_IMDURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random I-TLB data parity error.",

	"imturand",		do_k_err,		NI_IMTURAND,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Inject a random I-TLB tag parity error, note that tag\n"
	"\t\t\terrors are not normally detected.",

	/*
	 * Integer register file (SPARC Internal) errors.
	 */
	"hdirul",		do_k_err,		NI_HD_IRUL,
	MASK(0xff),		BIT(4)|BIT(3),
	MASK(0xff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file uncorrectable ecc\n"
	"\t\t\terror on load access.",

	"hdirus",		do_k_err,		NI_HD_IRUS,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"hdiruo",		do_k_err,		NI_HD_IRUO,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* kern */
	"kdirul",		do_k_err,		NI_KD_IRUL,
	MASK(0xff),		BIT(7)|BIT(6),
	MASK(0xff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file uncorrectable ecc\n"
	"\t\t\terror on load access.",

	"kdirus",		do_k_err,		NI_KD_IRUS,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"kdiruo",		do_k_err,		NI_KD_IRUO,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* correctable */
	"hdircl",		do_k_err,		NI_HD_IRCL,
	MASK(0xff),		BIT(3),
	MASK(0xff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file correctable ecc\n"
	"\t\t\terror on load access.",

	"hdircs",		do_k_err,		NI_HD_IRCS,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file correctable ecc\n"
	"\t\t\terror on store access.",

	"hdirco",		do_k_err,		NI_HD_IRCO,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper integer register file correctable ecc\n"
	"\t\t\terror on op access.",

	/* ce kern */
	"kdircl",		do_k_err,		NI_KD_IRCL,
	MASK(0xff),		BIT(6),
	MASK(0xff),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file correctable ecc\n"
	"\t\t\terror on load access.",

	"kdircs",		do_k_err,		NI_KD_IRCS,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file correctable ecc\n"
	"\t\t\terror on store access.",

	"kdirco",		do_k_err,		NI_KD_IRCO,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern integer register file correctable ecc\n"
	"\t\t\terror on op access.",

	/*
	 * Floating-point register file (SPARC Internal) errors.
	 */
	"hdfrul",		do_k_err,		NI_HD_FRUL,
	MASK(0xff),		BIT(4)|BIT(3),
	MASK(0xff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file uncorrectable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"hdfrus",		do_k_err,		NI_HD_FRUS,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"hdfruo",		do_k_err,		NI_HD_FRUO,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* kern */
	"kdfrul",		do_k_err,		NI_KD_FRUL,
	MASK(0xff),		BIT(7)|BIT(6),
	MASK(0xff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file uncorrectable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"kdfrus",		do_k_err,		NI_KD_FRUS,
	MASK(0xff),		BIT(6)|BIT(5),
	MASK(0xff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file uncorrectable ecc\n"
	"\t\t\terror on store access.",

	"kdfruo",		do_k_err,		NI_KD_FRUO,
	MASK(0xff),		BIT(5)|BIT(4),
	MASK(0xff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file uncorrectable ecc\n"
	"\t\t\terror on op access.",

	/* correctable */
	"hdfrcl",		do_k_err,		NI_HD_FRCL,
	MASK(0xff),		BIT(3),
	MASK(0xff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file correctable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"hdfrcs",		do_k_err,		NI_HD_FRCS,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file correctable ecc\n"
	"\t\t\terror on store access.",

	"hdfrco",		do_k_err,		NI_HD_FRCO,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper FP register file correctable ecc\n"
	"\t\t\terror on op access.",

	/* ce kern */
	"kdfrcl",		do_k_err,		NI_KD_FRCL,
	MASK(0xff),		BIT(6),
	MASK(0xff),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file correctable ecc\n"
	"\t\t\terror on load access (no error detected by HW).",

	"kdfrcs",		do_k_err,		NI_KD_FRCS,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file correctable ecc\n"
	"\t\t\terror on store access.",

	"kdfrco",		do_k_err,		NI_KD_FRCO,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern FP register file correctable ecc\n"
	"\t\t\terror on op access.",

	/*
	 * Modular Arithmetic Unit (SPARC Internal) errors.
	 */
	"hdmaul",		do_k_err,		NI_HD_MAUL,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Insert a modular arithmetic parity error with load\n"
	"\t\t\taccess (no error detected by HW on load access).",

	"hdmaus",		do_k_err,		NI_HD_MAUS,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper modular arithmetic parity error on store.",

	"hdmauo",		do_k_err,		NI_HD_MAUO,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper modular arithmetic parity error on op.",

	/*
	 * JBus (system bus) errors.
	 */
	"kdbe",			do_k_err,		NI_KD_BE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern JBus bus-error on read.",

	"kpeekbe",		do_k_err,		NI_KD_BEPEEK,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern JBus bus-error on protected read.",

	"hdapar",		do_k_err,		NI_HD_APAR,
	MASK(0xf000000),	BIT(24),
	MASK(0xf000000),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper data JBus address parity error on read.",

	"hiapar",		do_notimp,		NI_HI_APAR,
	MASK(0xf000000),	BIT(26),
	MASK(0xf000000),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instruction JBus address parity error on read.",

	"hdcpar",		do_notimp,		NI_HD_CPAR,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus control parity error on read.",

	"hicpar",		do_notimp,		NI_HI_CPAR,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper instruction JBus control parity error on read.",

	"hddpar",		do_k_err,		NI_HD_DPAR,
	MASK(0xf000000),	BIT(24),
	MASK(0xf000000),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper data JBus data parity error on read.",

	"hidpar",		do_notimp,		NI_HI_DPAR,
	MASK(0xf000000),	BIT(26),
	MASK(0xf000000),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instruction JBus data parity error on read.",

	"hddpars",		do_k_err,		NI_HD_DPARS,
	MASK(0xf000000),	BIT(24),
	MASK(0xf000000),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper data JBus data parity error on store.",

	"hddparo",		do_notimp,		NI_HD_DPARO,
	MASK(0xf000000),	BIT(26),
	MASK(0xf000000),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper data JBus data parity error on other.",

	"hdl2to",		do_k_err,		NI_HD_L2TO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus L2 cache timeout error on read.",

	"hdarbto",		do_k_err,		NI_HD_ARBTO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus arbitration timeout error\n"
	"\t\t\ton read.",

	"hdrto",		do_k_err,		NI_HD_RTO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus transaction timeout error\n"
	"\t\t\ton read.",

	"hdintrto",		do_k_err,		NI_HD_INTRTO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus interrupt timeout error on read.",

	"hdums",		do_k_err,		NI_HD_UMS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus unmapped store error.",

	"hdnems",		do_k_err,		NI_HD_NEMS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus non-existent memory error\n"
	"\t\t\ton store.",

	"hdnemr",		do_k_err,		NI_HD_NEMR,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a hyper data JBus non-existent memory error\n"
	"\t\t\ton read.",

	"niclrjbi",		do_k_err,		NI_CLR_JBI_LOG,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"DEBUG test case to clear the contents of the JBI LOG\n"
	"\t\t\tregisters.",

	"niprintjbi",		do_k_err,		NI_PRINT_JBI,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"DEBUG test case to print the contents of the JBI\n"
	"\t\t\tregisters.",

	"nitestjbi",		do_k_err,		NI_TEST_JBI,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"DEBUG test case to ensure JBus framework is executing\n"
	"\t\t\tproperly.",

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

	"niprintssi",		do_k_err,		NI_PRINT_SSI,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"DEBUG test case to print the contents of the SSI\n"
	"\t\t\tregisters.",

	/*
	 * DEBUG test case to ensure framework calls HV correctly.
	 */
	"nitest",		do_k_err,		NI_TEST,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to ensure mtst framework can execute\n"
	"\t\t\tin HV mode correctly.",

	/*
	 * DEBUG test cases to print the current ESR values from HV.
	 */
	"niprintesrs",		do_k_err,		NI_PRINT_ESRS,
	MASK(ALL_BITS),		BIT(0),
	MASK(ALL_BITS),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to print the current ESR values from HV.",

	/*
	 * DEBUG test cases to print the current UE ERPT from HV.
	 */
	"niprintue",		do_k_err,		NI_PRINT_UE,
	MASK(ALL_BITS),		BIT(0)|BIT(1),
	MASK(ALL_BITS),		BIT(0)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to print the current UE ERPT from HV.",

	/*
	 * DEBUG test cases to print the current CE ERPT from HV.
	 */
	"niprintce",		do_k_err,		NI_PRINT_CE,
	MASK(ALL_BITS),		BIT(0),
	MASK(ALL_BITS),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to print the current CE ERPT from HV.",

	/*
	 * End of list.
	 */

	NULL,			NULL,		NULL,
	NULL,			NULL,		NULL,
	NULL};

static cmd_t *commands[] = {
	niagara_cmds,
	sun4v_generic_cmds,
	NULL
};

static  opsvec_t operations = {
	gen_flushall_l2,	/* requires L2$ to be in DM mode first */
	ni_pre_test,		/* pre-test routine */
	NULL			/* no post-test routine */

};

void
ni_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}

/*
 * This routine gets executed prior to running a test.
 */
int
ni_pre_test(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	/*
	 * If this is a Data-cache test then ensure that the check-bit
	 * flag is set since Niagara HW can only inject check-bit errors.
	 */
	if (ERR_CLASS_ISDC(iocp->ioc_command) &&
	    !ERR_SUBCLASS_ISMH(iocp->ioc_command) && !F_CHKBIT(iocp)) {
		msg(MSG_ERROR, "Niagara family processors can only inject "
		    "L1 data cache errors into the data and tag "
		    "check-bits, please use the -c option to produce "
		    "this type of error");
		return (EIO);
	}

	return (0);
}
