/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains sun4v Rainbow Falls (US-RF aka US-KT) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_v.h>
#include <sys/memtestio_ni.h>
#include <sys/memtestio_n2.h>
#include <sys/memtestio_vf.h>
#include <sys/memtestio_kt.h>
#include "mtst.h"
#include "mtst_kt.h"

/*
 * RF (KT) specific routines located in this file, and externs.
 */
	void	kt_init(mdata_t *);
static	int	kt_get_mem_node_id(cpu_info_t *, uint64_t);
	int	kt_pre_test(mdata_t *);

/*
 * These Rainbow Falls (aka KT) errors are grouped according to the definitions
 * in the KT specific header files.
 *
 * Note that ALL the tests available for Rainbow Falls (KT) are listed in this
 * file including the ones that exist for other members of the Niagara family.
 * This way similar tests can be given new command names (to match PRM) and
 * commands which are not applicablt to KT are not listed as available.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t kt_cmds[] = {

	/*
	 * Memory (DRAM) uncorrectable errors.
	 */
	"hddau",		do_k_err,		NI_HD_DAU,
	MASK(0xffffffff),	BIT(17)|BIT(0),
	MASK(0xffffffff),	BIT(17)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory uncorrectable ecc error.",

	"hddauint",		do_k_err,		KT_HD_DAUINT,
	MASK(0xffffffff),	BIT(18)|BIT(1),
	MASK(0xffffffff),	BIT(18)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an intermittent hyper memory uncorrectable ecc\n"
	"\t\t\terror.",

	"hddauma",		do_k_err,		NI_HD_DAUMA,
	MASK(0xffffffff),	BIT(19)|BIT(2),
	MASK(0xffffffff),	BIT(19)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hddaucwq",		do_k_err,		N2_HD_DAUCWQ,
	MASK(0xffffffff),	BIT(20)|BIT(3),
	MASK(0xffffffff),	BIT(20)|BIT(3),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta control word queue operation.",

	"hidau",		do_k_err,		NI_HI_DAU,
	MASK(0xffffffff),	BIT(21)|BIT(5),
	MASK(0xffffffff),	BIT(21)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instr memory uncorrectable ecc error.",

	"kddau",		do_k_err,		NI_KD_DAU,
	MASK(0xffffffff),	BIT(22)|BIT(4),
	MASK(0xffffffff),	BIT(22)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable ecc error.",

	"kddaudtlb",		do_k_err,		N2_KD_DAUDTLB,
	MASK(0xffffffff),	BIT(23)|BIT(4),
	MASK(0xffffffff),	BIT(23)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable memory error via\n"
	"\t\t\ta data TLB fill operation.",

	"kidauitlb",		do_k_err,		N2_KI_DAUITLB,
	MASK(0xffffffff),	BIT(24)|BIT(5),
	MASK(0xffffffff),	BIT(24)|BIT(5),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable memory error via\n"
	"\t\t\tan instr TLB fill operation.",

	"kddautl1",		do_k_err,		NI_KD_DAUTL1,
	MASK(0xffffffff),	BIT(25)|BIT(6),
	MASK(0xffffffff),	BIT(25)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"kidau",		do_k_err,		NI_KI_DAU,
	MASK(0xffffffff),	BIT(26)|BIT(8),
	MASK(0xffffffff),	BIT(26)|BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr uncorrectable memory error.",

	"kidautl1",		do_k_err,		NI_KI_DAUTL1,
	MASK(0xffffffff),	BIT(27)|BIT(9),
	MASK(0xffffffff),	BIT(27)|BIT(9),
	OFFSET(0),		NULL,
	"Cause a kern instr uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"uddau",		do_u_err,		NI_UD_DAU,
	MASK(0xffffffff),	BIT(28)|BIT(10),
	MASK(0xffffffff),	BIT(28)|BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a user data uncorrectable memory error.",

	"uidau",		do_u_err,		NI_UI_DAU,
	MASK(0xffffffff),	BIT(29)|BIT(11),
	MASK(0xffffffff),	BIT(29)|BIT(11),
	OFFSET(0),		NULL,
	"Cause a user instr uncorrectable memory error.",

	"kddsu",		do_k_err,		NI_KD_DSU,
	MASK(0xffffffff),	BIT(30)|BIT(12),
	MASK(0xffffffff),	BIT(30)|BIT(12),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable ecc error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"kddbu",		do_k_err,		NI_KD_DBU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory uncorrectable out of range error.",

	"iodru",		do_io_err,		NI_IO_DRU,
	MASK(0xffffffff),	BIT(31)|BIT(13),
	MASK(0xffffffff),	BIT(31)|BIT(13),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) uncorrectable memory error.",

	/*
	 * Memory (DRAM) correctable errors.
	 */
	"hddac",		do_k_err,		NI_HD_DAC,
	MASK(0xffffffff),	BIT(2)|BIT(1),
	MASK(0xffffffff),	BIT(9),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory correctable ecc error.",

	"hddacint",		do_k_err,		KT_HD_DACINT,
	MASK(0xffffffff),	BIT(14),
	MASK(0xffffffff),	BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause an intermittent hyper memory correctable ecc\n"
	"\t\t\terror.",

	"hddacma",		do_k_err,		NI_HD_DACMA,
	MASK(0xffffffff),	BIT(6),
	MASK(0xffffffff),	BIT(11),
	OFFSET(0),		NULL,
	"Cause a hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hddaccwq",		do_k_err,		N2_HD_DACCWQ,
	MASK(0xffffffff),	BIT(7),
	MASK(0xffffffff),	BIT(11),
	OFFSET(0),		NULL,
	"Cause a hyper data correctable memory error via\n"
	"\t\t\ta control word queue operation.",

	"hidac",		do_k_err,		NI_HI_DAC,
	MASK(0xffffffff),	BIT(4)|BIT(3),
	MASK(0xffffffff),	BIT(23),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instr memory correctable ecc error.",

	"kddac",		do_k_err,		NI_KD_DAC,
	MASK(0xffffffff),	BIT(2),
	MASK(0xffffffff),	BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory correctable ecc error.",

	"kddacdtlb",		do_k_err,		N2_KD_DACDTLB,
	MASK(0xffffffff),	BIT(8),
	MASK(0xffffffff),	BIT(12),
	OFFSET(0),		NULL,
	"Cause a kern data correctable memory error via\n"
	"\t\t\ta data TLB fill operation.",

	"kidacitlb",		do_k_err,		N2_KI_DACITLB,
	MASK(0xffffffff),	BIT(9),
	MASK(0xffffffff),	BIT(13),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable memory error via\n"
	"\t\t\tan instr TLB fill operation.",

	"kddactl1",		do_k_err,		NI_KD_DACTL1,
	MASK(0xffffffff),	BIT(8),
	MASK(0xffffffff),	BIT(12),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"kddacstorm",		do_k_err,		NI_KD_DACSTORM,
	MASK(0xffffffff),	BIT(10),
	MASK(0xffffffff),	BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error storm\n"
	"\t\t\t(use misc1 for count, default is 64 errors).",

	"kidac",		do_k_err,		NI_KI_DAC,
	MASK(0xffffffff),	BIT(11),
	MASK(0xffffffff),	BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr correctable memory error.",

	"kidactl1",		do_k_err,		NI_KI_DACTL1,
	MASK(0xffffffff),	BIT(12),
	MASK(0xffffffff),	BIT(2),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"uddac",		do_u_err,		NI_UD_DAC,
	MASK(0xffffffff),	BIT(13),
	MASK(0xffffffff),	BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user data correctable memory error.",

	"uidac",		do_u_err,		NI_UI_DAC,
	MASK(0xffffffff),	BIT(14),
	MASK(0xffffffff),	BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr correctable memory error.",

	"kddsc",		do_k_err,		NI_KD_DSC,
	MASK(0xffffffff),	BIT(15),
	MASK(0xffffffff),	BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory correctable ecc error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"iodrc",		do_io_err,		NI_IO_DRC,
	MASK(0xffffffff),	BIT(13),
	MASK(0xffffffff),	BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) correctable memory error.",

	/*
	 * Memory (DRAM) NotData errors.
	 */
	"hdmemnd",		do_k_err,		KT_HD_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper memory NotData error.",

	"hdmemndma",		do_k_err,		KT_HD_MEMNDMA,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a hyper data NotData memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdmemndcwq",		do_k_err,		KT_HD_MEMNDCWQ,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a hyper data NotData memory error via\n"
	"\t\t\ta control word queue operation.",

	"himemnd",		do_k_err,		KT_HI_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper instr memory NotData error.",

	"kdmemnd",		do_k_err,		KT_KD_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory NotData error.",

	"kdmemnddtlb",		do_k_err,		KT_KD_MEMNDDTLB,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a kern data NotData memory error via\n"
	"\t\t\ta data TLB fill operation.",

	"kimemnditlb",		do_k_err,		KT_KI_MEMNDITLB,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a kern instr NotData memory error via\n"
	"\t\t\tan instr TLB fill operation.",

	"kdmemndtl1",		do_k_err,		KT_KD_MEMNDTL1,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data NotData memory error at\n"
	"\t\t\ttrap level 1.",

	"kimemnd",		do_k_err,		KT_KI_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a kern instr NotData memory error.",

	"kimemndtl1",		do_k_err,		KT_KI_MEMNDTL1,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a kern instr NotData memory error at\n"
	"\t\t\ttrap level 1.",

	"udmemnd",		do_u_err,		KT_UD_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a user data NotData memory error.",

	"uimemnd",		do_u_err,		KT_UI_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr NotData memory error.",

	"kdmemndsc",		do_k_err,		KT_KD_MEMNDSC,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a kern memory NotData error detected\n"
	"\t\t\ton scrub access (use the -d option for delay time).",

	"iomemnd",		do_io_err,		KT_IO_MEMND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) NotData memory error.",

	"memndphys=paddr,xor",	do_k_err,		KT_MNDPHYS,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Insert NotData into the DRAM data at physical\n"
	"\t\t\t\taddress \"paddr\".",

	/*
	 * Remote memory (DRAM) uncorrectable errors.
	 */
	"hdfdau",		do_k_err,		VF_HD_FDAU,
	MASK(0xffffffff),	BIT(17)|BIT(0),
	MASK(0xffffffff),	BIT(17)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a remote hyper memory uncorrectable ecc error.",

	"hdfdauma",		do_k_err,		VF_HD_FDAUMA,
	MASK(0xffffffff),	BIT(18)|BIT(1),
	MASK(0xffffffff),	BIT(18)|BIT(1),
	OFFSET(0),		NULL,
	"Cause a remote hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdfdaucwq",		do_k_err,		VF_HD_FDAUCWQ,
	MASK(0xffffffff),	BIT(19)|BIT(2),
	MASK(0xffffffff),	BIT(19)|BIT(2),
	OFFSET(0),		NULL,
	"Cause a remote hyper data uncorrectable memory error via\n"
	"\t\t\ta control word queue operation.",

	"kdfdau",		do_k_err,		VF_KD_FDAU,
	MASK(0xffffffff),	BIT(20)|BIT(3),
	MASK(0xffffffff),	BIT(20)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern memory uncorrectable ecc error.",

	"kdfdautl1",		do_k_err,		VF_KD_FDAUTL1,
	MASK(0xffffffff),	BIT(21)|BIT(6),
	MASK(0xffffffff),	BIT(21)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a remote kern data uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	"kifdau",		do_k_err,		VF_KI_FDAU,
	MASK(0xffffffff),	BIT(22)|BIT(8),
	MASK(0xffffffff),	BIT(22)|BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern instr uncorrectable memory error.",

	"kifdautl1",		do_k_err,		VF_KI_FDAUTL1,
	MASK(0xffffffff),	BIT(23)|BIT(9),
	MASK(0xffffffff),	BIT(23)|BIT(9),
	OFFSET(0),		NULL,
	"Cause a remote kern instr uncorrectable memory error at\n"
	"\t\t\ttrap level 1.",

	/*
	 * Remote memory (DRAM) correctable errors.
	 */
	"hdfdac",		do_k_err,		VF_HD_FDAC,
	MASK(0xffffffff),	BIT(2)|BIT(1),
	MASK(0xffffffff),	BIT(9),
	OFFSET(0),		OFFSET(0),
	"Cause a remote hyper memory correctable ecc error.",

	"hdfdacma",		do_k_err,		VF_HD_FDACMA,
	MASK(0xffffffff),	BIT(6),
	MASK(0xffffffff),	BIT(11),
	OFFSET(0),		NULL,
	"Cause a remote hyper data uncorrectable memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdfdaccwq",		do_k_err,		VF_HD_FDACCWQ,
	MASK(0xffffffff),	BIT(7),
	MASK(0xffffffff),	BIT(11),
	OFFSET(0),		NULL,
	"Cause a remote hyper data correctable memory error via\n"
	"\t\t\ta control word queue operation.",

	"kdfdac",		do_k_err,		VF_KD_FDAC,
	MASK(0xffffffff),	BIT(3)|BIT(2),
	MASK(0xffffffff),	BIT(10),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern memory correctable ecc error.",

	"kdfdactl1",		do_k_err,		VF_KD_FDACTL1,
	MASK(0xffffffff),	BIT(8),
	MASK(0xffffffff),	BIT(12),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"kdfdacstorm",		do_k_err,		VF_KD_FDACSTORM,
	MASK(0xffffffff),	BIT(10),
	MASK(0xffffffff),	BIT(14),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data correctable memory error storm\n"
	"\t\t\t(use misc1 for count, default is 64 errors).",

	"kifdac",		do_k_err,		VF_KI_FDAC,
	MASK(0xffffffff),	BIT(11),
	MASK(0xffffffff),	BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern instr correctable memory error.",

	"kifdactl1",		do_k_err,		VF_KI_FDACTL1,
	MASK(0xffffffff),	BIT(12),
	MASK(0xffffffff),	BIT(2),
	OFFSET(0),		NULL,
	"Cause a remote kern instr correctable memory error at\n"
	"\t\t\ttrap level 1.",

	/*
	 * Remote memory (DRAM) NotData errors.
	 */
	"hdmfrnd",		do_k_err,		KT_HD_MFRND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a remote hyper data NotData memory error.",

	"hdmfrndma",		do_k_err,		KT_HD_MFRNDMA,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a remote hyper data NotData memory error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdmfrndcwq",		do_k_err,		KT_HD_MFRNDCWQ,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a remote hyper data NotData memory error via\n"
	"\t\t\ta control word queue operation.",

	"kdmfrnd",		do_k_err,		KT_KD_MFRND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern data NotData memory error.",

	"kdmfrndtl1",		do_k_err,		KT_KD_MFRNDTL1,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a remote kern data NotData memory error at\n"
	"\t\t\ttrap level 1.",

	"kimfrnd",		do_k_err,		KT_KI_MFRND,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		OFFSET(0),
	"Cause a remote kern instr NotData memory error.",

	"kimfrndtl1",		do_k_err,		KT_KI_MFRNDTL1,
	MASK(0xffffffff),	MASK(0x00150014),
	MASK(0xffffffff),	MASK(0x00150001),
	OFFSET(0),		NULL,
	"Cause a remote kern instr NotData memory error at\n"
	"\t\t\ttrap level 1.",

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
	"Cause a hyper instr L2 cache instr uncorrectable error.",

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

	"ioldru",		do_io_err,		NI_IO_LDRU,
	MASK(ALL_BITS),		BIT(16)|BIT(7),
	MASK(0x7f),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) L2 cache uncorrectable error.",

	/*
	 * L2 cache data correctable errors.
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

	"ioldrc",		do_io_err,		NI_IO_LDRC,
	MASK(ALL_BITS),		BIT(15),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause an IO (DMA) L2 cache correctable error.",

	/*
	 * L2 cache tag fatal and correctable errors.
	 */
	"hdldtf",		do_k_err,		KT_HD_LDTF,
	MASK(0x3ffffff),	BIT(14)|BIT(0),
	MASK(0x3f),		BIT(4)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache tag fatal error.",

	"hildtf",		do_k_err,		KT_HI_LDTF,
	MASK(0x3ffffff),	BIT(13)|BIT(1),
	MASK(0x3f),		BIT(5)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction tag fatal error.",

	"kdldtf",		do_k_err,		KT_KD_LDTF,
	MASK(0x3ffffff),	BIT(12)|BIT(2),
	MASK(0x3f),		BIT(0)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag fatal error.",

	"kdldtftl1",		do_k_err,		KT_KD_LDTFTL1,
	MASK(0x3ffffff),	BIT(11)|BIT(3),
	MASK(0x3f),		BIT(1)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag fatal error\n"
	"\t\t\tat trap level 1.",

	"kildtf",		do_k_err,		KT_KI_LDTF,
	MASK(0x3ffffff),	BIT(11)|BIT(4),
	MASK(0x3f),		BIT(2)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag fatal error.",

	"kildtftl1",		do_k_err,		KT_KI_LDTFTL1,
	MASK(0x3ffffff),	BIT(10)|BIT(5),
	MASK(0x3f),		BIT(3)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag fatal error\n"
	"\t\t\tat trap level 1.",

	"udldtf",		do_u_err,		KT_UD_LDTF,
	MASK(0x3ffffff),	BIT(9)|BIT(6),
	MASK(0x3f),		BIT(4)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache tag fatal error.",

	"uildtf",		do_u_err,		KT_UI_LDTF,
	MASK(0x3ffffff),	BIT(8)|BIT(7),
	MASK(0x3f),		BIT(5)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache instruction tag fatal error.",

	"hdldtc",		do_k_err,		NI_HD_LDTC,
	MASK(0x3ffffff),	BIT(15),
	MASK(0x3f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache tag correctable error.",

	"hildtc",		do_k_err,		NI_HI_LDTC,
	MASK(0x3ffffff),	BIT(14),
	MASK(0x3f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache instruction tag correctable error.",

	"kdldtc",		do_k_err,		NI_KD_LDTC,
	MASK(0x3ffffff),	BIT(13),
	MASK(0x3f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag correctable error.",

	"kdldtctl1",		do_k_err,		NI_KD_LDTCTL1,
	MASK(0x3ffffff),	BIT(12),
	MASK(0x3f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache tag correctable error\n"
	"\t\t\tat trap level 1.",

	"kildtc",		do_k_err,		NI_KI_LDTC,
	MASK(0x3ffffff),	BIT(11),
	MASK(0x3f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag correctable error.",

	"kildtctl1",		do_k_err,		NI_KI_LDTCTL1,
	MASK(0x3ffffff),	BIT(10),
	MASK(0x3f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache instruction tag correctable error\n"
	"\t\t\tat trap level 1.",

	"udldtc",		do_u_err,		NI_UD_LDTC,
	MASK(0x3ffffff),	BIT(9),
	MASK(0x3f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache tag correctable error.",

	"uildtc",		do_u_err,		NI_UI_LDTC,
	MASK(0x3ffffff),	BIT(8),
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
	 * XXX	Note that if the kvaddr is not mapped in (TLB) Niagara would
	 * 	panic.  This may be due to TLB errata or is perhaps a
	 * 	kernel bug, or just normal behaviour since I'm sure the
	 * 	kvaddr is being used be the memtest driver.  This may not
	 * 	be a useful test to run on sun4v/anymore.
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

	/*
	 * L2 cache NotData errors.
	 */
	"hdl2nd",		do_k_err,		N2_HD_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData data access error.",

	"hdl2ndma",		do_k_err,		N2_HD_L2NDMA,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData error via\n"
	"\t\t\ta modular arithmetic operation.",

	"hdl2ndcwq",		do_k_err,		N2_HD_L2NDCWQ,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData error via\n"
	"\t\t\ta control word queue operation.",

	"hil2nd",		do_k_err,		N2_HI_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData instruction\n"
	"\t\t\taccess error.",

	"kdl2nd",		do_k_err,		N2_KD_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData data access error.",

	"kdl2nddtlb",		do_k_err,		N2_KD_L2NDDTLB,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\ta data TLB fill operation.",

	"kil2nditlb",		do_k_err,		N2_KI_L2NDITLB,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\tan instr TLB fill operation.",

	"l2ndcopyin",		do_k_err,		N2_L2NDCOPYIN,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern/user L2 cache NotData error\n"
	"\t\t\tvia copyin.",

	"kdl2ndtl1",		do_k_err,		N2_KD_L2NDTL1,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error at\n"
	"\t\t\ttrap level 1.",

	"hdl2ndpri",		do_k_err,		N2_HD_L2NDPRI,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\ta prefetch ICE.",

	"obpl2nd",		do_k_err,		N2_OBP_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache NotData error via\n"
	"\t\t\tan OBP access.",

	"kil2nd",		do_k_err,		N2_KI_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache instruction NotData\n"
	"\t\t\terror.",

	"kil2ndtl1",		do_k_err,		N2_KI_L2NDTL1,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a kern L2 cache instruction NotData\n"
	"\t\t\terror at trap level 1.",

	"udl2nd",		do_u_err,		N2_UD_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a user L2 cache NotData data access error.",

	"uil2nd",		do_u_err,		N2_UI_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a user L2 cache NotData instruction\n"
	"\t\t\taccess error.",

	"hdl2ndwb",		do_k_err,		N2_HD_L2NDWB,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData write-back, note that\n"
	"\t\t\tno error is generated.",

	"hil2ndwb",		do_k_err,		N2_HI_L2NDWB,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause a hyper L2 cache NotData write-back, note that\n"
	"\t\t\tno error is generated.",

	"iol2nd",		do_io_err,		N2_IO_L2ND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		NULL,
	"Cause an IO (DMA) L2 cache NotData error.",

	"l2ndphys=offset",	do_k_err,		N2_L2NDPHYS,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	NULL,			NULL,
	"Insert NotData into the L2 cache at byte\n"
	"\t\t\t\toffset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	/*
	 * Remote L2 cache data (copy back) uncorrectable errors.
	 */
	"hdlcbu",		do_k_err,		VF_HD_L2CBU,
	MASK(ALL_BITS),		BIT(30)|BIT(21),
	MASK(0x7f),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back uncorrectable\n"
	"\t\t\terror.",

	"hdlcbuma",		do_k_err,		VF_HD_L2CBUMA,
	MASK(ALL_BITS),		BIT(29)|BIT(20),
	MASK(0x7f),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back uncorrectable\n"
	"\t\t\terror via a modular arithmetic operation.",

	"hdlcbucwq",		do_k_err,		VF_HD_L2CBUCWQ,
	MASK(ALL_BITS),		BIT(28)|BIT(19),
	MASK(0x7f),		BIT(0)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back uncorrectable\n"
	"\t\t\terror via a control word queue operation.",

	"hdlcbupri",		do_k_err,		VF_HD_L2CBUPRI,
	MASK(ALL_BITS),		BIT(27)|BIT(18),
	MASK(0x7f),		BIT(1)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back uncorrectable\n"
	"\t\t\terror via a prefetch ICE.",

	"kdlcbu",		do_k_err,		VF_KD_L2CBU,
	MASK(ALL_BITS),		BIT(26)|BIT(17),
	MASK(0x7f),		BIT(2)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back uncorrectable\n"
	"\t\t\terror.",

	"kdlcbutl1",		do_k_err,		VF_KD_L2CBUTL1,
	MASK(ALL_BITS),		BIT(25)|BIT(16),
	MASK(0x7f),		BIT(3)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back uncorrectable\n"
	"\t\t\terror at trap level 1.",

	"kilcbu",		do_k_err,		VF_KI_L2CBU,
	MASK(ALL_BITS),		BIT(24)|BIT(15),
	MASK(0x7f),		BIT(4)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instr copy back uncorrectable\n"
	"\t\t\terror.",

	"kilcbutl1",		do_k_err,		VF_KI_L2CBUTL1,
	MASK(ALL_BITS),		BIT(23)|BIT(14),
	MASK(0x7f),		BIT(5)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instr copy back uncorrectable\n"
	"\t\t\terror at trap level 1.",

	/*
	 * Remote L2 cache data (copy back) correctable errors.
	 */
	"hdlcbc",		do_k_err,		VF_HD_L2CBC,
	MASK(ALL_BITS),		BIT(21),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back correctable error.",

	"hdlcbcma",		do_k_err,		VF_HD_L2CBCMA,
	MASK(ALL_BITS),		BIT(20),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back correctable\n"
	"\t\t\terror via a modular arithmetic operation.",

	"hdlcbucwq",		do_k_err,		VF_HD_L2CBCCWQ,
	MASK(ALL_BITS),		BIT(19),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back correctable\n"
	"\t\t\terror via a control word queue operation.",

	"hdlcbcpri",		do_k_err,		VF_HD_L2CBCPRI,
	MASK(ALL_BITS),		BIT(18),
	MASK(0x7f),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back correctable\n"
	"\t\t\terror via a prefetch ICE.",

	"kdlcbc",		do_k_err,		VF_KD_L2CBC,
	MASK(ALL_BITS),		BIT(17),
	MASK(0x7f),		BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back correctable\n"
	"\t\t\terror.",

	"kdlcbctl1",		do_k_err,		VF_KD_L2CBCTL1,
	MASK(ALL_BITS),		BIT(16),
	MASK(0x7f),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back correctable\n"
	"\t\t\terror at trap level 1.",

	"kilcbc",		do_k_err,		VF_KI_L2CBC,
	MASK(ALL_BITS),		BIT(15),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instr copy back correctable\n"
	"\t\t\terror.",

	"kilcbctl1",		do_k_err,		VF_KI_L2CBCTL1,
	MASK(ALL_BITS),		BIT(14),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instr copy back correctable\n"
	"\t\t\terror at trap level 1.",

	/*
	 * Remote L2 cache data (copy back) NotData errors.
	 */
	"hdl2frnd",		do_k_err,		KT_HD_L2FRND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back NotData error.",

	"hdl2frndma",		do_k_err,		KT_HD_L2FRNDMA,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back NotData\n"
	"\t\t\terror via a modular arithmetic operation.",

	"hdl2frndcwq",		do_k_err,		KT_HD_L2FRNDCWQ,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back NotData\n"
	"\t\t\terror via a control word queue operation.",

	"hdl2frndpri",		do_k_err,		KT_HD_L2FRNDPRI,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache data copy back NotData\n"
	"\t\t\terror via a prefetch ICE.",

	"kdl2frnd",		do_k_err,		KT_KD_L2FRND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back NotData error.",

	"kdl2frndtl1",		do_k_err,		KT_KD_L2FRNDTL1,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache data copy back NotData error\n"
	"\t\t\tat trap level 1.",

	"kil2frnd",		do_k_err,		KT_KI_L2FRND,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instr copy back NotData error.",

	"kil2frndtl1",		do_k_err,		KT_KI_L2FRNDTL1,
	MASK(0x7f),		MASK(0x7e),
	MASK(0x7f),		MASK(0x7d),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel L2 cache instr copy back NotData error\n"
	"\t\t\tat trap level 1.",

	/*
	 * Remote L2 cache data (copy back) write-back errors.
	 */
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

	/*
	 * L2 cache V(U)ADS uncorrectable (fatal) errors.
	 */
	"hdlvfv",		do_k_err,		N2_HD_LVF_VD,
	MASK(0xffffff),		BIT(22)|BIT(23),
	MASK(0x3f000000ULL),	BIT(27)|BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache valid field fatal data error.",

	"hilvfv",		do_k_err,		N2_HI_LVF_VD,
	MASK(0xffffff),		BIT(21)|BIT(22),
	MASK(0x3f000000ULL),	BIT(26)|BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache valid field fatal instruction error.",

	"kdlvfv",		do_k_err,		N2_KD_LVF_VD,
	MASK(0xffffff),		BIT(20)|BIT(21),
	MASK(0x3f000000ULL),	BIT(25)|BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field fatal data error.",

	"kilvfv",		do_k_err,		N2_KI_LVF_VD,
	MASK(0xffffff),		BIT(19)|BIT(20),
	MASK(0x3f000000ULL),	BIT(24)|BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field fatal instruction error.",

	"udlvfv",		do_u_err,		N2_UD_LVF_VD,
	MASK(0xffffff),		BIT(18)|BIT(19),
	MASK(0x3f000000ULL),	BIT(28)|BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field fatal data error.",

	"uilvfv",		do_u_err,		N2_UI_LVF_VD,
	MASK(0xffffff),		BIT(17)|BIT(18),
	MASK(0x3f000000ULL),	BIT(27)|BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field fatal instruction error.",

	"hdlvfa",		do_k_err,		N2_HD_LVF_UA,
	MASK(0xffffff),		BIT(16)|BIT(17),
	MASK(0x3f000000ULL),	BIT(26)|BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache alloc field fatal data error.",

	"hilvfa",		do_k_err,		N2_HI_LVF_UA,
	MASK(0xffffff),		BIT(15)|BIT(16),
	MASK(0x3f000000ULL),	BIT(25)|BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache alloc field fatal instruction error.",

	"kdlvfa",		do_k_err,		N2_KD_LVF_UA,
	MASK(0xffffff),		BIT(14)|BIT(15),
	MASK(0x3f000000ULL),	BIT(24)|BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field fatal data error.",

	"kilvfa",		do_k_err,		N2_KI_LVF_UA,
	MASK(0xffffff),		BIT(13)|BIT(14),
	MASK(0x3f000000ULL),	BIT(28)|BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field fatal instruction error.",

	"udlvfa",		do_u_err,		N2_UD_LVF_UA,
	MASK(0xffffff),		BIT(12)|BIT(13),
	MASK(0x3f000000ULL),	BIT(27)|BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field fatal data error.",

	"uilvfa",		do_u_err,		N2_UI_LVF_UA,
	MASK(0xffffff),		BIT(11)|BIT(12),
	MASK(0x3f000000ULL),	BIT(26)|BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field fatal instruction error.",

	"hdlvfd",		do_k_err,		KT_HD_LVF_D,
	MASK(0xffffff),		BIT(10)|BIT(11),
	MASK(0x3f000000ULL),	BIT(25)|BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache dirty field fatal data error.",

	"hilvfd",		do_k_err,		KT_HI_LVF_D,
	MASK(0xffffff),		BIT(9)|BIT(10),
	MASK(0x3f000000ULL),	BIT(26)|BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache dirty field fatal instruction error.",

	"kdlvfd",		do_k_err,		KT_KD_LVF_D,
	MASK(0xffffff),		BIT(8)|BIT(9),
	MASK(0x3f000000ULL),	BIT(25)|BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field fatal data error.",

	"kilvfd",		do_k_err,		KT_KI_LVF_D,
	MASK(0xffffff),		BIT(7)|BIT(8),
	MASK(0x3f000000ULL),	BIT(24)|BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field fatal instruction error.",

	"udlvfd",		do_u_err,		KT_UD_LVF_D,
	MASK(0xffffff),		BIT(6)|BIT(7),
	MASK(0x3f000000ULL),	BIT(28)|BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field fatal data error.",

	"uilvfd",		do_u_err,		KT_UI_LVF_D,
	MASK(0xffffff),		BIT(5)|BIT(6),
	MASK(0x3f000000ULL),	BIT(27)|BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field fatal instruction error.",

	"hdlvfs",		do_k_err,		KT_HD_LVF_S,
	MASK(0xffffff),		BIT(4)|BIT(5),
	MASK(0x3f000000ULL),	BIT(26)|BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache shared field fatal data error.",

	"hilvfs",		do_k_err,		KT_HI_LVF_S,
	MASK(0xffffff),		BIT(3)|BIT(4),
	MASK(0x3f000000ULL),	BIT(25)|BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache shared field fatal instruction error.",

	"kdlvfs",		do_k_err,		KT_KD_LVF_S,
	MASK(0xffffff),		BIT(2)|BIT(3),
	MASK(0x3f000000ULL),	BIT(24)|BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field fatal data error.",

	"kilvfs",		do_k_err,		KT_KI_LVF_S,
	MASK(0xffffff),		BIT(1)|BIT(2),
	MASK(0x3f000000ULL),	BIT(28)|BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field fatal instruction error.",

	"udlvfs",		do_u_err,		KT_UD_LVF_S,
	MASK(0xffffff),		BIT(0)|BIT(1),
	MASK(0x3f000000ULL),	BIT(27)|BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field fatal data error.",

	"uilvfs",		do_u_err,		KT_UI_LVF_S,
	MASK(0xffffff),		BIT(22)|BIT(23),
	MASK(0x3f000000ULL),	BIT(26)|BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field fatal instruction error.",

	/*
	 * L2 cache V(U)ADS correctable errors.
	 */
	"hdlvcv",		do_k_err,		N2_HD_LVC_VD,
	MASK(0xffffff),		BIT(22),
	MASK(0x3f000000ULL),	BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache valid field correctable data\n"
	"\t\t\terror.",

	"hilvcv",		do_k_err,		N2_HI_LVC_VD,
	MASK(0xffffff),		BIT(21),
	MASK(0x3f000000ULL),	BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache valid field correctable instruction\n"
	"\t\t\terror.",

	"kdlvcv",		do_k_err,		N2_KD_LVC_VD,
	MASK(0xffffff),		BIT(20),
	MASK(0x3f000000ULL),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field correctable data\n"
	"\t\t\terror.",

	"kilvcv",		do_k_err,		N2_KI_LVC_VD,
	MASK(0xffffff),		BIT(19),
	MASK(0x3f000000ULL),	BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field correctable instruction\n"
	"\t\t\terror.",

	"udlvcv",		do_u_err,		N2_UD_LVC_VD,
	MASK(0xffffff),		BIT(18),
	MASK(0x3f000000ULL),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field correctable data\n"
	"\t\t\terror.",

	"uilvcv",		do_u_err,		N2_UI_LVC_VD,
	MASK(0xffffff),		BIT(17),
	MASK(0x3f000000ULL),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache valid field correctable instruction\n"
	"\t\t\terror.",

	"hdlvca",		do_k_err,		N2_HD_LVC_UA,
	MASK(0xffffff),		BIT(16),
	MASK(0x3f000000ULL),	BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache alloc field correctable data\n"
	"\t\t\terror.",

	"hilvca",		do_k_err,		N2_HI_LVC_UA,
	MASK(0xffffff),		BIT(15),
	MASK(0x3f000000ULL),	BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache alloc field correctable instruction\n"
	"\t\t\terror.",

	"kdlvca",		do_k_err,		N2_KD_LVC_UA,
	MASK(0xffffff),		BIT(14),
	MASK(0x3f000000ULL),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field correctable data\n"
	"\t\t\terror.",

	"kilvca",		do_k_err,		N2_KI_LVC_UA,
	MASK(0xffffff),		BIT(13),
	MASK(0x3f000000ULL),	BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field correctable instruction\n"
	"\t\t\terror.",

	"udlvca",		do_u_err,		N2_UD_LVC_UA,
	MASK(0xffffff),		BIT(12),
	MASK(0x3f000000ULL),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field correctable data\n"
	"\t\t\terror.",

	"uilvca",		do_u_err,		N2_UI_LVC_UA,
	MASK(0xffffff),		BIT(11),
	MASK(0x3f000000ULL),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache alloc field correctable instruction\n"
	"\t\t\terror.",

	"hdlvcd",		do_k_err,		KT_HD_LVC_D,
	MASK(0xffffff),		BIT(22),
	MASK(0x3f000000ULL),	BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache dirty field correctable data\n"
	"\t\t\terror.",

	"hilvcd",		do_k_err,		KT_HI_LVC_D,
	MASK(0xffffff),		BIT(21),
	MASK(0x3f000000ULL),	BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache dirty field correctable instruction\n"
	"\t\t\terror.",

	"kdlvcd",		do_k_err,		KT_KD_LVC_D,
	MASK(0xffffff),		BIT(20),
	MASK(0x3f000000ULL),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field correctable data\n"
	"\t\t\terror.",

	"kilvcd",		do_k_err,		KT_KI_LVC_D,
	MASK(0xffffff),		BIT(19),
	MASK(0x3f000000ULL),	BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field correctable instruction\n"
	"\t\t\terror.",

	"udlvcd",		do_u_err,		KT_UD_LVC_D,
	MASK(0xffffff),		BIT(18),
	MASK(0x3f000000ULL),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field correctable data\n"
	"\t\t\terror.",

	"uilvcd",		do_u_err,		KT_UI_LVC_D,
	MASK(0xffffff),		BIT(17),
	MASK(0x3f000000ULL),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache dirty field correctable instruction\n"
	"\t\t\terror.",

	"hdlvcs",		do_k_err,		KT_HD_LVC_S,
	MASK(0xffffff),		BIT(16),
	MASK(0x3f000000ULL),	BIT(29),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache shared field correctable data\n"
	"\t\t\terror.",

	"hilvcs",		do_k_err,		KT_HI_LVC_S,
	MASK(0xffffff),		BIT(15),
	MASK(0x3f000000ULL),	BIT(28),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache shared field correctable instruction\n"
	"\t\t\terror.",

	"kdlvcs",		do_k_err,		KT_KD_LVC_S,
	MASK(0xffffff),		BIT(14),
	MASK(0x3f000000ULL),	BIT(27),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field correctable data\n"
	"\t\t\terror.",

	"kilvcs",		do_k_err,		KT_KI_LVC_S,
	MASK(0xffffff),		BIT(13),
	MASK(0x3f000000ULL),	BIT(26),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field correctable instruction\n"
	"\t\t\terror.",

	"udlvcs",		do_u_err,		KT_UD_LVC_S,
	MASK(0xffffff),		BIT(12),
	MASK(0x3f000000ULL),	BIT(25),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field correctable data\n"
	"\t\t\terror.",

	"uilvcs",		do_u_err,		KT_UI_LVC_S,
	MASK(0xffffff),		BIT(11),
	MASK(0x3f000000ULL),	BIT(24),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache shared field correctable instruction\n"
	"\t\t\terror.",

	/*
	 * L2 cache V(U)AD errors injected by address.
	 */
	"l2lvcvphys=offset,xor", do_k_err,		NI_L2VDPHYS,
	MASK(0xffffff),		BIT(6),
	MASK(0x3f000000ULL),	BIT(28),
	NULL,			NULL,
	"Insert an error into the L2 cache valid field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l2lvcaphys=offset,xor", do_k_err,		NI_L2UAPHYS,
	MASK(0xffffff),		BIT(5),
	MASK(0x3f000000ULL),	BIT(27),
	NULL,			NULL,
	"Insert an error into the L2 cache alloc field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l2lvcdphys=offset,xor", do_k_err,		KT_L2LVCDPHYS,
	MASK(0xffffff),		BIT(4),
	MASK(0x3f000000ULL),	BIT(26),
	NULL,			NULL,
	"Insert an error into the L2 cache dirty field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	"l2lvcsphys=offset,xor", do_k_err,		KT_L2LVCSPHYS,
	MASK(0xffffff),		BIT(3),
	MASK(0x3f000000ULL),	BIT(25),
	NULL,			NULL,
	"Insert an error into the L2 cache shared field\n"
	"\t\t\t\tat byte offset \"offset\".\n"
	"\t\t\t\tThis does not modify the cache line state.",

	/*
	 * L2 cache directory, fill, miss, and write buffer errors.
	 */
	"hdldc",		do_k_err,		KT_HD_LDC,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache directory hw corrected\n"
	"\t\t\t\tdata error.",

	"hildc",		do_k_err,		KT_HI_LDC,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache directory hw corrected\n"
	"\t\t\t\tinstruction error.",

	"kdldc",		do_k_err,		KT_KD_LDC,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache directory hw corrected\n"
	"\t\t\t\tdata error.",

	"kildc",		do_k_err,		KT_KI_LDC,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache directory hw corrected\n"
	"\t\t\t\tinstruction error.",

	"udldc",		do_u_err,		KT_UD_LDC,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache directory hw corrected\n"
	"\t\t\t\tdata error.",

	"uildc",		do_u_err,		KT_UI_LDC,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a user L2 cache directory hw corrected\n"
	"\t\t\t\tinstruction error.",

	"hdfbdc",		do_k_err,		KT_HD_FBDC,
	MASK(0x7f),		BIT(0),
	MASK(0x7f),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache fill buffer correctable\n"
	"\t\t\t\tdata error.",

	"hifbdc",		do_k_err,		KT_HI_FBDC,
	MASK(0x7f),		BIT(1),
	MASK(0x7f),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache fill buffer correctable\n"
	"\t\t\t\tinstruction error.",

	"kdfbdc",		do_k_err,		KT_KD_FBDC,
	MASK(0x7f),		BIT(2),
	MASK(0x7f),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache fill buffer correctable\n"
	"\t\t\t\tdata error.",

	"kifbdc",		do_k_err,		KT_KI_FBDC,
	MASK(0x7f),		BIT(3),
	MASK(0x7f),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache fill buffer correctable\n"
	"\t\t\t\tinstruction error.",

	"hdfbdu",		do_k_err,		KT_HD_FBDU,
	MASK(0x7f),		BIT(0)|BIT(1),
	MASK(0x7f),		BIT(1)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache fill buffer uncorrectable\n"
	"\t\t\t\tdata error.",

	"hifbdu",		do_k_err,		KT_HI_FBDU,
	MASK(0x7f),		BIT(1)|BIT(2),
	MASK(0x7f),		BIT(2)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache fill buffer uncorrectable\n"
	"\t\t\t\tinstruction error.",

	"kdfbdu",		do_k_err,		KT_KD_FBDU,
	MASK(0x7f),		BIT(2)|BIT(3),
	MASK(0x7f),		BIT(3)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache fill buffer uncorrectable\n"
	"\t\t\t\tdata error.",

	"kifbdu",		do_k_err,		KT_KI_FBDU,
	MASK(0x7f),		BIT(3)|BIT(4),
	MASK(0x7f),		BIT(4)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache fill buffer uncorrectable\n"
	"\t\t\t\tinstruction error.",

	"hdmbdu",		do_k_err,		KT_HD_MBDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache miss buffer parity\n"
	"\t\t\t\tdata error.",

	"himbdu",		do_k_err,		KT_HI_MBDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache miss buffer parity\n"
	"\t\t\t\tinstruction error.",

	"kdmbdu",		do_k_err,		KT_KD_MBDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache miss buffer parity\n"
	"\t\t\t\tdata error.",

	"kimbdu",		do_k_err,		KT_KI_MBDU,
	NULL,			NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache miss buffer parity\n"
	"\t\t\t\tinstruction error.",

	"hdldwbc",		do_k_err,		KT_HD_LDWBC,
	MASK(0xff),		BIT(0),
	MASK(0xff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache write buffer correctable\n"
	"\t\t\t\tdata error.",

	"hildwbc",		do_k_err,		KT_HI_LDWBC,
	MASK(0xff),		BIT(1),
	MASK(0xff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache write buffer correctable\n"
	"\t\t\t\tinstruction error.",

	"kdldwbc",		do_k_err,		KT_KD_LDWBC,
	MASK(0xff),		BIT(2),
	MASK(0xff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache write buffer correctable\n"
	"\t\t\t\tdata error.",

	"kildwbc",		do_k_err,		KT_KI_LDWBC,
	MASK(0xff),		BIT(3),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache write buffer correctable\n"
	"\t\t\t\tinstruction error.",

	"hdldwbu",		do_k_err,		KT_HD_LDWBU,
	MASK(0xff),		BIT(0)|BIT(1),
	MASK(0xff),		BIT(1)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache write buffer uncorrectable\n"
	"\t\t\t\tdata error.",

	"hildwbu",		do_k_err,		KT_HI_LDWBU,
	MASK(0xff),		BIT(1)|BIT(2),
	MASK(0xff),		BIT(2)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L2 cache write buffer uncorrectable\n"
	"\t\t\t\tinstruction error.",

	"kdldwbu",		do_k_err,		KT_KD_LDWBU,
	MASK(0xff),		BIT(2)|BIT(3),
	MASK(0xff),		BIT(3)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache write buffer uncorrectable\n"
	"\t\t\t\tdata error.",

	"kildwbu",		do_k_err,		KT_KI_LDWBU,
	MASK(0xff),		BIT(3)|BIT(4),
	MASK(0xff),		BIT(4)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L2 cache write buffer uncorrectable\n"
	"\t\t\t\tinstruction error.",

	"l2dirphys=offset",	do_k_err,		KT_L2DIRPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert an error into the L2 cache directory\n"
	"\t\t\t\tat byte offset \"offset\".",

	"l2fbufphys=offset",	do_k_err,		KT_L2FBUFPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert an ecc error into the L2 cache fill buffer\n"
	"\t\t\t\tat byte offset \"offset\".",

	"l2mbufphys=offset",	do_k_err,		KT_L2MBUFPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert an error into the L2 cache miss buffer\n"
	"\t\t\t\tat byte offset \"offset\".",

	"l2ccxphys=offset",	do_k_err,		KT_L2WBUFPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert an ecc error into the L2 cache CCX ingress\n"
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
	MASK(0x7fffffffcULL),	BIT(22)|BIT(6),
	BIT(16),		BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a hyper L1 instruction cache tag correctable\n"
	"\t\t\terror.",

	"kiictp",		do_k_err,		NI_KI_ITC,
	MASK(0x7fffffffcULL),	BIT(21)|BIT(5),
	BIT(16),		BIT(16),
	OFFSET(0),		OFFSET(0),
	"Cause a kern L1 instruction cache tag correctable\n"
	"\t\t\terror.",

	"kiictptl1",		do_k_err,		NI_KI_ITCTL1,
	MASK(0x7fffffffcULL),	BIT(20)|BIT(4),
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
	BIT(0),			BIT(0),
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
	MASK(0x7fffffffcULL),	BIT(8)|BIT(2),
	BIT(16),		BIT(16),
	NULL,			NULL,
	"Insert an error into the L1\n"
	"\t\t\t\tinstruction cache tag at byte offset \"offset\".",

	"ivphys=offset,xor",	do_k_err,		N2_IVPHYS,
	BIT(1),			BIT(1),
	BIT(15),		BIT(15),
	NULL,			NULL,
	"Insert an error into the L1\n"
	"\t\t\t\tinstruction cache valid bits at byte offset \"offset\".",

	"imphys=offset,xor",	do_k_err,		N2_IMPHYS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Insert a tag multi-hit error into the L1\n"
	"\t\t\t\tinstruction cache at byte offset \"offset\".",

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
	 * NCU, LFU, and link errors.
	 */
	"ioncuto", 		do_k_err,		VF_IO_NCXFSRTO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a fatal NCU PIO timeout error.",

	"lfuslf",		do_k_err,		VF_LFU_SLF,
	MASK(ALL_BITS),		BIT(2)|(BIT(2) << 14),
	MASK(ALL_BITS),		BIT(12)|(BIT(12) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU single-lane failure.",

	"lfumlf",		do_k_err,		VF_LFU_MLF,
	MASK(ALL_BITS),		BIT(4)|(BIT(10) << 14),
	MASK(ALL_BITS),		BIT(8)|(BIT(6) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU multi-lane failure.",

	"lfurtf",		do_k_err,		VF_LFU_RTF,
	MASK(ALL_BITS),		BIT(1),
	MASK(ALL_BITS),		BIT(52),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU retrain failed error, or increment the retrain count.",

	"lfutto",		do_k_err,		VF_LFU_TTO,
	MASK(ALL_BITS),		BIT(7)|BIT(11)|(BIT(7) << 14)|(BIT(11) << 14),
	MASK(ALL_BITS),		BIT(5)|BIT(13)|(BIT(5) << 14)|(BIT(13) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU link training timeout error.",

	"lfucto",		do_k_err,		VF_LFU_CTO,
	MASK(ALL_BITS),		BIT(3)|(BIT(3) << 14),
	MASK(ALL_BITS),		BIT(9)|(BIT(9) << 14),
	OFFSET(0),		OFFSET(0),
	"Cause a LFU link configure timeout error.",

	/*
	 * System on Chip (SOC) MCU FB DIMM link errors.
	 *
	 * NOTE: most SOC commands are defined as NA1 and some could
	 *	 likely make use of an explicit DMA transaction.
	 *	 Could add KT specific versions of some of the below.
	 */
	"iomcufbr",		do_k_err,		N2_HD_MCUFBR,
	MASK(0xffffff),		BIT(4),
	MASK(0xffffff),		BIT(5),
	NULL,			NULL,
	"Cause an MCU branch FBDIMM recoverable link\n"
	"\t\t\terror.",

	"iomcufbu",		do_k_err,		N2_HD_MCUFBU,
	MASK(0xffffff),		BIT(6)|BIT(11),
	MASK(0xffffff),		BIT(7)|BIT(12),
	NULL,			NULL,
	"Cause an MCU branch FBDIMM unrecoverable link\n"
	"\t\t\terror.",

	"iomcufbrf",		do_k_err,		VF_HD_MCUFBRF,
	MASK(0xffff),		MASK(0xffff),
	MASK(0xffff),		MASK(0xf0f0),
	OFFSET(0),		OFFSET(0),
	"Cause a FBDIMM lane failover.",

	/*
	 * System on Chip (SOC) Internal errors.
	 *
	 * NOTE: the NIU errors require that infinite injection be set.
	 */
	"ioniudpar",		do_k_err,		N2_IO_NIUDPAR,
	BIT(KT_SOC_NIUDATAPARITY_SHIFT), BIT(KT_SOC_NIUDATAPARITY_SHIFT),
	BIT(KT_SOC_NIUDATAPARITY_SHIFT), BIT(KT_SOC_NIUDATAPARITY_SHIFT),
	NULL,				 NULL,
	"Cause an NIU (SOC) data parity error in the DMA\n"
	"\t\t\tdata going to the SIU.",

	"ioniuctague",		do_k_err,		N2_IO_NIUCTAGUE,
	BIT(KT_SOC_NIUCTAGUE_SHIFT),	BIT(KT_SOC_NIUCTAGUE_SHIFT),
	BIT(KT_SOC_NIUCTAGUE_SHIFT),	BIT(KT_SOC_NIUCTAGUE_SHIFT),
	NULL,				NULL,
	"Cause an NIU (SOC) CTAG uncorrectable error in the\n"
	"\t\t\tDMA data going to the SIU.",

	"ioniuctagce",		do_k_err,		N2_IO_NIUCTAGCE,
	BIT(KT_SOC_NIUCTAGCE_SHIFT),	BIT(KT_SOC_NIUCTAGCE_SHIFT),
	BIT(KT_SOC_NIUCTAGCE_SHIFT),	BIT(KT_SOC_NIUCTAGCE_SHIFT),
	NULL,				NULL,
	"Cause an NIU (SOC) CTAG correctable error in the\n"
	"\t\t\tDMA data going to the SIU.",

	/*
	 * The SOC injection register uses a 4-bit field to define the
	 * different types of injectable SIU errors.  It is described
	 * in the SIU MAS as follows:
	 *	0000	No error injection
	 *	0001	DMU -> COU DMA request parity error in ID field [63:49]
	 *	0010	DMU -> COU DMA request parity error in bits [48:32]
	 *	0011	DMU -> COU DMA request parity error in bits [31:0]
	 *	0100	DMU -> COU DMA write data uncorrectable error
	 *	0101	DMU -> COU DMA write data correctable error
	 *	0110	DMU -> NCU request parity error in ID field [15:0]
	 *	0111	DMU -> NCU request parity error in bits [29:16]
	 *	1000	DMU -> NCU parity error in mondo data
	 *	1001	DMU -> NCU PIO data corectable error
	 *	1010	DMU -> NCU PIO data uncorectable error
	 *	1011	COU -> DMU DMA data read uncorrectable error
	 *	1100	COU -> DMU DMA data read correctable error
	 *	1101	parity error in partial DMA write byte enables
	 *	1110	COU -> NIU header uncorrectable error
	 *	1111	COU -> NIU header correctable error
	 */
	"iosiuerr",		do_k_err,		KT_IO_SIU_ERR,
	MASK(0xf),		BIT(1),
	MASK(0xf),		BIT(2),
	NULL,			NULL,
	"Cause an SIU (SOC) error whose type depends on the\n"
	"\t\t\t\"xorpat\" option.",

	/* SOC CPU buffer errors */
	"iocbdu",		do_k_err,		KT_IO_SOC_CBDU,
	MASK(0xff),		BIT(2)|BIT(6),
	MASK(0xff),		BIT(3)|BIT(7),
	NULL,			NULL,
	"Cause a CPU buffer (SOC) uncorrectable ecc error.",

	"iocbdc",		do_k_err,		KT_IO_SOC_CBDC,
	MASK(0xff),		BIT(3),
	MASK(0xff),		BIT(4),
	NULL,			NULL,
	"Cause a CPU buffer (SOC) correctable ecc error.",

	"iocbap",		do_k_err,		KT_IO_SOC_CBAP,
	BIT(KT_SOC_CBA_SHIFT),	BIT(KT_SOC_CBA_SHIFT),
	BIT(KT_SOC_CBA_SHIFT),	BIT(KT_SOC_CBA_SHIFT),
	NULL,			NULL,
	"Cause a CPU buffer (SOC) address parity error.",

	"iocbhp",		do_k_err,		KT_IO_SOC_CBHP,
	BIT(KT_SOC_CBH_SHIFT),	BIT(KT_SOC_CBH_SHIFT),
	BIT(KT_SOC_CBH_SHIFT),	BIT(KT_SOC_CBH_SHIFT),
	NULL,			NULL,
	"Cause a CPU buffer (SOC) header parity error.",

	/* SOC SIU buffer errors */
	"iosbdu",		do_k_err,		KT_IO_SOC_SBDU,
	MASK(0xff),		BIT(3)|BIT(7),
	MASK(0xff),		BIT(4)|BIT(1),
	NULL,			NULL,
	"Cause a SIU buffer (SOC) uncorrectable ecc error.",

	"iosbdc",		do_k_err,		KT_IO_SOC_SBDC,
	MASK(0xff),		BIT(4),
	MASK(0xff),		BIT(5),
	NULL,			NULL,
	"Cause a SIU buffer (SOC) correctable ecc error.",

	"iosbhp",		do_k_err,		KT_IO_SOC_SBHP,
	BIT(KT_SOC_SBH_SHIFT),	BIT(KT_SOC_SBH_SHIFT),
	BIT(KT_SOC_SBH_SHIFT),	BIT(KT_SOC_SBH_SHIFT),
	NULL,			NULL,
	"Cause a SIU buffer (SOC) header parity error.",

	/* SOC DMU buffer errors */
	"iodbdu",		do_k_err,		KT_IO_SOC_DBDU,
	MASK(0xff),		BIT(4)|BIT(1),
	MASK(0xff),		BIT(5)|BIT(2),
	NULL,			NULL,
	"Cause a DMU buffer (SOC) uncorrectable ecc error.",

	"iodbdc",		do_k_err,		KT_IO_SOC_DBDC,
	MASK(0xff),		BIT(5),
	MASK(0xff),		BIT(6),
	NULL,			NULL,
	"Cause a DMU buffer (SOC) correctable ecc error.",

	"iodbap",		do_k_err,		KT_IO_SOC_DBAP,
	BIT(KT_SOC_DBA_SHIFT),	BIT(KT_SOC_DBA_SHIFT),
	BIT(KT_SOC_DBA_SHIFT),	BIT(KT_SOC_DBA_SHIFT),
	NULL,			NULL,
	"Cause a DMU buffer (SOC) address parity error.",

	"iodbhp",		do_k_err,		KT_IO_SOC_DBHP,
	BIT(KT_SOC_DBH_SHIFT),	BIT(KT_SOC_DBH_SHIFT),
	BIT(KT_SOC_DBH_SHIFT),	BIT(KT_SOC_DBH_SHIFT),
	NULL,			NULL,
	"Cause a DMU buffer (SOC) header parity error.",

	"ioibhp",		do_k_err,		KT_IO_SOC_IBHP,
	BIT(KT_SOC_IBH_SHIFT),	BIT(KT_SOC_IBH_SHIFT),
	BIT(KT_SOC_IBH_SHIFT),	BIT(KT_SOC_IBH_SHIFT),
	NULL,			NULL,
	"Cause an IO buffer (SOC) header parity error.",

	/*
	 * SSI (bootROM interface) errors.
	 */
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

	"ktprintssi",		do_k_err,		NI_PRINT_SSI,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"DEBUG test case to print the contents of the SSI\n"
	"\t\t\tregisters.",

	/*
	 * DEBUG test case to ensure framework calls HV correctly.
	 */
	"kttest",		do_k_err,		KT_TEST,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG test case to ensure mtst framework can execute\n"
	"\t\t\tin HV mode correctly.",

	/*
	 * DEBUG print case to check the processors ESR values.
	 */
	"ktprintesrs",		do_k_err,		KT_PRINT_ESRS,
	MASK(ALL_BITS),		BIT(32),
	MASK(ALL_BITS),		BIT(32),
	OFFSET(0),		OFFSET(0),
	"DEBUG print case to check the processors ESR values.",

	/*
	 * DEBUG test case to clear the contents of certain ESRs.
	 */
	"ktclearesrs",		do_k_err,		KT_CLEAR_ESRS,
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
	kt_cmds,
	sun4v_generic_cmds,
	NULL
};

static  opsvec_t operations = {
	gen_flushall_l2,	/* requires L2$ to be in DM mode first */
	kt_pre_test,		/* pre-test routine */
	NULL			/* no post-test routine */
};

void
kt_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}

/*
 * Main memory errors must be placed in local or remote memory
 * depending on the specific command.
 *
 * This routine makes use of the fact that the EI injection buffer
 * is 8K in size and that the instruction portion takes up the
 * second half of the buffer.
 */
int
kt_adjust_buf_to_local(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	uint64_t	cpu_node_id;
	uint64_t	mem_node_id;
	uint_t		cpu_mode;
	uint64_t	raddr;
	uint64_t	paddr;
	char		*fname = "kt_adjust_buf_to_local";

	/*
	 * Only main memory (DRAM) errors require that the injection
	 * buffer be in local memory.  The other injection targets are
	 * internal to the processor or use IO.
	 */
	if (!ERR_CLASS_ISMEM(iocp->ioc_command) &&
	    !ERR_CLASS_ISMCU(iocp->ioc_command)) {
		return (0);
	}

	cpu_node_id = KT_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	if ((raddr = uva_to_pa((uint64_t)(uintptr_t)mdatap->m_databuf,
	    iocp->ioc_pid)) == -1) {
		msg(MSG_ERROR, "%s: failed to convert uvaddr to raddr\n",
		    fname);
		return (-1);
	}

	if ((mem_node_id = kt_get_mem_node_id(cip, raddr)) == -1) {
		msg(MSG_ERROR, "%s: failed to get mem_node_id\n", fname);
		return (-1);
	}

	msg(MSG_DEBUG3, "%s: base of the mdatabuf is at VA 0x%p, "
	    "CPU node id=0x%llx (cpuid=%d), mem node id=0x%llx\n", fname,
	    mdatap->m_databuf, cpu_node_id, cip->c_cpuid, mem_node_id);

	/*
	 * If the current cpu_node_id matches that of the target address
	 * (the paddr was checked not the raddr) then there is nothing to do.
	 */
	if (mem_node_id == cpu_node_id) {
		msg(MSG_DEBUG3, "%s: buffers already local to CPU %d\n", fname,
		    cip->c_cpuid);
		return (0);
	}

	if ((paddr = ra_to_pa(raddr)) == -1) {
		msg(MSG_ERROR, "%s: failed to convert raddr to paddr\n",
		    fname);
		return (-1);
	}

	/*
	 * XXX could enhance this routine to allocate a local/remote buffer
	 *	(whichever is required) regardless of the interleave
	 *	as long as the use of the "-a" option is accounted for.
	 *	In practice the buffer will be local since it is allocated from
	 *	the consumer thread bound cpuid so the code should not fail.
	 */
	if (!KT_IS_FINE_INTERLEAVE(cip->c_l2_ctl, paddr)) {
		msg(MSG_ERROR, "mem buffer (raddr=0x%llx, nodeid=0x%x) is "
		    "not local to CPU %d (nodeid=0x%x) due to coarse "
		    "interleave\n", raddr, mem_node_id, cip->c_cpuid,
		    cpu_node_id);
		msg(MSG_ERROR, "local memory is required for memory tests, "
		    "this routine is not smart enough to move the buffer "
		    "so try re-running with the \"-b local_mem\" option\n");
		return (-1);
	}

	/*
	 * Otherwise the system may be using fine interleave mode (1K)
	 * and the buffer pointers may need to be moved.
	 *
	 * Note that for degraded modes KT/RF still partitions the
	 * memory in the same way (leaving memory holes).  So this
	 * method will still work in those cases.  Also moving the
	 * buffer pointers around will not have an adverse affect on
	 * systems running in coarse interleave mode.
	 *
	 * NOTE: this routine is moving both the data and instr buffer
	 *	 pointers though a check on the command could be performed
	 *	 and only the appropriate buffer moved (yes, the command
	 *	 has already been parsed at this point so it can be done).
	 */
	cpu_mode = KT_SYS_MODE_GET_MODE(cip->c_sys_mode);
	iocp->ioc_databuf += (1024 * cpu_node_id);
	mdatap->m_databuf += (1024 * cpu_node_id);

	if (cpu_mode == KT_SYS_MODE_8MODE) {
		mdatap->m_instbuf = mdatap->m_databuf;
	} else {
		mdatap->m_instbuf += (1024 * cpu_node_id);
	}

	msg(MSG_DEBUG3, "%s: ioc_databuf=0x%llx, m_databuf=0x%lx, "
	    "m_instbuf=0x%lx, ioc_addr=0x%lx\n", fname, iocp->ioc_databuf,
	    mdatap->m_databuf, mdatap->m_instbuf, iocp->ioc_addr);

	return (0);
}

/*
 * Determine the node ID of a physical address.
 */
static int
kt_get_mem_node_id(cpu_info_t *cip, uint64_t raddr)
{
	int		mem_node_id;
	uint64_t	paddr;
	uint_t		cpu_mode;
	uint_t		num_nodes;
	char		*fname = "kt_get_mem_node_id";

	num_nodes = ((cip->c_sys_mode & KT_SYS_MODE_COUNT_MASK) >>
	    KT_SYS_MODE_COUNT_SHIFT) + 1;

	/*
	 * Convert the real address to a true physical address.
	 */
	if ((paddr = ra_to_pa(raddr)) == -1) {
		msg(MSG_ERROR, "%s: cannot determine memory locality",
		    fname);
		return (-1);
	}

	cpu_mode = KT_SYS_MODE_GET_MODE(cip->c_sys_mode);

	switch (cpu_mode) {
	case KT_SYS_MODE_1MODE:
		mem_node_id = KT_SYS_MODE_GET_NODEID(cip->c_sys_mode);
		break;

	case KT_SYS_MODE_2MODE:
		if (KT_IS_FINE_INTERLEAVE(cip->c_l2_ctl, paddr)) {
			mem_node_id = KT_2NODE_FINE_PADDR_NODEID(paddr);
		} else {
			mem_node_id = KT_2NODE_COARSE_PADDR_NODEID(paddr);
		}
		break;

	case KT_SYS_MODE_4MODE:
		if ((KT_IS_FINE_INTERLEAVE(cip->c_l2_ctl, paddr)) &&
		    (num_nodes != 3)) {
			mem_node_id = KT_4NODE_FINE_PADDR_NODEID(paddr);
		} else {
			mem_node_id = KT_4NODE_COARSE_PADDR_NODEID(paddr);
		}
		break;

	case KT_SYS_MODE_8MODE:
		/*
		 * XXX The PRM does not describe this case though it could
		 * occur on future systems.  Setting it to no interleave
		 * so that testing on such systems will not be blocked.
		 */
		msg(MSG_DEBUG0, "%s: 8 mode value read from SYS_MODE "
		    "reg = 0x%x, is not officially supported, using no "
		    "interleave (value = 0)\n", fname, cpu_mode);
		mem_node_id = 0;
		break;

	default:
		msg(MSG_DEBUG0, "%s: unsupported mode value read from SYS_MODE "
		    "reg = 0x%x\n", fname, cpu_mode);
		return (-1);
	}

	msg(MSG_DEBUG2, "%s: cpu_node (read from SYS_MODE) = 0x%x, "
	    "mem_node_id = 0x%x", fname, cpu_mode, mem_node_id);

	return (mem_node_id);
}

/*
 * Determine if a physical address is local to a particular CPU.
 * This is required for the local and remote memory binding options.
 */
int
kt_mem_is_local(cpu_info_t *cip, uint64_t raddr)
{
	uint_t		cpu_node_id;
	uint_t		mem_node_id;
	char		*fname = "kt_mem_is_local";

	/*
	 * Get the node that the CPU belongs to, and then get the node
	 * that raddr belongs to. If they're the same, then return true
	 */
	cpu_node_id = KT_SYS_MODE_GET_NODEID(cip->c_sys_mode);

	mem_node_id = kt_get_mem_node_id(cip, raddr);

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
kt_pre_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		tmp_thr_criteria;
	uint64_t	tmp_thr_data;

	/*
	 * Some DCU and IFU (L1 cache) errors can only be
	 * injected into the check-bits because of HW limitations.
	 * Ensure that the check-bit flag is set for these error types.
	 *
	 * Specifically D$ data, tag, and valid-bit errors, and
	 * I$ tag and valid-bit errors (the only other type of L1 error
	 * on KT is multi-hit (MH)).
	 */
	if ((ERR_CLASS_ISDC(iocp->ioc_command) ||
	    (ERR_CLASS_ISIC(iocp->ioc_command) &&
	    (ERR_SUBCLASS_ISTAG(iocp->ioc_command) ||
	    ERR_PROT_ISVAL(iocp->ioc_command)))) &&
	    !ERR_SUBCLASS_ISMH(iocp->ioc_command) &&
	    !F_CHKBIT(iocp)) {
		msg(MSG_ERROR, "Niagara family processors can only inject\n");
		msg(MSG_ERROR, "this error type into the data or tag\n");
		msg(MSG_ERROR, "check-bits.  Please use the -c option to\n");
		msg(MSG_ERROR, "produce this type of error\n");
		return (EIO);
	}

	/*
	 * Note that KT/RF is using the VF remote command definitions.
	 */
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
	 * Return if the user specified CPU IDs for both threads
	 * (note that the bind criteria are mutually exclusive).
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
