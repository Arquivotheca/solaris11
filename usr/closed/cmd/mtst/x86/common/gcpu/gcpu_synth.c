/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "gcpu.h"
#include <sys/mca_x86.h>
#include <sys/trap.h>
#include <errno.h>
#include <strings.h>

struct gcpu_cmdopts {
	uint64_t gco_validmask;		/* bitmask of specified options */
	int gco_bank;			/* bank for injection */
	uint64_t gco_status;		/* MCi_STATUS bits to add */
	uint64_t gco_addr;		/* MCi_ADDR value */
	uint64_t gco_misc;		/* MCi_MISC value */
};

struct gcpu_errcode_mn {
	const char *gem_name;
	uint16_t gem_val;
};

/* Flags for gco_validmask */
#define	GCPU_CMDIDX_BANK	0x0001
#define	GCPU_CMDIDX_STATUS	0x0002
#define	GCPU_CMDIDX_ADDR	0x0004
#define	GCPU_CMDIDX_MISC	0x0008
#define	GCPU_CMDIDX_PRIVADDR	0x0010
#define	GCPU_CMDIDX_RIPV	0x0020

static int
gcpu_optmatch(const mtst_argspec_t *arg, const char *s)
{
	return (strcmp(arg->mas_argnm, s) == 0);
}

static uint64_t
gcpu_optval(const mtst_argspec_t *arg, int *err)
{
	if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
		++*err;
		return (0);
	}

	return (arg->mas_argval);
}

static uint16_t
gcpu_mnval(const mtst_argspec_t *arg, const struct gcpu_errcode_mn *mapent,
    int nents, int *err)
{
	const char *optnm = arg->mas_argstr;
	int i;

	if (arg->mas_argtype != MTST_ARGTYPE_STRING) {
		++*err;
		return (0);
	}

	for (i = 0; i < nents; i++, mapent++) {
		if (strcmp(optnm, mapent->gem_name) == 0)
			return (mapent->gem_val);
	}

	++*err;
	return (0);
}

static int
gcpu_arg_process(const mtst_argspec_t *args, int nargs,
    struct gcpu_cmdopts *opt)
{
	mca_x86_mcistatus_t *up = (mca_x86_mcistatus_t *)(&opt->gco_status);
	int i, err = 0;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];

		if (gcpu_optmatch(arg, "bank")) {
			opt->gco_bank = (int)gcpu_optval(arg, &err);
			opt->gco_validmask |= GCPU_CMDIDX_BANK;

		} else if (gcpu_optmatch(arg, "status")) {
			opt->gco_status = gcpu_optval(arg, &err);
			opt->gco_validmask |= GCPU_CMDIDX_STATUS;

		} else if (gcpu_optmatch(arg, "addr")) {
			opt->gco_addr = gcpu_optval(arg, &err);
			up->mcistatus_addrv = 1;
			opt->gco_validmask |= GCPU_CMDIDX_ADDR;

		} else if (gcpu_optmatch(arg, "addrv")) {
			up->mcistatus_addrv = 1;

		} else if (gcpu_optmatch(arg, "privaddr")) {
			up->mcistatus_addrv = 1;
			opt->gco_validmask |= GCPU_CMDIDX_PRIVADDR;

		} else if (gcpu_optmatch(arg, "misc")) {
			opt->gco_misc = gcpu_optval(arg, &err);
			opt->gco_validmask |= GCPU_CMDIDX_MISC;

		} else if (gcpu_optmatch(arg, "ripv")) {
			if (gcpu_optval(arg, &err) == 0)
				opt->gco_validmask  &= ~GCPU_CMDIDX_RIPV;

		} else if (gcpu_optmatch(arg, "en")) {
			if (gcpu_optval(arg, &err))
				up->mcistatus_en = 1;

		} else if (gcpu_optmatch(arg, "over")) {
			if (gcpu_optval(arg, &err))
				up->mcistatus_over = 1;

		} else if (gcpu_optmatch(arg, "uc")) {
			if (gcpu_optval(arg, &err))
				up->mcistatus_uc = 1;

		} else if (gcpu_optmatch(arg, "pcc")) {
			if (gcpu_optval(arg, &err))
				up->mcistatus_pcc = 1;

		} else if (gcpu_optmatch(arg, "tbes")) {
			if (gcpu_optval(arg, &err))
				up->mcistatus_tbes = 1;

		} else if (gcpu_optmatch(arg, "unclassval")) {
			up->mcistatus_errcode |=
			    (uint32_t)gcpu_optval(arg, &err) &
			    MCAX86_SIMPLE_INTERNAL_UNCLASS_VALUE_MASK;

		} else if (gcpu_optmatch(arg, "otherinfo")) {
			uint32_t val = gcpu_optval(arg, &err);
			if (up->mcistatus_tbes)
				up->mcistatus_otherinfo_tes_p = val;
			else
				up->mcistatus_otherinfo_tes_np = val;

		} else if (gcpu_optmatch(arg, "mserrcode")) {
			up->mcistatus_mserrcode =
			    (uint32_t)gcpu_optval(arg, &err);

		} else if (gcpu_optmatch(arg, "TT")) {
			const struct gcpu_errcode_mn map[] = {
			    { "I", MCAX86_ERRCODE_TT_INSTR },
			    { "D", MCAX86_ERRCODE_TT_DATA },
			    { "G", MCAX86_ERRCODE_TT_GEN }
			};
			up->mcistatus_errcode |= gcpu_mnval(arg, &map[0],
			    sizeof (map) / sizeof (map[0]), &err);

		} else if (gcpu_optmatch(arg, "LL")) {
			const struct gcpu_errcode_mn map[] = {
			    { "L0", MCAX86_ERRCODE_LL_L0 },
			    { "L1", MCAX86_ERRCODE_LL_L1 },
			    { "L2", MCAX86_ERRCODE_LL_L2 },
			    { "LG", MCAX86_ERRCODE_LL_LG }
			};
			up->mcistatus_errcode |= gcpu_mnval(arg, &map[0],
			    sizeof (map) / sizeof (map[0]), &err);

		} else if (gcpu_optmatch(arg, "RRRR")) {
			const struct gcpu_errcode_mn map[] = {
			    { "ERR", MCAX86_ERRCODE_RRRR_ERR },
			    { "RD", MCAX86_ERRCODE_RRRR_RD },
			    { "WR", MCAX86_ERRCODE_RRRR_WR },
			    { "DRD", MCAX86_ERRCODE_RRRR_DRD },
			    { "DWR", MCAX86_ERRCODE_RRRR_DWR },
			    { "IRD", MCAX86_ERRCODE_RRRR_IRD },
			    { "PREFETCH", MCAX86_ERRCODE_RRRR_PREFETCH },
			    { "EVICT", MCAX86_ERRCODE_RRRR_EVICT },
			    { "SNOOP", MCAX86_ERRCODE_RRRR_SNOOP }
			};
			up->mcistatus_errcode |= gcpu_mnval(arg, &map[0],
			    sizeof (map) / sizeof (map[0]), &err);

		} else if (gcpu_optmatch(arg, "PP")) {
			const struct gcpu_errcode_mn map[] = {
			    { "SRC", MCAX86_ERRCODE_PP_SRC },
			    { "RES", MCAX86_ERRCODE_PP_RES },
			    { "OBS", MCAX86_ERRCODE_PP_OBS },
			    { "GEN", MCAX86_ERRCODE_PP_GEN }
			};
			up->mcistatus_errcode |= gcpu_mnval(arg, &map[0],
			    sizeof (map) / sizeof (map[0]), &err);

		} else if (gcpu_optmatch(arg, "II")) {
			const struct gcpu_errcode_mn map[] = {
			    { "MEM", MCAX86_ERRCODE_II_MEM },
			    { "IO", MCAX86_ERRCODE_II_IO },
			    { "GEN", MCAX86_ERRCODE_II_GEN }
			};
			up->mcistatus_errcode |= gcpu_mnval(arg, &map[0],
			    sizeof (map) / sizeof (map[0]), &err);

		} else if (gcpu_optmatch(arg, "T")) {
			const struct gcpu_errcode_mn map[] = {
			    { "NOTIMEOUT", MCAX86_ERRCODE_T_NONE },
			    { "TIMEOUT", MCAX86_ERRCODE_T_TIMEOUT }
			};
			up->mcistatus_errcode |= gcpu_mnval(arg, &map[0],
			    sizeof (map) / sizeof (map[0]), &err);
		}

		if (err != 0)
			return (0);
	}

	return (1);
}

#define	GCPU_SYNTH_STMTS	5	/* number of statements below */

int
gcpu_synthesize_cmn(mtst_cpuid_t *cpi, uint_t cmdflags,
    const mtst_argspec_t *args, int nargs, uint64_t initstatus)
{
	struct gcpu_cmdopts opts;
	mtst_inj_stmt_t mis[GCPU_SYNTH_STMTS], *misp = mis;
	mca_x86_mcistatus_t *sp = (mca_x86_mcistatus_t *)(&opts.gco_status);
	uint64_t mcg_status = 0;
	uint_t msrflags = 0;
	int id = -1;
	int err = 0;
	int n;

	opts.gco_validmask = GCPU_CMDIDX_RIPV;
	opts.gco_bank = -1;
	opts.gco_status = initstatus;
	opts.gco_addr = MTST_MEM_ADDR_UNSPEC;
	opts.gco_misc = 0;

	if (!gcpu_arg_process(args, nargs, &opts)) {
		errno = EINVAL;
		return (MTST_CMD_USAGE);
	}

	if (opts.gco_bank == -1) {
		/*
		 * Should have been caught as a required argument, but just
		 * in case.
		 */
		mtst_cmd_warn("no MCA bank number specified\n");
		errno = EINVAL;
		return (MTST_CMD_ERR);
	}

	/*
	 * If the user has not specified a raw status value add in
	 * valid and enabled bits.
	 */
	if ((opts.gco_validmask & GCPU_CMDIDX_STATUS) == 0)
		opts.gco_status |= MSR_MC_STATUS_VAL | MSR_MC_STATUS_EN;

	if (sp->mcistatus_addrv &&
	    (opts.gco_validmask & GCPU_CMDIDX_ADDR) == 0) {
		/*
		 * Either the 'initstatus' value included ADDRV or we added
		 * it as the result of processing 'addr' or 'addrv' arguments.
		 * If an address has not been provided via the 'addr' option
		 * then allocate some memory and lock and translate it.
		 */
		int type = (opts.gco_validmask & GCPU_CMDIDX_PRIVADDR) ?
		    MTST_MEM_RESERVE_KERNEL : MTST_MEM_RESERVE_USER;
		if ((id = mtst_mem_reserve(type, NULL, NULL, NULL,
		    &opts.gco_addr)) < 0) {
			mtst_cmd_warn("failed to allocate memory\n");
			errno = ENOMEM;
			return (MTST_CMD_ERR);
		}
	}

	if (cmdflags & MTST_CMD_F_FORCEMSRWR)
		msrflags |= MTST_MIS_FLAG_MSR_FORCE;
	if (cmdflags & MTST_CMD_F_INTERPOSEOK)
		msrflags |= MTST_MIS_FLAG_MSR_INTERPOSEOK;
	if (cmdflags & MTST_CMD_F_INTERPOSE)
		msrflags |= MTST_MIS_FLAG_MSR_INTERPOSE;

	/*
	 * Build MCG_STATUS value.
	 */
	if ((cmdflags & MTST_CMD_F_INT18) || (cmdflags & (MTST_CMD_F_INT18 |
	    MTST_CMD_F_POLLED)) == 0 && (opts.gco_status & MSR_MC_STATUS_UC)) {
		/* If we will #MC on injection, set MCIP, EIPV and RIPV */
		mcg_status |= MCG_STATUS_MCIP | MCG_STATUS_EIPV;
		if (opts.gco_validmask & GCPU_CMDIDX_RIPV)
			mcg_status |= MCG_STATUS_RIPV;
	}

	/* One injector statement to fill MCi_STATUS */
	mtst_mis_init_msr_wr(misp++, cpi,
	    IA32_MSR_MC(opts.gco_bank, STATUS), opts.gco_status, msrflags);

	/* Another for MCi_ADDR, but only if ADDRV */
	if (sp->mcistatus_addrv)
		mtst_mis_init_msr_wr(misp++, cpi,
		    IA32_MSR_MC(opts.gco_bank, ADDR), opts.gco_addr, msrflags);

	/* One more for MCi_MISC, if specified */
	if (opts.gco_validmask & GCPU_CMDIDX_MISC)
		mtst_mis_init_msr_wr(misp++, cpi,
		    IA32_MSR_MC(opts.gco_bank, MISC), opts.gco_misc, msrflags);

	/* Another for MCG_STATUS */
	mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS, mcg_status,
	    msrflags);

	/* And the final statement to raise an #MC or to poke the poller */
	switch (cmdflags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* #MC if uncorrected, otherwise wake poller */
		if (opts.gco_status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE, 0);
		else
			mtst_mis_init_poll(misp++, cpi, 0);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE, 0);
		break;
	}

	n = ((uintptr_t)misp - (uintptr_t)mis) / sizeof (mtst_inj_stmt_t);
	if (n >= GCPU_SYNTH_STMTS) {
		mtst_cmd_warn("gcpu_synthesize_cmn: %d statements "
		    "overflows mis array ... aborting\n");
		err = E2BIG;
	} else {
		err = mtst_inject(mis, n);
	}

	if (id != -1)
		(void) mtst_mem_unreserve(id);

	if (err != 0)
		errno = err;

	return (err ? MTST_CMD_ERR : MTST_CMD_OK);
}
