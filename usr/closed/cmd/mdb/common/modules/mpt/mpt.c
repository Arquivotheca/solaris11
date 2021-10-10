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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <limits.h>
#include <sys/mdb_modapi.h>
#include <sys/sysinfo.h>
#include <sys/sunmdi.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/mptvar.h>
#include <sys/scsi/adapters/mptreg.h>
#include <sys/mpt/mpi.h>
#include <sys/mpt/mpi_ioc.h>

/*
 * Values for the SAS DeviceInfo field used in SAS Device Status Change Event
 * data and SAS IO Unit Configuration pages.
 *
 * From usr/closed/uts/common/io/scsi/adapters/mpt.c
 */
#define	MPI_SAS_DEVICE_INFO_SEP			(0x00004000)
#define	MPI_SAS_DEVICE_INFO_ATAPI_DEVICE	(0x00002000)
#define	MPI_SAS_DEVICE_INFO_LSI_DEVICE		(0x00001000)
#define	MPI_SAS_DEVICE_INFO_DIRECT_ATTACH	(0x00000800)
#define	MPI_SAS_DEVICE_INFO_SSP_TARGET		(0x00000400)
#define	MPI_SAS_DEVICE_INFO_STP_TARGET		(0x00000200)
#define	MPI_SAS_DEVICE_INFO_SMP_TARGET		(0x00000100)
#define	MPI_SAS_DEVICE_INFO_SATA_DEVICE		(0x00000080)
#define	MPI_SAS_DEVICE_INFO_SSP_INITIATOR	(0x00000040)
#define	MPI_SAS_DEVICE_INFO_STP_INITIATOR	(0x00000020)
#define	MPI_SAS_DEVICE_INFO_SMP_INITIATOR	(0x00000010)
#define	MPI_SAS_DEVICE_INFO_SATA_HOST		(0x00000008)

#define	MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE	(0x00000007)
#define	MPI_SAS_DEVICE_INFO_NO_DEVICE		(0x00000000)
#define	MPI_SAS_DEVICE_INFO_END_DEVICE		(0x00000001)
#define	MPI_SAS_DEVICE_INFO_EDGE_EXPANDER	(0x00000002)
#define	MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER	(0x00000003)


struct {
	int	value;
	char	*text;
} devinfo_array[] = {
	{ MPI_SAS_DEVICE_INFO_SEP, 		"SEP" },
	{ MPI_SAS_DEVICE_INFO_ATAPI_DEVICE,	"ATAPI device" },
	{ MPI_SAS_DEVICE_INFO_LSI_DEVICE,	"LSI device" },
	{ MPI_SAS_DEVICE_INFO_DIRECT_ATTACH,	"direct attach" },
	{ MPI_SAS_DEVICE_INFO_SSP_TARGET,	"SSP tgt" },
	{ MPI_SAS_DEVICE_INFO_STP_TARGET,	"STP tgt" },
	{ MPI_SAS_DEVICE_INFO_SMP_TARGET,	"SMP tgt" },
	{ MPI_SAS_DEVICE_INFO_SATA_DEVICE,	"SATA dev" },
	{ MPI_SAS_DEVICE_INFO_SSP_INITIATOR,	"SSP init" },
	{ MPI_SAS_DEVICE_INFO_STP_INITIATOR,	"STP init" },
	{ MPI_SAS_DEVICE_INFO_SMP_INITIATOR,	"SMP init" },
	{ MPI_SAS_DEVICE_INFO_SATA_HOST,	"SATA host" }
};

static int
atoi(const char *p)
{
	int n;
	int c = *p++;

	for (n = 0; c >= '0' && c <= '9'; c = *p++) {
		n *= 10; /* two steps to avoid unnecessary overflow */
		n += '0' - c; /* accum neg to avoid surprises at MAX */
	}
	return (-n);
}

int
construct_path(uintptr_t addr, char *result)
{
	struct	dev_info	d;
	char	devi_node[PATH_MAX];
	char	devi_addr[PATH_MAX];

	if (mdb_vread(&d, sizeof (d), addr) == -1) {
		mdb_warn("couldn't read dev_info");
		return (DCMD_ERR);
	}

	if (d.devi_parent) {
		construct_path((uintptr_t)d.devi_parent, result);
		mdb_readstr(devi_node, sizeof (devi_node),
		    (uintptr_t)d.devi_node_name);
		mdb_readstr(devi_addr, sizeof (devi_addr),
		    (uintptr_t)d.devi_addr);
		mdb_snprintf(result+strlen(result),
		    PATH_MAX-strlen(result),
		    "/%s%s%s", devi_node, (*devi_addr ? "@" : ""),
		    devi_addr);
	}
	return (DCMD_OK);
}

/* ARGSUSED */
int
mdi_info_cb(uintptr_t addr, const void *data, void *cbdata)
{
	struct	mdi_pathinfo	pi;
	struct	mdi_client	c;
	char	dev_path[PATH_MAX];
	char	string[PATH_MAX];
	int	mdi_target = 0, mdi_lun = 0;
	int	target = *(int *)cbdata;

	if (mdb_vread(&pi, sizeof (pi), addr) == -1) {
		mdb_warn("couldn't read mdi_pathinfo");
		return (DCMD_ERR);
	}
	mdb_readstr(string, sizeof (string), (uintptr_t)pi.pi_addr);
	mdi_target = atoi(string);
	mdi_lun = atoi(strchr(string, ',')+1);
	if (target != mdi_target)
		return (0);

	if (mdb_vread(&c, sizeof (c), (uintptr_t)pi.pi_client) == -1) {
		mdb_warn("couldn't read mdi_client");
		return (-1);
	}

	*dev_path = NULL;
	if (construct_path((uintptr_t)c.ct_dip, dev_path) != DCMD_OK)
		strcpy(dev_path, "unknown");

	mdb_printf("LUN %d: %s\n", mdi_lun, dev_path);
	mdb_printf("       dip: %p %s path", c.ct_dip,
	    (pi.pi_preferred ? "preferred" : ""));
	switch (pi.pi_state & MDI_PATHINFO_STATE_MASK) {
		case MDI_PATHINFO_STATE_INIT:
			mdb_printf(" initializing");
			break;
		case MDI_PATHINFO_STATE_ONLINE:
			mdb_printf(" online");
			break;
		case MDI_PATHINFO_STATE_STANDBY:
			mdb_printf(" standby");
			break;
		case MDI_PATHINFO_STATE_FAULT:
			mdb_printf(" fault");
			break;
		case MDI_PATHINFO_STATE_OFFLINE:
			mdb_printf(" offline");
			break;
		default:
			mdb_printf(" invalid state");
			break;
	}
	mdb_printf("\n");
	return (0);
}

void
mdi_info(struct mpt m, int target)
{
	struct	dev_info	d;
	struct	mdi_phci	p;

	if (mdb_vread(&d, sizeof (d), (uintptr_t)m.m_dip) == -1) {
		mdb_warn("couldn't read m_dip");
		return;
	}

	if (MDI_PHCI(&d)) {
		if (mdb_vread(&p, sizeof (p), (uintptr_t)d.devi_mdi_xhci)
		    == -1) {
			mdb_warn("couldn't read m_dip.devi_mdi_xhci");
			return;
		}
		if (p.ph_path_head)
			mdb_pwalk("mdipi_phci_list", (mdb_walk_cb_t)mdi_info_cb,
			    &target, (uintptr_t)p.ph_path_head);
		return;
	}
}

void
print_cdb(mpt_cmd_t *m)
{
	struct	scsi_pkt	pkt;
	uchar_t	cdb[512];	/* an arbitrarily large number */
	int	j;

	if (mdb_vread(&pkt, sizeof (pkt), (uintptr_t)m->cmd_pkt) == -1) {
		mdb_warn("couldn't read cmd_pkt");
		return;
	}

	/*
	 * We use cmd_cdblen here because 5.10 doesn't
	 * have the cdb length in the pkt
	 */
	if (mdb_vread(&cdb, m->cmd_cdblen, (uintptr_t)pkt.pkt_cdbp) == -1) {
		mdb_warn("couldn't read pkt_cdbp");
		return;
	}

	mdb_printf("%3d,%-3d [ ",
	    pkt.pkt_address.a_target, pkt.pkt_address.a_lun);

	for (j = 0; j < m->cmd_cdblen; j++)
		mdb_printf("%02x ", cdb[j]);

	mdb_printf("]\n");
}


void
display_targets(struct mpt m, struct mpt_slots *s, int verbose)
{
	int	i, loop, comma;
	char	target_path[PATH_MAX];

	mdb_printf("\n");
	mdb_printf("targ         wwn      ncmds throttle "
	    "dr_flag  timeout  dups\n");
	mdb_printf("-------------------------------"
	    "--------------------------------\n");
	for (i = 0; i < MPT_MAX_TARGETS; i++) {
		if (s->m_target[i].m_sas_wwn || s->m_target[i].m_deviceinfo) {
			mdb_printf("%4d ", i);
			if (s->m_target[i].m_sas_wwn)
				mdb_printf("%"PRIx64" ",
				    s->m_target[i].m_sas_wwn);
			mdb_printf("%3d", s->m_target[i].m_t_ncmds);
			switch (s->m_target[i].m_t_throttle) {
				case QFULL_THROTTLE:
					mdb_printf("   QFULL ");
					break;
				case DRAIN_THROTTLE:
					mdb_printf("   DRAIN ");
					break;
				case HOLD_THROTTLE:
					mdb_printf("    HOLD ");
					break;
				case MAX_THROTTLE:
					mdb_printf("     MAX ");
					break;
				case CHOKE_THROTTLE:
					mdb_printf("   CHOKE ");
					break;
				default:
					mdb_printf("%8d ",
					    s->m_target[i].m_t_throttle);
			}
			switch (s->m_target[i].m_dr_flag) {
				case MPT_DR_INACTIVE:
					mdb_printf("  INACTIVE ");
					break;
				case MPT_DR_PRE_OFFLINE_TIMEOUT:
					mdb_printf("   TIMEOUT ");
					break;
				case MPT_DR_PRE_OFFLINE_TIMEOUT_NO_CANCEL:
					mdb_printf("TIMEOUT_NC ");
					break;
				case MPT_DR_OFFLINE_IN_PROGRESS:
					mdb_printf(" OFFLINING ");
					break;
				case MPT_DR_ONLINE_IN_PROGRESS:
					mdb_printf("  ONLINING ");
					break;
				default:
					mdb_printf("   UNKNOWN ");
					break;
				}
			mdb_printf("%3d/%-3d   %d/%d\n",
			    s->m_target[i].m_dr_timeout, m.m_offline_delay,
			    s->m_target[i].m_dr_online_dups,
			    s->m_target[i].m_dr_offline_dups);

			if (verbose) {
				mdb_inc_indent(5);
				if ((s->m_target[i].m_deviceinfo &
				    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) ==
				    MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER)
					mdb_printf("Fanout expander: ");
				if ((s->m_target[i].m_deviceinfo &
				    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) ==
				    MPI_SAS_DEVICE_INFO_EDGE_EXPANDER)
					mdb_printf("Edge expander: ");
				if ((s->m_target[i].m_deviceinfo &
				    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) ==
				    MPI_SAS_DEVICE_INFO_END_DEVICE)
					mdb_printf("End device: ");
				if ((s->m_target[i].m_deviceinfo &
				    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) ==
				    MPI_SAS_DEVICE_INFO_NO_DEVICE)
					mdb_printf("No device ");

				for (loop = 0, comma = 0;
				    loop < (sizeof (devinfo_array) /
				    sizeof (devinfo_array[0])); loop++) {
					if (s->m_target[i].m_deviceinfo &
					    devinfo_array[loop].value) {
						mdb_printf("%s%s",
						    (comma ? ", " : ""),
						    devinfo_array[loop].text);
						comma++;
					}
				}
				mdb_printf("\n");

				if (s->m_target[i].m_tgt_dip) {
					*target_path = 0;
					if (construct_path((uintptr_t)
					    s->m_target[i].m_tgt_dip,
					    target_path)
					    == DCMD_OK)
						mdb_printf("%s\n", target_path);
				}
				mdi_info(m, i);
				mdb_dec_indent(5);
			}
		}
	}
}

int
display_slotinfo(struct mpt m, struct mpt_slots *s)
{
	int	i, nslots;
	struct	mpt_cmd		c, *q, *slots;
	int	header_output = 0;
	int	rv = DCMD_OK;
	int	slots_in_use = 0;
	int	tcmds = 0;
	int	mismatch = 0;
	int	wq, dq;
	int	ncmds = 0;
	ulong_t	saved_indent;

	nslots = s->m_n_slots;

	slots = mdb_alloc(sizeof (mpt_cmd_t) * nslots, UM_SLEEP);

	for (i = 0; i < nslots; i++)
		if (s->m_slot[i]) {
			slots_in_use++;
			if (mdb_vread(&slots[i], sizeof (mpt_cmd_t),
			    (uintptr_t)s->m_slot[i]) == -1) {
				mdb_warn("couldn't read slot");
				s->m_slot[i] = NULL;
			}
			if ((slots[i].cmd_flags & CFLAG_CMDIOC) == 0)
				tcmds++;
			if (i != slots[i].cmd_slot)
				mismatch++;
		}

	for (q = m.m_waitq, wq = 0; q; q = c.cmd_linkp, wq++)
		if (mdb_vread(&c, sizeof (mpt_cmd_t), (uintptr_t)q) == -1) {
			mdb_warn("couldn't follow m_waitq");
			rv = DCMD_ERR;
			goto exit;
		}

	for (q = m.m_doneq, dq = 0; q; q = c.cmd_linkp, dq++)
		if (mdb_vread(&c, sizeof (mpt_cmd_t), (uintptr_t)q) == -1) {
			mdb_warn("couldn't follow m_doneq");
			rv = DCMD_ERR;
			goto exit;
		}

	for (i = 0; i < MPT_MAX_TARGETS; i++)
		ncmds += s->m_target[i].m_t_ncmds;

	mdb_printf("\n");
	mdb_printf("   mpt.  slot               mpt_slots     slot");
	mdb_printf("\n");
	mdb_printf("m_ncmds total"
	    " targ throttle m_t_ncmds targ_tot wq dq");
	mdb_printf("\n");
	mdb_printf("----------------------------------------------------");
	mdb_printf("\n");

	mdb_printf("%7d ", m.m_ncmds);
	mdb_printf("%s", (m.m_ncmds == slots_in_use ? "  " : "!="));
	mdb_printf("%3d               total %3d ", slots_in_use, ncmds);
	mdb_printf("%s", (tcmds == ncmds ? "     " : "   !="));
	mdb_printf("%3d %2d %2d\n", tcmds, wq, dq);

	saved_indent = mdb_dec_indent(0);
	mdb_dec_indent(saved_indent);

	for (i = 0; i < s->m_n_slots; i++)
		if (s->m_slot[i]) {
			if (!header_output) {
				mdb_printf("\n");
				mdb_printf("mpt_cmd          slot cmd_slot "
				    "cmd_flags cmd_pkt_flags scsi_pkt      "
				    "  targ,lun [ pkt_cdbp ...\n");
				mdb_printf("-------------------------------"
				    "--------------------------------------"
				    "--------------------------------------"
				    "------\n");
				header_output = 1;
			}
			mdb_printf("%16p %4d %s %4d  %8x      %8x %16p ",
			    s->m_slot[i], i,
			    (i == slots[i].cmd_slot?"   ":"BAD"),
			    slots[i].cmd_slot,
			    slots[i].cmd_flags,
			    slots[i].cmd_pkt_flags,
			    slots[i].cmd_pkt);
			(void) print_cdb(&slots[i]);
		}

	/* print the wait queue */

	for (q = m.m_waitq; q; q = c.cmd_linkp) {
		if (q == m.m_waitq)
			mdb_printf("\n");
		if (mdb_vread(&c, sizeof (mpt_cmd_t), (uintptr_t)q)
		    == -1) {
			mdb_warn("couldn't follow m_waitq");
			rv = DCMD_ERR;
			goto exit;
		}
		mdb_printf("%16p wait n/a %4d  %8x      %8x %16p ",
		    q, c.cmd_slot, c.cmd_flags, c.cmd_pkt_flags,
		    c.cmd_pkt);
		print_cdb(&c);
	}

	/* print the done queue */

	for (q = m.m_doneq; q; q = c.cmd_linkp) {
		if (q == m.m_doneq)
			mdb_printf("\n");
		if (mdb_vread(&c, sizeof (mpt_cmd_t), (uintptr_t)q)
		    == -1) {
			mdb_warn("couldn't follow m_doneq");
			rv = DCMD_ERR;
			goto exit;
		}
		mdb_printf("%16p done  n/a %4d  %8x      %8x %16p ",
		    q, c.cmd_slot, c.cmd_flags, c.cmd_pkt_flags,
		    c.cmd_pkt);
		print_cdb(&c);
	}

	mdb_inc_indent(saved_indent);

	if (m.m_ncmds != slots_in_use)
		mdb_printf("WARNING: mpt.m_ncmds does not match the number of "
		    "slots in use\n");

	if (tcmds != ncmds)
		mdb_printf("WARNING: the total of m_target[].m_t_ncmds does "
		    "not match the slots in use\n");

	if (mismatch)
		mdb_printf("WARNING: corruption in slot table, "
		    "m_slot[].cmd_slot incorrect\n");

	/* now check for corruptions */

	for (q = m.m_waitq; q; q = c.cmd_linkp) {
		for (i = 0; i < nslots; i++)
			if (s->m_slot[i] == q)
				mdb_printf("WARNING: m_waitq entry (mpt_cmd_t) "
				    "%p is in m_slot[%i]\n", q, i);

		if (mdb_vread(&c, sizeof (mpt_cmd_t), (uintptr_t)q) == -1) {
			mdb_warn("couldn't follow m_waitq");
			rv = DCMD_ERR;
			goto exit;
		}
	}

	for (q = m.m_doneq; q; q = c.cmd_linkp) {
		for (i = 0; i < nslots; i++)
			if (s->m_slot[i] == q)
				mdb_printf("WARNING: m_doneq entry (mpt_cmd_t) "
				    "%p is in m_slot[%i]\n", q, i);

		if (mdb_vread(&c, sizeof (mpt_cmd_t), (uintptr_t)q) == -1) {
			mdb_warn("couldn't follow m_doneq");
			rv = DCMD_ERR;
			goto exit;
		}
		if ((c.cmd_flags & CFLAG_FINISHED) == 0)
			mdb_printf("WARNING: m_doneq entry (mpt_cmd_t) %p "
			    "should have CFLAG_FINISHED set\n", q);
		if (c.cmd_flags & CFLAG_IN_TRANSPORT)
			mdb_printf("WARNING: m_doneq entry (mpt_cmd_t) %p "
			    "should not have CFLAG_IN_TRANSPORT set\n", q);
		if (c.cmd_flags & CFLAG_CMDARQ)
			mdb_printf("WARNING: m_doneq entry (mpt_cmd_t) %p "
			    "should not have CFLAG_CMDARQ set\n", q);
		if (c.cmd_flags & CFLAG_COMPLETED)
			mdb_printf("WARNING: m_doneq entry (mpt_cmd_t) %p "
			    "should not have CFLAG_COMPLETED set\n", q);
	}

exit:
	mdb_free(slots, sizeof (mpt_cmd_t) * nslots);
	return (rv);
}

void
display_deviceinfo(struct mpt m)
{
	int	i;
	char	device_path[PATH_MAX];

	*device_path = 0;
	if (construct_path((uintptr_t)m.m_dip, device_path) != DCMD_OK) {
		strcpy(device_path, "couldn't determine device path");
	}

	mdb_printf("\n");

	mdb_printf("base_wwid          phys "
	    "mptid prodid  devid        revid   ssid\n");
	mdb_printf("-----------------------------"
	    "----------------------------------\n");
	mdb_printf("%"PRIx64"     %2d   %3d "
	    "0x%04x 0x%04x ", m.un.m_base_wwid, m.m_num_phys, m.m_mptid,
	    m.m_productid, m.m_devid);
	switch (m.m_devid) {
		case MPT_909:
			mdb_printf("(909)   ");
			break;
		case MPT_929:
			mdb_printf("(929)   ");
			break;
		case MPT_919:
			mdb_printf("(919)   ");
			break;
		case MPT_1030:
			mdb_printf("(1030)  ");
			break;
		case MPT_1064:
			mdb_printf("(1064)  ");
			break;
		case MPT_1068:
			mdb_printf("(1068)  ");
			break;
		case MPT_1064E:
			mdb_printf("(1064E) ");
			break;
		case MPT_1068E:
			mdb_printf("(1068E) ");
			break;
		default:
			mdb_printf("(?????) ");
			break;
	}
	mdb_printf("0x%02x 0x%04x\n", m.m_revid, m.m_ssid);
	mdb_printf("%s\n", device_path);

	for (i = 0; i < MAX_MPI_PORTS; i++) {
		if (i%4 == 0)
			mdb_printf("\n");

		mdb_printf("%d:", i);

		switch (m.m_port_type[i]) {
			case MPI_PORTFACTS_PORTTYPE_INACTIVE:
				mdb_printf("inactive     ",
				    m.m_protocol_flags[i]);
				break;
			case MPI_PORTFACTS_PORTTYPE_SCSI:
				mdb_printf("SCSI (0x%1x)   ",
				    m.m_protocol_flags[i]);
				break;
			case MPI_PORTFACTS_PORTTYPE_FC:
				mdb_printf("FC (0x%1x)     ",
				    m.m_protocol_flags[i]);
				break;
			case MPI_PORTFACTS_PORTTYPE_ISCSI:
				mdb_printf("iSCSI (0x%1x)  ",
				    m.m_protocol_flags[i]);
				break;
			case MPI_PORTFACTS_PORTTYPE_SAS:
				mdb_printf("SAS (0x%1x)    ",
				    m.m_protocol_flags[i]);
				break;
			default:
				mdb_printf("unknown      ");
		}
	}
	mdb_printf("\n");
}

static int
mpt_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct	mpt		m;
	struct	mpt_slots	*s;

	int			nslots;
	int			slot_size = 0;
	uint_t			verbose = FALSE;
	uint_t			target_info = FALSE;
	uint_t			slot_info = FALSE;
	uint_t			device_info = FALSE;
	int			rv = DCMD_OK;
	void			*mpt_state;

	if (!(flags & DCMD_ADDRSPEC)) {
		mpt_state = NULL;
		if (mdb_readvar(&mpt_state, "mpt_state") == -1) {
			mdb_warn("can't read mpt_state");
			return (DCMD_ERR);
		}
		if (mdb_pwalk_dcmd("genunix`softstate", "mpt`mpt", argc, argv,
		    (uintptr_t)mpt_state) == -1) {
			mdb_warn("mdb_pwalk_dcmd failed");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	if (mdb_getopts(argc, argv,
	    's', MDB_OPT_SETBITS, TRUE, &slot_info,
	    'd', MDB_OPT_SETBITS, TRUE, &device_info,
	    't', MDB_OPT_SETBITS, TRUE, &target_info,
	    'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL) != argc)
		return (DCMD_USAGE);


	if (mdb_vread(&m, sizeof (m), addr) == -1) {
		mdb_warn("couldn't read mpt struct at 0x%p", addr);
		return (DCMD_ERR);
	}

	s = mdb_alloc(sizeof (mpt_slots_t), UM_SLEEP);

	if (mdb_vread(s, sizeof (mpt_slots_t), (uintptr_t)m.m_active) == -1) {
		mdb_warn("couldn't read small mpt_slots_t at 0x%p", m.m_active);
		mdb_free(s, sizeof (mpt_slots_t));
		return (DCMD_ERR);
	}

	nslots = s->m_n_slots;

	mdb_free(s, sizeof (mpt_slots_t));

	slot_size = sizeof (mpt_slots_t) +
	    (sizeof (mpt_cmd_t *) * (nslots-1));

	s = mdb_alloc(slot_size, UM_SLEEP);

	if (mdb_vread(s, slot_size, (uintptr_t)m.m_active) == -1) {
		mdb_warn("couldn't read large mpt_slots_t at 0x%p", m.m_active);
		mdb_free(s, slot_size);
		return (DCMD_ERR);
	}

	/* processing completed */

	if (((flags & DCMD_ADDRSPEC) && !(flags & DCMD_LOOP)) ||
	    (flags & DCMD_LOOPFIRST) || slot_info || device_info ||
	    target_info) {
		if ((flags & DCMD_LOOP) && !(flags & DCMD_LOOPFIRST))
			mdb_printf("\n");
		mdb_printf("           mpt_t inst mpxio suspend ntargs  power");
		mdb_printf("\n");
		mdb_printf("========================================="
		    "=======================================");
		mdb_printf("\n");
	}

	mdb_printf("%16p %4d %5d ", addr, m.m_instance, m.m_mpxio_enable);
	mdb_printf("%7d %6d ", m.m_suspended, m.m_ntargets);
	switch (m.m_power_level) {
		case PM_LEVEL_D0:
			mdb_printf(" ON=D0 ");
			break;
		case PM_LEVEL_D1:
			mdb_printf("    D1 ");
			break;
		case PM_LEVEL_D2:
			mdb_printf("    D2 ");
			break;
		case PM_LEVEL_D3:
			mdb_printf("OFF=D3 ");
			break;
		default:
			mdb_printf("INVALD ");
	}
	mdb_printf("\n");

	mdb_inc_indent(17);

	if (target_info)
		display_targets(m, s, verbose);

	if (device_info)
		display_deviceinfo(m);

	if (slot_info)
		display_slotinfo(m, s);

	mdb_dec_indent(17);

	mdb_free(s, slot_size);

	return (rv);
}

void
mpt_help()
{
	mdb_printf("Prints summary information about each mpt instance, "
	    "including warning\nmessages when slot usage doesn't match "
	    "summary information.\n"
	    "Without the address of a \"struct mpt\", prints every "
	    "instance.\n\n"
	    "Switches:\n"
	    "  -t   includes information about targets\n"
	    "  -s   includes information about slots in use\n"
	    "  -d   includes information about the hardware\n"
	    "  -v   displays extra information for some options\n");
}

static const mdb_dcmd_t dcmds[] = {
	{ "mpt", "?[-tsdv]", "print mpt information", mpt_dcmd, mpt_help},
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
