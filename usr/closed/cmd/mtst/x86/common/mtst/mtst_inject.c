/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Prepares statements for transfer to the memtest driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <umem.h>
#include <strings.h>
#include <sys/memtest.h>
#include <errno.h>
#include <sys/processor.h>

#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst_inject.h>
#include <mtst_memtest.h>
#include <mtst_cpu.h>
#include <mtst_cpumod_api.h>
#include <mtst.h>

static void
mtst_inject_dump_target(int idx, mtst_inj_stmt_t *mis)
{
	mtst_dprintf("  %2d: chip/core/strand %d/%d/%d ", idx,
	    mis->mis_target.mci_hwchipid,
	    mis->mis_target.mci_hwcoreid,
	    mis->mis_target.mci_hwstrandid);

	if (mis->mis_target.mci_cpuid != -1)
		mtst_dprintf("cpuid %d ", mis->mis_target.mci_cpuid);
}

static void
mtst_inject_dump_one(int idx, mtst_inj_stmt_t *mis)
{

	switch (mis->mis_type) {
	case MTST_INJ_STMT_MSR_RD:
		mtst_inject_dump_target(idx, mis);
		mtst_dprintf("%3d READ MSR 0x%08lx\n", mis->mis_msrnum);
		break;

	case MTST_INJ_STMT_MSR_WR:
		mtst_inject_dump_target(idx, mis);
		mtst_dprintf("WRITE MSR 0x%08lx <- 0x%016llx\n",
		    mis->mis_msrnum, (u_longlong_t)mis->mis_msrval);
		break;

	case MTST_INJ_STMT_PCICFG_RD:
	case MTST_INJ_STMT_PCICFG_WR: {
		int wr = (mis->mis_type == MTST_INJ_STMT_PCICFG_WR);

		if (wr) {
			mtst_dprintf("  %2d: PCICFG WRITE "
			    "bus %02u device %02u function %x offset %02x"
			    " <- 0x%x\n", idx,
			    mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg,
			    mis->mis_pcival);
		} else {
			mtst_dprintf("  %2d: PCICFG READ "
			    "bus %02u device %02u function %x offset %02x\n",
			    idx,
			    mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg);
		}
		break;
	}

	case MTST_INJ_STMT_INT:
		mtst_inject_dump_target(idx, mis);
		mtst_dprintf("INT 0x%02x\n", mis->mis_int);
		break;

	case MTST_INJ_STMT_POLL:
		mtst_inject_dump_target(idx, mis);
		mtst_dprintf("POLL\n");
		break;

	default:
		mtst_dprintf("  %2d: Unknown type %u\n", idx, mis->mis_type);
	}
}

static void
mtst_inject_dump(mtst_inj_stmt_t *mis, uint_t n)
{
	int i;

	for (i = 0; i < n; i++)
		mtst_inject_dump_one(i, &mis[i]);
}

static int
mtst_count_online_cpus(void)
{
	int maxcpu = sysconf(_SC_CPUID_MAX);
	int id, count = 0;

	for (id = 0; id <= maxcpu; id++) {
		processor_info_t info;
		if (processor_info(id, &info) == 0 &&
		    info.pi_state == P_ONLINE)
			++count;
	}

	return (count);
}

int
mtst_inject_sequence(mtst_inj_stmt_t *mis, uint_t n)
{
	memtest_inject_t *stmts;
	mtst_inj_stmt_t *ins_stmt;
	size_t stmtsz;
	int rc, i, num_cpus;
	uint64_t j;
	uint_t num_stmts;
	int maxcpu = sysconf(_SC_CPUID_MAX);
	processor_info_t info;
	mtst_cpu_info_t *cpu_info;

	mtst_dprintf("injecting %d statements\n", n);

	if (mtst.mtst_flags & MTST_F_DEBUG)
		mtst_inject_dump(mis, n);

	num_cpus = mtst_count_online_cpus();
	num_stmts = n;
	for (i = 0; i < n; i++) {
		if ((mis[i].mis_flags & MTST_MIS_FLAG_ALLCPU) != 0)
			num_stmts += num_cpus - 1;
	}

	stmtsz = sizeof (memtest_inject_t) +
	    sizeof (memtest_inj_stmt_t) * (num_stmts - 1);
	stmts = umem_zalloc(stmtsz, UMEM_NOFAIL);
	stmts->mi_nstmts = num_stmts;

	ins_stmt = (mtst_inj_stmt_t *)stmts->mi_stmts;
	for (i = 0; i < n; i++) {
		bcopy(&mis[i], ins_stmt, sizeof (memtest_inj_stmt_t));
		++ins_stmt;
		if ((mis[i].mis_flags & MTST_MIS_FLAG_ALLCPU) == 0)
			continue;
		for (j = 0; j < maxcpu; ++j) {
			if (processor_info(j, &info) != 0 ||
			    info.pi_state != P_ONLINE)
				continue;
			/* if this cpu is the original target, skip */
			if (mis[i].mis_target.mci_cpuid == j)
				continue;
			bcopy(&mis[i], ins_stmt, sizeof (memtest_inj_stmt_t));
			cpu_info = mtst_cpuinfo_read_logicalid(j);
			if (cpu_info == NULL) {
				mtst_dprintf("Unable to read cpu info for %d\n",
				    j);
				errno = ENXIO;
				return (-1);
			}
			bcopy(cpu_info, &(ins_stmt->mis_target),
			    sizeof (mtst_cpuid_t));
			ins_stmt++;
		}
	}

	rc = mtst_memtest_ioctl(MEMTESTIOC_INJECT, stmts);

	umem_free(stmts, stmtsz);

	return (rc);
}
