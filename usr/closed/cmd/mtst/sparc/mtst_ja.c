/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Jalapeno (US-IIIi) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_ch.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_ja.h>
#include "mtst.h"

/*
 * Jalapeno specific routines located in this file.
 */
int	ja_flushall_l2(mdata_t *);
void	ja_init(mdata_t *);
int	ja_pre_test(mdata_t *);

/*
 * These US3i generic and Jalapeno errors are grouped according to the
 * definitions in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t jalapeno_cmds[] = {

	/*
	 * E$ tag (ETP) errors.
	 */

	"kdetp",		do_k_err,		JA_KD_ETP,
	MASK(0xe1fffff),	BIT(2),
	MASK(BIT(28)),		BIT(28),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag parity error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

cmd_t us3i_generic_cmds[] = {

	/*
	 * Foreign/Remote Memory errors.
	 */

	"kdfrue",		do_k_err,		JA_KD_FRUE,
	MASK(ALL_BITS),		BIT(1)|BIT(0),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data foreign/remote uncorrectable\n"
	"\t\t\tmemory error.",

	"kifrue",		do_k_err,		JA_KI_FRUE,
	MASK(ALL_BITS),		BIT(2) | BIT(1),
	MASK(0x1ff),		BIT(2) | BIT(1),
	OFFSET(8),		NULL,
	"Cause a kern instr foreign/remote uncorrectable\n"
	"\t\t\tmemory error.",

	"udfrue",		do_u_cp_err,		JA_UD_FRUE,
	MASK(ALL_BITS),		BIT(3) | BIT(2),
	MASK(0x1ff),		BIT(3) | BIT(2),
	OFFSET(8),		OFFSET(0),
	"Cause a user data foreign/remote uncorrectable\n"
	"\t\t\tmemory error.",

	"uifrue",		do_u_cp_err,		JA_UI_FRUE,
	MASK(ALL_BITS),		BIT(4) | BIT(3),
	MASK(0x1ff),		BIT(4) | BIT(3),
	OFFSET(8),		NULL,
	"Cause a user instr foreign/remote uncorrectable\n"
	"\t\t\tmemory error.",

	"kdfrce",		do_k_err,		JA_KD_FRCE,
	MASK(ALL_BITS),		BIT(5),
	MASK(0x1ff),		BIT(5),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data foreign/remote correctable\n"
	"\t\t\tmemory error.",

	"kdfrcetl1",		do_k_err,		JA_KD_FRCETL1,
	MASK(ALL_BITS),		BIT(6),
	MASK(0x1ff),		BIT(6),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data foreign/remote correctable memory\n"
	"\t\t\terror at trap level 1.",

	"kdfrcestorm",		do_k_err,		JA_KD_FRCESTORM,
	MASK(ALL_BITS),		BIT(7),
	MASK(0x1ff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data foreign/remote correctable memory\n"
	"\t\t\terror storm.",

	"kifrce",		do_k_err,		JA_KI_FRCE,
	MASK(ALL_BITS),		BIT(8),
	MASK(0x1ff),		BIT(8),
	OFFSET(8),		NULL,
	"Cause a kern instr foreign/remote correctable\n"
	"\t\t\tmemory error.",

	"kifrcetl1",		do_k_err,		JA_KI_FRCETL1,
	MASK(ALL_BITS),		BIT(9),
	MASK(0x1ff),		BIT(0),
	OFFSET(8),		NULL,
	"Cause a kern instr foreign/remote correctable memory\n"
	"\t\t\terror at trap level 1.",

	"udfrce",		do_u_cp_err,		JA_UD_FRCE,
	MASK(ALL_BITS),		BIT(10),
	MASK(0x1ff),		BIT(1),
	OFFSET(8),		OFFSET(0),
	"Cause a user data foreign/remote correctable\n"
	"\t\t\tmemory error.",

	"uifrce",		do_u_cp_err,		JA_UI_FRCE,
	MASK(ALL_BITS),		BIT(11),
	MASK(0x1ff),		BIT(2),
	OFFSET(8),		NULL,
	"Cause a user instr foreign/remote correctable\n"
	"\t\t\tmemory error.",

	/*
	 * JBus errors.
	 */

	"kdbe",			do_k_err,		JA_KD_BE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern JBus bus-error on read.",

	"kpeekbe",		do_k_err,		JA_KD_BEPEEK,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern JBus bus-error on protected read.",

	"kdbepr",		do_k_err,		JA_KD_BEPR,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus parity error on prefetch.",

	"kdto",			do_k_err,		JA_KD_TO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus unmapped error on read.",

	"kdtopr",		do_k_err,		JA_KD_TOPR,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus unmapped error on prefetch.",

	"kdbp",			do_k_err,		JA_KD_BP,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data JBus parity error on read.",

	"kdom",			do_k_err,		JA_KD_OM,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus out of range memory error.",

	"kdums",		do_k_err,		JA_KD_UMS,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus unmapped store error.",

	"kdwbp",		do_k_err,		JA_KD_WBP,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data JBus parity error on writeback.",

	"kdjeto",		do_k_err,		JA_KD_JETO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus protocol error due to timeout\n"
	"\t\t\tdue to a hardware timeout.",

	"kdsce",		do_notimp,		JA_KD_SCE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus parity error on system\n"
	"\t\t\tsnoop result.",

	"kdjeic",		do_k_err,		JA_KD_JEIC,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a system interface protocol error.",

	"kdisap",		do_k_err,		JA_KD_ISAP,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data JBus parity error on address or\n"
	"\t\t\tJ_ADTYPE.",

	/*
	 * D$ errors.
	 */

	"kddspel",		do_k_err,		CHP_KD_DDSPEL,
	MASK(ALL_BITS),		BIT(0),
	MASK(0xff00),		BIT(8),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable dcache data parity error\n"
	"\t\t\tdue to a load.",

	"kddspeltl1",		do_k_err,		CHP_KD_DDSPELTL1,
	MASK(ALL_BITS),		BIT(1),
	MASK(0xff00),		BIT(9),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable dcache data parity error\n"
	"\t\t\tdue to a load at trap level 1.",

	"uddspel",		do_notimp,		CHP_UD_DDSPEL,
	NULL, 			NULL,
	NULL, 			NULL,
	NULL,			NULL,
	"Cause a user sw correctable dcache data parity error\n"
	"\t\t\tdue to a load.",

	"kdtspel",		do_k_err,		CHP_KD_DTSPEL,
	MASK(0x3FFFFFFE),	BIT(3),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"kdtspeltl1",		do_k_err,		CHP_KD_DTSPELTL1,
	MASK(0x3FFFFFFE),	BIT(4),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable dcache tag parity error\n"
	"\t\t\tdue to a load at trap level 1.",

	"udtspel",		do_notimp,		CHP_UD_DTSPEL,
	MASK(0x3FFFFFFE), 	BIT(5),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user sw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"kdthpel",		do_k_err,		CHP_KD_DTHPEL,
	MASK(0x3FFFFFFE),	BIT(6),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel hw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"udthpel",		do_notimp,		CHP_UD_DTHPEL,
	MASK(0x3FFFFFFE), 	BIT(7),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user hw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"dtphys=addr,xor",	do_k_err,		G4U_DTPHYS,
	MASK(0x3FFFFFFE),	BIT(8),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Insert an error into the dcache tag at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.\n",

	/*
	 * I$ errors.
	 */

	"kidspe",		do_k_err,		CHP_KI_IDSPE,
	MASK(0xffffffff),	BIT(0),
	MASK(0x200000000ULL),	BIT(33),
	NULL,			NULL,
	"Cause a kernel sw correctable icache data parity error.",

	"kidspetl1",		do_k_err,		CHP_KI_IDSPETL1,
	MASK(0xffffffff),	BIT(1),
	MASK(0x200000000ULL),	BIT(33),
	NULL,			NULL,
	"Cause a kernel sw correctable icache data parity error\n"
	"\t\t\tat trap level 1.",

	"kidspepcr",		do_k_err,		CHP_KI_IDSPEPCR,
	MASK(0xfffff800),	BIT(11),
	MASK(0x200000000ULL),	BIT(33),
	NULL,			NULL,
	"Cause a kernel sw correctable icache data (pc-rel)\n"
	"\t\t\tparity error.",

	"uidspe",		do_notimp,		CHP_UI_IDSPE,
	NULL, 			NULL,
	NULL, 			NULL,
	NULL,			NULL,
	"Cause a user sw correctable icache data parity error.",

	"kitspe",		do_k_err,		CHP_KI_ITSPE,
	MASK(0x1FFFFFFF00ULL),	BIT(8),
	MASK(0x2000000000ULL),	BIT(37),
	NULL,			NULL,
	"Cause a kernel sw correctable icache tag parity error.",

	"kitspetl1",		do_k_err,		CHP_KI_ITSPETL1,
	MASK(0x1FFFFFFF00ULL),	BIT(9),
	MASK(0x2000000000ULL),	BIT(37),
	NULL,			NULL,
	"Cause a kernel sw correctable icache tag parity error\n"
	"\t\t\tat trap level 1.",

	"uitspe",		do_notimp,		CHP_UI_ITSPE,
	MASK(0x1FFFFFFF00ull),	BIT(10),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		OFFSET(0),
	"Cause a user sw correctable icache tag parity error.",

	"kithpe",		do_k_err,		CHP_KI_ITHPE,
	MASK(0x1FFFFFFF00ULL),	BIT(11),
	MASK(0x2000000000ULL),	BIT(37),
	NULL,			NULL,
	"Cause a kernel hw correctable icache tag parity error.",

	"uithpe",		do_notimp,		CHP_UI_ITHPE,
	MASK(0x1FFFFFFF00ull), 	BIT(12),
	MASK(0x2000000000ull), 	BIT(37),
	OFFSET(0),		OFFSET(0),
	"Cause a user hw correctable icache tag parity error.",

	"itphys=addr,xor",	do_k_err,		G4U_ITPHYS,
	MASK(0x1FFFFFFF00ULL),	BIT(13),
	MASK(0x2000000000ULL),	BIT(37),
	NULL,			NULL,
	"Insert an error into the icache tag at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

static cmd_t *commands[] = {
	jalapeno_cmds,
	us3i_generic_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static	opsvec_t operations = {
	ja_flushall_l2,			/* flush entire L2$ */
	ja_pre_test,			/* pre-test routine */
	NULL				/* no post-test routine */
};

void
ja_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}

/*
 * This routine flushes the entire L2$.
 */
int
ja_flushall_l2(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;
	int		l2_size = (int)cip->c_l2_size;
	int		l2_linesize = (int)cip->c_l2_linesize;
	int		l2_assoc = (int)cip->c_l2_assoc;
	int		l2_flushsize = (int)cip->c_l2_flushsize;
	int		i, j, offset;
	caddr_t		displace = mdatap->m_displace;

	if (!displace) {
		msg(MSG_ABORT, "ja_flushall_l2: displacement area has "
			"not been initialized!\n");
		/* NOT REACHED */
	}

	/*
	 * Jalapeno has a 4 way associative e$ with an LFSR algorithm.
	 * 18 accesses per index are required to displace all 4 ways
	 * The LFSR is shared for all misses so we flush one index at a time.
	 * The displacement flush algorithm is:
	 *	For each way index
	 *	    For 18 times
	 *		read unique data at that index
	 */
	for (i = 0; i < (l2_size / l2_assoc / l2_linesize); i++) {
		offset = i * l2_linesize;
		for (j = 0; j < 18; j++) {
			asmld_quick((caddr_t)(displace + offset));
			offset += l2_size/l2_assoc;
			if (offset >= l2_flushsize) {
				msg(MSG_ERROR, "ja_flushall_l2: "
					"flush address wrapped!\n");
				msg(MSG_ERROR, "ja_flushall_l2: "
					"offset=0x%x, i=0x%x, j=0x%x, "
					"l2_flushsize=0x%x\n",
					offset, i, j, l2_flushsize);
				offset = 0;
				return (-1);
			}
		}
	}

	return (0);
}

/*
 * This routine gets executed prior to running a test.
 */
int
ja_pre_test(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	/*
	 * Initialize thread binding criteria based on test type.
	 */
	if (IOC_COMMAND(iocp) == CH_KI_OUCU) {
		/*
		 * This actually starts out as a memory error and must
		 * therefore be injected into local memory.
		 */
		if ((IOC_FLAGS(iocp) & FLAGS_BINDCPU) == 0)
			iocp->ioc_bind_thr_criteria[0] = THREAD_BIND_LOCAL_MEM;
	} else if ((IOC_COMMAND(iocp) == CHP_KD_DTHPEL) ||
	    (IOC_COMMAND(iocp) == JA_KD_BP)) {
		iocp->ioc_nthreads = 2;
	} else if (ERR_JBUS_ISFR(iocp->ioc_command)) {
		/*
		 * Thread 0 is the consumer and references remote memory.
		 * Thread 1 is the producer and must inject the error
		 * into local memory.
		 */
		iocp->ioc_nthreads = 2;
		/*
		 * If user specified options have not already set the
		 * cpu bindings then set them here.
		 */
		if ((IOC_FLAGS(iocp) & FLAGS_BINDCPU) == 0) {
			iocp->ioc_bind_thr_criteria[0] = THREAD_BIND_REMOTE_MEM;
			iocp->ioc_bind_thr_criteria[1] = THREAD_BIND_LOCAL_MEM;
		}
	} else {
		/*
		 * Memory errors can only be injected into local memory.
		 * This covers the producer thread for single threaded tests.
		 */
		if (ERR_CLASS_ISMEM(IOC_COMMAND(iocp)) &&
		    ((IOC_FLAGS(iocp) & FLAGS_BINDCPU) == 0)) {
			iocp->ioc_bind_thr_criteria[0] = THREAD_BIND_LOCAL_MEM;
		}
	}

	return (0);
}
