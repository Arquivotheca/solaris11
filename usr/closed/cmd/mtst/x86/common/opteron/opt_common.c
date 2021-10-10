/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>
#include <sys/mca_amd.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "opt.h"

static int
opt_bad(const char *msg)
{
	mtst_cmd_warn("%s\n", msg);
	return (0);
}

static int
opt_arg_process(const mtst_argspec_t *args, int nargs, uint64_t *pap,
    int *syndp, int *syndtypep, int *privp, int *chanp)
{
	int synd = -1;
	int syndtype = -1;
	int priv = -1;
	int chan = -1;
	int i;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];

		if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (opt_bad("'addr' needs value type"));
			*pap = arg->mas_argval;

		} else if (strcmp(arg->mas_argnm, "syndrome") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (opt_bad("'syndrome' needs value type"));
			synd = arg->mas_argval;

		} else if (strcmp(arg->mas_argnm, "syndrome-type") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_STRING)
				return (opt_bad("'syndrome-type' needs string "
				    "type"));

			if (strcmp(arg->mas_argstr, "E") == 0)
				syndtype = OPT_SYNDTYPE_ECC;
			else if (strcmp(arg->mas_argstr, "C") == 0)
				syndtype = OPT_SYNDTYPE_CK;
			else
				return (opt_bad("'syndrome-type' is 'E' or "
				    "'C'"));

		} else if (strcmp(arg->mas_argnm, "priv") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_STRING)
				return (opt_bad("'priv' needs string type"));

			if (strcmp(arg->mas_argstr, "true") == 0)
				priv = 1;
			else if (strcmp(arg->mas_argstr, "false") == 0)
				priv = 0;
			else
				return (opt_bad("'priv' is 'true' or 'false'"));
		} else if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_STRING)
				return (opt_bad("'channel' needs string type"));

			if (strcmp(arg->mas_argstr, "A") == 0)
				chan = OPT_NBSTAT_CHANNELA;
			else if (strcmp(arg->mas_argstr, "B") == 0)
				chan = OPT_NBSTAT_CHANNELB;
			else
				return (opt_bad("'channel' is 'A' or 'B'"));
		}
	}

	if (syndtype != -1 && syndtypep != NULL)
		*syndtypep = syndtype;

	if (synd != -1 && syndp != NULL)
		*syndp = synd;

	/* if they don't specify a privilege mode, assume kernel */
	if (priv == -1)
		priv = 1;

	if (privp != NULL)
		*privp = priv;

	if (chan != -1 && chanp != NULL)
		*chanp = chan;

	return (1);
}

static uint64_t
optstat2cmnbankstat(uint64_t optstat)
{
	uint64_t bankstat = 0;

	if (optstat & OPT_STAT_VALID)
		bankstat |= MSR_MC_STATUS_VAL;
	if (optstat & OPT_STAT_EN)
		bankstat |= MSR_MC_STATUS_EN;
	if (optstat & OPT_STAT_ADDRV)
		bankstat |= MSR_MC_STATUS_ADDRV;
	if (optstat & OPT_STAT_UC)
		bankstat |= MSR_MC_STATUS_UC;
	if (optstat & OPT_STAT_CECC)
		bankstat |= AMD_BANK_STAT_CECC;
	if (optstat & OPT_STAT_UECC)
		bankstat |= AMD_BANK_STAT_UECC;
	if (optstat & OPT_STAT_PCC)
		bankstat |= MSR_MC_STATUS_PCC;
	if (optstat & OPT_STAT_SCRUB)
		bankstat |= AMD_BANK_STAT_SCRUB;
	/* OPT_STAT_SYNDV doesn't map to AMD_BANK_STAT_x */

	return (bankstat);
}

static uint64_t
optstat2nbbankstat(uint64_t optstat)
{
	uint64_t bankstat = 0;

	/* OPT_NBSTAT_CHANNELA sets no bank status bit */

	if (optstat & OPT_NBSTAT_CHANNELB)
		bankstat |= AMD_NB_STAT_DRAMCHANNEL;

	return (bankstat);
}

/*ARGSUSED*/
int
opt_synthesize_common(mtst_cpuid_t *cpi, uint_t cmdflags,
    const mtst_argspec_t *args, int nargs, uint64_t errcode, uint64_t optstat,
    uint_t statmsr, uint_t addrmsr, uint_t oksyndtype, uint64_t cmdpriv)
{
	mtst_inj_stmt_t mis[4], *misp = mis;
	uint64_t pa = MTST_MEM_ADDR_UNSPEC;
	uint_t msrflags = 0;
	uint64_t status;
	int synd = -1;
	int syndtype = -1;
	int id = -1;
	int priv = -1;
	int chan = -1;
	int err;

	if (!opt_arg_process(args, nargs, &pa, &synd, &syndtype, &priv, &chan))
		return (MTST_CMD_USAGE);

	status = errcode;
	status |= optstat2cmnbankstat(optstat);
	if (chan != -1)
		status |= optstat2nbbankstat(chan);

	if (optstat & OPT_STAT_SYNDV) {
		/* If a syndrome-type was provided, check it is ok */
		if (syndtype != -1 && !(syndtype & oksyndtype)) {
			mtst_cmd_warn("Invalid syndrome-type for this error");
			return (MTST_CMD_USAGE);
		}

		/* Provide a default syndrome-type */
		if (syndtype == -1)
			syndtype = oksyndtype;

		/* Provide a default syndrome */
		if (synd == -1)
			synd = 1;

		switch (syndtype) {
		case OPT_SYNDTYPE_ECC:
			status |= AMD_BANK_MKSYND(synd);
			break;

		case OPT_SYNDTYPE_CK:
			status |= AMD_NB_STAT_MKCKSYND(synd);
			break;
		}
	}

	if (pa == MTST_MEM_ADDR_UNSPEC && (status & MSR_MC_STATUS_ADDRV)) {
		uint_t type = priv ?
		    MTST_MEM_RESERVE_KERNEL : MTST_MEM_RESERVE_USER;

		if ((id = mtst_mem_reserve(type, NULL, NULL, NULL, &pa)) < 0) {
			mtst_cmd_warn("failed to allocate %s memory\n",
			    priv ? "kernel" : "user");
			return (MTST_CMD_ERR);
		}
	}

	if (cmdflags & MTST_CMD_F_FORCEMSRWR)
		msrflags |= MTST_MIS_FLAG_MSR_FORCE;
	if (cmdflags & MTST_CMD_F_INTERPOSEOK)
		msrflags |= MTST_MIS_FLAG_MSR_INTERPOSEOK;
	if (cmdflags & MTST_CMD_F_INTERPOSE)
		msrflags |= MTST_MIS_FLAG_MSR_INTERPOSE;

	/* First injector statement to fill MCi_STATUS */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status, msrflags);

	/* Another to fill MCi_ADDR */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, pa, msrflags);

	/* Another for MCG_STATUS */
	if ((cmdflags & MTST_CMD_F_INT18) || (cmdflags &
	    (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0 &&
	    (status & MSR_MC_STATUS_UC)) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV | MCG_STATUS_EIPV | MCG_STATUS_MCIP,
		    msrflags);
	}

	/* A final statement to trigger the observation */
	switch (cmdflags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE, 0);
		else
			mtst_mis_init_poll(misp++, cpi, 0);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		mtst_mis_init_int(misp++, cpi, T_MCE, 0);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));

	if (err != 0)
		err = errno;

	if (id != -1)
		(void) mtst_mem_unreserve(id);

	errno = err;
	return (err ? MTST_CMD_ERR : MTST_CMD_OK);
}
