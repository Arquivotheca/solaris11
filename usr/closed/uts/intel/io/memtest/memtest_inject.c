/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/archsystm.h>
#include <sys/trap.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci_cfgspace.h>
#include <sys/x86_archext.h>
#include <sys/ontrap.h>
#include <sys/cpu_module.h>
#include <sys/memtest.h>

#include "memtest_impl.h"

static cmi_hdl_t
memtest_get_hdl(memtest_inj_stmt_t *mis)
{
	cmi_hdl_t hdl;

	memtest_dprintf("Looking up handle for chip %d core %d strand %d\n",
	    mis->mis_target.mci_hwchipid, mis->mis_target.mci_hwcoreid,
	    mis->mis_target.mci_hwstrandid);

	/* cmi_hdl_lookup holds any handle it finds - caller to release */
	if ((hdl = cmi_hdl_lookup(CMI_HDL_NATIVE, mis->mis_target.mci_hwchipid,
	    mis->mis_target.mci_hwcoreid, mis->mis_target.mci_hwstrandid)) ==
	    NULL)
		memtest_dprintf("No handle found\n");

	return (hdl);
}

static int
memtest_inj_msr_rd(memtest_inj_stmt_t *mis, boolean_t dryrun)
{
	uintptr_t udst = mis->mis_msrdest;
	cmi_hdl_t hdl;
	uint64_t val;
	int err;

	memtest_dprintf("MSR READ of MSR 0x%x\n", mis->mis_msrnum);

	if ((hdl = memtest_get_hdl(mis)) == NULL) {
		return (ENXIO);
	} else if (dryrun) {
		cmi_hdl_rele(hdl);
		return (0);
	}

	if ((err = cmi_hdl_rdmsr(hdl, mis->mis_msrnum, &val)) != CMI_SUCCESS) {
		cmi_hdl_rele(hdl);
		memtest_dprintf("cmi_hdl_rdmsr failed: error %d\n", err);
		return (EINVAL);
	}

	err = ddi_copyout(&val, (void *)udst, sizeof (val), 0);

	cmi_hdl_rele(hdl);

	return (err == 0 ? 0 : EFAULT);
}

static int
memtest_inj_msr_wr(memtest_inj_stmt_t *mis, boolean_t dryrun)
{
	cmi_mca_regs_t reg;
	cmi_hdl_t hdl;
	uint_t flags = mis->mis_flags;
	int rc;

	memtest_dprintf("MSR WRITE (%s) of MSR 0x%x to value 0x%llx\n",
	    flags & MEMTEST_INJ_FLAG_INTERPOSE ? "interposed" :
	    (flags & MEMTEST_INJ_FLAG_INTERPOSEOK ?
	    "hardware, interpose fallback" :
	    "hardware, no fallback to interpose"),
	    mis->mis_msrnum, mis->mis_msrval);

	reg.cmr_msrnum = mis->mis_msrnum;
	reg.cmr_msrval = mis->mis_msrval;

	if ((hdl = memtest_get_hdl(mis)) == NULL) {
		return (ENXIO);
	} else if (dryrun) {
		cmi_hdl_rele(hdl);
		return (0);
	}

	/*
	 * Unless we have been told to prefer interposing, call
	 * cmi_hdl_msrinject to attempt to write to the MSR.  If the
	 * force flag is indicated then the cpu module code may try a
	 * direct write to the MSR if no model-specific support exists
	 * but this can (and often will) fail.
	 */
	if (!(flags & MEMTEST_INJ_FLAG_INTERPOSE)) {
		rc = cmi_hdl_msrinject(hdl, &reg, 1,
		    flags & MEMTEST_INJ_FLAG_MSR_FORCE);

		if (rc == CMI_SUCCESS) {
			memtest_dprintf("MSR WRITE succeeded\n");
			cmi_hdl_rele(hdl);
			return (0);
		}

		if (!(flags & MEMTEST_INJ_FLAG_INTERPOSEOK)) {
			memtest_dprintf("MSR WRITE failed\n");
			cmi_hdl_rele(hdl);
			return (EINVAL);
		}

		memtest_dprintf("MSR WRITE falling back to interposition\n");
	}

	/*
	 * Request interposition, either because the user asked for it,
	 * or after a failed MSR write attempt for which a fallback of
	 * interposing is acceptable.
	 */
	memtest_dprintf("MSR WRITE interposing value 0x%llx for MSR 0x%x\n",
	    reg.cmr_msrval, reg.cmr_msrnum);
	cmi_hdl_msrinterpose(hdl, &reg, 1);

	cmi_hdl_rele(hdl);

	return (0);
}

static int
memtest_inj_pcicfg_rd(memtest_inj_stmt_t *mis, boolean_t dryrun)
{
	uint32_t regval;
	uintptr_t udst = mis->mis_pcidest;
	int err;

	memtest_dprintf("PCICFG READ bus %d dev %d function %d offset 0x%x\n",
	    mis->mis_pcibus, mis->mis_pcidev,
	    mis->mis_pcifunc, mis->mis_pcireg);

	if (dryrun)
		return (0);

	switch (mis->mis_asz) {
	case MEMTEST_INJ_ASZ_B:
		regval = cmi_pci_getb(mis->mis_pcibus, mis->mis_pcidev,
		    mis->mis_pcifunc, mis->mis_pcireg, 0, 0);
		break;

	case MEMTEST_INJ_ASZ_W:
		regval = cmi_pci_getw(mis->mis_pcibus, mis->mis_pcidev,
		    mis->mis_pcifunc, mis->mis_pcireg, 0, 0);
		break;

	case MEMTEST_INJ_ASZ_L:
		regval = cmi_pci_getl(mis->mis_pcibus, mis->mis_pcidev,
		    mis->mis_pcifunc, mis->mis_pcireg, 0, 0);
		break;

	default:
		return (EINVAL);
	}

	err = ddi_copyout(&regval, (void *)udst, sizeof (regval), 0);

	return (err == 0 ? 0 : EFAULT);
}

static int
memtest_inj_pcicfg_wr(memtest_inj_stmt_t *mis, boolean_t dryrun)
{
	uint_t flags = mis->mis_flags;

	memtest_dprintf("PCICFG WRITE (%s) value 0x%x to bus %d dev %d "
	    "function %d offset 0x%x\n",
	    flags & MEMTEST_INJ_FLAG_INTERPOSE ? "interposed" : "to hardware",
	    mis->mis_pcival,
	    mis->mis_pcibus, mis->mis_pcidev,
	    mis->mis_pcifunc, mis->mis_pcireg);

	if (dryrun)
		return (0);

	if ((flags & MEMTEST_INJ_FLAG_INTERPOSE)) {
		switch (mis->mis_asz) {
		case MEMTEST_INJ_ASZ_B:
			cmi_pci_interposeb(mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg, mis->mis_pcival);
			break;

		case MEMTEST_INJ_ASZ_W:
			cmi_pci_interposew(mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg, mis->mis_pcival);
			break;

		case MEMTEST_INJ_ASZ_L:
			cmi_pci_interposel(mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg, mis->mis_pcival);
			break;

		default:
			return (EINVAL);
		}
	} else {
		switch (mis->mis_asz) {
		case MEMTEST_INJ_ASZ_B:
			cmi_pci_putb(mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg, 0,
			    mis->mis_pcival);
			break;
		case MEMTEST_INJ_ASZ_W:
			cmi_pci_putw(mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg, 0,
			    mis->mis_pcival);
			break;
		case MEMTEST_INJ_ASZ_L:
			cmi_pci_putl(mis->mis_pcibus, mis->mis_pcidev,
			    mis->mis_pcifunc, mis->mis_pcireg, 0,
			    mis->mis_pcival);
			break;

		default:
			return (EINVAL);
		}
	}
	return (0);
}

static int
memtest_inj_int(memtest_inj_stmt_t *mis, boolean_t dryrun)
{
	cmi_hdl_t hdl;
	int rc;

	memtest_dprintf("INT# 0x%x\n", mis->mis_int);

	if ((hdl = memtest_get_hdl(mis)) == NULL) {
		return (EINVAL);
	} else if (dryrun) {
		cmi_hdl_rele(hdl);
		return (0);
	}

	switch (mis->mis_int) {
	case T_MCE:
	case T_ENOEXTFLT:
		cmi_hdl_int(hdl, mis->mis_int);
		rc = 0;
		break;

	default:
		rc = EINVAL;
		break;
	}

	cmi_hdl_rele(hdl);
	return (rc);
}

/*ARGSUSED*/
static int
memtest_inj_poll(memtest_inj_stmt_t *mis, boolean_t dryrun)
{
	cmi_hdl_t hdl;

	memtest_dprintf("POLL\n");

	if ((hdl = memtest_get_hdl(mis)) == NULL) {
		return (EINVAL);
	} else if (dryrun) {
		cmi_hdl_rele(hdl);
		return (0);
	}

	cmi_hdl_poke(hdl);
	cmi_hdl_rele(hdl);
	return (0);
}

int
memtest_inject(intptr_t arg, int mode)
{
	memtest_inject_t *mi;
	size_t misz;
	int i, n, err;
	boolean_t dryrun = memtest_dryrun();

	if (ddi_copyin((void *)arg, &n, sizeof (int), mode) < 0) {
		memtest_dprintf("Failed to copyin number of statements\n");
		return (EFAULT);
	}

	memtest_dprintf("%d statements for injection%s\n", n,
	    dryrun ? " dryrun" : "");

	if (n > memtest.mt_inject_maxnum) {
		memtest_dprintf("Maximum of %d statements exceeded\n",
		    memtest.mt_inject_maxnum);
		return (E2BIG);
	}

	if (n == 0)
		return (0);

	misz = sizeof (*mi) + sizeof (memtest_inj_stmt_t) * (n - 1);
	mi = kmem_zalloc(misz, KM_SLEEP);

	if (ddi_copyin((void *)arg, mi, misz, mode) < 0) {
		kmem_free(mi, misz);
		memtest_dprintf("copyin of injector statements failed\n");
		return (EFAULT);
	}

	for (i = 0; i < mi->mi_nstmts; i++) {
		memtest_inj_stmt_t *mis = &mi->mi_stmts[i];

		memtest_dprintf("Statement %d begins ...\n", i);

		switch (mis->mis_type) {
		case MEMTEST_INJ_STMT_MSR_RD:
			err = memtest_inj_msr_rd(mis, dryrun);
			break;
		case MEMTEST_INJ_STMT_MSR_WR:
			err = memtest_inj_msr_wr(mis, dryrun);
			break;
		case MEMTEST_INJ_STMT_PCICFG_RD:
			err = memtest_inj_pcicfg_rd(mis, dryrun);
			break;
		case MEMTEST_INJ_STMT_PCICFG_WR:
			err = memtest_inj_pcicfg_wr(mis, dryrun);
			break;
		case MEMTEST_INJ_STMT_INT:
			err = memtest_inj_int(mis, dryrun);
			break;
		case MEMTEST_INJ_STMT_POLL:
			err = memtest_inj_poll(mis, dryrun);
			break;
		default:
			err = ENOTTY;
		}

		if (err != 0) {
			cmn_err(CE_WARN,
			    "injection error on statement %d (type %d): "
			    "errno=%d", i, mis->mis_type, err);
			break;
		}
	}

	kmem_free(mi, misz);
	return (err);
}
