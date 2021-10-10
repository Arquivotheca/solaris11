/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <strings.h>
#include <unistd.h>
#include <mtst_cpumod_api.h>
#include <sys/trap.h>
#include <sys/mca_x86.h>

#define	T_CMCI	T_ENOEXTFLT

uint32_t northbridge_did;

/*
 * For register desriptions see Intel Chipset Memory Controller Hub Datasheet
 * for 5000 series, 5100, 5400 and 7300
 *
 * For machine check see Intel 64 and IA-32 Architecture Software Developer's
 * Manuals Volume 3A and 3B
 */

static void
icpu_fini(void)
{
}

static int
is_7300()
{
	uint32_t did;
	mtst_inj_stmt_t mis;
	int err;

	if (northbridge_did) {
		did = northbridge_did;
	} else {
		mtst_mis_init_pci_rd(&mis, 0, 0, 0, 0, MTST_MIS_ASZ_L, &did,
		    0);
		err = mtst_inject(&mis, 1);
		if (err == 0) {
			northbridge_did = did;
		} else {
			mtst_cmd_warn("read of did register failed\n");
			return (0);
		}
	}
	if (did == 0x36008086)
		return (1);
	else
		return (0);
}

static int
is_5400()
{
	uint32_t did;
	mtst_inj_stmt_t mis;
	int err;

	if (northbridge_did) {
		did = northbridge_did;
	} else {
		mtst_mis_init_pci_rd(&mis, 0, 0, 0, 0, MTST_MIS_ASZ_L, &did,
		    0);
		err = mtst_inject(&mis, 1);
		if (err == 0) {
			northbridge_did = did;
		} else {
			mtst_cmd_warn("read of did register failed\n");
			return (0);
		}
	}
	if (did == 0x40008086 || did == 0x40018086 || did == 0x40038086)
		return (1);
	else
		return (0);
}

static int
is_5100()
{
	uint32_t did;
	mtst_inj_stmt_t mis;
	int err;

	if (northbridge_did) {
		did = northbridge_did;
	} else {
		mtst_mis_init_pci_rd(&mis, 0, 0, 0, 0, MTST_MIS_ASZ_L, &did,
		    0);
		err = mtst_inject(&mis, 1);
		if (err == 0) {
			northbridge_did = did;
		} else {
			mtst_cmd_warn("read of did register failed\n");
			return (0);
		}
	}
	if (did == 0x65c08086)
		return (1);
	else
		return (0);
}

static int
is_NehalemEP()
{
	uint32_t did = 0;
	mtst_inj_stmt_t mis;
	int err;
	uint_t bus;

	if (northbridge_did) {
		did = northbridge_did;
	} else {
		for (bus = 0xff; bus > 0; --bus) {
			mtst_mis_init_pci_rd(&mis, bus, 0, 0, 0,
			    MTST_MIS_ASZ_L, &did, 0);
			err = mtst_inject(&mis, 1);
			if ((err == 0) && (did != 0xffffffff)) {
				northbridge_did = did;
				break;
			}
		}
		if (did == 0) {
			mtst_cmd_warn("read of did register failed\n");
			return (0);
		}
	}
	if ((did == 0x2c408086) || (did == 0x2c588086) || (did == 0x2c708086))
		return (1);
	else
		return (0);
}

static int
is_NehalemEX()
{
	uint32_t did = 0;
	mtst_inj_stmt_t mis;
	int err;
	uint_t bus;

	if (northbridge_did) {
		did = northbridge_did;
	} else {
		for (bus = 0xff; bus > 0; --bus) {
			mtst_mis_init_pci_rd(&mis, bus, 0, 0, 0,
			    MTST_MIS_ASZ_L, &did, 0);
			err = mtst_inject(&mis, 1);
			if ((err == 0) && (did != 0xffffffff)) {
				northbridge_did = did;
				break;
			}
		}
		if (did == 0) {
			mtst_cmd_warn("read of did register failed\n");
			return (0);
		}
	}
	if (did == 0x2b008086)
		return (1);
	else
		return (0);
}

/*ARGSUSED*/
int
intel_cpu_unclassified(mtst_cpuid_t *cpi, uint_t flags,
    const mtst_argspec_t *args, int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 1;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int fatal = 0;
	int i;
	int err;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	if (fatal) {
		/* mis[1] */
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		/* mis[1] */
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_mrpe(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 2;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		/* mis[1] */
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_ext(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	uint_t addrmsr = IA32_MSR_MC(3, ADDR);
	uint_t miscmsr = IA32_MSR_MC(3, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[3] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_frc(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 4;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_it(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x400;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	uint_t addrmsr = IA32_MSR_MC(3, ADDR);
	uint_t miscmsr = IA32_MSR_MC(3, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[3] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_iunc(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x401;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "bits") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0x3ff;
			status |= arg->mas_argval & 0x3ff;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_gmh(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0xc;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "f") == 0) {
			status |= 0x1000;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "l0") == 0) {
			status &= ~0x3;
		} else if (strcmp(arg->mas_argnm, "l1") == 0) {
			status &= ~0x3;
			status |= 1;
		} else if (strcmp(arg->mas_argnm, "l2") == 0) {
			status &= ~0x3;
			status |= 2;
		} else if (strcmp(arg->mas_argnm, "lg") == 0) {
			status |= 3;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_tlb(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x10;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "f") == 0) {
			status |= 0x1000;
		} else if (strcmp(arg->mas_argnm, "tt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "instruction") == 0) {
			status &= ~(0x3 << 2);
		} else if (strcmp(arg->mas_argnm, "data") == 0) {
			status &= ~(0x3 << 2);
			status |= 1 << 2;
		} else if (strcmp(arg->mas_argnm, "generic") == 0) {
			status &= ~(0x3 << 2);
			status |= 2 << 2;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "l0") == 0) {
			status &= ~0x3;
		} else if (strcmp(arg->mas_argnm, "l1") == 0) {
			status &= ~0x3;
			status |= 1;
		} else if (strcmp(arg->mas_argnm, "l2") == 0) {
			status &= ~0x3;
			status |= 2;
		} else if (strcmp(arg->mas_argnm, "lg") == 0) {
			status |= 3;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_mh(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[3], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x100;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "f") == 0) {
			status |= 0x1000;
		} else if (strcmp(arg->mas_argnm, "tt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "instruction") == 0) {
			status &= ~(0x3 << 2);
		} else if (strcmp(arg->mas_argnm, "data") == 0) {
			status &= ~(0x3 << 2);
			status |= 1 << 2;
		} else if (strcmp(arg->mas_argnm, "generic") == 0) {
			status &= ~(0x3 << 2);
			status |= 2 << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "err") == 0) {
			status &= ~(0xf << 4);
		} else if (strcmp(arg->mas_argnm, "rd") == 0) {
			status &= ~(0xf << 4);
			status |= 1 << 4;
		} else if (strcmp(arg->mas_argnm, "wr") == 0) {
			status &= ~(0xf << 4);
			status |= 2 << 4;
		} else if (strcmp(arg->mas_argnm, "drd") == 0) {
			status &= ~(0xf << 4);
			status |= 3 << 4;
		} else if (strcmp(arg->mas_argnm, "dwr") == 0) {
			status &= ~(0xf << 4);
			status |= 4 << 4;
		} else if (strcmp(arg->mas_argnm, "ird") == 0) {
			status &= ~(0x5 << 4);
			status |= 5 << 4;
		} else if (strcmp(arg->mas_argnm, "prefetch") == 0) {
			status &= ~(0xf << 4);
			status |= 6 << 4;
		} else if (strcmp(arg->mas_argnm, "evict") == 0) {
			status &= ~(0xf << 4);
			status |= 7 << 4;
		} else if (strcmp(arg->mas_argnm, "snoop") == 0) {
			status &= ~(0xf << 4);
			status |= 8 << 4;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "l0") == 0) {
			status &= ~0x3;
		} else if (strcmp(arg->mas_argnm, "l1") == 0) {
			status &= ~0x3;
			status |= 1;
		} else if (strcmp(arg->mas_argnm, "l2") == 0) {
			status &= ~0x3;
			status |= 2;
		} else if (strcmp(arg->mas_argnm, "lg") == 0) {
			status |= 3;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_cpu_bus(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x800;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	uint_t addrmsr = IA32_MSR_MC(3, ADDR);
	uint_t miscmsr = IA32_MSR_MC(3, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;
	int allcpu = 0;

	isNehalemEX = is_NehalemEX();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "f") == 0) {
			status |= 0x1000;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "m") == 0 ||
		    strcmp(arg->mas_argnm, "mem") == 0) {
			status &= ~(0x3 << 2);
		} else if (strcmp(arg->mas_argnm, "io") == 0) {
			status &= ~(0x3 << 2);
			status |= 2 << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "err") == 0) {
			status &= ~(0xf << 4);
		} else if (strcmp(arg->mas_argnm, "rd") == 0) {
			status &= ~(0xf << 4);
			status |= 1 << 4;
		} else if (strcmp(arg->mas_argnm, "wr") == 0) {
			status &= ~(0xf << 4);
			status |= 2 << 4;
		} else if (strcmp(arg->mas_argnm, "drd") == 0) {
			status &= ~(0xf << 4);
			status |= 3 << 4;
		} else if (strcmp(arg->mas_argnm, "dwr") == 0) {
			status &= ~(0xf << 4);
			status |= 4 << 4;
		} else if (strcmp(arg->mas_argnm, "ird") == 0) {
			status &= ~(0x5 << 4);
			status |= 5 << 4;
		} else if (strcmp(arg->mas_argnm, "prefetch") == 0) {
			status &= ~(0xf << 4);
			status |= 6 << 4;
		} else if (strcmp(arg->mas_argnm, "evict") == 0) {
			status &= ~(0xf << 4);
			status |= 7 << 4;
		} else if (strcmp(arg->mas_argnm, "snoop") == 0) {
			status &= ~(0xf << 4);
			status |= 8 << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			status |= 0x100;
		} else if (strcmp(arg->mas_argnm, "notimeout") == 0) {
			status &= ~0x100;
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0x3) << 9;
		} else if (strcmp(arg->mas_argnm, "src") == 0) {
			status &= ~(0x3 << 9);
		} else if (strcmp(arg->mas_argnm, "res") == 0) {
			status &= ~(0x3 << 9);
			status |= 1 << 9;
		} else if (strcmp(arg->mas_argnm, "obs") == 0) {
			status &= ~(0x3 << 9);
			status |= 2 << 9;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "l0") == 0) {
			status &= ~0x3;
		} else if (strcmp(arg->mas_argnm, "l1") == 0) {
			status &= ~0x3;
			status |= 1;
		} else if (strcmp(arg->mas_argnm, "l2") == 0) {
			status &= ~0x3;
			status |= 2;
		} else if (strcmp(arg->mas_argnm, "lg") == 0) {
			status |= 3;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "allcpu") == 0) {
			allcpu = MTST_MIS_FLAG_ALLCPU;
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
	/* mis[1] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
	/* mis[2] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
	/* mis[3] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE | allcpu);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}


/*ARGSUSED*/
int
intel_cmci(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[4], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0xc;
	uint64_t old_status;
	uint64_t misc2;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	uint_t addrmsr = IA32_MSR_MC(3, ADDR);
	uint_t miscmsr = IA32_MSR_MC(3, MISC);
	uint_t misc2msr = IA32_MSR_MC_CTL2(3);
	int i;
	int err;
	uint_t count = 1;
	uint_t old_count;
	int isNehalemEP;
	int isNehalemEX;

	isNehalemEP = is_NehalemEP();
	isNehalemEX = is_NehalemEX();

	if (!isNehalemEP && !isNehalemEX) {
		mtst_cmd_warn("No CMCI present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
			misc2msr = IA32_MSR_MC_CTL2(arg->mas_argval);
		} else if (strcmp(arg->mas_argnm, "count") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			count = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "tbes") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= MSR_MC_STATUS_TBES_MASK;
			status |= (arg->mas_argval <<
			    MSR_MC_STATUS_TBES_SHIFT) & MSR_MC_STATUS_TBES_MASK;
		} else if (strcmp(arg->mas_argnm, "code") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= MSR_MC_STATUS_MSERR_MASK |
			    MSR_MC_STATUS_MCAERR_MASK;
			status |= arg->mas_argval & (MSR_MC_STATUS_MSERR_MASK |
			    MSR_MC_STATUS_MCAERR_MASK);
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	mtst_mis_init_msr_rd(&mis[0], cpi, statmsr, &old_status,
	    MTST_MIS_FLAG_MSR_INTERPOSEOK);
	err = mtst_inject(mis, 1);
	if (err != 0) {
		old_status = 0;
	}
	mtst_mis_init_msr_rd(&mis[0], cpi, misc2msr, &misc2,
	    MTST_MIS_FLAG_MSR_INTERPOSEOK);
	err = mtst_inject(mis, 1);
	if (err != 0) {
		misc2 = 0;
	}
	old_count = (old_status >> MSR_MC_STATUS_CEC_SHIFT) &
	    MSR_MC_CTL2_THRESHOLD_MASK;
	if ((old_status & MSR_MC_STATUS_VAL) != 0) {
		status = old_status;
		if ((old_status & MSR_MC_STATUS_UC) != 0)
			status |= MSR_MC_STATUS_OVER;
	}
	count += old_count;
	count |= old_count & MSR_MC_CTL2_THRESHOLD_OVERFLOW;
	count &= MSR_MC_CTL2_THRESHOLD_MASK;
	status = (status & ~(MSR_MC_STATUS_CEC_MASK)) |
	    ((uint64_t)count << MSR_MC_STATUS_CEC_SHIFT);
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if ((status & MSR_MC_STATUS_ADDRV) != 0 &&
	    (old_status & MSR_MC_STATUS_ADDRV) == 0) {
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[2] */
	if ((status & MSR_MC_STATUS_MISCV) != 0 &&
	    (old_status & MSR_MC_STATUS_MISCV) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[3] */
	if ((misc2 & MSR_MC_CTL2_EN) != 0 &&
	    count == (misc2 & MSR_MC_CTL2_THRESHOLD_MASK)) {
		mtst_mis_init_int(misp++, cpi, T_CMCI,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
intel_memory(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | MSR_MC_STATUS_MISCV | 0x80;
	uint64_t old_status;
	uint64_t misc2;
	uint64_t addr;
	uint64_t misc = 0;
	uint_t statmsr = IA32_MSR_MC(8, STATUS);
	uint_t addrmsr = IA32_MSR_MC(8, ADDR);
	uint_t miscmsr = IA32_MSR_MC(8, MISC);
	uint_t misc2msr = IA32_MSR_MC_CTL2(8);
	int i;
	int err;
	int fatal = 0;
	uint_t cmci = 0;
	uint_t old_count;
	int isNehalemEP;

	isNehalemEP = is_NehalemEP();

	if (!isNehalemEP) {
		mtst_cmd_warn("No EP integrated memory controller\n");
		return (0);
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			status |= (arg->mas_argval & 0xffff) << 16;
		} else if (strcmp(arg->mas_argnm, "ecc_read") == 0) {
			status |= 1 << 16;
		} else if (strcmp(arg->mas_argnm, "ecc_scrub") == 0) {
			status |= 1 << 17;
		} else if (strcmp(arg->mas_argnm, "parity") == 0) {
			status |= 1 << 18;
		} else if (strcmp(arg->mas_argnm, "redundant") == 0) {
			status |= 1 << 19;
		} else if (strcmp(arg->mas_argnm, "spare") == 0) {
			status |= 1 << 20;
		} else if (strcmp(arg->mas_argnm, "out_of_range") == 0) {
			status |= 1 << 21;
		} else if (strcmp(arg->mas_argnm, "invalid_id") == 0) {
			status |= 1 << 22;
		} else if (strcmp(arg->mas_argnm, "address_parity") == 0) {
			status |= 1 << 23;
		} else if (strcmp(arg->mas_argnm, "byte_parity") == 0) {
			status |= 1 << 24;
		} else if (strcmp(arg->mas_argnm, "cmci") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			cmci = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "read") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0x70;
			status |= 0x10;
		} else if (strcmp(arg->mas_argnm, "write") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0x70;
			status |= 0x20;
		} else if (strcmp(arg->mas_argnm, "command") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0x70;
			status |= 0x30;
		} else if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0xf;
			status |= arg->mas_argval & 0xf;
			misc &= ~(3 << 18);
			misc |= (arg->mas_argval & 3) << 18;
		} else if (strcmp(arg->mas_argnm, "dimm") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc &= ~(3 << 16);
			misc |= (arg->mas_argval & 3) << 16;
		} else if (strcmp(arg->mas_argnm, "rtid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc &= ~0xff;
			misc |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "syndrome") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc &= ~(0xffffffffULL << 32);
			misc |= (arg->mas_argval & 0xffffffffULL) << 32;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		}
	}
	if (cmci && !(status & MSR_MC_STATUS_UC)) {
		mtst_mis_init_msr_rd(&mis[0], cpi, statmsr, &old_status,
		    MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 1);
		if (err != 0) {
			old_status = 0;
		}
		mtst_mis_init_msr_rd(&mis[0], cpi, misc2msr, &misc2,
		    MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 1);
		if (err != 0) {
			misc2 = 0;
		}
		old_count = (old_status >> MSR_MC_STATUS_CEC_SHIFT) &
		    MSR_MC_CTL2_THRESHOLD_MASK;
		if ((old_status & MSR_MC_STATUS_VAL) != 0) {
			status = old_status;
			if ((old_status & MSR_MC_STATUS_UC) != 0)
				status |= MSR_MC_STATUS_OVER;
		}
		cmci += old_count;
		cmci |= old_count & MSR_MC_CTL2_THRESHOLD_OVERFLOW;
		cmci &= MSR_MC_CTL2_THRESHOLD_MASK;
		status = (status & ~(MSR_MC_STATUS_CEC_MASK)) |
		    ((uint64_t)cmci << MSR_MC_STATUS_CEC_SHIFT);

		/* write MCi address */
		if ((status & MSR_MC_STATUS_ADDRV) != 0 &&
		    (old_status & MSR_MC_STATUS_ADDRV) == 0) {
			mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}

		/* write MCi misc */
		if ((status & MSR_MC_STATUS_MISCV) != 0 &&
		    (old_status & MSR_MC_STATUS_MISCV) != 0) {
			mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}

		/* write MCi status */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write interrupt */
		if ((misc2 & MSR_MC_CTL2_EN) != 0 &&
		    cmci == (misc2 & MSR_MC_CTL2_THRESHOLD_MASK)) {
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
		    sizeof (mtst_inj_stmt_t));
	} else {
		/* write addr */
		if (status & MSR_MC_STATUS_ADDRV)
			mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write misc */
		if (status & MSR_MC_STATUS_MISCV)
			mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write fatal flag */
		if (fatal) {
			mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
			    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
			    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* write status */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
		case 0:
			/* mis[4] #MC if uncorrected, otherwise wake poller */
			if (status & MSR_MC_STATUS_UC)
				mtst_mis_init_int(misp++, cpi, T_MCE,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			else
				mtst_mis_init_int(misp++, cpi, T_CMCI,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;

		case MTST_CMD_F_POLLED:
			/* Leave the poller to find this at it's next wakeup */
			break;

		case MTST_CMD_F_INT18:
			/* mis[4] Arrange a #MC exception */
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;
		}

		err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
		    sizeof (mtst_inj_stmt_t));
	}
	return (err);
}

/*ARGSUSED*/
int
intel_memory_ex(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[11], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | MSR_MC_STATUS_MISCV | 0x100LL;
	uint64_t old_status;
	uint64_t misc2;
	uint64_t addr;
	uint64_t misc = 0;
	uint_t device = 0x05;
	uint_t statmsr = IA32_MSR_MC(8, STATUS);
	uint_t addrmsr = IA32_MSR_MC(8, ADDR);
	uint_t miscmsr = IA32_MSR_MC(8, MISC);
	uint_t misc2msr = IA32_MSR_MC_CTL2(8);
	uint_t log_r0 = 0;
	uint_t log_r1 = 0;
	uint_t log_r2 = 0;
	uint_t log_r3 = 0;
	uint64_t mem_err_log_addr = 0;
	uint_t templ;
	int i;
	int err;
	int fatal = 0;
	uint_t cmci = 0;
	uint_t old_count;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	if (!isNehalemEX) {
		mtst_cmd_warn("No EX integrated memory controller\n");
		return (0);
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
			if (isNehalemEX)
				device = 0x05 + (arg->mas_argval % 2) * 2;
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status |= (arg->mas_argval & 0xffff) << 16;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status |= MSR_MC_STATUS_ADDRV;
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "cmci") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			cmci = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status &= ~0x03;
			status |= (arg->mas_argval & 0x03);
		} else if (strcmp(arg->mas_argnm, "tt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status &= ~0x0c;
			status |= (arg->mas_argval & 0x03)<< 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status &= ~0xf0;
			status |= (arg->mas_argval & 0x0f) << 4;
		} else if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			status &= ~(MSR_MC_STATUS_MISCV | 0x1ff);
			status |= MSR_MC_STATUS_EN | 0x80 |
			    (arg->mas_argval & 0xf);
		} else if (strcmp(arg->mas_argnm, "lnktrns") == 0) {
			status |= 1LL << 16;
		} else if (strcmp(arg->mas_argnm, "lnkpers") == 0) {
			status |= 1LL << 17;
		} else if (strcmp(arg->mas_argnm, "lnkuncorr") == 0) {
			status |= 1LL << 18;
		} else if (strcmp(arg->mas_argnm, "sbfbdlnkerr") == 0) {
			status |= 1LL << 19;
		} else if (strcmp(arg->mas_argnm, "nbfbdlnkerr") == 0) {
			status |= 1LL << 20;
		} else if (strcmp(arg->mas_argnm, "lnkcrcvld") == 0) {
			status |= 1LL << 21;
		} else if (strcmp(arg->mas_argnm, "ptrl_fsm_err") == 0) {
			status |= 1LL << 22;
		} else if (strcmp(arg->mas_argnm, "errflw_fsm_fail") == 0) {
			status |= 1LL << 23;
		} else if (strcmp(arg->mas_argnm, "mcpar") == 0) {
			status |= 1LL << 24;
		} else if (strcmp(arg->mas_argnm, "vberr") == 0) {
			status |= 1LL << 25;
		} else if (strcmp(arg->mas_argnm, "fberr") == 0) {
			status |= 1LL << 26;
		} else if (strcmp(arg->mas_argnm, "memeccerr") == 0) {
			status |= 1LL << 27;
		} else if (strcmp(arg->mas_argnm, "fbdfrmpar") == 0) {
			status |= 1LL << 28;
		} else if (strcmp(arg->mas_argnm, "failover_to_mirror") == 0) {
			status |= 1LL << 29;
		} else if (strcmp(arg->mas_argnm, "poison") == 0) {
			status |= 1LL << 31;
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			status |= 1LL << 55;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			status |= 1LL << 56;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= 1LL << 57;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= 1LL << 61;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= 1LL << 62;
		} else if (strcmp(arg->mas_argnm, "mem_scrub") == 0) {
			status &= ~0xffffLL;
			status |= 0x00cfLL;
		} else if (strcmp(arg->mas_argnm, "ep_syndrome") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			misc &= ~0xffffffffLL;
			misc |= (arg->mas_argval & 0xffffffffLL);
		} else if (strcmp(arg->mas_argnm, "crc_syndrome") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			misc &= ~0xffff00000000LL;
			misc |= (arg->mas_argval & 0xffff00000000LL);
		} else if (strcmp(arg->mas_argnm, "rnk_0_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			log_r0 = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "rnk_1_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			log_r1 = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "rnk_2_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			log_r2 = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "rnk_3_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			log_r3 = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "mem_err_log_addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("option '%s' missing value\n",
				    arg->mas_argnm);
				return (0);
			}
			mem_err_log_addr = arg->mas_argval;
		}
	}
	if (cmci && !(status & MSR_MC_STATUS_UC)) {
		mtst_mis_init_msr_rd(&mis[0], cpi, statmsr, &old_status,
		    MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 1);
		if (err != 0) {
			old_status = 0;
		}
		mtst_mis_init_msr_rd(&mis[0], cpi, misc2msr, &misc2,
		    MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 1);
		if (err != 0) {
			misc2 = 0;
		}
		old_count = (old_status >> MSR_MC_STATUS_CEC_SHIFT) &
		    MSR_MC_CTL2_THRESHOLD_MASK;
		if ((old_status & MSR_MC_STATUS_VAL) != 0) {
			status = old_status;
			if ((old_status & MSR_MC_STATUS_UC) != 0)
				status |= MSR_MC_STATUS_OVER;
		}
		cmci += old_count;
		cmci |= old_count & MSR_MC_CTL2_THRESHOLD_OVERFLOW;
		cmci &= MSR_MC_CTL2_THRESHOLD_MASK;
		status = (status & ~(MSR_MC_STATUS_CEC_MASK)) |
		    ((uint64_t)cmci << MSR_MC_STATUS_CEC_SHIFT);

		/* write MCi address */
		if ((status & MSR_MC_STATUS_ADDRV) != 0 &&
		    (old_status & MSR_MC_STATUS_ADDRV) == 0) {
			mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}

		/* write MCi misc */
		if ((status & MSR_MC_STATUS_MISCV) != 0 &&
		    (old_status & MSR_MC_STATUS_MISCV) != 0) {
			mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}

		/* write log 0 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0x94,
		    MTST_MIS_ASZ_L, log_r0,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 1 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0x98,
		    MTST_MIS_ASZ_L, log_r1,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 2 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0x9c,
		    MTST_MIS_ASZ_L, log_r2,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 3 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0xa0,
		    MTST_MIS_ASZ_L, log_r3,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write mem_err_log_addr */
		templ = mem_err_log_addr & 0xffffffff;
		mtst_mis_init_pci_wr(misp++, 0, device, 0x02, 0x50,
		    MTST_MIS_ASZ_L, templ,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		templ = (mem_err_log_addr >> 32) & 0x3;
		mtst_mis_init_pci_wr(misp++, 0, device, 0x02, 0x54,
		    MTST_MIS_ASZ_L, templ,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write MCi status */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write interrupt */
		if ((misc2 & MSR_MC_CTL2_EN) != 0 &&
		    cmci == (misc2 & MSR_MC_CTL2_THRESHOLD_MASK)) {
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
		    sizeof (mtst_inj_stmt_t));
	} else {
		/* write addr */
		if (status & MSR_MC_STATUS_ADDRV)
			mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write misc */
		if (status & MSR_MC_STATUS_MISCV)
			mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 0 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0x94,
		    MTST_MIS_ASZ_L, log_r0,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 1 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0x98,
		    MTST_MIS_ASZ_L, log_r1,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 2 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0x9c,
		    MTST_MIS_ASZ_L, log_r2,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* write log 3 */
		mtst_mis_init_pci_wr(misp++, 0, device, 0x04, 0xa0,
		    MTST_MIS_ASZ_L, log_r3,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write mem_err_log_addr */
		templ = mem_err_log_addr & 0xffffffff;
		mtst_mis_init_pci_wr(misp++, 0, device, 0x02, 0x50,
		    MTST_MIS_ASZ_L, templ,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		templ = (mem_err_log_addr >> 32) & 0x3;
		mtst_mis_init_pci_wr(misp++, 0, device, 0x02, 0x54,
		    MTST_MIS_ASZ_L, templ,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write status */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);

		/* write fatal flag */
		if (fatal) {
			mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
			    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
			    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
		}

		switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
		case 0:
			/* mis[4] #MC if uncorrected, otherwise wake poller */
			if (status & MSR_MC_STATUS_UC)
				mtst_mis_init_int(misp++, cpi, T_MCE,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			else
				mtst_mis_init_int(misp++, cpi, T_CMCI,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;

		case MTST_CMD_F_POLLED:
			/* Leave the poller to find this at it's next wakeup */
			break;

		case MTST_CMD_F_INT18:
			/* mis[4] Arrange a #MC exception */
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;
		}

		err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
		    sizeof (mtst_inj_stmt_t));
	}
	return (err);
}

/*ARGSUSED*/
int
intel_quickpath(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x800;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(0, STATUS);
	uint_t addrmsr = IA32_MSR_MC(0, ADDR);
	uint_t miscmsr = IA32_MSR_MC(0, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEP;
	int isNehalemEX;

	isNehalemEP = is_NehalemEP();
	isNehalemEX = is_NehalemEX();

	if (!isNehalemEP && !isNehalemEX) {
		mtst_cmd_warn("No Quickpath present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			status |= (arg->mas_argval & 0x7ff) << 16;
		} else if (strcmp(arg->mas_argnm, "header_parity") == 0) {
			status |= 1 << 16;
		} else if (strcmp(arg->mas_argnm, "data_parity") == 0) {
			status |= 1 << 17;
		} else if (strcmp(arg->mas_argnm, "retries_exceeded") == 0) {
			status |= 1 << 18;
		} else if (strcmp(arg->mas_argnm, "poison") == 0) {
			status |= 1 << 19;
		} else if (strcmp(arg->mas_argnm, "unsupported_msg") == 0) {
			status |= 1 << 22;
		} else if (strcmp(arg->mas_argnm, "unsupported_credit") == 0) {
			status |= 1 << 23;
		} else if (strcmp(arg->mas_argnm, "buffer_overrun") == 0) {
			status |= 1 << 24;
		} else if (strcmp(arg->mas_argnm, "response_status") == 0) {
			status |= 1 << 25;
		} else if (strcmp(arg->mas_argnm, "clock_jitter") == 0) {
			status |= 1 << 26;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "f") == 0) {
			status |= 0x1000;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "m") == 0 ||
		    strcmp(arg->mas_argnm, "mem") == 0) {
			status &= ~(0x3 << 2);
		} else if (strcmp(arg->mas_argnm, "io") == 0) {
			status &= ~(0x3 << 2);
			status |= 2 << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "err") == 0) {
			status &= ~(0xf << 4);
		} else if (strcmp(arg->mas_argnm, "rd") == 0) {
			status &= ~(0xf << 4);
			status |= 1 << 4;
		} else if (strcmp(arg->mas_argnm, "wr") == 0) {
			status &= ~(0xf << 4);
			status |= 2 << 4;
		} else if (strcmp(arg->mas_argnm, "drd") == 0) {
			status &= ~(0xf << 4);
			status |= 3 << 4;
		} else if (strcmp(arg->mas_argnm, "dwr") == 0) {
			status &= ~(0xf << 4);
			status |= 4 << 4;
		} else if (strcmp(arg->mas_argnm, "ird") == 0) {
			status &= ~(0x5 << 4);
			status |= 5 << 4;
		} else if (strcmp(arg->mas_argnm, "prefetch") == 0) {
			status &= ~(0xf << 4);
			status |= 6 << 4;
		} else if (strcmp(arg->mas_argnm, "evict") == 0) {
			status &= ~(0xf << 4);
			status |= 7 << 4;
		} else if (strcmp(arg->mas_argnm, "snoop") == 0) {
			status &= ~(0xf << 4);
			status |= 8 << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			status |= 0x100;
		} else if (strcmp(arg->mas_argnm, "notimeout") == 0) {
			status &= ~0x100;
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= (arg->mas_argval & 0x3) << 9;
		} else if (strcmp(arg->mas_argnm, "src") == 0) {
			status &= ~(0x3 << 9);
		} else if (strcmp(arg->mas_argnm, "res") == 0) {
			status &= ~(0x3 << 9);
			status |= 1 << 9;
		} else if (strcmp(arg->mas_argnm, "obs") == 0) {
			status &= ~(0x3 << 9);
			status |= 2 << 9;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "l0") == 0) {
			status &= ~0x3;
		} else if (strcmp(arg->mas_argnm, "l1") == 0) {
			status &= ~0x3;
			status |= 1;
		} else if (strcmp(arg->mas_argnm, "l2") == 0) {
			status &= ~0x3;
			status |= 2;
		} else if (strcmp(arg->mas_argnm, "lg") == 0) {
			status |= 3;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			if (isNehalemEX)
				status |= 1LL << 56;
			else {
				mtst_cmd_warn("s bit not available\n");
				return (0);
			}
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			if (isNehalemEX)
				status |= 1LL << 55;
			else {
				mtst_cmd_warn("ar bit not available\n");
				return (0);
			}
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[3] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}


/*
 * Handle error injector for NehalemEX rbox
 */
/*ARGSUSED*/
int
intel_qprouter(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0xf0f;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(0, STATUS);
	uint_t addrmsr = IA32_MSR_MC(0, ADDR);
	uint_t miscmsr = IA32_MSR_MC(0, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	if (!isNehalemEX) {
		mtst_cmd_warn("No rbox present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for bank");
				return (0);
			}
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for addr");
				return (0);
			}
			status |= MSR_MC_STATUS_ADDRV;
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for misc");
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ll");
				return (0);
			}
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ii");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for rrrr");
				return (0);
			}
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			if (arg->mas_argtype == MTST_ARGTYPE_VALUE) {
				status &= ~0x100;
				status |= (arg->mas_argval & 0x1) << 8;
			} else {
				status |= 0x100;
			}
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for pp");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 9;
		} else if (strcmp(arg->mas_argnm, "poison") == 0) {
			status |= 1LL << 31;
		} else if (strcmp(arg->mas_argnm, "corr_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for corr_err_cnt");
				return (0);
			}
			status |= (arg->mas_argval & 0x7fff) << 38;
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			status |= 1LL << 55;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			status |= 1LL << 56;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for error");
				return (0);
			}
			status |= (arg->mas_argval & 0x7ff) << 16;
		} else if (strcmp(arg->mas_argnm, "opr_ecc") == 0) {
			status |= 1LL << 16;
		} else if (strcmp(arg->mas_argnm, "ipr_crc") == 0) {
			status |= 1LL << 17;
		} else if (strcmp(arg->mas_argnm, "opr_retry_abort") == 0) {
			status |= 1LL << 18;
		} else if (strcmp(arg->mas_argnm, "ipr_link_init") == 0) {
			status |= 1LL << 19;
		} else if (strcmp(arg->mas_argnm, "opr_poison") == 0) {
			status |= 1LL << 20;
		} else if (strcmp(arg->mas_argnm, "eot_parity") == 0) {
			status |= 1LL << 21;
		} else if (strcmp(arg->mas_argnm, "rta_parity") == 0) {
			status |= 1LL << 22;
		} else if (strcmp(arg->mas_argnm, "ipr_bad_route") == 0) {
			status |= 1LL << 23;
		} else if (strcmp(arg->mas_argnm, "ipr_bad_msg") == 0) {
			status |= 1LL << 24;
		} else if (strcmp(arg->mas_argnm, "ipr_bad_credit") == 0) {
			status |= 1LL << 25;
		} else if (strcmp(arg->mas_argnm, "opr_hdr_ecc") == 0) {
			status |= 1LL << 26;
		} else if (strcmp(arg->mas_argnm, "opr_link_retry") == 0) {
			status |= 1LL << 27;
		} else if (strcmp(arg->mas_argnm, "qpi_port") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for qpi_port");
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval & 0xf;
		}
	}

	/* mis[0] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[3] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*
 * Handle error injector for NehalemEX sbox
 */
/*ARGSUSED*/
int
intel_bicache(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[6], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x800;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(12, STATUS);
	uint_t addrmsr = IA32_MSR_MC(12, ADDR);
	uint_t miscmsr = IA32_MSR_MC(12, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;
	uint32_t summary = 0;
	uint_t sbox_dev = 0;

	isNehalemEX = is_NehalemEX();

	if (!isNehalemEX) {
		mtst_cmd_warn("No sbox present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for bank");
				return (0);
			}
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
			sbox_dev = (arg->mas_argval & 0x1) | 0x02;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for addr");
				return (0);
			}
			status |= MSR_MC_STATUS_ADDRV;
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for misc");
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ll");
				return (0);
			}
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ii");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for rrrr");
				return (0);
			}
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			status |= 0x100;
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for pp");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 9;
		} else if (strcmp(arg->mas_argnm, "err_src_summary") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn("Missing argument value for "
				    "err_src_summary");
				return (0);
			}
			summary |= arg->mas_argval & 0x000d0005;
		} else if (strcmp(arg->mas_argnm, "err_cbox_top") == 0) {
			summary |= 0x00000001;
		} else if (strcmp(arg->mas_argnm, "err_cbox_bot") == 0) {
			summary |= 0x00000004;
		} else if (strcmp(arg->mas_argnm, "err_bbox") == 0) {
			summary |= 0x00010000;
		} else if (strcmp(arg->mas_argnm, "err_sbox") == 0) {
			summary |= 0x00040000;
		} else if (strcmp(arg->mas_argnm, "err_rbox") == 0) {
			summary |= 0x00080000;
		} else if (strcmp(arg->mas_argnm, "mscod") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for mscod");
				return (0);
			}
			status |= (arg->mas_argval & 0x1f) << 16;
		} else if (strcmp(arg->mas_argnm, "poison") == 0) {
			status |= 1LL << 31;
		} else if (strcmp(arg->mas_argnm, "corr_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for corr_err_cnt");
				return (0);
			}
			status |= (arg->mas_argval & 0x7fff) << 38;
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			status |= 1LL << 55;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			status |= 1LL << 56;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for error");
				return (0);
			}
			status |= (arg->mas_argval & 0x7ff) << 16;
		}
	}

	/* mis[0] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[3] */
	mtst_mis_init_pci_wr(misp++, 0, sbox_dev, 0, 0x2ac, MTST_MIS_ASZ_L,
	    summary, MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[4] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[5] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[5] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}



/*
 * Handle error injector for NehalemEX bbox
 */
/*ARGSUSED*/
int
intel_homeagent(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x800;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(10, STATUS);
	uint_t addrmsr = IA32_MSR_MC(10, ADDR);
	uint_t miscmsr = IA32_MSR_MC(10, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	if (!isNehalemEX) {
		mtst_cmd_warn("No bbox present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for bank");
				return (0);
			}
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for addr");
				return (0);
			}
			status |= MSR_MC_STATUS_ADDRV;
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for misc");
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ll");
				return (0);
			}
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ii");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for rrrr");
				return (0);
			}
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			status |= 0x100;
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for pp");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 9;
		} else if (strcmp(arg->mas_argnm, "ha_cod") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ha_cod");
				return (0);
			}
			status |= (arg->mas_argval & 0xffff) << 16;
		} else if (strcmp(arg->mas_argnm, "corr_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for corr_err_cnt");
				return (0);
			}
			status |= (arg->mas_argval & 0x7fff) << 38;
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			status |= 1LL << 55;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			status |= 1LL << 56;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for error");
				return (0);
			}
			status |= (arg->mas_argval & 0x7ff) << 16;
		}
	}

	/* mis[0] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[3] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}


/*
 * Handle error injector for NehalemEX ubox
 */
/*ARGSUSED*/
int
intel_sysconf(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x800;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(6, STATUS);
	uint_t addrmsr = IA32_MSR_MC(6, ADDR);
	uint_t miscmsr = IA32_MSR_MC(6, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	if (!isNehalemEX) {
		mtst_cmd_warn("No ubox present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for bank");
				return (0);
			}
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for addr");
				return (0);
			}
			status |= MSR_MC_STATUS_ADDRV;
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for misc");
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ll");
				return (0);
			}
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ii");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for rrrr");
				return (0);
			}
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			status |= 0x100;
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for pp");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 9;

		} else if (strcmp(arg->mas_argnm, "cfa_err_fatal") == 0) {
			status |= 1LL << 16;
		} else if (strcmp(arg->mas_argnm, "err_pin1_fatal") == 0) {
			status |= 1LL << 17;
		} else if (strcmp(arg->mas_argnm, "credit_fatal") == 0) {
			status |= 1LL << 18;
		} else if (strcmp(arg->mas_argnm, "pbox_fatal") == 0) {
			status |= 1LL << 19;
		} else if (strcmp(arg->mas_argnm, "main_timeout_fatal")
		    == 0) {
			status |= 1LL << 20;
		} else if (strcmp(arg->mas_argnm, "ill_op_fatal") == 0) {
			status |= 1LL << 21;
		} else if (strcmp(arg->mas_argnm, "poison_fatal") == 0) {
			status |= 1LL << 22;
		} else if (strcmp(arg->mas_argnm, "err_pin0_unc") == 0) {
			status |= 1LL << 24;
		} else if (strcmp(arg->mas_argnm, "response_fail_unc") == 0) {
			status |= 1LL << 25;
		} else if (strcmp(arg->mas_argnm, "scratch_reg_parity_unc")
		    == 0) {
			status |= 1LL << 26;
		} else if (strcmp(arg->mas_argnm, "misalign_unc") == 0) {
			status |= 1LL << 27;
		} else if (strcmp(arg->mas_argnm, "rsvd_chk_unc") == 0) {
			status |= 1LL << 28;
		} else if (strcmp(arg->mas_argnm, "cfa_ecc_cor") == 0) {
			status |= 1LL << 29;
		} else if (strcmp(arg->mas_argnm, "corr_err_cnt") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for corr_err_cnt");
				return (0);
			}
			status |= (arg->mas_argval & 0x7fff) << 38;
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			status |= 1LL << 55;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			status |= 1LL << 56;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for error");
				return (0);
			}
			status |= (arg->mas_argval & 0x7ff) << 16;
		}
	}

	/* mis[0] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[3] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*
 * Handle error injector for NehalemEX cbox
 */
/*ARGSUSED*/
int
intel_llccoher(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL | 0x17a;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(14, STATUS);
	uint_t addrmsr = IA32_MSR_MC(14, ADDR);
	uint_t miscmsr = IA32_MSR_MC(14, MISC);
	int i;
	int err;
	int fatal = 0;
	int isNehalemEX;

	isNehalemEX = is_NehalemEX();

	if (!isNehalemEX) {
		mtst_cmd_warn("No cbox present\n");
		return (0);
	}


	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for bank");
				return (0);
			}
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for addr");
				return (0);
			}
			status |= MSR_MC_STATUS_ADDRV;
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for misc");
				return (0);
			}
			status |= MSR_MC_STATUS_MISCV;
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "ll") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ll");
				return (0);
			}
			status |= arg->mas_argval & 0x3;
		} else if (strcmp(arg->mas_argnm, "ii") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for ii");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 2;
		} else if (strcmp(arg->mas_argnm, "rrrr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for rrrr");
				return (0);
			}
			status |= (arg->mas_argval & 0xf) << 4;
		} else if (strcmp(arg->mas_argnm, "t") == 0 ||
		    strcmp(arg->mas_argnm, "timeout") == 0) {
			status |= 0x100;
		} else if (strcmp(arg->mas_argnm, "pp") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for pp");
				return (0);
			}
			status |= (arg->mas_argval & 0x3) << 9;

		} else if (strcmp(arg->mas_argnm, "poison") == 0) {
			status |= 1LL << 31;
		} else if (strcmp(arg->mas_argnm, "ar") == 0) {
			status |= 1LL << 55;
		} else if (strcmp(arg->mas_argnm, "s") == 0) {
			status |= 1LL << 56;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "error") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE) {
				mtst_cmd_warn(
				    "Missing argument value for error");
				return (0);
			}
			status |= (arg->mas_argval & 0x7ff) << 16;
		}
	}

	/* mis[0] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	/* mis[3] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}


/*ARGSUSED*/
int
intel_cpu_raw(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmd_arg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	uint64_t status = MSR_MC_STATUS_VAL;
	uint64_t addr, misc;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	uint_t addrmsr = IA32_MSR_MC(3, ADDR);
	uint_t miscmsr = IA32_MSR_MC(3, MISC);
	int i;
	int err;
	int fatal = 0;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			statmsr = IA32_MSR_MC(arg->mas_argval, STATUS);
			addrmsr = IA32_MSR_MC(arg->mas_argval, ADDR);
			miscmsr = IA32_MSR_MC(arg->mas_argval, MISC);
		} else if (strcmp(arg->mas_argnm, "status") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0xffff;
			status |= arg->mas_argval & 0xffff;
		} else if (strcmp(arg->mas_argnm, "mcacod") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~0xffff;
			status |= arg->mas_argval & 0xffff;
		} else if (strcmp(arg->mas_argnm, "mscod") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~(0xffffULL << 16);
			status |= (arg->mas_argval & 0xffff) << 16;
		} else if (strcmp(arg->mas_argnm, "other") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~(0x3ULL << 32);
			status |= (arg->mas_argval & 3) << 32;
		} else if (strcmp(arg->mas_argnm, "corrected") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~(0x7fffULL << 38);
			status |= (arg->mas_argval & 0x7fff) << 38;
		} else if (strcmp(arg->mas_argnm, "threshold") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~(0x3ULL << 53);
			status |= (arg->mas_argval & 0x3) << 53;
		} else if (strcmp(arg->mas_argnm, "rsvd") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			status &= ~(0x3ULL << 55);
			status |= (arg->mas_argval & 0x3) << 55;
		} else if (strcmp(arg->mas_argnm, "uc") == 0) {
			status |= MSR_MC_STATUS_UC;
		} else if (strcmp(arg->mas_argnm, "overflow") == 0) {
			status |= MSR_MC_STATUS_OVER;
		} else if (strcmp(arg->mas_argnm, "pcc") == 0) {
			status |= MSR_MC_STATUS_PCC;
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			status |= MSR_MC_STATUS_ADDRV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			addr = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "misc") == 0) {
			status |= MSR_MC_STATUS_MISCV;
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			misc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "en") == 0) {
			status |= MSR_MC_STATUS_EN;
		}
	}
	/* mis[0] */
	mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
	    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[1] */
	if (status & MSR_MC_STATUS_ADDRV)
		mtst_mis_init_msr_wr(misp++, cpi, addrmsr, addr,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[2] */
	if (status & MSR_MC_STATUS_MISCV)
		mtst_mis_init_msr_wr(misp++, cpi, miscmsr, misc,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	/* mis[3] */
	if (fatal) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else if ((flags & MTST_CMD_F_POLLED) != 0) {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_RIPV, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP | MCG_STATUS_RIPV,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
	}

	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
fsb_parity(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[8], *misp = mis;
	int i;
	int err;
	int fsb = 0;
	uint8_t ferr_fat_fsb = 1;
	uint32_t nerr_fat_fsb;
	uint32_t nrecfsb = 0;
	uint8_t nrecaddrh = 0;
	uint32_t nrecaddrl = 0x123456;
	uint32_t ferr_global = 1 << 28;
	uint32_t ferr_global_hi = 0;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int is7300;
	int next = 0;
	uint32_t nerr_global;
	uint32_t new_nerr_global = 1 << 28;

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);

			if (is7300) {
				fsb = arg->mas_argval & 3;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					new_nerr_global = 1 << 28;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 29;
					break;
				case 2:
					ferr_global = 0;
					ferr_global_hi = 1 << 2;
					new_nerr_global = 1 << 27;
					break;
				case 3:
					ferr_global = 0;
					ferr_global_hi = 1 << 3;
					new_nerr_global = 1 << 26;
					break;
				}
			} else {
				fsb = arg->mas_argval & 1;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					new_nerr_global = 1 << 28;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 29;
					break;
				}
			}
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecaddrl = (uint32_t)arg->mas_argval;
			nrecaddrh = (uint8_t)(arg->mas_argval >> 32);
		} else if (strcmp(arg->mas_argnm, "nrecfsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfsb = (uint32_t)arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (is7300) {
		if (next) {
			mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44,
			    MTST_MIS_ASZ_L, &nerr_global,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			mtst_mis_init_pci_rd(&mis[1], 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc2 : 0x42, MTST_MIS_ASZ_B,
			    &nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 2);
			if (err != 0) {
				nerr_fat_fsb = 0;
				nerr_global = 0;
			}
			nerr_global |= new_nerr_global;
			nerr_fat_fsb |= ferr_fat_fsb;
			/* mis[0] NERR_FAT_FSB */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc2 : 0x42, MTST_MIS_ASZ_B,
			    nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc0 : 0x40, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc4 : 0x44, MTST_MIS_ASZ_B,
			    (uint32_t)nrecfsb, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xcc : 0x4c, MTST_MIS_ASZ_B,
			    (uint32_t)nrecaddrl, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xd0 : 0x50, MTST_MIS_ASZ_B,
			    (uint32_t)nrecaddrh, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	} else {
		if (next) {
			mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44,
			    MTST_MIS_ASZ_L, &nerr_global,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			mtst_mis_init_pci_rd(&mis[1], 0, 16, 0,
			    fsb ? 0x482 : 0x182, MTST_MIS_ASZ_B,
			    &nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 2);
			if (err != 0) {
				nerr_fat_fsb = 0;
				nerr_global = 0;
			}
			nerr_global |= new_nerr_global;
			nerr_fat_fsb |= ferr_fat_fsb;
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x482 : 0x182, MTST_MIS_ASZ_B, nerr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x480 : 0x180,
			    MTST_MIS_ASZ_B, (uint32_t)ferr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x484 : 0x184,
			    MTST_MIS_ASZ_L, nrecfsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x48c : 0x18c,
			    MTST_MIS_ASZ_L, nrecaddrl,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x490 : 0x190,
			    MTST_MIS_ASZ_B, (uint32_t)nrecaddrh,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	if (next) {
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40, MTST_MIS_ASZ_L,
		    ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[5] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
		case 0:
			/* mis[6] #MC if uncorrected, otherwise wake poller */
			mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[7] */
			if (status & MSR_MC_STATUS_UC)
				mtst_mis_init_int(misp++, cpi, T_MCE,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			else
				mtst_mis_init_poll(misp++, cpi,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;

		case MTST_CMD_F_POLLED:
			/* Leave the poller to find this at it's next wakeup */
			break;

		case MTST_CMD_F_INT18:
			/* mis[6] Arrange a #MC exception */
			mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[7] */
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;
		}
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
fsb_protocol(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[8], *misp = mis;
	int i;
	int err;
	int fsb = 0;
	uint8_t ferr_fat_fsb = 0x20;
	uint32_t nerr_fat_fsb;
	uint32_t nrecfsb = 0;
	uint8_t nrecaddrh = 0;
	uint32_t nrecaddrl = 0x123456;
	uint32_t ferr_global = 1 << 28;
	uint32_t ferr_global_hi = 0;
	uint32_t new_nerr_global = 1 << 28;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				fsb = arg->mas_argval & 3;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					ferr_global_hi = 0;
					new_nerr_global = 1 << 28;
					break;
				case 1:
					ferr_global = 1 << 29;
					ferr_global_hi = 0;
					new_nerr_global = 1 << 29;
					break;
				case 2:
					ferr_global = 0;
					ferr_global_hi = 1 << 2;
					new_nerr_global = 1 << 27;
					break;
				case 3:
					ferr_global = 0;
					ferr_global_hi = 1 << 3;
					new_nerr_global = 1 << 26;
					break;
				}
			} else {
				fsb = arg->mas_argval & 1;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					new_nerr_global = 1 << 28;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 29;
					break;
				}
			}
		} else if (strcmp(arg->mas_argnm, "addr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecaddrl = (uint32_t)arg->mas_argval;
			nrecaddrh = (uint8_t)(arg->mas_argval >> 32);
		} else if (strcmp(arg->mas_argnm, "nrecfsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfsb = (uint32_t)arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is7300) {
			mtst_mis_init_pci_rd(&mis[1], 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc2 : 0x42, MTST_MIS_ASZ_B,
			    &nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		} else {
			mtst_mis_init_pci_rd(&mis[1], 0, 16, 0,
			    fsb ? 0x482 : 0x182, MTST_MIS_ASZ_B,
			    &nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		}
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_fat_fsb = 0;
			nerr_global = 0;
		}
		nerr_global |= new_nerr_global;
		nerr_fat_fsb |= ferr_fat_fsb;
		/* mis[0] NERR_FAT_FSB */
		if (is7300) {
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc2 : 0x42, MTST_MIS_ASZ_B,
			    nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x482 : 0x182, MTST_MIS_ASZ_B, nerr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc0 : 0x40, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc4 : 0x44, MTST_MIS_ASZ_B,
			    (uint32_t)nrecfsb, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xcc : 0x4c, MTST_MIS_ASZ_B,
			    (uint32_t)nrecaddrl, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xd0 : 0x50, MTST_MIS_ASZ_B,
			    (uint32_t)nrecaddrh, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x180 : 0x480, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x184 : 0x484, MTST_MIS_ASZ_L, nrecfsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x18c : 0x48c, MTST_MIS_ASZ_L,
			    nrecaddrl, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x190 : 0x490, MTST_MIS_ASZ_B,
			    (uint32_t)nrecaddrh, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40, MTST_MIS_ASZ_L,
		    ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[5] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[6] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[7] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[6] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[7] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
fsb_unsup(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[6], *misp = mis;
	int i;
	int err;
	int fsb = 0;
	uint8_t ferr_fat_fsb = 8;
	uint32_t nerr_fat_fsb;
	uint32_t nrecfsb = 0;
	uint32_t ferr_global = 1 << 28;
	uint32_t ferr_global_hi = 0;
	uint32_t new_nerr_global = 1 << 28;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				fsb = arg->mas_argval & 3;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					ferr_global_hi = 0;
					new_nerr_global = 1 << 28;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 29;
					ferr_global_hi = 0;
					break;
				case 2:
					ferr_global = 0;
					ferr_global_hi = 1 << 2;
					new_nerr_global = 1 << 27;
					break;
				case 3:
					ferr_global = 0;
					ferr_global_hi = 1 << 3;
					new_nerr_global = 1 << 26;
					break;
				}
			} else {
				fsb = arg->mas_argval & 1;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					new_nerr_global = 1 << 28;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 29;
					break;
				}
			}
		} else if (strcmp(arg->mas_argnm, "nrecfsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfsb = (uint32_t)arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is7300) {
			mtst_mis_init_pci_rd(&mis[1], 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc2 : 0x42, MTST_MIS_ASZ_B,
			    &nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		} else {
			mtst_mis_init_pci_rd(&mis[1], 0, 16, 0,
			    fsb ? 0x482 : 0x182, MTST_MIS_ASZ_B,
			    &nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		}
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_fat_fsb = 0;
			nerr_global = 0;
		}
		nerr_global |= new_nerr_global;
		nerr_fat_fsb |= ferr_fat_fsb;
		/* mis[0] NERR_FAT_FSB */
		if (is7300) {
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc2 : 0x42, MTST_MIS_ASZ_B,
			    nerr_fat_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    fsb ? 0x482 : 0x182, MTST_MIS_ASZ_B, nerr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc0 : 0x40, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc4 : 0x44, MTST_MIS_ASZ_B,
			    (uint32_t)nrecfsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x180 : 0x480, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_fat_fsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x184 : 0x484, MTST_MIS_ASZ_L, nrecfsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
fsb_dparity(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[6], *misp = mis;
	int i;
	int err;
	int fsb = 0;
	uint8_t ferr_nf_fsb = 1;
	uint32_t nerr_nf_fsb;
	uint32_t recfsb = 0;
	uint32_t ferr_global = 1 << 12;
	uint32_t ferr_global_hi = 0;
	uint32_t new_nerr_global = 1 << 12;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				fsb = arg->mas_argval & 3;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 12;
					ferr_global_hi = 0;
					new_nerr_global = 1 << 12;
					break;
				case 1:
					ferr_global = 1 << 13;
					ferr_global_hi = 0;
					new_nerr_global = 1 << 13;
					break;
				case 2:
					ferr_global = 0;
					ferr_global_hi = 1;
					new_nerr_global = 1 << 11;
					break;
				case 3:
					ferr_global = 0;
					ferr_global_hi = 1 << 1;
					new_nerr_global = 1 << 10;
					break;
				}
			} else {
				fsb = arg->mas_argval & 1;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					new_nerr_global = 1 << 12;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 13;
					break;
				}
			}
		} else if (strcmp(arg->mas_argnm, "recfsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfsb = (uint32_t)arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is7300) {
			mtst_mis_init_pci_rd(&mis[1], 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc3 : 0x43, MTST_MIS_ASZ_B,
			    &nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		} else {
			mtst_mis_init_pci_rd(&mis[1], 0, 16, 0,
			    (fsb == 0) ? 0x183 : 0x483, MTST_MIS_ASZ_B,
			    &nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		}
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fsb = 0;
			nerr_global = 0;
		}
		nerr_global |= new_nerr_global;
		nerr_nf_fsb |= ferr_nf_fsb;
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc3 : 0x43, MTST_MIS_ASZ_B,
			    nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x183 : 0x483, MTST_MIS_ASZ_B,
			    nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc1 : 0x41, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc8 : 0x48, MTST_MIS_ASZ_B,
			    (uint32_t)recfsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x181 : 0x481, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x188 : 0x488, MTST_MIS_ASZ_L, recfsb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
fsb_mcerr(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	int i;
	int err;
	int fsb = 0;
	uint8_t ferr_nf_fsb = 4;
	uint32_t nerr_nf_fsb;
	uint32_t ferr_global = 1 << 12;
	uint32_t ferr_global_hi = 0;
	uint32_t new_nerr_global = 1 << 12;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				fsb = arg->mas_argval & 3;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 12;
					new_nerr_global = 1 << 12;
					break;
				case 1:
					ferr_global = 1 << 13;
					new_nerr_global = 1 << 13;
					break;
				case 2:
					ferr_global = 0;
					ferr_global_hi = 1;
					new_nerr_global = 1 << 11;
					break;
				case 3:
					ferr_global = 0;
					ferr_global_hi = 1 << 1;
					new_nerr_global = 1 << 10;
					break;
				}
			} else {
				fsb = arg->mas_argval & 1;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 28;
					new_nerr_global = 1 << 12;
					break;
				case 1:
					ferr_global = 1 << 29;
					new_nerr_global = 1 << 13;
					break;
				}
			}
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is7300) {
			mtst_mis_init_pci_rd(&mis[1], 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc3 : 0x43, MTST_MIS_ASZ_B,
			    &nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		} else {
			mtst_mis_init_pci_rd(&mis[1], 0, 16, 0,
			    (fsb == 0) ? 0x183 : 0x483, MTST_MIS_ASZ_B,
			    &nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		}
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fsb = 0;
			nerr_global = 0;
		}
		nerr_global |= new_nerr_global;
		nerr_nf_fsb |= ferr_nf_fsb;
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc3 : 0x43, MTST_MIS_ASZ_B,
			    nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x183 : 0x483, MTST_MIS_ASZ_B,
			    nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc1 : 0x41, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x181 : 0x481, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[3] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[3] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
fsb_bint(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	int i;
	int err;
	int fsb = 0;
	uint8_t ferr_nf_fsb = 2;
	uint32_t nerr_nf_fsb;
	uint32_t ferr_global = 1 << 12;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	uint32_t new_nerr_global = 1 << 12;
	int is7300;

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fsb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				fsb = arg->mas_argval & 3;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 12;
					new_nerr_global = 1 << 12;
					break;
				case 1:
					ferr_global = 1 << 13;
					new_nerr_global = 1 << 13;
					break;
				case 2:
					ferr_global = 0;
					ferr_global_hi = 1;
					new_nerr_global = 1 << 11;
					break;
				case 3:
					ferr_global = 0;
					ferr_global_hi = 1 << 1;
					new_nerr_global = 1 << 10;
					break;
				}
			} else {
				fsb = arg->mas_argval & 1;
				switch (fsb) {
				case 0:
					ferr_global = 1 << 12;
					new_nerr_global = 1 << 12;
					break;
				case 1:
					ferr_global = 1 << 13;
					new_nerr_global = 1 << 13;
					break;
				}
			}
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is7300) {
			mtst_mis_init_pci_rd(&mis[1], 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc3 : 0x43, MTST_MIS_ASZ_B,
			    &nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		} else {
			mtst_mis_init_pci_rd(&mis[1], 0, 16, 0,
			    (fsb == 0) ? 0x183 : 0x483, MTST_MIS_ASZ_B,
			    &nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		}
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fsb = 0;
			nerr_global = 0;
		}
		nerr_global |= new_nerr_global;
		nerr_nf_fsb |= ferr_nf_fsb;
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc3 : 0x43, MTST_MIS_ASZ_B,
			    nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x183 : 0x483, MTST_MIS_ASZ_B,
			    nerr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is7300) {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 17, (fsb & 2) ? 3 : 0,
			    (fsb & 1) ? 0xc1 : 0x41, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 0,
			    (fsb == 0) ? 0x181 : 0x481, MTST_MIS_ASZ_B,
			    (uint32_t)ferr_nf_fsb, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[3] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[3] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_to(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[8], *misp = mis;
	int i;
	int err;
	uint32_t ferr_fat_fbd = 1;
	uint32_t nerr_fat_fbd;
	uint16_t nrecmema = 0;
	uint32_t nrecmemb = 0;
	uint32_t nrecfglog = 0;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int dev_rec;
	int is7300;
	int is5400;

	if (is_5100())
		return (0);

	is7300 = is_7300();
	is5400 = is_5400();

	if (is5400) {
		ferr_global = 1 << 27;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 24;
		dev_rec = 16;
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
			} else {
				ferr_global = (ferr_global & ~(0xf << 24)) &
				    1 << (24 + (arg->mas_argval & 3));
			}
			ferr_fat_fbd = ((arg->mas_argval & 3) << 28) | 1;
		} else if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema &= ~0x7000;
			nrecmema |= (arg->mas_argval & 7) << 12;
		} else if (strcmp(arg->mas_argnm, "rank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmema &= ~0xf00;
				nrecmema |= (arg->mas_argval & 0xf) << 8;
			} else {
				nrecmema &= ~0x700;
				nrecmema |= (arg->mas_argval & 7) << 8;
			}
		} else if (strcmp(arg->mas_argnm, "rec_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema &= ~0xff;
			nrecmema |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "cas") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0x1fff0000;
				nrecmemb |= (arg->mas_argval & 0x1fff) << 16;
			} else {
				nrecmemb &= ~0xfff0000;
				nrecmemb |= (arg->mas_argval & 0xfff) << 16;
			}
		} else if (strcmp(arg->mas_argnm, "ras") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0xffff;
				nrecmemb |= arg->mas_argval & 0xffff;
			} else {
				nrecmemb &= ~0x7fff;
				nrecmemb |= arg->mas_argval & 0x7fff;
			}
		} else if (strcmp(arg->mas_argnm, "be") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0xf000000;
			nrecfglog |= (arg->mas_argval & 0xf) << 24;
		} else if (strcmp(arg->mas_argnm, "reg") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0xff0000;
			nrecfglog |= (arg->mas_argval & 0xff) << 16;
		} else if (strcmp(arg->mas_argnm, "rdwr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0x80000000;
				nrecmemb |= (arg->mas_argval & 1) << 31;
			} else {
				nrecmema &= ~0x800;
				nrecmema |= (arg->mas_argval & 1) << 11;
			}
			nrecfglog &= ~0x800;
			nrecfglog |= (arg->mas_argval & 1) << 11;
		} else if (strcmp(arg->mas_argnm, "function") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0x700;
			nrecfglog |= (arg->mas_argval & 7) << 8;
		} else if (strcmp(arg->mas_argnm, "cfg_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0xff;
			nrecfglog |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "nrecmema") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecmemb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmemb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfglog") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0x9c, MTST_MIS_ASZ_L,
		    &nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_fat_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_fat_fbd |= ferr_fat_fbd & 0x0fffffff;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x9c,
		    MTST_MIS_ASZ_L, nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xbe,
		    MTST_MIS_ASZ_W, (uint32_t)nrecmema,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xc0,
		    MTST_MIS_ASZ_L, nrecmemb, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 ? 0x74 : 0xc4,
		    MTST_MIS_ASZ_L, nrecfglog, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x98,
		    MTST_MIS_ASZ_L, ferr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[5] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[6] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[7] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[6] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[7] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_frto(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[6], *misp = mis;
	int i;
	int err;
	uint32_t ferr_fat_fbd = 1 << 22;
	uint32_t nerr_fat_fbd;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;
	int is5400;

	if (is_5100())
		return (0);

	is7300 = is_7300();
	is5400 = is_5400();

	if (is5400)
		ferr_global = 1 << 27;
	else
		ferr_global = 1 << 24;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (!is5400) {
				ferr_global = (ferr_global & ~(0xf << 24)) &
				    1 << (24 + (arg->mas_argval & 3));
			}
			ferr_fat_fbd = ((arg->mas_argval & 3) << 28) |
			    (1 << 22);
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0x9c,  MTST_MIS_ASZ_L,
		    &nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_fat_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_fat_fbd |= 1 << 22;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x9c,
		    MTST_MIS_ASZ_L, nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x98,
		    MTST_MIS_ASZ_L, ferr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}


/*ARGSUSED*/
int
dimm_ma(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[6], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd;
	uint32_t nerr_nf_fbd;
	uint32_t recfglog = 0;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int dev_rec;
	int is7300;
	int is5400;

	if (is_5100())
		return (0);

	is7300 = is_7300();
	is5400 = is_5400();

	if (is5400) {
		ferr_global = 1 << 11;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 8;
		dev_rec = 16;
	}

	if (cmdarg)
		ferr_nf_fbd = 1 << 9;
	else
		ferr_nf_fbd = 1 << 10;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
			} else {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
			}
			ferr_nf_fbd = ((arg->mas_argval & 3) << 28) |
			    (cmdarg) ? (1 << 9) : (1 << 10);
		} else if (strcmp(arg->mas_argnm, "be") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0xf000000;
			recfglog |= (arg->mas_argval & 0xf) << 24;
		} else if (strcmp(arg->mas_argnm, "reg") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0xff0000;
			recfglog |= (arg->mas_argval & 0xff) << 16;
		} else if (strcmp(arg->mas_argnm, "rdwr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0x800;
			recfglog |= (arg->mas_argval & 1) << 11;
		} else if (strcmp(arg->mas_argnm, "function") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0x700;
			recfglog |= (arg->mas_argval & 7) << 8;
		} else if (strcmp(arg->mas_argnm, "cfg_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0xff;
			recfglog |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "recfglog") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= ferr_nf_fbd & 0x0fffffff;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0x78 : 0xe8,
		    MTST_MIS_ASZ_L, recfglog, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[4] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[4] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_ce_ecc(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[10], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd;
	uint32_t nerr_nf_fbd;
	uint32_t recmema = 0;
	uint32_t recmemb = 0;
	uint32_t redmema = 0;
	uint32_t redmemb = 0;
	uint32_t recfglog = 0;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint32_t validlog;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int mem_dev = 21;
	int dev_rec;
	int is7300;
	int is5100;
	int is5400;

	is7300 = is_7300();
	is5100 = is_5100();
	is5400 = is_5400();

	/* By default, thie error bit is injected */
	if (is5100)
		ferr_nf_fbd = 1 << 14;
	else
		ferr_nf_fbd = 1 << 16;

	if (is5400) {
		ferr_global = 1 << 11;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 8;
		dev_rec = 16;
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				ferr_global = (ferr_global & ~(0x3 << 8)) |
				    (1 << (8 + (arg->mas_argval & 1)));
				ferr_nf_fbd &= ~(1 << 28);
				ferr_nf_fbd |= (arg->mas_argval & 1) << 28;
				mem_dev = 21 + (arg->mas_argval & 1);
			} else if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
				ferr_nf_fbd &= ~(3 << 28);
				ferr_nf_fbd |= (arg->mas_argval & 3) << 28;
			} else {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
				ferr_nf_fbd &= ~(3 << 28);
				ferr_nf_fbd |= (arg->mas_argval & 3) << 28;
			}
		} else if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmema &= ~0x7000;
			recmema |= (arg->mas_argval & 7) << 12;
		} else if (strcmp(arg->mas_argnm, "rank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				recmema &= ~0xf00;
				recmema |= (arg->mas_argval & 0xf) << 8;
			} else {
				recmema &= ~0x700;
				recmema |= (arg->mas_argval & 7) << 8;
			}
		} else if (strcmp(arg->mas_argnm, "rec_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmema &= ~0xff;
			recmema |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "cas") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				recmemb &= ~0x1fff0000;
				recmemb |= (arg->mas_argval & 0x1fff) << 16;
			} else {
				recmemb &= ~0xfff0000;
				recmemb |= (arg->mas_argval & 0xfff) << 16;
			}
		} else if (strcmp(arg->mas_argnm, "ras") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100 || is7300) {
				recmemb &= ~0xffff;
				recmemb |= arg->mas_argval & 0xffff;
			} else {
				recmemb &= ~0x7fff;
				recmemb |= arg->mas_argval & 0x7fff;
			}
		} else if (strcmp(arg->mas_argnm, "be") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0xf000000;
			recfglog |= (arg->mas_argval & 0xf) << 24;
		} else if (strcmp(arg->mas_argnm, "reg") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0xff0000;
			recfglog |= (arg->mas_argval & 0xff) << 16;
		} else if (strcmp(arg->mas_argnm, "rdwr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100 || is7300) {
				recmemb &= ~0x80000000;
				recmemb |= (arg->mas_argval & 1) << 31;
			} else {
				recmema &= ~0x800;
				recmema |= (arg->mas_argval & 1) << 11;
			}
			recfglog &= ~0x800;
			recfglog |= (arg->mas_argval & 1) << 11;
		} else if (strcmp(arg->mas_argnm, "function") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0x700;
			recfglog |= (arg->mas_argval & 7) << 8;
		} else if (strcmp(arg->mas_argnm, "cfg_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog &= ~0xff;
			recfglog |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "redmema") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			redmema = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "redmemb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			redmemb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recmema") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmema = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recmemb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmemb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfglog") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfglog = arg->mas_argval;
				break;
		} else if (strcmp(arg->mas_argnm, "mirror") == 0) {
			if (is5100)
				return (0);
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			ferr_nf_fbd &= ~(0xf << 13);
			ferr_nf_fbd |= 1 << 14;
		} else if (strcmp(arg->mas_argnm, "spare") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			ferr_nf_fbd &= ~(0xf << 13);
			ferr_nf_fbd |= 1 << 15;
		} else if (strcmp(arg->mas_argnm, "patrol") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			ferr_nf_fbd &= ~(0xf << 13);
			ferr_nf_fbd |= 1 << 16;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= ferr_nf_fbd & 0x0fffffff;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is5100) {
			validlog = 0;
			mtst_mis_init_pci_rd(&mis[0], 0, mem_dev, 0, 0x18c,
			    MTST_MIS_ASZ_L, &validlog,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 1);
			validlog |= 6;
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x198,
			    MTST_MIS_ASZ_L, redmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x19c,
			    MTST_MIS_ASZ_L, redmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x1a0,
			    MTST_MIS_ASZ_L, (uint32_t)recmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x1a4,
			    MTST_MIS_ASZ_L, recmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[4] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x18c,
			    MTST_MIS_ASZ_L, validlog,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0x7c,
			    MTST_MIS_ASZ_L, redmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
			    is7300 || is5400 ? 0xe0 : 0xe2,
			    is5400 ? MTST_MIS_ASZ_L : MTST_MIS_ASZ_W, recmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xe4,
			    MTST_MIS_ASZ_L, recmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
			    is7300 || is5400 ? 0x78 : 0xe8, MTST_MIS_ASZ_L,
			    recfglog, MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[5] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[6] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[7] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[8] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[9] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[8] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[9] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_ue_ecc(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[11], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd;
	uint32_t nerr_nf_fbd;
	uint32_t recmema = 0;
	uint32_t recmemb = 0;
	uint32_t redmema = 0;
	uint32_t redmemb = 0;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint32_t validlog;
	uint64_t status = 0xb200001044000e0fULL;
	uint_t statmsr = IA32_MSR_MC(5, STATUS);
	int next = 0;
	int mem_dev = 21;
	int dev_rec;
	int is7300;
	int is5100;
	int is5400;

	is7300 = is_7300();
	is5100 = is_5100();
	is5400 = is_5400();

	/* By default, this error bit is injected */
	if (is5100)
		ferr_nf_fbd = 1 << 4;
	else
		ferr_nf_fbd = 1 << 5;

	if (is5400) {
		ferr_global = 1 << 11;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 8;
		dev_rec = 16;
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				ferr_global = (ferr_global & ~(0x3 << 8)) |
				    (1 << (8 + (arg->mas_argval & 1)));
				ferr_nf_fbd &= ~(1 << 28);
				ferr_nf_fbd |= (arg->mas_argval & 1) << 28;
				mem_dev = 21 + (arg->mas_argval & 1);
			} else if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
				ferr_nf_fbd &= ~(3 << 28);
				ferr_nf_fbd |= (arg->mas_argval & 3) << 28;
			} else {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
				ferr_nf_fbd &= ~(3 << 28);
				ferr_nf_fbd |= (arg->mas_argval & 3) << 28;
			}
		} else if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmema &= ~0x7000;
			recmema |= (arg->mas_argval & 7) << 12;
		} else if (strcmp(arg->mas_argnm, "rank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				recmema &= ~0x700;
				recmema |= (arg->mas_argval & 7) << 8;
			} else {
				recmema &= ~0xf00;
				recmema |= (arg->mas_argval & 0xf) << 8;
			}
		} else if (strcmp(arg->mas_argnm, "rec_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmema &= ~0xff;
			recmema |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "cas") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				recmemb &= ~0x1fff0000;
				recmemb |= (arg->mas_argval & 0x1fff) << 16;
			} else {
				recmemb &= ~0xfff0000;
				recmemb |= (arg->mas_argval & 0xfff) << 16;
			}
		} else if (strcmp(arg->mas_argnm, "ras") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100 || is7300) {
				recmemb &= ~0xffff;
				recmemb |= arg->mas_argval & 0xffff;
			} else {
				recmemb &= ~0x7fff;
				recmemb |= arg->mas_argval & 0x7fff;
			}
		} else if (strcmp(arg->mas_argnm, "rdwr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100 || is7300) {
				recmemb &= ~0x80000000;
				recmemb |= (arg->mas_argval & 1) << 31;
			} else {
				recmema &= ~0x800;
				recmema |= (arg->mas_argval & 1) << 11;
			}
		} else if (strcmp(arg->mas_argnm, "recmema") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmema = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recmemb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recmemb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "mirror") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (!is5100) {
				if (ferr_nf_fbd & (0xf << 5)) {
					ferr_nf_fbd &= ~(0xf << 5);
					ferr_nf_fbd |= 1 << 6;
				} else {
					ferr_nf_fbd &= ~(0xf << 1);
					ferr_nf_fbd |= 1 << 2;
				}
			}
		} else if (strcmp(arg->mas_argnm, "spare") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				if (ferr_nf_fbd & (0x7 << 10)) {
					ferr_nf_fbd &= ~(0x7 << 10);
					ferr_nf_fbd |= 1 << 11;
				} else {
					ferr_nf_fbd &= ~(0x7 << 4);
					ferr_nf_fbd |= 1 << 5;
				}
			} else {
				if (ferr_nf_fbd & (0xf << 5)) {
					ferr_nf_fbd &= ~(0xf << 5);
					ferr_nf_fbd |= 1 << 7;
				} else {
					ferr_nf_fbd &= ~(0xf << 1);
					ferr_nf_fbd |= 1 << 3;
				}
			}
		} else if (strcmp(arg->mas_argnm, "patrol") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				if (ferr_nf_fbd & (0x7 << 10)) {
					ferr_nf_fbd &= ~(0x7 << 10);
					ferr_nf_fbd |= 1 << 12;
				} else {
					ferr_nf_fbd &= ~(0x7 << 4);
					ferr_nf_fbd |= 1 << 6;
				}
			} else {
				if (ferr_nf_fbd & (0xf << 5)) {
					ferr_nf_fbd &= ~(0xf << 5);
					ferr_nf_fbd |= 1 << 8;
				} else {
					ferr_nf_fbd &= ~(0xf << 1);
					ferr_nf_fbd |= 1 << 4;
				}
			}
		} else if (strcmp(arg->mas_argnm, "aliased") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				if (ferr_nf_fbd & (0x7 << 10)) {
					ferr_nf_fbd |=
					    (ferr_nf_fbd & (0x7 << 10)) >> 6;
					ferr_nf_fbd &= ~(0x7 << 10);
				}
			} else {
				if (ferr_nf_fbd & (0xf << 5)) {
					ferr_nf_fbd |=
					    (ferr_nf_fbd & (0xf << 5)) >> 4;
					ferr_nf_fbd &= ~(0xf << 5);
				}
			}
		} else if (strcmp(arg->mas_argnm, "non_aliased") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				if (ferr_nf_fbd & (0x7 << 4)) {
					ferr_nf_fbd |=
					    (ferr_nf_fbd & (0x7 << 4)) << 6;
					ferr_nf_fbd &= ~(0x7 << 4);
				}
			} else {
				if (ferr_nf_fbd & (0xf << 1)) {
					ferr_nf_fbd |=
					    (ferr_nf_fbd & (0xf << 1)) << 4;
					ferr_nf_fbd &= ~(0xf << 1);
				}
			}
		} else if (strcmp(arg->mas_argnm, "redmema") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			redmema = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "redmemb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			redmemb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= ferr_nf_fbd & 0x0fffffff;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is5100) {
			validlog = 0;
			mtst_mis_init_pci_rd(&mis[0], 0, mem_dev, 0, 0x18c,
			    MTST_MIS_ASZ_L, &validlog,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 1);
			validlog |= 2;
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x198,
			    MTST_MIS_ASZ_L, redmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x19c,
			    MTST_MIS_ASZ_L, redmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x1a0,
			    MTST_MIS_ASZ_L, (uint32_t)recmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x1a4,
			    MTST_MIS_ASZ_L, recmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[4] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x18c,
			    MTST_MIS_ASZ_L, validlog,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0x7c,
			    MTST_MIS_ASZ_L, redmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
			    is7300 || is5400 ? 0xe0 : 0xe2,
			    is5400 ? MTST_MIS_ASZ_L : MTST_MIS_ASZ_W, recmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xe4,
			    MTST_MIS_ASZ_L, recmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[5] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[6] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[7] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[8] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		if (status & MSR_MC_STATUS_UC) {
			/* mis[9] */
			mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
			    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[10] */
			mtst_mis_init_int(misp++, cpi, T_MCE, 0);
		} else {
			/* mis[9] */
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[8] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[9] */
		mtst_mis_init_msr_wr(misp++, cpi, IA32_MSR_MCG_STATUS,
		    MCG_STATUS_MCIP, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[10] */
		mtst_mis_init_int(misp++, cpi, T_MCE, 0);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_crc_sync(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[11], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd = 1 << 17;
	uint32_t nerr_nf_fbd;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint32_t recfbda = 0;
	uint32_t recfbdb = 0;
	uint32_t recfbdc = 0;
	uint32_t recfbdd = 0;
	uint32_t recfbde = 0;
	uint32_t recfbdf = 0;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int dev_rec;
	int is7300;
	int is5400;

	if (is_5100())
		return (0);

	is7300 = is_7300();
	is5400 = is_5400();

	if (is5400) {
		ferr_global = 1 << 11;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 8;
		dev_rec = 16;
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
			} else {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
			}
			ferr_nf_fbd = ((arg->mas_argval & 3) << 28) | (1 << 11);
		} else if (strcmp(arg->mas_argnm, "recfbda") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbda = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdd") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdd = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbde") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbde = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdf") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdf = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= (1 << 11);
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xe8 : 0xec,
		    MTST_MIS_ASZ_L, recfbda, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xec : 0xf0,
		    MTST_MIS_ASZ_L, recfbdb, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xf0 : 0xf4,
		    MTST_MIS_ASZ_L, recfbdc, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xf4 : 0xf8,
		    MTST_MIS_ASZ_L, recfbdd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xf8 : 0xfc,
		    MTST_MIS_ASZ_L, recfbde, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300 || is5400) {
			/* mis[6] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xfc,
			    is5400 ?  MTST_MIS_ASZ_L : MTST_MIS_ASZ_W, recfbdf,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[7] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[8] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[9] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[10] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[9] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[10] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_crc(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[11], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd = 1 << 11;
	uint32_t nerr_nf_fbd;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint32_t recfbda = 0;
	uint32_t recfbdb = 0;
	uint32_t recfbdc = 0;
	uint32_t recfbdd = 0;
	uint32_t recfbde = 0;
	uint32_t recfbdf = 0;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int dev_rec;
	int is7300;
	int is5400;

	if (is_5100())
		return (0);

	is7300 = is_7300();
	is5400 = is_5400();

	if (is5400) {
		ferr_global = 1 << 11;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 8;
		dev_rec = 16;
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
			} else {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
			}
			ferr_nf_fbd = ((arg->mas_argval & 3) << 28) | (1 << 15);
		} else if (strcmp(arg->mas_argnm, "recfbda") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbda = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdd") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdd = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbde") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbde = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recfbdf") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recfbdf = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= (1 << 15);
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xe8 : 0xec,
		    MTST_MIS_ASZ_L, recfbda, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xec : 0xf0,
		    MTST_MIS_ASZ_L, recfbdb, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xf0 : 0xf4,
		    MTST_MIS_ASZ_L, recfbdc, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xf4 : 0xf8,
		    MTST_MIS_ASZ_L, recfbdd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[6] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xf8 : 0xfc,
		    MTST_MIS_ASZ_L, recfbde, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300 || is5400) {
			/* mis[7] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xfc,
			    is5400 ? MTST_MIS_ASZ_L : MTST_MIS_ASZ_W, recfbdf,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[8] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[9] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[10] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[11] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[10] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[11] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
spd_protocol(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd = 1 << 18;
	uint32_t nerr_nf_fbd;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;
	int is5100;
	int is5400;

	is7300 = is_7300();
	is5100 = is_5100();
	is5400 = is_5400();

	if (is5400)
		ferr_global = 1 << 11;
	else
		ferr_global = 1 << 8;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				ferr_global = (ferr_global & ~(0x3 << 8)) |
				    (1 << (8 + (arg->mas_argval & 1)));
				ferr_nf_fbd = ((arg->mas_argval & 1) << 28) |
				    (1 << 18);
			} else if (!is5400) {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
				ferr_nf_fbd = ((arg->mas_argval & 3) << 28) |
				    (1 << 18);
			} else {
				ferr_nf_fbd = ((arg->mas_argval & 3) << 28) |
				    (1 << 18);
			}
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= 1 << 18;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[3] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[3] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_ue_crc(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[14], *misp = mis;
	int i;
	int err;
	uint32_t ferr_fat_fbd = 2;
	uint32_t nerr_fat_fbd;
	uint16_t nrecmema = 0;
	uint32_t nrecmemb = 0;
	uint32_t nrecfglog = 0;
	uint32_t nrecfbda = 0;
	uint32_t nrecfbdb = 0;
	uint32_t nrecfbdc = 0;
	uint32_t nrecfbdd = 0;
	uint32_t nrecfbde = 0;
	uint32_t nrecfbdf = 0;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int dev_rec;
	int is7300;
	int is5400;

	if (is_5100())
		return (0);

	is7300 = is_7300();
	is5400 = is_5400();

	if (is5400) {
		ferr_global = 1 << 27;
		dev_rec = 21;
	} else {
		ferr_global = 1 << 24;
		dev_rec = 16;
	}

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5400) {
				dev_rec = (arg->mas_argval & 2) ? 22 : 21;
			} else {
				ferr_global = (ferr_global & ~(0xf << 24)) &
				    1 << (24 + (arg->mas_argval & 3));
			}
			ferr_fat_fbd = ((arg->mas_argval & 3) << 28) | 2;
		} else if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema &= ~0x7000;
			nrecmema |= (arg->mas_argval & 7) << 12;
		} else if (strcmp(arg->mas_argnm, "rank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmema &= ~0xf00;
				nrecmema |= (arg->mas_argval & 0xf) << 8;
			} else {
				nrecmema &= ~0x700;
				nrecmema |= (arg->mas_argval & 7) << 8;
			}
		} else if (strcmp(arg->mas_argnm, "rec_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema &= ~0xff;
			nrecmema |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "cas") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0x1fff0000;
				nrecmemb |= (arg->mas_argval & 0x1fff) << 16;
			} else {
				nrecmemb &= ~0xfff0000;
				nrecmemb |= (arg->mas_argval & 0xfff) << 16;
			}
		} else if (strcmp(arg->mas_argnm, "ras") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0xffff;
				nrecmemb |= arg->mas_argval & 0xffff;
			} else {
				nrecmemb &= ~0x7fff;
				nrecmemb |= arg->mas_argval & 0x7fff;
			}
		} else if (strcmp(arg->mas_argnm, "be") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0xf000000;
			nrecfglog |= (arg->mas_argval & 0xf) << 24;
		} else if (strcmp(arg->mas_argnm, "reg") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0xff0000;
			nrecfglog |= (arg->mas_argval & 0xff) << 16;
		} else if (strcmp(arg->mas_argnm, "rdwr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0x80000000;
				nrecmemb |= (arg->mas_argval & 1) << 31;
			} else {
				nrecmema &= ~0x800;
				nrecmema |= (arg->mas_argval & 1) << 11;
			}
			nrecfglog &= ~0x800;
			nrecfglog |= (arg->mas_argval & 1) << 11;
		} else if (strcmp(arg->mas_argnm, "function") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0x700;
			nrecfglog |= (arg->mas_argval & 7) << 8;
		} else if (strcmp(arg->mas_argnm, "cfg_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog &= ~0xff;
			nrecfglog |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "nrecmema") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecmemb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmemb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfbda") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfbda = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfbdb") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfbdb = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfbdc") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfbdc = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfbdd") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfbdd = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfbde") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfbde = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfbdf") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfbdf = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecfglog") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecfglog = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0x9c, MTST_MIS_ASZ_L,
		    &nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_fat_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_fat_fbd |= ferr_fat_fbd;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x9c,
		    MTST_MIS_ASZ_L, nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xbe,
		    MTST_MIS_ASZ_W, (uint32_t)nrecmema,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xc0,
		    MTST_MIS_ASZ_L, nrecmemb, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xc4 : 0xc8,
		    MTST_MIS_ASZ_L, nrecfbda, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xc8 : 0xcc,
		    MTST_MIS_ASZ_L, nrecfbdb, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xcc : 0xd0,
		    MTST_MIS_ASZ_L, nrecfbdc, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xd0 : 0xd4,
		    MTST_MIS_ASZ_L, nrecfbdd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[6] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0xd4 : 0xd8,
		    MTST_MIS_ASZ_L, nrecfbde, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300 || is5400) {
			/* mis[7] */
			mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1, 0xd8,
			    is5400 ? MTST_MIS_ASZ_L : MTST_MIS_ASZ_W, nrecfbdf,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[8] */
		mtst_mis_init_pci_wr(misp++, 0, dev_rec, 1,
		    is7300 || is5400 ? 0x74 : 0xc4,
		    MTST_MIS_ASZ_L, nrecfglog, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[9] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x98,
		    MTST_MIS_ASZ_L, ferr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[10] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[11] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[12] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[13] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[12] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[13] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_thermal_5400(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	int i;
	int err;
	uint32_t ferr_fat_thr = 0;
	uint32_t nerr_fat_thr;
	uint32_t ferr_nf_thr = 0;
	uint32_t nerr_nf_thr;
	uint32_t ferr_global = 1 << 24;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int fatal = 0;
	int errno = 0;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "fatal") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "errno") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			errno = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (fatal || errno == 1 || errno == 2) {
		ferr_global = 1 << 26;
		if (errno == 1)
			ferr_fat_thr = 1;
		else
			ferr_fat_thr = 2;
	} else {
		ferr_global = 1 << 10;
		if (errno == 5)
			ferr_nf_thr = 1 << 4;
		else if (errno == 4)
			ferr_nf_thr = 1 << 3;
		else
			ferr_nf_thr = 1 << 2;
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 2, 0xf2, MTST_MIS_ASZ_B,
		    &nerr_fat_thr, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[2], 0, 16, 2, 0xf3, MTST_MIS_ASZ_B,
		    &nerr_nf_thr, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 3);
		if (err != 0) {
			nerr_fat_thr = 0;
			nerr_nf_thr = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_fat_thr |= ferr_fat_thr;
		nerr_nf_thr |= ferr_nf_thr;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xf2,
		    MTST_MIS_ASZ_B, nerr_fat_thr, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xf3,
		    MTST_MIS_ASZ_B, nerr_nf_thr, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xf0,
		    MTST_MIS_ASZ_B, ferr_fat_thr, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xf1,
		    MTST_MIS_ASZ_B, ferr_nf_thr, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[3] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[3] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}
	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_thermal(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	int i;
	int err;
	uint32_t ferr_fat_fbd = 4;
	uint32_t nerr_fat_fbd;
	uint32_t ferr_global = 1 << 24;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;

	if (is_5100())
		return (0);

	if (is_5400())
		return (dimm_thermal_5400(cpi, flags, args, nargs, cmdarg));

	is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			ferr_fat_fbd = ((arg->mas_argval & 3) << 28) | 4;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0x9c, MTST_MIS_ASZ_L,
		    &nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_fat_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_fat_fbd |= 4;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x9c,
		    MTST_MIS_ASZ_L, nerr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0x98,
		    MTST_MIS_ASZ_L, ferr_fat_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[3] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[3] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_m4(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[8], *misp = mis;
	int i;
	int err;
	uint32_t ferr_nf_fbd = 1;
	uint32_t nerr_nf_fbd;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint32_t validlog;
	uint32_t nrecmema = 0;
	uint32_t nrecmemb = 0;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int mem_dev = 21;
	int next = 0;
	int is7300;
	int is5100;
	int is5400;

	is7300 = is_7300();
	is5100 = is_5100();
	is5400 = is_5400();

	if (is5100)
		ferr_nf_fbd = 2;

	if (is5400)
		ferr_global = 1 << 11;
	else
		ferr_global = 1 << 8;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				ferr_global = (ferr_global & ~(0x3 << 8)) |
				    (1 << (8 + (arg->mas_argval & 1)));
				ferr_nf_fbd = ((arg->mas_argval & 1) << 28) | 2;
				mem_dev = 21 + (arg->mas_argval & 1);
			} else if (!is5400) {
				ferr_global = (ferr_global & ~(0xf << 8)) |
				    (1 << (8 + (arg->mas_argval & 3)));
				ferr_nf_fbd = ((arg->mas_argval & 3) << 28) | 1;
			} else {
				ferr_nf_fbd = ((arg->mas_argval & 3) << 28) | 1;
			}
		} else if (strcmp(arg->mas_argnm, "bank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema &= ~0x7000;
			nrecmema |= (arg->mas_argval & 7) << 12;
		} else if (strcmp(arg->mas_argnm, "rank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmema &= ~0x700;
				nrecmema |= (arg->mas_argval & 7) << 8;
			} else {
				nrecmema &= ~0xf00;
				nrecmema |= (arg->mas_argval & 0xf) << 8;
			}
		} else if (strcmp(arg->mas_argnm, "rec_bufid") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecmema &= ~0xff;
			nrecmema |= arg->mas_argval & 0xff;
		} else if (strcmp(arg->mas_argnm, "cas") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300) {
				nrecmemb &= ~0x1fff0000;
				nrecmemb |= (arg->mas_argval & 0x1fff) << 16;
			} else {
				nrecmemb &= ~0xfff0000;
				nrecmemb |= (arg->mas_argval & 0xfff) << 16;
			}
		} else if (strcmp(arg->mas_argnm, "ras") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100 || is7300) {
				nrecmemb &= ~0xffff;
				nrecmemb |= arg->mas_argval & 0xffff;
			} else {
				nrecmemb &= ~0x7fff;
				nrecmemb |= arg->mas_argval & 0x7fff;
			}
		} else if (strcmp(arg->mas_argnm, "rdwr") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100 || is7300) {
				nrecmemb &= ~0x80000000;
				nrecmemb |= (arg->mas_argval & 1) << 31;
			} else {
				nrecmema &= ~0x800;
				nrecmema |= (arg->mas_argval & 1) << 11;
			}
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_fbd |= ferr_nf_fbd;
		/* msr[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* msr[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		if (is5100) {
			validlog = 0;
			mtst_mis_init_pci_rd(&mis[0], 0, mem_dev, 0, 0x18c,
			    MTST_MIS_ASZ_L, &validlog,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 1);
			validlog |= 1;
			/* mis[0] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x190,
			    MTST_MIS_ASZ_L, nrecmema,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[1] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x194,
			    MTST_MIS_ASZ_L, nrecmemb,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, mem_dev, 0, 0x18c,
			    MTST_MIS_ASZ_L, validlog,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* msr[3] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* msr[4] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* msr[5] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[6] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* msr[7] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[6] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* msr[7] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
dimm_spare(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args,
    int nargs, uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[7], *misp = mis;
	int i;
	int err;
	int branch = 0;
	int channel = 0;
	int rank = 0;
	int complete = 0;
	uint8_t spcps;
	uint32_t ferr_nf_fbd;
	uint32_t nerr_nf_fbd;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;
	int is5100;
	int is5400;

	is7300 = is_7300();
	is5100 = is_5100();
	is5400 = is_5400();

	if (is5400)
		ferr_global = 1 << 11;
	else
		ferr_global = 1 << 8;

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "channel") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is5100) {
				channel = arg->mas_argval & 1;
				ferr_global = (ferr_global & ~(0x3 << 8)) |
				    (1 << (8 + channel));
				branch = channel;
			} else {
				channel = arg->mas_argval & 3;
				if (!is5400) {
					ferr_global =
					    (ferr_global & ~(0xf << 8)) |
					    (1 << (8 + channel));
				}
				if ((arg->mas_argval & 2) == 0) {
					branch = 0;
				} else {
					branch = 1;
				}
			}
		} else if (strcmp(arg->mas_argnm, "rank") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (is7300)
				rank = arg->mas_argval & 0xf;
			else
				rank = arg->mas_argval & 3;
		} else if (strcmp(arg->mas_argnm, "complete") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			complete = 1;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (is5400 || is7300) {
		if (complete) {
			spcps = 1 << 5;
			ferr_nf_fbd = (1 << 23) | (channel << 28);
			nerr_nf_fbd = 1 << 24;
		} else {
			spcps = 1 << 6;
			ferr_nf_fbd = (1 << 23) | (channel << 28);
			nerr_nf_fbd = 0;
		}
		spcps |= rank;
	} else if (is5100) {
		if (complete) {
			spcps = 1 << 5;
			ferr_nf_fbd = (1 << 20) | (channel << 28);
			nerr_nf_fbd = 1 << 21;
		} else {
			spcps = 1 << 6;
			ferr_nf_fbd = (1 << 20) | (channel << 28);
			nerr_nf_fbd = 0;
		}
		spcps |= rank;
	} else {
		if (complete) {
			spcps = 0x1;
			ferr_nf_fbd = (1 << 23) | (channel << 28);
			nerr_nf_fbd = 1 << 24;
		} else {
			spcps = 1 << 4;
			ferr_nf_fbd = (1 << 23) | (channel << 28);
			nerr_nf_fbd = 0;
		}
		spcps |= rank << 1;
	}
	/* mis[0] */
	if (branch == 0) {
		mtst_mis_init_pci_wr(misp++, 0, 21, 0,
		    is5100 || is5400 || is7300 ? 0x43 : 0x41, MTST_MIS_ASZ_B,
		    (uint32_t)spcps, MTST_MIS_FLAG_MSR_INTERPOSE);
	} else {
		mtst_mis_init_pci_wr(misp++, 0, 22, 0,
		    is5100 || is5400 || is7300 ? 0x43 : 0x41, MTST_MIS_ASZ_B,
		    (uint32_t)spcps, MTST_MIS_FLAG_MSR_INTERPOSE);
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 1, 0xa4, MTST_MIS_ASZ_L,
		    &nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 2);
		if (err != 0) {
			nerr_nf_fbd = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		if (is5100) {
			if (complete)
				nerr_nf_fbd |= (1 << 20) | (1 << 21);
			else
				nerr_nf_fbd |= (1 << 20);
		} else {
			if (complete)
				nerr_nf_fbd |= (1 << 23) | (1 << 24);
			else
				nerr_nf_fbd |= (1 << 23);
		}
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[4] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[5] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[6] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[5] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[6] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}


	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	if (complete == 0) {
		(void) sleep(15);
		misp = mis;
		if (is5100 || is5400 || is7300) {
			spcps = 1 << 5;
			spcps |= rank;
		} else {
			spcps = 1;
			spcps |= rank << 1;
		}
		if (is5100)
			ferr_nf_fbd = (1 << 21) | (channel << 28);
		else
			ferr_nf_fbd = (1 << 24) | (channel << 28);
		nerr_nf_fbd = 0;
		/* mis[0] */
		if (branch == 0) {
			mtst_mis_init_pci_wr(misp++, 0, 21, 0,
			    is5400 || is7300 ? 0x43 : 0x41,
			    MTST_MIS_ASZ_B, (uint32_t)spcps,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_pci_wr(misp++, 0, 22, 0,
			    is5400 || is7300 ? 0x43 : 0x41,
			    MTST_MIS_ASZ_B, (uint32_t)spcps,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa0,
		    MTST_MIS_ASZ_L, ferr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 1, 0xa4,
		    MTST_MIS_ASZ_L, nerr_nf_fbd, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[4] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
		case 0:
			/* mis[5] #MC if uncorrected, otherwise wake poller */
			mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[6] */
			if (status & MSR_MC_STATUS_UC)
				mtst_mis_init_int(misp++, cpi, T_MCE,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			else
				mtst_mis_init_poll(misp++, cpi,
				    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;

		case MTST_CMD_F_POLLED:
			/* Leave the poller to find this at it's next wakeup */
			break;

		case MTST_CMD_F_INT18:
			/* mis[5] Arrange a #MC exception */
			mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			/* mis[6] */
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			break;
		}


		err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
		    sizeof (mtst_inj_stmt_t));
	}
	return (err);
}

/*ARGSUSED*/
int
int_nf(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[10], *misp = mis;
	int i;
	int err;
	uint8_t ferr_nf_int = 0;
	uint8_t ferr_nf_int2 = 0;
	uint32_t nerr_nf_int;
	uint32_t nerr_nf_int2;
	uint32_t recint = 0;
	uint64_t nrecsf = 0;
	uint64_t recsf = 0;
	uint32_t ferr_global = 1 << 15;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300;
	int is5400;

	is7300 = is_7300();
	is5400 = is_5400();

	switch (cmdarg) {
	case 22:
		ferr_nf_int = 1 << 7;
		break;
	case 20:
		ferr_nf_int = 1 << 6;
		break;
	case 11:
		ferr_nf_int = 1 << 5;
		break;
	case 10:
		ferr_nf_int = 1 << 4;
		break;
	case 9:
		ferr_nf_int = 1 << 3;
		break;
	case 8:
	case 26:
		ferr_nf_int = 1 << 2;
		break;
	case 6:
		ferr_nf_int = 1 << 1;
		break;
	case 5:
		ferr_nf_int = 1;
		break;
	case 27:
		ferr_nf_int2 = 1 << 6;
		break;
	case 24:
		ferr_nf_int2 = 1 << 5;
		break;
	case 19:
		ferr_nf_int2 = 1 << 4;
		break;
	case 18:
		ferr_nf_int2 = 1 << 3;
		break;
	case 14:
	case 17:
		ferr_nf_int2 = 1 << 2;
		break;
	case 16:
		ferr_nf_int2 = 1 << 1;
		break;
	case 12:
		ferr_nf_int2 = 1;
		break;
	}
	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "recint") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recint = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecsf") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecsf = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "recsf") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			recsf = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 2, 0xc3, MTST_MIS_ASZ_B,
		    &nerr_nf_int, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is5400) {
			mtst_mis_init_pci_rd(&mis[2], 0, 16, 2, 0xc7,
			    MTST_MIS_ASZ_B, &nerr_nf_int2,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 3);
		} else {
			err = mtst_inject(mis, 2);
		}
		if (err != 0) {
			nerr_nf_int = 0;
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		nerr_nf_int |= ferr_nf_int;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc3,
		    MTST_MIS_ASZ_B, nerr_nf_int, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is5400) {
			nerr_nf_int2 |= ferr_nf_int2;
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc7,
			    MTST_MIS_ASZ_B, nerr_nf_int2,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, is5400 ? 0xcc : 0xc8,
		    MTST_MIS_ASZ_L, recint, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xb4,
		    MTST_MIS_ASZ_L, (uint32_t)(nrecsf >> 32),
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xb0, MTST_MIS_ASZ_L,
		    (uint32_t)nrecsf, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xbc,
		    MTST_MIS_ASZ_L, (uint32_t)(recsf >> 32),
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xb8, MTST_MIS_ASZ_L,
		    (uint32_t)recsf, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[5] */
		if (is5400) {
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc2,
			    MTST_MIS_ASZ_B, ferr_nf_int,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc1,
			    MTST_MIS_ASZ_B, ferr_nf_int,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		/* mis[6] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[7] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else if (is5400) {
			/* mis[7] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc3,
			    MTST_MIS_ASZ_B, ferr_nf_int2,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[8] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[9] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[8] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[9] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
int_fat(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[8], *misp = mis;
	int i;
	int err;
	uint8_t ferr_fat_int = 0;
	uint8_t ferr_fat_int2 = 0;
	uint32_t nerr_fat_int;
	uint32_t nerr_fat_int2;
	uint32_t nrecint = 0;
	uint64_t nrecsf = 0;
	uint32_t ferr_global = 1U << 31;
	uint32_t ferr_global_hi = 0;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int is7300;
	int is5400;
	int next = 0;
	uint32_t nerr_global;

	is7300 = is_7300();
	is5400 = is_5400();

	switch (cmdarg) {
	case 14:
		ferr_fat_int2 = 1 << 2;
		break;
	case 12:
		ferr_fat_int2 = 1;
		break;
	case 25:
		ferr_fat_int = 1 << 7;
		break;
	case 23:
		ferr_fat_int = 1 << 6;
		break;
	case 21:
		ferr_fat_int = 1 << 5;
		break;
	case 7:
		ferr_fat_int = 1 << 4;
		break;
	case 4:
		ferr_fat_int = 1 << 3;
		break;
	case 3:
		ferr_fat_int = 1 << 2;
		break;
	case 2:
		ferr_fat_int = 1 << 1;
		break;
	case 1:
		ferr_fat_int = 1;
		break;
	}
	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "nrecint") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecint = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "nrecsf") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			nrecsf = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		mtst_mis_init_pci_rd(&mis[1], 0, 16, 2, 0xc2, MTST_MIS_ASZ_B,
		    &nerr_fat_int, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		if (is5400) {
			mtst_mis_init_pci_rd(&mis[2], 0, 16, 2, 0xc5,
			    MTST_MIS_ASZ_B, &nerr_fat_int2,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 3);
		} else {
			err = mtst_inject(mis, 2);
		}
		if (err != 0) {
			nerr_global = 0;
			nerr_fat_int = 0;
			nerr_fat_int2 = 0;
		}
		nerr_global |= ferr_global;
		nerr_fat_int |= ferr_fat_int;
		nerr_fat_int2 |= ferr_fat_int2;
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc2, MTST_MIS_ASZ_B,
		    nerr_fat_int, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is5400) {
			/* mis[3] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc5,
			    MTST_MIS_ASZ_B, nerr_fat_int2,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* mis[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, is5400 ? 0xc8 : 0xc4,
		    MTST_MIS_ASZ_L, nrecint, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[1] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xb4,
		    MTST_MIS_ASZ_L, (uint32_t)(nrecsf >> 32),
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[2] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xb0, MTST_MIS_ASZ_L,
		    (uint32_t)nrecsf, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[3] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc0,
		    MTST_MIS_ASZ_B, ferr_fat_int, MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[5] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else if (is5400) {
			/* mis[5] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0xc1,
			    MTST_MIS_ASZ_B, ferr_fat_int2,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[6] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[7] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[6] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[7] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
io_err(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[5], *misp = mis;
	int i;
	int err;
	uint32_t pex_fat_ferr = 0;
	uint32_t pex_nf_cor_ferr = 0;
	uint32_t pex_fat_nerr = 0;
	uint32_t pex_nf_cor_nerr = 0;
	uint32_t ferr_global;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int pex = 0;
	int num = 0;
	int fatal = 0;
	int next = 0;
	int is7300;
	int is5400;

	is7300 = is_7300();
	is5400 = is_5400();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "num") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			num = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "pex") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			if (pex < 0 || ((is5400 && pex > 9) ||
			    (!is5400 && pex > 7)))
				return (0);
			pex = arg->mas_argval;
		} else if (strcmp(arg->mas_argnm, "fatal") == 0) {
			if (arg->mas_argtype != MTST_ARGTYPE_VALUE)
				return (0);
			fatal = 1;
		} else if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}
	if (fatal) {
		if (is5400) {
			switch (num) {
			case 0:
				pex_fat_ferr = 1;
				break;
			case 19:
				pex_fat_ferr = 1 << 1;
				break;
			case 4:
				pex_fat_ferr = 1 << 2;
				break;
			case 5:
				pex_fat_ferr = 1 << 3;
				break;
			case 6:
				pex_fat_ferr = 1 << 4;
				break;
			case 7:
				pex_fat_ferr = 1 << 5;
				break;
			case 8:
				pex_fat_ferr = 1 << 6;
				break;
			case 10:
				pex_fat_ferr = 1 << 7;
				break;
			case 9:
				pex_fat_ferr = 1 << 8;
				break;
			case 2:
				pex_fat_ferr = 1 << 9;
				break;
			case 1:
				pex_fat_ferr = 1 << 10;
				break;
			case 18:
				pex_fat_ferr = 1 << 12;
				break;
			case 22:
				pex_fat_ferr = 1 << 13;
				break;
			case 23:
				pex_fat_ferr = 1 << 14;
				break;
			case 24:
				pex_fat_ferr = 1 << 15;
				break;
			case 25:
				pex_fat_ferr = 1 << 16;
				break;
			case 26:
				pex_fat_ferr = 1 << 17;
				break;
			case 27:
				pex_fat_ferr = 1 << 18;
				break;
			case 29:
				pex_fat_ferr = 1 << 20;
				break;
			case 30:
				pex_fat_ferr = 1 << 21;
				break;
			case 31:
				pex_fat_ferr = 1 << 22;
				break;
			case 32:
				pex_fat_ferr = 1 << 23;
				break;
			default:
				return (0);
			}
		} else {
			switch (num) {
			case 0:
				pex_fat_ferr = 1;
				break;
			case 1:
				pex_fat_ferr = 1 << 1;
				break;
			case 2:
				pex_fat_ferr = 1 << 2;
				break;
			case 3:
				pex_fat_ferr = 1 << 3;
				break;
			case 4:
				pex_fat_ferr = 1 << 4;
				break;
			case 5:
				pex_fat_ferr = 1 << 5;
				break;
			case 6:
				pex_fat_ferr = 1 << 6;
				break;
			case 7:
				pex_fat_ferr = 1 << 7;
				break;
			case 8:
				pex_fat_ferr = 1 << 8;
				break;
			case 9:
				pex_fat_ferr = 1 << 10;
				break;
			case 10:
				pex_fat_ferr = 1 << 9;
				break;
			case 18:
				pex_fat_ferr = 1 << 11;
				break;
			case 19:
				pex_fat_ferr = 1 << 12;
				break;
			default:
				return (0);
			}
		}
		ferr_global =  1 << (16 + pex);
		/* mis[0] */
		if (next) {
			mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44,
			    MTST_MIS_ASZ_L, &nerr_global,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			mtst_mis_init_pci_rd(&mis[1], 0, pex, 0, 0x15c,
			    MTST_MIS_ASZ_L, &pex_fat_nerr,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 2);
			if (err != 0) {
				nerr_global = 0;
				pex_fat_nerr = 0;
			}
			nerr_global |= ferr_global;
			pex_fat_nerr |= pex_fat_ferr;
			mtst_mis_init_pci_wr(misp++, 0, pex, 0, 0x15c,
			    MTST_MIS_ASZ_L, pex_fat_nerr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_pci_wr(misp++, 0, pex, 0, 0x154,
			    MTST_MIS_ASZ_L, pex_fat_ferr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	} else {
		if (is5400) {
			switch (num) {
			case 12:
				pex_nf_cor_ferr = 1;
				break;
			case 13:
				pex_nf_cor_ferr = 1 << 1;
				break;
			case 14:
				pex_nf_cor_ferr = 1 << 2;
				break;
			case 15:
				pex_nf_cor_ferr = 1 << 3;
				break;
			case 16:
				pex_nf_cor_ferr = 1 << 4;
				break;
			case 20:
				pex_nf_cor_ferr = 1 << 5;
				break;
			case 0:
				pex_nf_cor_ferr = 1 << 6;
				break;
			case 19:
				pex_nf_cor_ferr = 1 << 7;
				break;
			case 4:
				pex_nf_cor_ferr = 1 << 8;
				break;
			case 5:
				pex_nf_cor_ferr = 1 << 9;
				break;
			case 6:
				pex_nf_cor_ferr = 1 << 10;
				break;
			case 7:
				pex_nf_cor_ferr = 1 << 11;
				break;
			case 8:
				pex_nf_cor_ferr = 1 << 12;
				break;
			case 10:
				pex_nf_cor_ferr = 1 << 13;
				break;
			case 9:
				pex_nf_cor_ferr = 1 << 14;
				break;
			case 2:
				pex_nf_cor_ferr = 1 << 15;
				break;
			case 17:
				pex_nf_cor_ferr = 1 << 17;
				break;
			case 11:
				pex_nf_cor_ferr = 1 << 18;
				break;
			case 23:
				pex_nf_cor_ferr = 1 << 19;
				break;
			case 24:
				pex_nf_cor_ferr = 1 << 20;
				break;
			case 25:
				pex_nf_cor_ferr = 1 << 21;
				break;
			case 26:
				pex_nf_cor_ferr = 1 << 22;
				break;
			case 27:
				pex_nf_cor_ferr = 1 << 23;
				break;
			case 28:
				pex_nf_cor_ferr = 1 << 24;
				break;
			case 29:
				pex_nf_cor_ferr = 1 << 25;
				break;
			case 30:
				pex_nf_cor_ferr = 1 << 26;
				break;
			case 31:
				pex_nf_cor_ferr = 1 << 27;
				break;
			case 32:
				pex_nf_cor_ferr = 1 << 28;
				break;
			case 33:
				pex_nf_cor_ferr = 1 << 29;
				break;
			default:
				break;
			}
		} else {
			switch (num) {
			case 0:
				pex_nf_cor_ferr = 1;
				break;
			case 2:
				pex_nf_cor_ferr = 1 << 1;
				break;
			case 3:
				pex_nf_cor_ferr = 1 << 2;
				break;
			case 4:
				pex_nf_cor_ferr = 1 << 3;
				break;
			case 5:
				pex_nf_cor_ferr = 1 << 4;
				break;
			case 6:
				pex_nf_cor_ferr = 1 << 5;
				break;
			case 7:
				pex_nf_cor_ferr = 1 << 6;
				break;
			case 8:
				pex_nf_cor_ferr = 1 << 7;
				break;
			case 9:
				pex_nf_cor_ferr = 1 << 8;
				break;
			case 10:
				pex_nf_cor_ferr = 1 << 9;
				break;
			case 11:
				pex_nf_cor_ferr = 1 << 10;
				break;
			case 12:
				pex_nf_cor_ferr = 1 << 11;
				break;
			case 13:
				pex_nf_cor_ferr = 1 << 12;
				break;
			case 14:
				pex_nf_cor_ferr = 1 << 13;
				break;
			case 15:
				pex_nf_cor_ferr = 1 << 14;
				break;
			case 16:
				pex_nf_cor_ferr = 1 << 15;
				break;
			case 17:
				pex_nf_cor_ferr = 1 << 16;
				break;
			case 19:
				pex_nf_cor_ferr = 1 << 17;
				break;
			default:
				break;
			}
		}
		ferr_global =  1 << pex;
		/* mis[0] */
		if (next) {
			mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44,
			    MTST_MIS_ASZ_L, &nerr_global,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			mtst_mis_init_pci_rd(&mis[1], 0, pex, 0, 0x160,
			    MTST_MIS_ASZ_L, &pex_nf_cor_nerr,
			    MTST_MIS_FLAG_MSR_INTERPOSEOK);
			err = mtst_inject(mis, 2);
			if (err != 0) {
				nerr_global = 0;
				pex_fat_nerr = 0;
			}
			nerr_global |= ferr_global;
			pex_nf_cor_nerr |= pex_nf_cor_ferr;
			mtst_mis_init_pci_wr(misp++, 0, pex, 0, 0x160,
			    MTST_MIS_ASZ_L, pex_nf_cor_nerr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		} else {
			mtst_mis_init_pci_wr(misp++, 0, pex, 0, 0x158,
			    MTST_MIS_ASZ_L, pex_nf_cor_ferr,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	/* mis[1] */
	if (next) {
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* mis[2] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
			}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[3] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[3] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* mis[4] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

/*ARGSUSED*/
int
nb_dma(mtst_cpuid_t *cpi, uint_t flags, const mtst_argspec_t *args, int nargs,
    uint64_t cmdarg)
{
	mtst_inj_stmt_t mis[4], *misp = mis;
	int i;
	int err;
	uint32_t ferr_global = 1 << 30;
	uint32_t ferr_global_hi = 0;
	uint32_t nerr_global;
	uint64_t status = MSR_MC_STATUS_VAL | 3;
	uint_t statmsr = IA32_MSR_MC(3, STATUS);
	int next = 0;
	int is7300 = is_7300();

	for (i = 0; i < nargs; i++) {
		const mtst_argspec_t *arg = &args[i];
		if (strcmp(arg->mas_argnm, "next") == 0) {
			next = 1;
		}
	}

	if (next) {
		mtst_mis_init_pci_rd(&mis[0], 0, 16, 2, 0x44, MTST_MIS_ASZ_L,
		    &nerr_global, MTST_MIS_FLAG_MSR_INTERPOSEOK);
		err = mtst_inject(mis, 1);
		if (err != 0) {
			nerr_global = 0;
		}
		nerr_global |= ferr_global;
		/* msr[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x44,
		    MTST_MIS_ASZ_L, nerr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if ((flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) == 0)
			flags |= MTST_CMD_F_POLLED;
	} else {
		/* msr[0] */
		mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x40,
		    MTST_MIS_ASZ_L, ferr_global, MTST_MIS_FLAG_MSR_INTERPOSE);
		if (is7300) {
			/* msr[1] */
			mtst_mis_init_pci_wr(misp++, 0, 16, 2, 0x48,
			    MTST_MIS_ASZ_L, ferr_global_hi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		}
	}
	switch (flags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED)) {
	case 0:
		/* mis[2] #MC if uncorrected, otherwise wake poller */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* msr[3] */
		if (status & MSR_MC_STATUS_UC)
			mtst_mis_init_int(misp++, cpi, T_MCE,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		else
			mtst_mis_init_poll(misp++, cpi,
			    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;

	case MTST_CMD_F_POLLED:
		/* Leave the poller to find this at it's next wakeup */
		break;

	case MTST_CMD_F_INT18:
		/* mis[2] Arrange a #MC exception */
		mtst_mis_init_msr_wr(misp++, cpi, statmsr, status,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		/* msr[3] */
		mtst_mis_init_int(misp++, cpi, T_MCE,
		    MTST_MIS_FLAG_MSR_INTERPOSE);
		break;
	}

	err = mtst_inject(mis, ((uintptr_t)misp - (uintptr_t)mis) /
	    sizeof (mtst_inj_stmt_t));
	return (err);
}

static const mtst_cmd_t icpu_cmds[] = {
	{ "cpu_mca_raw", "bank,status,addr,misc,uc,overflow,pcc,fatal,mcacod,"
	    "mscod,other,corrected,threshold,rsvd,s,ar,en", intel_cpu_raw, 0,
	    "cpu mca" },
	{ "cpu_unc", "bank,uc,overflow,pcc,fatal,s,ar",
	    intel_cpu_unclassified, 0, "MCA Unclassified" },
	{ "cpu_mrpe", "bank,uc,overflow,pcc,fatal,s,ar", intel_cpu_mrpe, 0,
	    "MCA Microcode ROM Parity" },
	{ "cpu_ext", "bank,uc,overflow,pcc,addr,misc,fatal,s,ar",
	    intel_cpu_ext, 0, "MCA cpu external error" },
	{ "cpu_frc", "bank,uc,overflow,pcc,fatal,s,ar", intel_cpu_frc, 0,
	    "MCA cpu FRC error" },
	{ "cpu_it", "bank,uc,overflow,pcc,addr,misc,fatal,s,ar", intel_cpu_it,
	    0, "MCA cpu internal timer error" },
	{ "cpu_iunc", "bank,uc,overflow,pcc,bits,fatal,s,ar", intel_cpu_iunc,
	    0, "MCA cpu internal unclassified" },
	{ "cpu_gmh", "bank,uc,overflow,pcc,f,ll,l0,l1,l2,lg,fatal,s,ar",
	    intel_cpu_gmh, 0, "MCA cpu Generic memory hierarchy error" },
	{ "cpu_tlb", "bank,uc,overflow,pcc,f,ll,l0,l1,l2,lg,tt,instruction,"
	    "data,generic,fatal,s,ar", intel_cpu_tlb, 0, "MCA cpu tlb error" },
	{ "cpu_mh", "bank,uc,overflow,pcc,f,ll,l0,l1,l2,lg,"
	    "tt,instruction,data,generic,"
	    "rrrr,err,rd,wr,drd,dwr,ird,prefetch,wvict,snoop,fatal,s,ar",
	    intel_cpu_mh, 0, "MCA cpu Memory Hierarchy error" },
	{ "cpu_bus", "bank,uc,overflow,pcc,f,ll,l0,l1,l2,lg,"
	    "tt,instruction,data,generic,"
	    "rrrr,err,rd,wr,drd,dwr,ird,prefetch,wvict,snoop,pp,src,res,obs,"
	    "t,timeout,notimeout,ii,m,mem,io,fatal,s,ar,allcpu",
	    intel_cpu_bus, 0, "MCA cpu bus and interconnect error" },
	{ "cmci", "bank,overflow,count,tbes,code", intel_cmci, 0,
	    "CMCI error" },
	{ "memory", "bank,ecc_read,ecc_scrub,parity,redundant,spare,"
	    "out_of_range,invalid_id,address_parity,byte_parity,cmci,error,"
	    "read,write,command,channel,addr,rtid,dimm,syndrome,misc,uc,"
	    "overflow,pcc,fatal", intel_memory, 0, "MCA memory" },
	{ "memoryex", "bank,error,addr,misc,cmci,fatal,ll,tt,rrrr,lnktrns,"
	    "lnkpers,lnkuncorr,sbfbdlnkerr,nbfbdlnkerr,lnkcrcvld,ptrl_fsm_err,"
	    "errflw_fsm_fail,mcpar,vberr,fberr,memeccerr,fbdfrmpar,"
	    "failover_to_mirror,poison,ar,s,pcc,uc,overflow,mem_scrub,"
	    "ep_syndrome,crc_syndrome,rnk_0_err_cnt,rnk_1_err_cnt,"
	    "rnk_2_err_cnt,rnk_3_err_cnt,mem_err_log_addr,channel",
	    intel_memory_ex, 0, "MCA EX memory" },
	{ "quickpath", "bank,header_parity,data_parity,retries_exceeded,poison,"
	    "unsupported_msg,unsupported_credit,buffer_overrun,response_status,"
	    "clock_jitter,error,uc,overflow,pcc,f,ll,l0,l1,l2,lg,"
	    "tt,instruction,data,generic,"
	    "rrrr,err,rd,wr,drd,dwr,ird,prefetch,wvict,snoop,pp,src,res,obs,"
	    "t,timeout,notimeout,ii,m,mem,io,fatal,s,ar",
	    intel_quickpath, 0, "MCA quickpath" },
	{ "qprouter", "bank,addr,misc,ll,ii,rrrr,t,pp,poison,corr_err_cnt,"
	    "ar,s,pcc,uc,overflow,error,opr_ecc,ipr_crc,opr_retry_abort,"
	    "ipr_link_init,opr_poison,eot_parity,rta_parity,ipr_bad_route,"
	    "ipr_bad_msg,ipr_bad_credit,opr_hdr_ecc,opr_link_retry,qpi_port",
	    intel_qprouter, 0, "quickpath router" },
	{ "bicache", "bank,addr,misc,ll,ii,rrrr,t,pp,poison,corr_err_cnt,"
	    "ar,s,pcc,uc,overflow,error,err_src_summary,err_cbox_top,"
	    "err_cbox_bot,err_bbox,err_sbox,err_rbox,mscod",
	    intel_bicache, 0, "bridge interconnect cache" },
	{ "homeagent", "bank,addr,misc,ll,ii,rrrr,t,pp,poison,corr_err_cnt,"
	    "ar,s,pcc,uc,overflow,error,ha_cod",
	    intel_homeagent, 0, "home agent" },
	{ "sysconf", "bank,addr,misc,ll,ii,rrrr,t,pp,corr_err_cnt,"
	    "ar,s,pcc,uc,overflow,error,cfa_err_fatal,err_pin1_fatal,"
	    "credit_fatal,pbox_fatal,main_timeout_fatal,ill_op_fatal,"
	    "poison_fatal,err_pin0_unc,response_fail_unc,"
	    "scratch_reg_parity_unc,misalign_unc,rsvd_chk_unc,cfa_ecc_cor",
	    intel_sysconf, 0, "system configuration controller" },
	{ "llccoher", "bank,addr,misc,ll,ii,rrrr,t,pp,poison,ar,s,pcc,uc,"
	    "overflow,error", intel_llccoher, 0, "LLC Coherence Engine" },
	{ "fsb_parity", "fsb,addr,nrefsb,next",
	    fsb_parity, 0, "Parity error on FSB (F1)" },
	{ "fsb_unsup", "fsb,nrefsb,next",
	    fsb_unsup, 0, "Unsupported transaction on FSB (F2)" },
	{ "fsb_data_parity", "fsb,recfsb,next",
	    fsb_dparity, 0, "Data Parity on FSB (F6)" },
	{ "fsb_mcerr", "fsb,next",
	    fsb_mcerr, 0, "Detected MCERR on FSB (F7)" },
	{ "fsb_bint", "fsb,next",
	    fsb_bint, 0, "B-INIT from cpu on FSB (F8)" },
	{ "fsb_protocol", "fsb,addr,nrefsb,next",
	    fsb_protocol, 0, "protocol error on FSB (F9)" },
	{ "dimm_to", "nrecmema,nrecmemb,nrecfglog,bank,rdwr,rank,rec_bufid,"
	    "cas,ras,be,reg,function,cfg_bufid,channel,next",
	    dimm_to, 0, "Alert on fbdimm replay or Fast reset time out - M1" },
	{ "dimm_ue_crc", "nrecmema,nrecmemb,nrecfglog,bank,rdwr,rank,rec_bufid,"
	    "nrecfbda,nrecfbdb,nrecfbdc,nrecfbdd,nrecfbde,nrecfbdf,"
	    "cas,ras,be,reg,function,cfg_bufid,channel,next", dimm_ue_crc, 0,
	    "northbound CRC error on memory channel replay - M2" },
	{ "thermal", "fatal,errno,next", dimm_thermal, 0,
	    "5400 thermal error" },
	{ "mem_thermal", "channel,next", dimm_thermal, 0,
	    "Tmid thermal event with intelligent throttling disabled - M3" },
	{ "dimm_data_ecc_replay",
	    "channel,bank,rank,rdwr,cas,ras,rec_bufid,next", dimm_m4, 0,
	    "Uncorrectable data ECC error on fb-dimm/ddr2 replay - M4/M1" },
	{ "dimm_ue_ecc", "recmema,recmemb,redmema,redmemb,bank,rdwr,rank,"
	    "rec_bufid,cas,ras,be,reg,function,cfg_bufid,channel,mirror,spare,"
	    "patrol,aliased,non_aliased,next",
	    dimm_ue_ecc, 0, "Uncorrectable demand data ecc error - M5-M12" },
	{ "dimm_ma", "recfglog,channel,be,reg,rdwr,func,cfg_bufid,next",
	    dimm_ma, 0, "Non-retry fb-dimm configuration alert - M14" },
	{ "dimm_crc", "recfbda,recfbdb,recfbdc,recfbdd,recfbde,recfbdf,channel,"
	    "next", dimm_crc, 0,
	    "northbound CRC error on memory channel replay - M15" },
	{ "dimm_write", "recfglog,channel,be,reg,rdwr,func,cfg_bufid,next",
	    dimm_ma, 1,
	    "memory write error - corrupt ack at first attempt - M13" },
	{ "dimm_ce_ecc", "recmema,recmemb,redmema,redmemb,bank,"
	    "rdwr,rank,rec_bufid,cas,ras,be,reg,function,cfg_bufid,channel,"
	    "mirror,spare,patrol,next",
	    dimm_ce_ecc, 0, "correctable demand data ecc error - M17-M20" },
	{ "dimm_crc_sync",
	    "channel,recfbda,recfbdb,recfbdc,recfbdd,recfbde,recfbdf,next",
	    dimm_crc_sync, 0,
	    "fb-dimm northbound crc error on a sync status - M21" },
	{ "spd_protocol", "channel,next",
	    spd_protocol, 0, "SPD interface error - M22" },
	{ "dimm_spare", "channel,rank,complete,next",
	    dimm_spare, 0, "SPD interface error - M27-M28" },
	{ "dimm_frto", "channel,next",
	    dimm_frto, 0, "non redundant fast reset timeout - M23" },
	{ "nb_int_vp", "nrecint,next", int_fat, 4,
	    "Virtual pin error - B4" },
	{ "nb_int_cv", "nrecint,nrecsf,next", int_fat, 3,
	    "Coherency Violation Error for EWB - B3" },
	{ "nb_int_tag", "nrecsf,next", int_fat, 2,
	    "Multi tag hit SF - B2" },
	{ "nb_int_dmp", "nrecint,next", int_fat, 1,
	    "DM parity error - B1" },
	{ "nb_int_ecc_ue", "nrecsf,next", int_fat, 7,
	    "multiple ECC error in any ways during SF lookup - B7" },
	{ "nb_int_sf_ce", "recint,nrecsf,next", int_nf, 8,
	    "SF coherency error for BIL - B8" },
	{ "nb_int_ecc_ce", "recsf,next", int_nf, 6,
	    "single ECC error on SF lookup - B6" },
	{ "nb_int_map", "recint,next", int_nf, 5,
	    "address amp error - B5" },
	{ "nb_int_ill", "recint,next", int_nf, 9,
	    "illegal Access Error - B9" },
	{ "nb_int_parity", "recsf,next", int_nf, 10,
	    "DM Parity Error - B10" },
	{ "nb_int_victim", "recsf,next", int_nf, 11,
	    "Victim RAM paroty Error - B11" },
	{ "nb_int_scrub", "next", int_fat, 14,
	    "Scrub DBE - B14" },
	{ "nb_int_smbus", "next", int_nf, 16,
	    "SMBus Error Status - B16" },
	{ "nb_int_jtag", "next", int_nf, 17,
	    "JTAG/TAP Error Status - B17" },
	{ "nb_int_perfmon", "next", int_nf, 18,
	    "PerfMon Task Completion - B18" },
	{ "nb_int_scrub_sbe", "next", int_nf, 19,
	    "Scrub SBE - B19" },
	{ "nb_int_write_abort", "recint,next", int_nf, 20,
	    "Configuration Write Abort - B20" },
	{ "nb_int_ill_way", "next", int_fat, 21,
	    "Illegal Way - B21" },
	{ "nb_int_vrom", "recsf,next", int_nf, 22,
	    "Victim ROM parity error - B22" },
	{ "nb_int_vt_port", "nrecint,next", int_fat, 23,
	    "Vt Unaffiliated Port Error (IOG) - B23" },
	{ "nb_int_hisimm", "next", int_fat, 25,
	    "HiSIMM/TSEG - B25" },
	{ "nb_int_s1", "next", int_nf, 27,
	    "Break from S1 (CE) - B27" },
	{ "nb_int_ppr", "next", int_fat, 12,
	    "Parity Protection Register - B12" },
	{ "io_err", "pex,num,fatal,next", io_err, 0,
	    "pex error IO0-IO19" },
	{ "nb_dma", "next", nb_dma, 0, "DMA engine error" },
	NULL
};

static const mtst_cpumod_ops_t icpu_cpumod_ops = {
	icpu_fini
};

static const mtst_cpumod_t icpu_cpumod = {
	MTST_CPUMOD_VERSION,
	"GenuineIntel",
	&icpu_cpumod_ops,
	icpu_cmds
};

const mtst_cpumod_t *
_mtst_cpumod_init(void)
{
	return (&icpu_cpumod);
}
