/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file contains support for SPARC64-VI Olympus-C processor.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_oc.h>
#include "mtst.h"

int	oc_pre_test(mdata_t *);

/*
 * These OC specific errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t olympusc_cmds[] = {
	/*
	 * CPU-detected or MAC-detected errors.
	 */
	"kdue",		do_k_err,		G4U_KD_UE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected or MAC-detected kern data UE.",

	"kiue",		do_k_err,		G4U_KI_UE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected or MAC-detected kern instr UE.",

	"udue",		do_u_err,		G4U_UD_UE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected or MAC-detected user data UE.",

	"uiue",		do_u_err,		G4U_UI_UE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected or MAC-detected user instr UE.",

	"mphys=addr",	do_k_err,		G4U_MPHYS,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Insert an error into memory at \"addr\".",

	"kmpeek=addr,,nc,size",	do_k_err,	G4U_KMPEEK,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Peek at physical memory location \"addr\".\n"
	"\t\t\tIf \"nc\" is non-zero the mapping is non-cacheable.\n"
	"\t\t\t\"size\" specifies the size of the access.",

	"kmpoke=addr,,nc,size",	do_k_err,	G4U_KMPOKE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Poke at physical memory location \"addr\".\n"
	"\t\t\tIf \"nc\" is non-zero the mapping is non-cacheable.\n"
	"\t\t\t\"size\" specifies the size of the access.",

	/*
	 * CPU-detected only errors.
	 */
	"kduetl1",	do_k_err,		OC_KD_UETL1,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected kernel data MI UE at TL1.",

	"kiuetl1",	do_k_err,		OC_KI_UETL1,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected kernel instr MI UE at TL1.",

	"l1due",	do_k_err,		OC_L1DUE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected L1D$ UE.",

	"l1duetl1",	do_k_err,		OC_L1DUETL1,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected L1D$ UE at TL1.",

	"l2ue",		do_k_err,		OC_L2UE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected L2$ UE.",

	"l2uetl1",	do_k_err,		OC_L2UETL1,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a CPU-detected L2$ UE at TL1.",

	"iugr",		do_k_err,		OC_IUGR,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a %rn parity error.",

	"kdmtlb",	do_k_err,		OC_KD_MTLB,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a kernel data TLB multiple-hit error.",

	"udmtlb",	do_u_err,		OC_UD_MTLB,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a user data TLB multiple-hit error.",

	"kimtlb",	do_k_err,		OC_KI_MTLB,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a kernel instr TLB multiple-hit error.",

	"uimtlb",	do_u_err,		OC_UI_MTLB,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a user instr TLB multiple-hit error.",

	/*
	 * MAC-detected errors
	 */

	/*
	 * ice: MI or PTRL intermittent CEs.
	 *	No e-reports are generated for these errors.
	 *	These are provided for verification purposes.
	 * ce:  MI or PTRL permanent CEs.
	 *	E-reports are generated for Permanent CEs only
	 *	in the normal mode.  No e-reports are generated
	 *	for Permanent CEs in mirror mode.
	 */
	"ice",		do_k_err,		OC_ICE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected PTRL or MI intermittent CE\n"
	"\t\t\t(normal mode only).",

	"ce",		do_k_err,		OC_PCE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected PTRL or MI permanent CE.",

	/*
	 * PTRL UE
	 */
	"pue",		do_k_err,		OC_PUE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected normal mode or 1-sided mirror\n"
	"\t\t\tmode PTRL UE.",

	/*
	 * MAC-detected errors, mirror mode.
	 */
	"kdcmpe",	do_k_err,		OC_KD_CMPE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected mirror mode kernel data MI\n"
	"\t\t\tcompare error.",

	"kicmpe",	do_k_err,		OC_KI_CMPE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected mirror mode kernel instr MI\n"
	"\t\t\tcompare error.",

	"udcmpe",	do_u_err,		OC_UD_CMPE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected mirror mode user data MI\n"
	"\t\t\tcompare error.",

	"uicmpe",	do_u_err,		OC_UI_CMPE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected mirror mode user instr MI\n"
	"\t\t\tcompare error.",

	"cmpe",		do_k_err,		OC_CMPE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected mirror mode PTRL compare\n"
	"\t\t\terror.",

	"mue",		do_k_err,		OC_MUE,
	NULL,		NULL,
	NULL,		NULL,
	NULL,		NULL,
	"Cause a MAC-detected mirror mode 2-sided PTRL UE.",


	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

static cmd_t *oc_commands[] = {
	olympusc_cmds,
	NULL
};

static	opsvec_t operations = {
	NULL,
	oc_pre_test,			/* pre-test routine */
	NULL
};

void
oc_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = oc_commands;
}

/*
 * This routine sets the NOERR flag for MI and Patrol errors,
 * which are triggered by the MC Patrol, to avoid the user land
 * triggering them.
 */
int
oc_pre_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	uint64_t	misc1;

	switch (IOC_COMMAND(iocp)) {
	case OC_ICE:
	case OC_PCE:
		misc1 = (F_MISC1(iocp)? iocp->ioc_misc1 : 0);
		if (misc1 == 0 || misc1 == 1)
			IOC_FLAGS(iocp) |= FLAGS_NOERR;
		break;

	case OC_PUE:
	case OC_MUE:
	case OC_KD_CMPE:	/* MI */
	case OC_KI_CMPE:	/* MI */
	case OC_UD_CMPE:	/* MI */
	case OC_UI_CMPE:	/* MI */
	case OC_CMPE:		/* PTRL */
		IOC_FLAGS(iocp) |= FLAGS_NOERR;
		break;

	case G4U_KD_UE:
	case G4U_KI_UE:
	case G4U_UD_UE:
	case G4U_UI_UE:
		misc1 = (F_MISC1(iocp)? iocp->ioc_misc1 : 0);
		if (misc1 == 1 || misc1 == 2)
			IOC_FLAGS(iocp) |= FLAGS_NOERR;
		break;

	default:
		break;
	}
	return (0);
}
