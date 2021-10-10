/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains sun4v Victoria Falls (US-T2plus) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtestio_n2.h>
#include <sys/memtestio_vf.h>
#include "mtst.h"
#include "mtst_vf.h"

/*
 * Victoria Falls specific routines located in this file, and externs.
 */
void		vf_init(mdata_t *);
int		vf_pre_test(mdata_t *);
static int	vf_check_lfu_options(mdata_t *);
static void	vf_copy_asm_1GB(mdata_t *);
static void	vf_copy_asm_512B(mdata_t *);
static int	vf_get_mem_node_id(cpu_info_t *, uint64_t);
static int	vf_get_num_ways(mdata_t *);
static int	vf_is_512B_interleave(cpu_info_t *, uint64_t);

/*
 * These Victoria Falls errors are grouped according to the definitions
 * in the Niagara-I, Niagara-II, and Victoria Falls header files.
 *
 * Note that ALL the tests available for Victoria Falls are listed in this
 * file including the ones that exist for Niagara-I and II.  This way similar
 * tests can be given new command names (to match PRM), without having
 * all the Niagara-I and II tests listed as available since not all are
 * applicable.
 *
 * NOTE: could add the concept of common NI tests, maybe with
 *	 a cputype field defined like: GNI = (NI1 | N12), but that
 *	 means the cputype will have to be changed to a bitfield...
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t vfalls_cmds[] = {

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
	MASK(0xffff),		BIT(11)|BIT(4),
	MASK(0xffff),		BIT(11)|BIT(4),
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
	BIT(25)|BIT(24),	BIT(25)|BIT(24),
	BIT(25)|BIT(24),	BIT(25)|BIT(24),
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
	 * Remote memory (DRAM) uncorrectable errors
	 */
	"hdfdau",		do_k_err,		VF_HD_FDAU,
	MASK(0xffff),		BIT(15)|BIT(0),
	MASK(0xffff),		BIT(15)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a remote hyper memory uncorrectable ecc error.",

	"hdfdauma",		do_k_err,		VF_HD_FDAUMA,
	MASK(0xffff),		BIT(14)|BIT(1),
	MASK(0xffff),		BIT(14)|BIT(1),
	OFFSET(0),		NULL,
	"Cause a remote hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdfdaucwq",		do_k_err,		VF_HD_FDAUCWQ,
	MASK(0xffff),		BIT(13)|BIT(2),
	MASK(0xffff),		BIT(13)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a remote hyper data uncorrectable memory error via\n"
	"\t\t\ta control word queue operation.",

	"kdfdau",		do_k_err,		VF_KD_FDAU,
	MASK(0xffff),		BIT(12)|BIT(3),
	MASK(0xffff),		BIT(12)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern memory uncorrectable ecc error.",

	"kdfdautl1",		do_k_err,		VF_KD_FDAUTL1,
	MASK(0xffff),		BIT(9)|BIT(6),
	MASK(0xffff),		BIT(9)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a remote kern data uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"kdfdaupr",		do_k_err,		VF_KD_FDAUPR,
	MASK(0xffff),		BIT(8)|BIT(7),
	MASK(0xffff),		BIT(8)|BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data uncorrectable memory error via\n"
	"\t\t\ta prefetch.",

	"kifdau",		do_k_err,		VF_KI_FDAU,
	MASK(0xffff),		BIT(7)|BIT(8),
	MASK(0xffff),		BIT(7)|BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern instr uncorrectable memory error.",

	"kifdautl1",		do_k_err,		VF_KI_FDAUTL1,
	MASK(0xffff),		BIT(6)|BIT(9),
	MASK(0xffff),		BIT(6)|BIT(9),
	OFFSET(0),		NULL,
	"Cause a remote kern instr uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"udfdau",		do_u_cp_err,		VF_UD_FDAU,
	MASK(0xffff),		BIT(5)|BIT(10),
	MASK(0xffff),		BIT(5)|BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a remote user data uncorrectable memory error.",

	"uifdau",		do_u_cp_err,		VF_UI_FDAU,
	MASK(0xffff),		BIT(4)|BIT(11),
	MASK(0xffff),		BIT(4)|BIT(11),
	OFFSET(0),		NULL,
	"Cause a remote user instr uncorrectable memory error.",

	"iofdru",		do_u_cp_err,		VF_IO_FDRU,
	MASK(0xffff),		BIT(2)|BIT(13),
	MASK(0xffff),		BIT(2)|BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a remote IO (DMA) uncorrectable memory error.",

	/*
	 * Remote memory (DRAM) correctable errors.
	 */
	"hdfdac",		do_k_err,		VF_HD_FDAC,
	MASK(0xffff),		BIT(2)|BIT(1),
	MASK(0xffff),		BIT(9),
	OFFSET(0),		OFFSET(0),
	"Cause a remote hyper memory correctable ecc error.",

	"hdfdacma",		do_k_err,		VF_HD_FDACMA,
	MASK(0xffff),		BIT(6),
	MASK(0xffff),		BIT(11),
	OFFSET(0),		NULL,
	"Cause a remote hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdfdaccwq",		do_k_err,		VF_HD_FDACCWQ,
	MASK(0xffff),		BIT(7),
	MASK(0xffff),		BIT(11),
	OFFSET(0),		NULL,
	"Cause a remote hyper data correctable memory error via\n"
	"\t\t\ta control word queue operation.",

	"kdfdac",		do_k_err,		VF_KD_FDAC,
	MASK(0xffff),		BIT(3)|BIT(2),
	MASK(0xffff),		BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern memory correctable ecc error.",

	"kdfdactl1",		do_k_err,		VF_KD_FDACTL1,
	MASK(0xffff),		BIT(8),
	MASK(0xffff),		BIT(12),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"kdfdacpr",		do_k_err,		VF_KD_FDACPR,
	MASK(0xffff),		BIT(9),
	MASK(0xffff),		BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data correctable memory error via\n"
	"\t\t\ta prefetch.",

	"kdfdacstorm",		do_k_err,		VF_KD_FDACSTORM,
	MASK(0xffff),		BIT(10),
	MASK(0xffff),		BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data correctable memory error storm\n"
	"\t\t\t(use misc1 for count, default is 64 errors).",

	"kifdac",		do_k_err,		VF_KI_FDAC,
	MASK(0xffff),		BIT(11),
	MASK(0xffff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern instr correctable memory error.",

	"kifdactl1",		do_k_err,		VF_KI_FDACTL1,
	MASK(0xffff),		BIT(12),
	MASK(0xffff),		BIT(2),
	OFFSET(0),		NULL,
	"Cause a remote kern instr correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"udfdac",		do_u_cp_err,		VF_UD_FDAC,
	MASK(0xffff),		BIT(13),
	MASK(0xffff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a remote user data correctable memory error.",

	"uifdac",		do_u_cp_err,		VF_UI_FDAC,
	MASK(0xffff),		BIT(14),
	MASK(0xffff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a remote user instr correctable memory error.",

	"iofdrc",		do_u_cp_err,		VF_IO_FDRC,
	MASK(0xffff),		BIT(13),
	MASK(0xffff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a remote IO (DMA) correctable memory error.",

	/*
	 * Remote L2 cache data (copy back) errors.
	 */
	"hdlcbu",		do_k_err,		VF_HD_L2CBU,
	MASK(ALL_BITS),		BIT(30)|BIT(21),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back uncorrectable error.",

	"hilcbu",		do_k_err,		VF_HI_L2CBU,
	MASK(ALL_BITS),		BIT(29)|BIT(20),
	MASK(0x7f),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction copy back uncorrectable error.",

	"kdlcbu",		do_k_err,		VF_KD_L2CBU,
	MASK(ALL_BITS),		BIT(27)|BIT(18),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back uncorrectable error.",

	"kilcbu",		do_k_err,		VF_KI_L2CBU,
	MASK(ALL_BITS),		BIT(21)|BIT(12),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instruction copy back uncorrectable error.",

	"udlcbu",		do_u_cp_err,		VF_UD_L2CBU,
	MASK(ALL_BITS),		BIT(19)|BIT(10),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache data copy back uncorrectable error.",

	"uilcbu",		do_u_cp_err,		VF_UI_L2CBU,
	MASK(ALL_BITS),		BIT(18)|BIT(9),
	MASK(0x7f),		BIT(0)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction copy back uncorrectable error.",

	"hdlcbc",		do_k_err,		VF_HD_L2CBC,
	MASK(ALL_BITS),		BIT(31),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back correctable error.",

	"hilcbc",		do_k_err,		VF_HI_L2CBC,
	MASK(ALL_BITS),		BIT(30),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction copy back correctable error.",

	"kdlcbc",		do_k_err,		VF_KD_L2CBC,
	MASK(ALL_BITS),		BIT(28),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back correctable error.",

	"kilcbc",		do_k_err,		VF_KI_L2CBC,
	MASK(ALL_BITS),		BIT(20),
	MASK(0x7f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instruction copy back correctable error.",

	"udlcbc",		do_u_cp_err,		VF_UD_L2CBC,
	MASK(ALL_BITS),		BIT(18),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache data copy back correctable error.",

	"uilcbc",		do_u_cp_err,		VF_UI_L2CBC,
	MASK(ALL_BITS),		BIT(17),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction copy back correctable error.",

	"hdlwbu",		do_k_err,		VF_HD_LWBU,
	MASK(ALL_BITS),		BIT(46)|BIT(45),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data uncorrectable write-back\n"
	"\t\t\tto remote memory error.",

	"hilwbu",		do_k_err,		VF_HI_LWBU,
	MASK(ALL_BITS),		BIT(46)|BIT(45),
	MASK(0x7f),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction uncorrectable write-back\n"
	"\t\t\tto remote memory error.",

	"kdlwbu",		do_k_err,		VF_KD_LWBU,
	MASK(ALL_BITS),		BIT(45)|BIT(44),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data uncorrectable write-back\n"
	"\t\t\tto remote memory error.",

	"kilwbu",		do_k_err,		VF_KI_LWBU,
	MASK(ALL_BITS),		BIT(44)|BIT(43),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instruction uncorrectable write-back\n"
	"\t\t\tto remote memory error.",

	"udlwbu",		do_u_cp_err,		VF_UD_LWBU,
	MASK(ALL_BITS),		BIT(42)|BIT(41),
	MASK(0x7f),		BIT(2)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache data uncorrectable write-back\n"
	"\t\t\tto remote memory error.",

	"iomcufbrf",		do_k_err,		VF_HD_MCUFBRF,
	0xffff,			0xffff,
	0xffff,			0xffff,
	OFFSET(0),		OFFSET(0),
	"Cause a FBDIMM lane failover.",

	"clto",			do_k_err,		VF_CLTO,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a coherency link timeout.",

	/*
	 * The XOR patterns for the LFU tests are the values written to
	 * the LFU SERDES Transmitter and Receiver Differential Pair
	 * Inversion Register.
	 */

	"lfurtf",		do_k_err,		VF_LFU_RTF,
	MASK(ALL_BITS),		BIT(1),
	MASK(ALL_BITS),		BIT(52),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU retrain failure.",

	"lfutto",		do_k_err,		VF_LFU_TTO,
	MASK(ALL_BITS),		BIT(7)|BIT(11)|(BIT(7) << 14)|(BIT(11) << 14),
	MASK(ALL_BITS),		BIT(5)|BIT(13)|(BIT(5) << 14)|(BIT(13) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU training timeout.",

	"lfucto",		do_k_err,		VF_LFU_CTO,
	MASK(ALL_BITS),		BIT(3)|(BIT(3) << 14),
	MASK(ALL_BITS),		BIT(9)|(BIT(9) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU configure timeout.",

	"lfumlf",		do_k_err,		VF_LFU_MLF,
	MASK(ALL_BITS),		BIT(4)|(BIT(10) << 14),
	MASK(ALL_BITS),		BIT(8)|(BIT(6) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU multi-lane failure.",

	"lfuslf",		do_k_err,		VF_LFU_SLF,
	MASK(ALL_BITS),		BIT(2)|(BIT(2) << 14),
	MASK(ALL_BITS),		BIT(12)|(BIT(12) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU single-lane failure.",

	"ioncxto", 		do_k_err,		VF_IO_NCXFSRTO,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"Cause a NCX timeout.",

	/*
	 * DEBUG test case to ensure framework calls HV correctly.
	 */
	"vftest",		do_k_err,		N2_TEST,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to ensure mtst framework can execute\n"
	"\t\t\tin HV mode correctly.",

	/*
	 * DEBUG test case for examining various error register values.
	 */
	"vfprintesrs",		do_k_err,		VF_PRINT_ESRS,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case for examining various error register values.",

	/*
	 * DEBUG test case for clearing various ESR values.
	 */
	"vfclearesrs",		do_k_err,		VF_CLEAR_ESRS,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case for clearing various ESR values.",

	"vfsetsteer",		do_k_err,		VF_SET_STEER,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test for setting the L2 and SOC error steer values\n"
	"\t\t\tDo not use unless you know what you are doing!",

	/*
	 * End of list.
	 */

	NULL,			NULL,		NULL,
	NULL,			NULL,		NULL,
	NULL};

static cmd_t *commands[] = {
	vfalls_cmds,
	sun4v_generic_cmds,
	NULL
};

static opsvec_t operations = {
	gen_flushall_l2,	/* requires L2$ to be in DM mode first */
	vf_pre_test,		/* pre-test routine (using N1 routine) */
	NULL			/* no post-test routine */

};

int
vf_adjust_buf_to_local(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	int		cpu_node_id;
	int		mem_node_id;
	uint32_t	offset;
	uint64_t	paddr;
	uint64_t	raddr;
	char		*fname = "vf_adjust_buf_to_local";

	/*
	 * Only memory error injections require that the memory
	 * buffers used by the EI be local to the CPU performing
	 * the injection.  This is because the memory error
	 * injection registers are accessed locally by the CPU
	 * of a given VF node and they can only be used to inject
	 * errors into the local memory of that node.
	 */
	if (!ERR_CLASS_ISMEM(iocp->ioc_command) &&
	    !ERR_CLASS_ISMCU(iocp->ioc_command)) {
		return (0);
	}

	cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	if ((raddr = uva_to_pa((uint64_t)(uintptr_t)mdatap->m_databuf,
	    mdatap->m_iocp->ioc_pid)) == -1) {
		msg(MSG_ERROR, "%s: failed to adjust buffers\n", fname);
		return (-1);
	}

	mem_node_id = vf_get_mem_node_id(cip, raddr);
	if (mem_node_id == -1) {
		msg(MSG_ERROR, "%s: failed to adjust buffers\n", fname);
		return (-1);
	}

	if (mem_node_id == cpu_node_id) {
		msg(MSG_DEBUG3, "%s: buffers already local to CPU %d\n", fname,
		    cip->c_cpuid);
		return (0);
	}

	if ((paddr = ra_to_pa(raddr)) == -1) {
		msg(MSG_ERROR, "%s: cannot determine memory interleave",
		    fname);
		return (-1);
	}

	if (!vf_is_512B_interleave(cip, paddr)) {
		msg(MSG_ERROR, "mem buffer (raddr=0x%llx, nodeid=0x%x) is "
		    "not local to CPU %d (nodeid=0x%x) due to 1GB "
		    "interleave\n", raddr, mem_node_id, cip->c_cpuid,
		    cpu_node_id);
		msg(MSG_ERROR, "local memory is required for memory tests\n");
		return (-1);
	}

	msg(MSG_DEBUG3, "%s: adjust buffers local to CPU %d\n", fname,
	    cip->c_cpuid);

	offset = cpu_node_id * 512;

	mdatap->m_databuf += offset;

	/*
	 * XXX Adjust m_instbuf, too?  Would need to rewrite copy asm routines.
	 *
	 * mdatap->m_instbuf += offset;
	 */

	msg(MSG_DEBUG3, "%s: ioc_databuf=0x%llx, m_databuf=0x%lx, "
	    "m_instbuf=0x%lx, ioc_addr=0x%lx\n", fname, iocp->ioc_databuf,
	    mdatap->m_databuf, mdatap->m_instbuf, iocp->ioc_addr);

	return (0);
}

static int
vf_check_lfu_options(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;

	if (F_MISC1(iocp)) {
		if (iocp->ioc_misc1 > (VF_LFU_MAX_UNITS - 1)) {
			msg(MSG_ERROR, "Invalid LFU unit (0x%x) specified\n",
			    iocp->ioc_misc1);
			return (EIO);
		}
	}

	if (F_MISC2(iocp)) {
		if (iocp->ioc_misc2 > VF_LFU_MAX_LANE_SELECT) {
			msg(MSG_ERROR, "Invalid LFU lane selection (0x%x) "
			    "specified\n", iocp->ioc_misc2);
			return (EIO);
		}
	}

	return (0);
}

/*
 * Copy asm routines into the buffer when the interleave is 1GB.
 * The routines are copied into successive 256-byte chunks.
 */
static void
vf_copy_asm_1GB(mdata_t *mdatap)
{
	caddr_t		tmpvaddr;
	int		len = 256;

	tmpvaddr = mdatap->m_instbuf;

	bcopy((caddr_t)asmld, tmpvaddr, len);
	mdatap->m_asmld = (asmld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)asmldst, tmpvaddr, len);
	mdatap->m_asmldst = (asmldst_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)blkld, tmpvaddr, len);
	mdatap->m_blkld = (blkld_t *)(tmpvaddr);
}

/*
 * Copy asm routines into the buffer when the interleave is 512B.
 * The routines are copied into 256-byte chunks within the
 * appropriate 512-byte interleave boundaries in order to keep
 * them in memory local (or non-local) to the appropriate CPU.
 */
static void
vf_copy_asm_512B(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	caddr_t		tmpvaddr;
	int		cpu_node_id;
	int		len, way;
	char		*fname = "vf_copy_asm_512B";

	msg(MSG_DEBUG2, "%s: copy data local to CPU %d\n", fname, cip->c_cpuid);

	len = 256;

	if ((way = vf_get_num_ways(mdatap)) == 1) {
		msg(MSG_DEBUG2, "%s: single way (node) system detected, "
		    "asm routines will not be moved\n", fname);
		return;
	}

	cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	tmpvaddr = mdatap->m_instbuf + (512 * cpu_node_id);

	/*
	 * The current size of m_instbuf is 4096 bytes (the upper
	 * half of an 8k page).  On a 4-way Victoria Falls system
	 * this means that for each node there are two 512-byte
	 * areas of memory in the buffer that are local to that
	 * node.
	 *
	 * Copy asmld() and asmldst() into the first 512 bytes
	 * of the buffer that are local to the specified CPU.
	 * These routines are each 144-bytes in length.
	 */
	tmpvaddr = mdatap->m_instbuf + (512 * cpu_node_id);
	bcopy((caddr_t)asmld, tmpvaddr, len);
	mdatap->m_asmld = (asmld_t *)(tmpvaddr);

	tmpvaddr += len;
	bcopy((caddr_t)asmldst, tmpvaddr, len);
	mdatap->m_asmldst = (asmldst_t *)(tmpvaddr);

	/*
	 * Copy blkld() into the next 512 bytes.
	 */
	tmpvaddr = mdatap->m_instbuf + (512 * way) + (512 * cpu_node_id);
	bcopy((caddr_t)blkld, tmpvaddr, len);
	mdatap->m_blkld = (blkld_t *)(tmpvaddr);
}

/*
 * Get the node id of a physical address.
 */
static int
vf_get_mem_node_id(cpu_info_t *cip, uint64_t raddr)
{
	int		mem_node_id;
	uint64_t	paddr;
	char		*fname = "vf_get_mem_node_id";

	/*
	 * What appears to Solaris as a physical address is actually
	 * a "real" address.  Convert it to its true physical address
	 * so we can figure out which node it is local to.
	 */
	if ((paddr = ra_to_pa(raddr)) == -1) {
		msg(MSG_ERROR, "%s: cannot determine memory locality",
		    fname);
		return (-1);
	}

	/*
	 * Get the node that raddr belongs to.
	 */
	switch (VF_SYS_MODE_GET_WAY(cip->c_sys_mode)) {
	case VF_SYS_MODE_1_WAY:
		mem_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);
		break;

	case VF_SYS_MODE_2_WAY:
		if (vf_is_512B_interleave(cip, paddr))
			mem_node_id = VF_2WY_512B_ADDR_NODEID(paddr);
		else
			mem_node_id = VF_2WY_1GB_ADDR_NODEID(paddr);
		break;

	case VF_SYS_MODE_3_WAY:
		/*
		 * There is no 512B interleave on a 3-way system and the
		 * 1GB interleave is equivalent to that of a 4-way
		 * system.
		 */
		mem_node_id = VF_4WY_1GB_ADDR_NODEID(paddr);
		break;

	case VF_SYS_MODE_4_WAY:
		if (vf_is_512B_interleave(cip, paddr))
			mem_node_id = VF_4WY_512B_ADDR_NODEID(paddr);
		else
			mem_node_id = VF_4WY_1GB_ADDR_NODEID(paddr);
		break;

	default:
		msg(MSG_ERROR, "%s: internal error", fname);
		return (-1);
	}

	return (mem_node_id);
}

static int
vf_get_num_ways(mdata_t *mdatap)
{
	char	*fname = "vf_get_num_ways";

	switch (VF_SYS_MODE_GET_WAY(mdatap->m_cip->c_sys_mode)) {
	case VF_SYS_MODE_1_WAY:
		return (1);
	case VF_SYS_MODE_2_WAY:
		return (2);
	case VF_SYS_MODE_3_WAY:
		return (3);
	case VF_SYS_MODE_4_WAY:
		return (4);
	default:
		msg(MSG_ERROR, "%s: internal error", fname);
		return (-1);
	}
}

void
vf_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}

/*
 * Determine if a physical address falls with the range of memory
 * interleaved at 512-byte boundaries.
 */
static int
vf_is_512B_interleave(cpu_info_t *cip, uint64_t paddr)
{
	char	*fname = "vf_is_512B_interleave";

	uint64_t ceiling_mask = VF_CEILING_MASK(cip->c_l2_ctl);

	msg(MSG_DEBUG3, "%s: ceiling mask is 0x%llx\n", fname, ceiling_mask);

	if (VF_ADDR_INTERLEAVE_BITS(paddr) >= ceiling_mask) {
		msg(MSG_DEBUG2, "%s: paddr 0x%llx is part of 1GB interleave\n",
		    fname, paddr);
		return (0);	/* addr is in 1GB interleave region */
	} else {
		msg(MSG_DEBUG2, "%s: paddr 0x%llx is part of 512B interleave\n",
		    fname, paddr);
		return (1);	/* addr is in 512B interleave region */
	}
}

/*
 * Determine if a physical address is local to a particular CPU.
 */
int
vf_mem_is_local(cpu_info_t *cip, uint64_t raddr)
{
	int		cpu_node_id;
	int		mem_node_id;
	char		*fname = "vf_mem_is_local";

	/*
	 * Get the node that the CPU belongs to, and then get the node
	 * that raddr belongs to. If they're the same, then return true
	 */
	cpu_node_id = VF_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	mem_node_id = vf_get_mem_node_id(cip, raddr);

	msg(MSG_DEBUG2, "%s: cpu_node_id = 0x%x, mem_node_id = 0x%x, "
	    "raddr = 0x%llx\n", fname, cpu_node_id,
	    mem_node_id, raddr);

	if (cpu_node_id == mem_node_id)
		return (1);
	else
		return (0);
}

/*
 * This routine gets executed prior to running a test.
 */
int
vf_pre_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		tmp_thr_criteria;
	uint64_t	tmp_thr_data;
	int		ret = 0;

	/*
	 * If this is a Data-cache test then ensure that the check-bit
	 * flag is set since Niagara HW can only inject check-bit errors.
	 */
	if (ERR_CLASS_ISDC(iocp->ioc_command) &&
	    !ERR_SUBCLASS_ISMH(iocp->ioc_command) && !F_CHKBIT(iocp)) {
		msg(MSG_ERROR, "Niagara family processors can only inject\n");
		msg(MSG_ERROR, "L1 data cache errors into the data and tag\n");
		msg(MSG_ERROR, "check-bits.  Please use the -c option to\n");
		msg(MSG_ERROR, "produce this type of error\n");
		return (EIO);
	}

	if (ERR_CLASS_ISLFU(iocp->ioc_command)) {
		ret = vf_check_lfu_options(mdatap);
		if (ret)
			return (ret);
	}

	if (!ERR_VF_ISFR(iocp->ioc_command)) {
		/*
		 * Memory errors can only be injected into local memory.
		 * This covers the producer thread for single threaded tests.
		 */
		if (ERR_CLASS_ISMEM(IOC_COMMAND(iocp))) {
			if ((IOC_FLAGS(iocp) & FLAGS_BINDCPU) == 0) {
				iocp->ioc_bind_thr_criteria[0] =
				    THREAD_BIND_LOCAL_MEM;
			} else if (iocp->ioc_bind_thr_criteria[0] &
			    THREAD_BIND_REMOTE_MEM) {
				msg(MSG_ERROR, "memory injections must "
				    "be performed to local memory, "
				    "cannot use the \"remote_mem\" option\n");
				return (ENXIO);
			}
		}

		return (0);
	}

	/*
	 * Except for the remote writeback tests:
	 * Thread 0 is the consumer and references remote memory.
	 * Thread 1 is the producer and must inject the error into local memory
	 * or local L2 cache.
	 *
	 * For the remote writeback tests:
	 * Thread 0 allocates local memory.
	 * Thread 1 is the producer and injects an error into its local L2 cache
	 * that references the memory allocated by thread 0 which must be remote
	 * to thread 1.
	 */

	iocp->ioc_nthreads = 2;

	/*
	 * If user specified options have not already set the
	 * cpu bindings then set them here.
	 *
	 * For multi-threaded tests except for remote L2 writeback tests:
	 * When the user specifies CPU binding (i.e. -bcpuid=<x>[,cpuid=<y>]),
	 * then the thread criteria data is swapped here.  This means that
	 * the first user-specified CPU in the options will always be the
	 * producer and perform the injection into its local resource.  This
	 * is necessary since the injection code binds itself to this CPU
	 * when setting up data structures, etc.., and when VF code copies
	 * asm functions into memory, it copies them local to this CPU since
	 * other CPUs have not been chosen yet.  Ensuring that the first CPU
	 * that mtst binds to is the injecting CPU keeps everything consistent.
	 *
	 * For remote L2 writeback tests:
	 * mtst allocates memory local to the CPU it is bound to during its
	 * initialization.  This means that the allocated memory will be
	 * local to the first user-specified CPU to bind to.  Since this
	 * memory must be remote to the producer, the CPU is instead
	 * assigned to be the consumer, unlike above.  This is accomplished
	 * by not swapping the thread criteria data.
	 *
	 * When no CPU (or only one) is specified, setting the appropriate
	 * thread criteria here ensures that choose_thr_cpu() picks the right
	 * CPU(s).
	 */
	if ((IOC_FLAGS(iocp) & FLAGS_BINDCPU) != 0 &&
	    ERR_CLASS_ISL2WB(IOC_COMMAND(iocp)) == 0) {
		tmp_thr_criteria = iocp->ioc_bind_thr_criteria[0];
		tmp_thr_data = iocp->ioc_bind_thr_data[0];
		iocp->ioc_bind_thr_criteria[0] = iocp->ioc_bind_thr_criteria[1];
		iocp->ioc_bind_thr_data[0] = iocp->ioc_bind_thr_data[1];
		iocp->ioc_bind_thr_criteria[1] = tmp_thr_criteria;
		iocp->ioc_bind_thr_data[1] = tmp_thr_data;
	}

	/*
	 * Return if the user specified CPU IDs for both threads.
	 */
	if (iocp->ioc_bind_thr_criteria[0] == THREAD_BIND_CPUID &&
	    iocp->ioc_bind_thr_criteria[1] == THREAD_BIND_CPUID) {
		return (0);
	}

	if (iocp->ioc_bind_thr_criteria[0] == THREAD_BIND_DEFAULT) {
		if (ERR_CLASS_ISL2WB(IOC_COMMAND(iocp))) {
			iocp->ioc_bind_thr_criteria[0] = THREAD_BIND_LOCAL_MEM;
		} else {
			iocp->ioc_bind_thr_criteria[0] = THREAD_BIND_REMOTE_MEM;
		}
	}

	if (iocp->ioc_bind_thr_criteria[1] == THREAD_BIND_DEFAULT) {
		if (ERR_CLASS_ISL2WB(IOC_COMMAND(iocp))) {
			iocp->ioc_bind_thr_criteria[1] = THREAD_BIND_REMOTE_MEM;
		} else {
			iocp->ioc_bind_thr_criteria[1] = THREAD_BIND_LOCAL_MEM;
		}
	}

	return (0);
}

int
vf_pre_test_copy_asm(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	raddr;
	uint64_t	paddr;
	char		*fname = "vf_pre_test_copy_asm";

	/*
	 * Convert the address of m_instbuf into a true physical address
	 * which is needed to determine the type of memory interleave between
	 * nodes.
	 */
	if ((raddr = uva_to_pa((uint64_t)(uintptr_t)mdatap->m_instbuf,
	    mdatap->m_iocp->ioc_pid)) == -1) {
		msg(MSG_ERROR, "%s: cannot determine memory locality", fname);
		return (-1);
	}

	if ((paddr = ra_to_pa(raddr)) == -1) {
		msg(MSG_ERROR, "%s: cannot determine memory locality", fname);
		return (-1);
	}

	/*
	 * We have to get fancy if the interleave for the buffer
	 * is 512B in order to ensure all of the routines copied
	 * are in the same memory locality.
	 *
	 * The current size of m_instbuf is 4096 bytes (the upper
	 * half of an 8k page).  On a 4-way
	 * Victoria Falls system this means that for each node
	 * there are two 512-byte areas of memory in the buffer
	 * that are local to that node.
	 *
	 * If it is 1GB interleave we duplicate the default behavior
	 * and simply copy all of the routines in.
	 */
	if (vf_is_512B_interleave(cip, paddr) &&
	    (vf_get_num_ways(mdatap) != 1)) {
		vf_copy_asm_512B(mdatap);
	} else {
		vf_copy_asm_1GB(mdatap);
	}

	return (0);
}
