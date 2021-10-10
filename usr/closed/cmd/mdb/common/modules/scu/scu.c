/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <limits.h>
#include <sys/mdb_modapi.h>
#include <mdb/mdb_ctf.h>
#include <sys/mdi_impldefs.h>
#include <sys/sysinfo.h>
#include <sys/byteorder.h>
#include <sys/nvpair.h>
#include <sys/damap.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/scu/scu_var.h>
#ifndef _KMDB
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif	/* _KMDB */

#define	MDB_RD(a, b, c)		mdb_vread(a, b, (uintptr_t)c)
#define	NOREAD(a, b)		mdb_warn("could not read " #a " at 0x%p", b)

#define	NONE_LEVEL	0
#define	DTC_LEVEL	1
#define	DAM_LEVEL	2

static scu_tgt_t **targets = NULL;

static void
display_targets(struct scu_ctl scu, int totals_only)
{
	int max_dev;
	int idx;
	scu_tgt_t	scu_target;
	char		*dtype;
	int	sas_targets = 0, smp_targets = 0, sata_targets = 0;

	max_dev = scu.scu_max_dev;

	if (targets == NULL) {
		targets = mdb_alloc(sizeof (targets) * max_dev, UM_SLEEP);
	}

	if (MDB_RD(targets, sizeof (targets) * max_dev, scu.scu_tgts) == -1) {
		NOREAD(targets, scu.scu_tgts);
		return;
	}

	if (!totals_only) {
		mdb_printf("\nTarget information:\n");
		mdb_printf("---------------------------------------\n");
		mdb_printf("Idx  %-16s %-17s %-2s %-2s %s", "Tgtdip",
		    "Unit_address", "DType", "Actv", "SATA_PRO");
		mdb_printf("\n");
	}

	for (idx = 0; idx < max_dev; idx++) {
		if (targets[idx] == NULL) {
			continue;
		}

		if (MDB_RD(&scu_target, sizeof (scu_target),
		    targets[idx]) == -1) {
			NOREAD(scu_tgt_t, targets[idx]);
			continue;
		}

		switch (scu_target.scut_dtype) {
		case SCU_DTYPE_NIL:
			dtype = "None";
			break;
		case SCU_DTYPE_SATA:
			dtype = "SATA";
			sata_targets++;
			break;
		case SCU_DTYPE_SAS:
			dtype = "SAS";
			sas_targets++;
			break;
		case SCU_DTYPE_EXPANDER:
			dtype = "SMP";
			smp_targets++;
			break;
		}

		if (totals_only) {
			continue;
		}

		mdb_printf("%-4d ", idx);

		mdb_printf("%-16p", scu_target.scut_dip);
		mdb_printf(" %-16s", scu_target.scut_unit_address);
		mdb_printf(" %-5s", dtype);
		mdb_printf(" %-4d", scu_target.scut_active_pkts);
		mdb_printf(" %-4d", scu_target.scut_protocol_sata);
		mdb_printf("\n");
	}

	if (!totals_only) {
		mdb_printf("\n");
	}

	mdb_printf("%19s %d (%d SAS + %d SATA + %d SMP)\n",
	    "targets:", (sas_targets + sata_targets + smp_targets),
	    sas_targets, sata_targets, smp_targets);
}

/* ARGSUSED */
static int
display_iport_di_cb(uintptr_t addr, const void *wdata, void *priv)
{
	uint_t *idx = (uint_t *)priv;
	struct dev_info dip;
	char devi_name[MAXNAMELEN];
	char devi_addr[MAXNAMELEN];
	mdb_arg_t cmdarg[] = {{MDB_TYPE_STRING, {"-s"}}};


	if (mdb_vread(&dip, sizeof (struct dev_info), (uintptr_t)addr) !=
	    sizeof (struct dev_info)) {
		return (DCMD_ERR);
	}

	if (mdb_readstr(devi_name, sizeof (devi_name),
	    (uintptr_t)dip.devi_node_name) == -1) {
		devi_name[0] = '?';
		devi_name[1] = '\0';
	}

	if (mdb_readstr(devi_addr, sizeof (devi_addr),
	    (uintptr_t)dip.devi_addr) == -1) {
		devi_addr[0] = '?';
		devi_addr[1] = '\0';
	}


	mdb_printf("  %3d: @%-21s%10s@\t%p\n",
	    (*idx)++, devi_addr, devi_name, addr);
	(void) mdb_call_dcmd("devinfo", addr, DCMD_ADDRSPEC, 1, cmdarg);
	mdb_printf("\n");

	return (DCMD_OK);
}

/* ARGSUSED */
static int
display_iport_pi_cb(uintptr_t addr, const void *wdata, void *priv)
{
	uint_t *idx = (uint_t *)priv;
	struct mdi_pathinfo mpi;
	char pi_addr[MAXNAMELEN];
	mdb_arg_t cmdarg[] = {
		{MDB_TYPE_STRING, {"struct"}},
		{MDB_TYPE_STRING, {"mdi_pathinfo"}}
	};

	if (mdb_vread(&mpi, sizeof (struct mdi_pathinfo), (uintptr_t)addr) !=
	    sizeof (struct mdi_pathinfo)) {
		return (DCMD_ERR);
	}

	if (mdb_readstr(pi_addr, sizeof (pi_addr),
	    (uintptr_t)mpi.pi_addr) == -1) {
		pi_addr[0] = '?';
		pi_addr[1] = '\0';
	}


	mdb_printf("  %3d: @%-21s %p\n", (*idx)++, pi_addr, addr);
	(void) mdb_call_dcmd("", addr, DCMD_ADDRSPEC, 2, cmdarg);
	mdb_printf("\n");

	return (DCMD_OK);
}


/*ARGSUSED*/
static int
scu_iport_phy_walk_cb(uintptr_t addr, const void *wdata, void *priv)
{
	struct scu_phy		phy;

	if (mdb_vread(&phy, sizeof (struct scu_phy), addr) !=
	    sizeof (struct scu_phy)) {
		return (DCMD_ERR);
	}

	mdb_printf("%-16p %-2d\n", addr, phy.scup_hba_index);

	return (0);
}

static int
display_iport_damap(dev_info_t *pdip)
{
	int rval = DCMD_ERR;
	struct dev_info dip;
	scsi_hba_tran_t sht;
	mdb_ctf_id_t istm_ctfid; /* impl_scsi_tgtmap_t ctf_id */
	ulong_t tmd_offset = 0; /* tgtmap_dam offset to impl_scsi_tgtmap_t */
	uintptr_t dam0;
	uintptr_t dam1;

	if (mdb_vread(&dip, sizeof (struct dev_info), (uintptr_t)pdip) !=
	    sizeof (struct dev_info)) {
		return (rval);
	}

	if (dip.devi_driver_data == NULL) {
		return (rval);
	}

	if (mdb_vread(&sht, sizeof (scsi_hba_tran_t),
	    (uintptr_t)dip.devi_driver_data) != sizeof (scsi_hba_tran_t)) {
		return (rval);
	}

	if (sht.tran_tgtmap == NULL) {
		return (rval);
	}

	if (mdb_ctf_lookup_by_name("impl_scsi_tgtmap_t", &istm_ctfid) != 0) {
		return (rval);
	}

	if (mdb_ctf_offsetof(istm_ctfid, "tgtmap_dam", &tmd_offset) != 0) {
		return (rval);
	}

	tmd_offset /= NBBY;
	mdb_vread(&dam0, sizeof (dam0),
	    (uintptr_t)(tmd_offset + (char *)sht.tran_tgtmap));
	mdb_vread(&dam1, sizeof (dam1),
	    (uintptr_t)(sizeof (dam0) + tmd_offset + (char *)sht.tran_tgtmap));

	if (dam0 != NULL) {
		rval = mdb_call_dcmd("damap", dam0, DCMD_ADDRSPEC, 0, NULL);
		mdb_printf("\n");
		if (rval != DCMD_OK) {
			return (rval);
		}
	}

	if (dam1 != NULL) {
		rval = mdb_call_dcmd("damap", dam1, DCMD_ADDRSPEC, 0, NULL);
		mdb_printf("\n");
	}

	return (rval);
}

static int
display_iport_dtc(dev_info_t *pdip)
{
	int rval = DCMD_ERR;
	struct dev_info dip;
	struct mdi_phci phci;
	uint_t didx = 1;
	uint_t pidx = 1;

	if (mdb_vread(&dip, sizeof (struct dev_info), (uintptr_t)pdip) !=
	    sizeof (struct dev_info)) {
		return (rval);
	}

	mdb_printf("Device tree children - dev_info:\n");
	if (dip.devi_child == NULL) {
		mdb_printf("\tdevi_child is NULL, no dev_info\n\n");
		goto skip_di;
	}

	/*
	 * First, we dump the iport's children dev_info node information.
	 * use existing walker: devinfo_siblings
	 */
	mdb_printf("    #: @unit-address               "
	    "name@\tdev-info\n");
	rval = mdb_pwalk("devinfo_siblings", display_iport_di_cb,
	    (void *)&didx, (uintptr_t)dip.devi_child);
	mdb_printf("\n");

skip_di:
	/*
	 * Then we try to dump the iport's path_info node information.
	 * use existing walker: mdipi_phci_list
	 */
	mdb_printf("Device tree children - path_info:\n");
	if (mdb_vread(&phci, sizeof (struct mdi_phci),
	    (uintptr_t)dip.devi_mdi_xhci) != sizeof (struct mdi_phci)) {
		mdb_printf("\tdevi_mdi_xhci is NULL, no path_info\n\n");
		return (rval);
	}

	if (phci.ph_path_head == NULL) {
		mdb_printf("\tph_path_head is NULL, no path_info\n\n");
		return (rval);
	}

	mdb_printf("    #: @unit-address               "
	    "name@\tdev-info\n");
	rval = mdb_pwalk("mdipi_phci_list", display_iport_pi_cb,
	    (void *)&pidx, (uintptr_t)phci.ph_path_head);
	mdb_printf("\n");
	return (rval);
}

static void
display_iport_more(dev_info_t *dip, int type)
{
	if (type == DAM_LEVEL) {
		(void) display_iport_damap(dip);
	}

	if (type == DTC_LEVEL) {
		(void) display_iport_dtc(dip);
	}
}

/*ARGSUSED*/
static int
scu_iport_walk_cb(uintptr_t addr, const void *wdata, void *priv)
{
	struct scu_iport	iport;
	uintptr_t		list_addr;
	char			*ua_state;
	char			unit_address[34];
	int			*type = (int *)(priv);

	if (mdb_vread(&iport, sizeof (struct scu_iport), addr) !=
	    sizeof (struct scu_iport)) {
		return (DCMD_ERR);
	}

	if (mdb_readstr(unit_address, sizeof (unit_address),
	    (uintptr_t)(iport.scui_ua)) == -1) {
		strncpy(unit_address, "Unset", sizeof (unit_address));
	}


	switch (iport.scui_ua_state) {
	case SCU_UA_INACTIVE:
		ua_state = "Inactive";
		break;
	case SCU_UA_ACTIVE:
		ua_state = "Active";
		break;
	default:
		ua_state = "Unknown";
		break;
	}

	if (strlen(unit_address) < 3) {
		/* Standard iport unit address */
		mdb_printf("UA %-16s %-8s %-2s %-3s", "Iport", "State",
		    "NumPhys", "DIP\n");
		mdb_printf("%-2s %-16p %-8s %-7d %p\n", unit_address, addr,
		    ua_state, iport.scui_phy_num, iport.scui_dip);
	} else {
		mdb_printf("%-32s %-16s %-20s %-8s %-3s", "UA", "Iport",
		    "State", "NumPhys", "DIP\n");
		/* Temporary iport unit address */
		mdb_printf("%-32s %-16p %-20s %-7d %p\n", unit_address, addr,
		    ua_state, iport.scui_phy_num, iport.scui_dip);
	}

	if (iport.scui_phy_num > 0) {
		mdb_inc_indent(3);
		mdb_printf("%-16s %-2s", "Phy", "PhyIdx\n");
		list_addr =
		    (uintptr_t)(addr + offsetof(struct scu_iport, scui_phys));
		if (mdb_pwalk("list", scu_iport_phy_walk_cb, NULL,
		    list_addr) == -1) {
			mdb_warn("scu iport walk failed");
		}
		mdb_dec_indent(3);
		mdb_printf("\n");
	}

	/*
	 * See if we need to show more information based on 'd' or 'm' options
	 */
	display_iport_more(iport.scui_dip, *type);
	return (0);
}

/*ARGSUSED*/
static void
display_iport(struct scu_ctl scu, uintptr_t addr, int verbose,
    int type)
{
	uintptr_t	list_addr;

	mdb_printf("Iport information:\n");
	mdb_printf("-----------------\n");

	list_addr = (uintptr_t)(addr + offsetof(struct scu_ctl, scu_iports));

	if (mdb_pwalk("list", scu_iport_walk_cb, &type, list_addr) == -1) {
		mdb_warn("scu iport walk failed");
	}

	mdb_printf("\n");

}

static void
print_cdb(scu_cmd_t *m)
{
	struct	scsi_pkt	pkt;
	uchar_t	cdb[512];	/* an arbitrarily large number */
	int	j;

	if (mdb_vread(&pkt, sizeof (pkt), (uintptr_t)m->cmd_pkt) == -1) {
		mdb_warn("couldn't read cmd_pkt");
		return;
	}

	if (mdb_vread(&cdb, pkt.pkt_cdblen, (uintptr_t)pkt.pkt_cdbp) == -1) {
		mdb_warn("couldn't read pkt_cdbp");
		return;
	}

	mdb_printf("[ ");

	for (j = 0; j < pkt.pkt_cdblen; j++)
		mdb_printf("%02x ", cdb[j]);

	mdb_printf("]\n");
}



static void
display_subctl_slot(struct scu_subctl scu)
{
	scu_io_slot_t		*scu_slot;
	scu_cmd_t		*scu_cmd;
	int			slot_size;
	int			i, active_cmd = 0;
	int			first_head = 0;


	slot_size = scu.scus_slot_num * sizeof (scu_io_slot_t);
	scu_slot = mdb_alloc(slot_size, UM_SLEEP);

	if (mdb_vread(scu_slot, slot_size,
	    (uintptr_t)scu.scus_io_slots) == -1) {
		mdb_warn("read scu slot failed");
		mdb_free(scu_slot, slot_size);
		return;
	}

	mdb_printf("\n");
	mdb_printf("scu_subctl\t\ttotal_slot\tactive_slot\n");
	mdb_printf("%-16p\t%d\t%d\n", &scu,
	    scu.scus_slot_num, scu.scus_slot_active_num);

	mdb_printf("\n");

	for (i = 0; i < scu.scus_slot_num; i++) {
		if (scu_slot[i].scu_io_slot_cmdp) {
			active_cmd++;
			scu_cmd = scu_slot[i].scu_io_slot_cmdp;
			if (!first_head) {
				mdb_printf("\n");
				mdb_printf("scu_cmd          idx scsi_pkt"
				    "          targ,lun [ pkt_cdbp ...\n");
				mdb_printf("-------------------------------"
				    "-------------------------------------\n");
				first_head = 1;
			}
			mdb_printf("%-16p %-4d %-16p %-3d  %-3d ",
			    scu_cmd, i,
			    scu_cmd->cmd_pkt,
			    scu_cmd->cmd_tgtp->scut_tgt_num,
			    scu_cmd->cmd_lunp->scul_lun_num);
			(void) print_cdb(scu_cmd);
		}
	}

	mdb_free(scu_slot, slot_size);
}

static void
display_slot(struct scu_ctl scu)
{
	int	i;

	for (i = 0; i < scu.scu_lib_ctl_num; i++) {
		display_subctl_slot(scu.scu_subctls[i]);
	}
}

static int
scu_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct scu_ctl		scu;
	uint_t			verbose = FALSE;
	uint_t			target_info = FALSE;
	uint_t			iport_info = FALSE;
	uint_t			tgt_phy_count = FALSE;
	uint_t			damap_info = FALSE;
	uint_t			dtc_info = FALSE;
	uint_t			slot_info = FALSE;
	int			rv = DCMD_OK;
	void			*scu_state;
	struct dev_info		dip;

	if (!(flags & DCMD_ADDRSPEC)) {
		scu_state = NULL;
		if (mdb_readvar(&scu_state, "scu_softc_state") == -1) {
			mdb_warn("can't read scu_softc_state");
			return (DCMD_ERR);
		}
		if (mdb_pwalk_dcmd("genunix`softstate", "scu`scu", argc, argv,
		    (uintptr_t)scu_state) == -1) {
			mdb_warn("mdb_pwalk_dcmd failed");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    'd', MDB_OPT_SETBITS, TRUE, &dtc_info,
	    'I', MDB_OPT_SETBITS, TRUE, &iport_info,
	    'm', MDB_OPT_SETBITS, TRUE, &damap_info,
	    's', MDB_OPT_SETBITS, TRUE, &slot_info,
	    't', MDB_OPT_SETBITS, TRUE, &target_info,
	    'T', MDB_OPT_SETBITS, TRUE, &tgt_phy_count,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL) != argc)
		return (DCMD_USAGE);


	if (MDB_RD(&scu, sizeof (scu), addr) == -1) {
		NOREAD(scu_ctl_t, addr);
		return (DCMD_ERR);
	}
	if (MDB_RD(&dip, sizeof (struct dev_info), scu.scu_dip) == -1) {
		NOREAD(scu_ctl_t, addr);
		return (DCMD_ERR);
	}

	/* processing completed */

	if (((flags & DCMD_ADDRSPEC) && !(flags & DCMD_LOOP)) ||
	    (flags & DCMD_LOOPFIRST) || dtc_info || iport_info || damap_info ||
	    slot_info || target_info || tgt_phy_count) {
		if ((flags & DCMD_LOOP) && !(flags & DCMD_LOOPFIRST))
			mdb_printf("\n");

		mdb_printf("scu_ctl_t        inst dip              "
		    "started state");
		mdb_printf("\n");
		mdb_printf("========================================="
		    "=======================================");
		mdb_printf("\n");
	}
	mdb_printf("%-16p %-4d %-16p ", addr, scu.scu_instance, scu.scu_dip);
	mdb_printf("%-4d", scu.scu_started);
	mdb_printf("\n");
	mdb_inc_indent(10);

	if (target_info || tgt_phy_count)
		display_targets(scu, tgt_phy_count);

	if (iport_info)
		display_iport(scu, addr, verbose, NONE_LEVEL);

	if (dtc_info)
		display_iport(scu, addr, verbose, DTC_LEVEL);

	if (damap_info)
		display_iport(scu, addr, verbose, DAM_LEVEL);

	if (slot_info)
		display_slot(scu);

	mdb_dec_indent(10);

	return (rv);

}

void
scu_help()
{
	mdb_printf("Prints summary information about each scu instance.\n"
	    "    -d: Print per-iport information about device tree children\n"
	    "    -I: Print information about each iport\n"
	    "    -m: Print per-iport information about DAM/damap state\n"
	    "    -s: Print information about slot in use\n"
	    "    -t: Print information about each configured target\n"
	    "    -T: Print target and PHY count summary\n"
	    "    -v: Add verbosity to the above options\n");
}

static const mdb_dcmd_t dcmds[] = {
	{ "scu", "?[-dImstTv]",
	    "print scu information", scu_dcmd, scu_help
	},
	{ NULL }
};

static const mdb_modinfo_t modinfo = {
	MDB_API_VERSION, dcmds, NULL
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
