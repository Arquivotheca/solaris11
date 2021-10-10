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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/log.h>
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/errorq.h>
#include <sys/controlregs.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>
#include <sys/sysevent.h>
#include <sys/pghw.h>
#include <sys/cyclic.h>
#include <sys/pci_cfgspace.h>
#include <sys/mc_intel.h>
#include <sys/cpu_module_impl.h>
#include <sys/smbios.h>
#include <sys/pci.h>
#include "nhmex.h"
#include "nhmex_log.h"

errorq_t *nhmex_queue;
kmutex_t nhmex_mutex;
uint32_t nhmex_chipset;

nhmex_dimm_t **nhmex_dimms;
int nhmex_dimm_slots;
int nhmex_max_cpu_nodes = 8;

int nhmex_patrol_scrub;
int nhmex_demand_scrub;
int nhmex_no_smbios;
int nhmex_smbios_serial;
int nhmex_smbios_manufacturer;
int nhmex_smbios_part_number;
int nhmex_smbios_version;
int nhmex_smbios_label;

extern char ecc_enabled;

#pragma weak plat_dr_support_cpu
extern boolean_t plat_dr_support_cpu(void);

static void
check_serial_number()
{
	nhmex_dimm_t *dimmp, *tp;
	nhmex_dimm_t **dimmpp, **tpp;
	nhmex_dimm_t **end;
	int not_unique;

	end = &nhmex_dimms[nhmex_dimm_slots];
	for (dimmpp = nhmex_dimms; dimmpp < end; dimmpp++) {
		dimmp = *dimmpp;
		if (dimmp == NULL)
			continue;
		not_unique = 0;
		for (tpp = dimmpp + 1; tpp < end; tpp++) {
			tp = *tpp;
			if (tp == NULL)
				continue;
			if (strncmp(dimmp->serial_number, tp->serial_number,
			    sizeof (dimmp->serial_number)) == 0) {
				not_unique = 1;
				tp->serial_number[0] = 0;
			}
		}
		if (not_unique)
			dimmp->serial_number[0] = 0;
	}
}

static void
dimm_manufacture_data(smbios_hdl_t *shp, id_t id, nhmex_dimm_t *dimmp)
{
	smbios_info_t cd;

	if (smbios_info_common(shp, id, &cd) == 0) {
		if (cd.smbi_serial && nhmex_smbios_serial) {
			(void) strncpy(dimmp->serial_number, cd.smbi_serial,
			    sizeof (dimmp->serial_number));
		}
		if (cd.smbi_manufacturer && nhmex_smbios_manufacturer) {
			(void) strncpy(dimmp->manufacturer,
			    cd.smbi_manufacturer,
			    sizeof (dimmp->manufacturer));
		}
		if (cd.smbi_part && nhmex_smbios_part_number) {
			(void) strncpy(dimmp->part_number, cd.smbi_part,
			    sizeof (dimmp->part_number));
		}
		if (cd.smbi_version && nhmex_smbios_version) {
			(void) strncpy(dimmp->revision, cd.smbi_version,
			    sizeof (dimmp->revision));
		}
	}
}

struct smb_dimm_rec {
	int dimms;
	int slots;
	int populated;
	nhmex_dimm_t **dimmpp;
};

static int
dimm_label(smbios_hdl_t *shp, const smbios_struct_t *sp, void *arg)
{
	nhmex_dimm_t *dimmp;
	nhmex_dimm_t ***dimmpp;
	smbios_memdevice_t md;
	struct smb_dimm_rec *rp = (struct smb_dimm_rec *)arg;

	dimmpp = &rp->dimmpp;
	if (sp->smbstr_type == SMB_TYPE_MEMDEVICE) {
		if (*dimmpp >= &nhmex_dimms[nhmex_dimm_slots])
			return (-1);
		dimmp = **dimmpp;
		if (smbios_info_memdevice(shp, sp->smbstr_id, &md) == 0 &&
		    md.smbmd_dloc != NULL) {
			if (md.smbmd_size) {
				if (dimmp == NULL &&
				    (rp->slots == nhmex_dimm_slots ||
				    rp->dimms < rp->populated)) {
					(*dimmpp)++;
					return (0);
				}
				/*
				 * if there is no physical dimm for this smbios
				 * record it is because this system has less
				 * physical slots than the controller supports
				 * so skip empty slots to find the slot this
				 * smbios record belongs too
				 */
				while (dimmp == NULL) {
					(*dimmpp)++;
					if (*dimmpp >=
					    &nhmex_dimms[nhmex_dimm_slots])
						return (-1);
					dimmp = **dimmpp;
				}
			}
			if (dimmp) {
				if (nhmex_smbios_label)
					(void) snprintf(dimmp->label,
					    sizeof (dimmp->label), "%s",
					    md.smbmd_dloc);
				dimm_manufacture_data(shp, sp->smbstr_id,
				    dimmp);
			}
		}
		(*dimmpp)++;
	}
	return (0);
}

static int
check_memdevice(smbios_hdl_t *shp, const smbios_struct_t *sp, void *arg)
{
	struct smb_dimm_rec *rp = (struct smb_dimm_rec *)arg;
	smbios_memdevice_t md;

	if (sp->smbstr_type == SMB_TYPE_MEMDEVICE) {
		if (smbios_info_memdevice(shp, sp->smbstr_id, &md) == 0) {
			rp->slots++;
			if (md.smbmd_size) {
				rp->populated++;
			}
		}
	}
	return (0);
}

void
nhmex_smbios()
{
	struct smb_dimm_rec r;
	int i;

	if (ksmbios != NULL && nhmex_no_smbios == 0) {
		r.dimms = 0;
		r.slots = 0;
		r.populated = 0;
		r.dimmpp = nhmex_dimms;
		for (i = 0; i < nhmex_dimm_slots; i++) {
			if (nhmex_dimms[i] != NULL)
				r.dimms++;
		}
		(void) smbios_iter(ksmbios, check_memdevice, &r);
		(void) smbios_iter(ksmbios, dimm_label, &r);
		check_serial_number();
	}
}

/*
 * Read hardware register, always return 32 bits, the code assumes registers do
 * not cross 32 bit boundary
 */
int
nhmex_mb_rd(int bus, int mc, int mb, int dimm, int func, int offset,
    uint32_t *datap)
{
	uint32_t val, cmd1, cmd3;
	int rt;
	int wait;
	int retry;
	uint32_t cmd[4];
	uint32_t data;
	int shift;
	int i;

	rt = -1;
	shift = (offset & 3) * 8;
	offset &= ~3;
	/*
	 * check and clear error state which may have been left by bios or
	 * previous command
	 */
	cmd3 = PCSR_FBD_CMD_RD(bus, mc, 3);
	if ((cmd3 & FBD_CMD_ERROR) != 0) {
		PCSR_FBD_CMD_WR(bus, mc, 3, FBD_CMD_ERROR);
		drv_usecwait(100);
	}
	cmd1 = MB_PCICFG_READ(func, dimm, offset);
	val = 0;
	wait = 1;
	retry = 20;
	while (retry) {
		PCSR_NB_REG_DATA_VAL_WR(bus, mc, 0);
		PCSR_FBD_CMD_WR(bus, mc, 0, 0);
		PCSR_FBD_CMD_WR(bus, mc, 1,  cmd1);
		PCSR_FBD_CMD_WR(bus, mc, 2, 0);
		PCSR_FBD_CMD_WR(bus, mc, 3, FBD_CMD_CFRD);
		while (val == 0) {
			val = PCSR_NB_REG_DATA_VAL_RD(bus, mc);
			cmd3 = PCSR_FBD_CMD_RD(bus, mc, 3);
			if (val == 0 || (cmd3 & FBD_CMD_ACTIVE) != 0) {
				if (--wait < 0)
					cmd3 = FBD_CMD_ERROR;
				else
					drv_usecwait(10);
			}
			cmd3 |= FBD_CMD_ACTIVE;
			if (cmd3 != FBD_CMD_CFRD)
				break;
		}

		data = PCSR_NB_REG_DATA_RD(bus, mc, mb);
		for (i = 0; i < 4; i++)
			cmd[i] = PCSR_FBD_CMD_RD(bus, mc, i);
		cmd[3] |= FBD_CMD_ACTIVE;
		cmd[3] &= ~FBD_CMD_ERROR;
		if (cmd[0] != 0 || cmd[1] != cmd1 || cmd[2] != 0 ||
		    cmd[3] != FBD_CMD_CFRD) {
			/* BIOS has taken over channel to MB */
			wait = 2;
			retry--;
		} else if ((cmd3 & FBD_CMD_ERROR) == 0) {
			*datap = data >> shift;
			rt = 0;
			retry = 0;
		} else {
			/*
			 * clear error and retry command
			 */
			PCSR_FBD_CMD_WR(bus, mc, 3, FBD_CMD_ERROR);
			drv_usecwait(100);
			wait = 2;
			retry--;
		}
	}
	return (rt);
}

void
nhmex_scrubber_enable()
{
	int hw_scrub = 0;

	if (ecc_enabled && (nhmex_patrol_scrub || nhmex_demand_scrub)) {
		if (hw_scrub)
			cmi_mc_sw_memscrub_disable();
	}
}

static void
dimm_data(int slot, int mem, int channel, int dimm, uint32_t mtr)
{
	nhmex_dimm_t *dimmp;

	dimmp = (nhmex_dimm_t *)kmem_zalloc(sizeof (nhmex_dimm_t), KM_SLEEP);
	dimmp->dimm_size = DIMMSIZE(mtr);
	dimmp->nranks = NUMRANK(mtr);
	dimmp->nbanks = NUMBANK(mtr);
	dimmp->ncolumn = NUMCOL(mtr);
	dimmp->nrow = NUMROW(mtr);
	dimmp->width = DIMMWIDTH;
	(void) snprintf(dimmp->label, sizeof (dimmp->label),
	    "Socket %d memory-controller %d channel %d dimm %d",
	    slot, mem, channel, dimm);
	nhmex_dimms[DIMM_NUM(slot, mem, channel, dimm)] = dimmp;
}

void
init_dimms()
{
	int i, j, k, l;
	uint32_t did;
	uint32_t mtr;

	nhmex_dimm_slots = nhmex_max_cpu_nodes * MAX_CPU_MEMORY_CONTROLLERS *
	    CHANNELS_PER_MEMORY_CONTROLLER * MAX_DIMMS_PER_CHANNEL;
	nhmex_dimms = (nhmex_dimm_t **)kmem_zalloc(sizeof (nhmex_dimm_t *) *
	    nhmex_dimm_slots, KM_SLEEP);
	for (i = 0; i < nhmex_max_cpu_nodes; i++) {
		did = CPU_ID_RD(i);
		if (did != NHMEX_EX_CPU) {
			continue;
		}
		for (j = 0; j < MAX_CPU_MEMORY_CONTROLLERS; j++) {
			for (k = 0; k < MAX_DIMMS_PER_CHANNEL; k++) {
				for (l = 0; l < CHANNELS_LOCKSTEP; l++) {
					if (MB_MTR_RD(i, j, l, k, &mtr) != -1 &&
					    DDR3_DIMM_PRESENT(mtr)) {
						dimm_data(i, j, l, k, mtr);
					}
				}
			}
		}
	}
}

int
nhmex_init(void)
{
	int slot;
	uint32_t did;
	int max_node;

	/* return ENOTSUP if there is no PCI config space support. */
	if (pci_getl_func == NULL)
		return (ENOTSUP);

	max_node = 0;
	for (slot = 0; slot < MAX_CPU_NODES; slot++) {
		did = CPU_ID_RD(slot);
		if (did == NHMEX_EX_CPU) {
			nhmex_chipset = did;
			max_node = slot + 1;
		}
	}
	if (nhmex_chipset != NHMEX_EX_CPU)
		return (ENOTSUP);

	if (&plat_dr_support_cpu == NULL || !plat_dr_support_cpu())
		nhmex_max_cpu_nodes = max_node;

	return (0);
}

int
nhmex_reinit(void)
{
	return (0);
}

int
nhmex_dev_init()
{
	init_dimms();
	return (0);
}

void
nhmex_dev_reinit()
{
}

void
nhmex_unload()
{
	int i;

	for (i = 0; i < nhmex_dimm_slots; i++) {
		if (nhmex_dimms[i] != NULL) {
			kmem_free(nhmex_dimms[i], sizeof (nhmex_dimm_t));
			nhmex_dimms[i] = NULL;
		}
	}
	kmem_free(nhmex_dimms, sizeof (nhmex_dimm_t *) * nhmex_dimm_slots);
	nhmex_dimms = NULL;
}
