/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/mca_x86.h>
#include <sys/mca_amd.h>
#include <sys/mc_amd.h>
#include <sys/trap.h>
#include <strings.h>
#include <errno.h>

#include "aamd.h"

struct aamd_cmdopts {
	uint64_t aao_validmask;		/* bitmask of specified options */
	int aao_bank;			/* bank for injection */
	uint64_t aao_status;		/* MCi_STATUS bits to add */
	uint64_t aao_addr;		/* MCi_ADDR value */
	uint64_t aao_misc;		/* MCi_MISC value */
	uint64_t aao_chip;		/* chip number for resource */
	uint64_t aao_chan;		/* 0 for A, 1 for B */
	uint64_t aao_cs;		/* Chip-select */
	uint64_t aao_ecccnt;		/* ECC count for chan/cs */
	uint64_t aao_chip2;		/* chip number for second resource */
	uint64_t aao_chan2;		/* 0 for A, 1 for B */
	uint64_t aao_cs2;		/* Chip-select */
	uint64_t aao_ecccnt2;		/* ECC count for chan/cs */
};

/* Flags for aao_validmask */
#define	AAMD_VMASK_BANK		0x0001
#define	AAMD_VMASK_STATUS	0x0002
#define	AAMD_VMASK_ADDR		0x0004
#define	AAMD_VMASK_MISC		0x0008
#define	AAMD_VMASK_CHIP		0x0010
#define	AAMD_VMASK_CHAN		0x0020
#define	AAMD_VMASK_CS		0x0040
#define	AAMD_VMASK_ECCCNT	0x0080
#define	AAMD_VMASK_CHIP2	0x0100
#define	AAMD_VMASK_CHAN2	0x0200
#define	AAMD_VMASK_CS2		0x0400
#define	AAMD_VMASK_ECCCNT2	0x0800
#define	AAMD_VMASK_PRIVADDR	0x1000
#define	AAMD_VMASK_RIPV		0x2000

static const char *curoptstr = "???";

static int
aamd_optmatch(const mtst_argspec_t *arg, const char *s)
{
	int rc = (strcmp(arg->mas_argnm, s) == 0);

	if (rc)
		curoptstr = arg->mas_argnm;

	return (rc);
}

static uint64_t
aamd_optval(const mtst_argspec_t *arg, int *err)
{
	if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
		++*err;
		mtst_cmd_warn("option '%s' should have value type\n",
		    curoptstr);
		return (0);
	}

	return (arg->mas_argval);
}

static const char *
aamd_optstr(const mtst_argspec_t *arg, int *err)
{
	if (arg->mas_argtype != MTST_ARGTYPE_STRING ||
	    arg->mas_argstr == NULL) {
		++*err;
		mtst_cmd_warn("option '%s' should have string type\n",
		    curoptstr);
		return (NULL);
	}

	return (arg->mas_argstr);
}

static int
aamd_optbool(const mtst_argspec_t *arg, int *err)
{
	if (arg->mas_argtype != MTST_ARGTYPE_BOOLEAN) {
		++*err;
		mtst_cmd_warn("option '%s' does not take a value\n",
		    curoptstr);
		return (0);
	}

	return (1);
}

static int
aamd_arg_process(mtst_cpuid_t *cpi, const mtst_argspec_t *args, int nargs,
    struct aamd_cmdopts *opt)
{
	mca_x86_mcistatus_t *up = (mca_x86_mcistatus_t *)(&opt->aao_status);

	int i, err = 0;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];

		if (aamd_optmatch(arg, "bank")) {
			opt->aao_bank = (int)aamd_optval(arg, &err);
			opt->aao_validmask |= AAMD_VMASK_BANK;

		} else if (aamd_optmatch(arg, "status")) {
			opt->aao_status = aamd_optval(arg, &err);
			opt->aao_validmask |= AAMD_VMASK_STATUS;

		} else if (aamd_optmatch(arg, "addr")) {
			opt->aao_addr = aamd_optval(arg, &err);
			up->mcistatus_addrv = 1;
			opt->aao_validmask |= AAMD_VMASK_ADDR;

		} else if (aamd_optmatch(arg, "addrv")) {
			up->mcistatus_addrv = aamd_optbool(arg, &err);

		} else if (aamd_optmatch(arg, "ripv")) {
			if (aamd_optval(arg, &err) == 0)
				opt->aao_validmask &= ~AAMD_VMASK_RIPV;

		} else if (aamd_optmatch(arg, "en")) {
			if (aamd_optval(arg, &err))
				up->mcistatus_en = 1;

		} else if (aamd_optmatch(arg, "privaddr")) {
			up->mcistatus_addrv = 1;
			opt->aao_validmask |= AAMD_VMASK_PRIVADDR;

		} else if (aamd_optmatch(arg, "otherinfo")) {
			up->mcistatus_otherinfo_tes_np =
			    aamd_optval(arg, &err);

		} else if (aamd_optmatch(arg, "misc")) {
			opt->aao_misc = aamd_optval(arg, &err);
			opt->aao_validmask |= AAMD_VMASK_MISC;

		} else if (aamd_optmatch(arg, "over")) {
			up->mcistatus_over = aamd_optbool(arg, &err);

		} else if (aamd_optmatch(arg, "uc")) {
			up->mcistatus_uc = aamd_optbool(arg, &err);

		} else if (aamd_optmatch(arg, "pcc")) {
			up->mcistatus_pcc = aamd_optbool(arg, &err);

		} else if (aamd_optmatch(arg, "mserrcode")) {
			up->mcistatus_mserrcode =
			    (uint32_t)aamd_optval(arg, &err);

		} else if (aamd_optmatch(arg, "syndrome")) {
			uint64_t val = aamd_optval(arg, &err);

			switch (AMD_EXT_ERRCODE(opt->aao_status)) {
			case 0:
				opt->aao_status |= AMD_BANK_MKSYND(val);
				break;

			case 8:
				opt->aao_status |= AMD_NB_STAT_MKCKSYND(val);
				break;
			}

		} else if (aamd_optmatch(arg, "scrubber")) {
			opt->aao_status |= AMD_BANK_STAT_SCRUB;

		} else if (aamd_optmatch(arg, "chip")) {
			opt->aao_chip = aamd_optval(arg, &err);
			opt->aao_validmask |= AAMD_VMASK_CHIP;
			if (cpi->mci_procnodes_per_pkg > 1)
				opt->aao_chip <<= 1;

		} else if (aamd_optmatch(arg, "chan")) {
			const char *chanstr = aamd_optstr(arg, &err);

			if (chanstr != NULL) {
				if (strcmp(chanstr, "A") == 0) {
					opt->aao_chan = 0;
					opt->aao_validmask |= AAMD_VMASK_CHAN;
				} else if (strcmp(chanstr, "B") == 0) {
					opt->aao_chan = 1;
					opt->aao_validmask |= AAMD_VMASK_CHAN;
				} else {
					err++;
					mtst_cmd_warn("option 'chan' must be "
					    "'A' or 'B'\n");
				}
			}
		} else if (aamd_optmatch(arg, "cs")) {
			opt->aao_cs = aamd_optval(arg, &err);
			if (err == 0) {
				if (opt->aao_cs <= 7) {
					opt->aao_validmask |= AAMD_VMASK_CS;
				} else {
					err++;
					mtst_cmd_warn("option 'cs' should be "
					    "in range 0 .. 7\n");
				}
			}
		} else if (aamd_optmatch(arg, "ecccnt")) {
			opt->aao_ecccnt = aamd_optval(arg, &err) & 0xf;
			opt->aao_validmask |= AAMD_VMASK_ECCCNT;

		} else if (aamd_optmatch(arg, "chip2")) {
			opt->aao_chip2 = aamd_optval(arg, &err);
			opt->aao_validmask |= AAMD_VMASK_CHIP2;
			if (cpi->mci_procnodes_per_pkg > 1)
				opt->aao_chip2 <<= 1;

		} else if (aamd_optmatch(arg, "chan2")) {
			const char *chanstr = aamd_optstr(arg, &err);

			if (chanstr != NULL) {
				if (strcmp(chanstr, "A") == 0) {
					opt->aao_chan2 = 0;
					opt->aao_validmask |= AAMD_VMASK_CHAN2;
				} else if (strcmp(chanstr, "B") == 0) {
					opt->aao_chan2 = 1;
					opt->aao_validmask |= AAMD_VMASK_CHAN2;
				} else {
					err++;
					mtst_cmd_warn("option 'chan2' must be "
					    "'A' or 'B'\n");
				}
			}
		} else if (aamd_optmatch(arg, "cs2")) {
			opt->aao_cs2 = aamd_optval(arg, &err);
			if (err == 0) {
				if (opt->aao_cs2 <= 7) {
					opt->aao_validmask |= AAMD_VMASK_CS2;
				} else {
					err++;
					mtst_cmd_warn("option 'cs2' should be "
					    "in range 0 .. 7\n");
				}
			}
		} else if (aamd_optmatch(arg, "ecccnt2")) {
			opt->aao_ecccnt2 = aamd_optval(arg, &err) & 0xf;
			opt->aao_validmask |= AAMD_VMASK_ECCCNT2;
		}
	}

	return (err == 0);
}

#define	AAMD_SYNTH_STMTS	5

static int
aamd_synth_impl(mtst_cpuid_t *cpi, uint_t cmdflags, const mtst_argspec_t *args,
    int nargs, uint64_t initstatus, int bank)
{
	struct aamd_cmdopts opts;
	mtst_inj_stmt_t mis[AAMD_SYNTH_STMTS], *misp;
	mca_x86_mcistatus_t *sp = (mca_x86_mcistatus_t *)(&opts.aao_status);
	uint64_t mcg_status = 0;
	uint_t msrflags = 0;
	uint64_t vmask, tmask, tmask2;
	int id = -1;
	int err = 0;
	int n;

	opts.aao_validmask = AAMD_VMASK_RIPV;
	opts.aao_bank = bank;
	opts.aao_status = initstatus;
	opts.aao_addr = MTST_MEM_ADDR_UNSPEC;
	opts.aao_misc = 0;
	opts.aao_chip = cpi->mci_hwprocnodeid;

	if (!aamd_arg_process(cpi, args, nargs, &opts)) {
		errno = EINVAL;
		return (MTST_CMD_USAGE);
	}
	vmask = opts.aao_validmask;

	if (opts.aao_bank == -1) {
		mtst_cmd_warn("no MCA bank number specified\n");
		errno = EINVAL;
		return (MTST_CMD_ERR);
	}

	tmask = vmask & (AAMD_VMASK_CHAN | AAMD_VMASK_CS);
	tmask2 = vmask & (AAMD_VMASK_CHIP2 | AAMD_VMASK_CHAN2 | AAMD_VMASK_CS2);
	if (tmask == 0 && tmask2 != 0) {
		mtst_cmd_warn("'cs2' must only supplement 'cs'\n");
		errno = EINVAL;
		return (MTST_CMD_ERR);
	}
	if ((tmask != 0 &&
	    tmask != (AAMD_VMASK_CHAN | AAMD_VMASK_CS)) ||
	    (tmask2 != 0 &&
	    tmask2 != (AAMD_VMASK_CHIP2 | AAMD_VMASK_CHAN2 | AAMD_VMASK_CS2))) {
		mtst_cmd_warn("must specifiy chip, channel and chip-select "
		    "together\n");
		errno = EINVAL;
		return (MTST_CMD_ERR);
	}

	if (tmask != 0 && !(vmask & AAMD_VMASK_ECCCNT))
		opts.aao_ecccnt = 1;

	if (tmask2 != 0 && !(vmask & AAMD_VMASK_ECCCNT2))
		opts.aao_ecccnt2 = 1;

	/*
	 * If the user has not specified a raw status value add in
	 * valid and enabled bits.
	 */
	if ((vmask & AAMD_VMASK_STATUS) == 0)
		opts.aao_status |= MSR_MC_STATUS_VAL | MSR_MC_STATUS_EN;

	if (sp->mcistatus_addrv &&
	    (vmask & AAMD_VMASK_ADDR) == 0) {
		/*
		 * Either the 'initstatus' value included ADDRV or we added
		 * it as the result of processing 'addr' or 'addrv' arguments.
		 * If an address has not been provided via the 'addr' option
		 * then allocate some memory and lock and translate it.
		 */
		int type = (opts.aao_validmask & AAMD_VMASK_PRIVADDR) ?
		    MTST_MEM_RESERVE_KERNEL : MTST_MEM_RESERVE_USER;

		if ((id = mtst_mem_reserve(type, NULL, NULL, NULL,
		    &opts.aao_addr)) < 0) {
			mtst_cmd_warn("failed to allocate memory\n");
			errno = ENOMEM;
			return (MTST_CMD_ERR);
		}
	}

	/*
	 * If a channel and chip-select were specified then write to the
	 * online spare control register to increment the ECC count for
	 * the requested combination.
	 *
	 * Family 0xf (rev F and later) and family 0x10 have online spare
	 * registers which include ECC counts by channel and chip-select.
	 * The register structure are similar but not identical - family 0x10
	 * has some extra bits for addressing the second dram controller
	 * indepdendently, and the channel and chip-select selection fields
	 * for reading EccErrCnt are one bit wider.  The EccErrCnt fields
	 * are identical in size and position, and the extra bit for the
	 * selection is not used on DDR products so the selection fields
	 * effectively overlap, too.  So we'll modify this register as
	 * if it is family 0xf rev F - family 0x10 is similar enough, and
	 * earlier family 0xf models will just discard the writes.
	 *
	 * It would be nicer to expose the revision info of the chip we are
	 * injecting on up to mtst, but a whole lot more plumbing.
	 */
	if (tmask != 0) {
		union mcreg_sparectl olsp;

		/*
		 * Reset series of injection statements.
		 */
		misp = mis;

		/* Statement #1: read online spare register */
		mtst_mis_init_pci_rd(misp++, 0, MC_AMD_DEV_OFFSET +
		    opts.aao_chip, MC_FUNC_MISCCTL, MC_CTL_REG_SPARECTL,
		    MTST_MIS_ASZ_L, (uint32_t *)&olsp, 0);

		err = mtst_inject(mis, 1);

		if (err != 0) {
			mtst_cmd_warn("read of online spare reg failed\n");
			errno = err;
			return (MTST_CMD_ERR);
		}

		MCREG_FIELD_F_revFG(&olsp, EccErrCntWrEn) = 1;
		MCREG_FIELD_F_revFG(&olsp, EccErrCntDramChan) = opts.aao_chan;
		MCREG_FIELD_F_revFG(&olsp, EccErrCntDramCs) = opts.aao_cs;
		MCREG_FIELD_F_revFG(&olsp, EccErrCnt) = opts.aao_ecccnt;

		/*
		 * Reset series of injection statements.
		 */
		misp = mis;

		/* Statement #1: write online spare register */
		mtst_mis_init_pci_wr(misp++, 0, MC_AMD_DEV_OFFSET +
		    opts.aao_chip, MC_FUNC_MISCCTL, MC_CTL_REG_SPARECTL,
		    MTST_MIS_ASZ_L, MCREG_VAL32(&olsp), 0);

		if (tmask2 != 0) {
			union mcreg_sparectl olspd;

			MCREG_VAL32(&olspd) = MCREG_VAL32(&olsp);
			MCREG_FIELD_F_revFG(&olspd, EccErrCntDramChan) =
			    opts.aao_chan;
			MCREG_FIELD_F_revFG(&olspd, EccErrCntDramCs) =
			    opts.aao_cs2;
			MCREG_FIELD_F_revFG(&olspd, EccErrCnt) =
			    opts.aao_ecccnt2;

			/* Statement #2: write online spare reg a 2nd time */
			mtst_mis_init_pci_wr(misp++, 0,
			    MC_AMD_DEV_OFFSET + opts.aao_chip2,
			    MC_FUNC_MISCCTL, MC_CTL_REG_SPARECTL,
			    MTST_MIS_ASZ_L, MCREG_VAL32(&olspd), 0);
		}
		n = ((uintptr_t)misp - (uintptr_t)mis) /
		    sizeof (mtst_inj_stmt_t);
		err = mtst_inject(mis, n);

		if (err != 0) {
			mtst_cmd_warn("write(s) of online spare reg failed\n");
			errno = err;
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
	 * Reset series of injection statements.
	 */
	misp = mis;

	/*
	 * Build MCG_STATUS value.
	 */
	if ((cmdflags & MTST_CMD_F_INT18) || (cmdflags & (MTST_CMD_F_INT18 |
	    MTST_CMD_F_POLLED)) == 0 && (opts.aao_status & MSR_MC_STATUS_UC)) {
		/* If we will #MC on injection, set MCIP and EIPV and RIPV */
		mcg_status |= MCG_STATUS_MCIP | MCG_STATUS_EIPV;
		if (opts.aao_validmask & AAMD_VMASK_RIPV)
			mcg_status |= MCG_STATUS_RIPV;
	}

	/* Statement #1: MCi_STATUS */
	mtst_mis_init_msr_wr(misp++, cpi,
	    IA32_MSR_MC(opts.aao_bank, STATUS), opts.aao_status, msrflags);

	/* Statement #2: MCi_ADDR, but only if ADDRV */
	if (sp->mcistatus_addrv)
		mtst_mis_init_msr_wr(misp++, cpi,
		    IA32_MSR_MC(opts.aao_bank, ADDR), opts.aao_addr, msrflags);

	/* Statement #3: MCi_MISC, if specified */
	if (vmask & AAMD_VMASK_MISC)
		mtst_mis_init_msr_wr(misp++, cpi,
		    IA32_MSR_MC(opts.aao_bank, MISC), opts.aao_misc, msrflags);

	/* Statement #4: MCG_STATUS */
	mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS, mcg_status,
	    msrflags);

	/* Statement #5: raise an #MC or to poke the poller */
	switch (cmdflags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* #MC if uncorrected, otherwise wake poller */
		if (opts.aao_status & MSR_MC_STATUS_UC)
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
	err = mtst_inject(mis, n);

	if (id != -1)
		(void) mtst_mem_unreserve(id);

	if (err != 0)
		errno = err;

	return (err ? MTST_CMD_ERR : MTST_CMD_OK);
}

int
aamd_synthesize_cmn(mtst_cpuid_t *cpi, uint_t cmdflags,
    const mtst_argspec_t *args, int nargs, uint64_t initstatus)
{
	return (aamd_synth_impl(cpi, cmdflags, args, nargs, initstatus, -1));
}

int
aamd_synthesize_nb(mtst_cpuid_t *cpi, uint_t cmdflags,
    const mtst_argspec_t *args, int nargs, uint64_t initstatus)
{
	return (aamd_synth_impl(cpi, cmdflags, args, nargs, initstatus,
	    AMD_MCA_BANK_NB));
}
