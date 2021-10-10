/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>

#include <mtst_cmd.h>
#include <mtst_cpumod_api.h>
#include <mtst_cpu.h>
#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst_inject.h>
#include <mtst_mem.h>
#include <mtst.h>

#define	MTST_FILL_STMT_CMN(mis, cpi, type, flags) \
	(mis)->mis_target.mci_hwchipid = (cpi)->mci_hwchipid; \
	(mis)->mis_target.mci_hwcoreid = (cpi)->mci_hwcoreid; \
	(mis)->mis_target.mci_hwstrandid = (cpi)->mci_hwstrandid; \
	(mis)->mis_target.mci_cpuid = (cpi)->mci_cpuid; \
	(mis)->mis_target.mci_hwprocnodeid = (cpi)->mci_hwprocnodeid; \
	(mis)->mis_target.mci_procnodes_per_pkg = \
	    (cpi)->mci_procnodes_per_pkg; \
	(mis)->mis_type = type; \
	(mis)->mis_flags = flags

void
mtst_mis_init_msr_rd(mtst_inj_stmt_t *mis, mtst_cpuid_t *cpi, uint32_t num,
    uint64_t *dest, uint_t flags)
{
	MTST_FILL_STMT_CMN(mis, cpi, MTST_INJ_STMT_MSR_RD, flags);
	mis->mis_msrnum = num;
	mis->mis_msrdest = (uint32_t)dest;
}

void
mtst_mis_init_msr_wr(mtst_inj_stmt_t *mis, mtst_cpuid_t *cpi, uint32_t num,
    uint64_t val, uint_t flags)
{
	MTST_FILL_STMT_CMN(mis, cpi, MTST_INJ_STMT_MSR_WR, flags);
	mis->mis_msrnum = num;
	mis->mis_msrval = val;
}

static void
mtst_mis_fill_pciaddr(mtst_inj_stmt_t *mis, uint_t bus, uint_t dev,
    uint_t func, uint_t reg, int asz)
{
	mis->mis_pcibus = bus;
	mis->mis_pcidev = dev;
	mis->mis_pcifunc = func;
	mis->mis_pcireg = reg;
	mis->mis_asz = asz;
}

void
mtst_mis_init_pci_rd(mtst_inj_stmt_t *mis, uint_t bus, uint_t dev,
    uint_t func, uint_t reg, int asz, uint32_t *dest, uint_t flags)
{
	mis->mis_type = MTST_INJ_STMT_PCICFG_RD;
	mtst_mis_fill_pciaddr(mis, bus, dev, func, reg, asz);
	mis->mis_flags = flags;
	mis->mis_pcidest = (uint32_t)dest;
}

void
mtst_mis_init_pci_wr(mtst_inj_stmt_t *mis, uint_t bus, uint_t dev,
    uint_t func, uint_t reg, int asz, uint32_t val, uint_t flags)
{
	mis->mis_type = MTST_INJ_STMT_PCICFG_WR;
	mtst_mis_fill_pciaddr(mis, bus, dev, func, reg, asz);
	mis->mis_flags = flags;
	mis->mis_pcival = val;
}

void
mtst_mis_init_int(mtst_inj_stmt_t *mis, mtst_cpuid_t *cpi, uint_t intno,
    uint_t flags)
{
	if ((intno & MTST_MIS_INT_MASK) != intno)
		mtst_die("illegal interrupt number 0x#x\n", intno);

	MTST_FILL_STMT_CMN(mis, cpi, MTST_INJ_STMT_INT, flags);
	mis->mis_int = intno;
}

void
mtst_mis_init_poll(mtst_inj_stmt_t *mis, mtst_cpuid_t *cpi, uint_t flags)
{
	MTST_FILL_STMT_CMN(mis, cpi, MTST_INJ_STMT_POLL, flags);
}

int
mtst_inject(mtst_inj_stmt_t *mis, uint_t num)
{
	int i;

	if (num == 0)
		mtst_die("injection without data\n");

	for (i = 0; i < num; i++) {
		switch (mis[i].mis_type) {
		case MTST_INJ_STMT_MSR_RD:
		case MTST_INJ_STMT_MSR_WR:
		case MTST_INJ_STMT_PCICFG_RD:
		case MTST_INJ_STMT_PCICFG_WR:
		case MTST_INJ_STMT_INT:
		case MTST_INJ_STMT_POLL:
			break;
		default:
			mtst_die("invalid injection statement type %u\n",
			    mis[i].mis_type);
		}
	}

	return (mtst_inject_sequence(mis, num));
}

/*
 * cpuidp needs to change to chip/core/strand, although it s used is
 * currently unsuppored in the memtest driver (must be -1).
 */
int
mtst_mem_reserve(uint_t apitype, int *cpuidp, size_t *sizep, uint64_t *vap,
    uint64_t *pap)
{
	int type;

	if (apitype == MTST_MEM_RESERVE_USER)
		type = MTST_MEM_RSRV_USER;
	else if (apitype == MTST_MEM_RESERVE_KERNEL)
		type = MTST_MEM_RSRV_KERNEL;
	else
		mtst_die("unknown reservation type %u\n", type);

	return (mtst_mem_rsrv(type, cpuidp, sizep, vap, pap));
}

int
mtst_mem_unreserve(int id)
{
	return (mtst_mem_unrsrv(id));
}

void
mtst_cmd_warn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	mtst_vwarn(format, ap);
	va_end(ap);
}

void
mtst_cmd_dprintf(const char *format, ...)
{
	va_list ap;

	ASSERT(mtst.mtst_curcmd != NULL);

	if (!(mtst.mtst_cmdflags & MTST_CMD_F_VERBOSE))
		return;

	va_start(ap, format);
	mtst_vdprintf(mtst.mtst_curcmd->mcmd_cmdname, format, ap);
	va_end(ap);
}
