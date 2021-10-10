/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "ixgb.h"

#define	IXGB_DBG	IXGB_DBG_STATS	/* debug flag for this code	*/

/*
 * Table of Hardware-defined Statistics Block Offsets and Names
 */
#define	KS_NAME(s)			{ KS_ ## s, #s }

const ixgb_ksindex_t ixgb_statistics[] = {

	KS_NAME(ifHCInPkts),
	KS_NAME(ifHCInGoodPkts),
	KS_NAME(ifHCInBroadcastPkts),
	KS_NAME(ifHCInMulticastPkts),
	KS_NAME(ifHCInUcastPkts),
	KS_NAME(ifHCInVlanPkts),
	KS_NAME(ifHCInJumboPkts),
	KS_NAME(ifHInGoodOctets),
	KS_NAME(ifHInOctets),
	KS_NAME(ifNoMoreRxBDs),
	KS_NAME(ifUndersizePkts),
	KS_NAME(ifOverrsizePkts),
	KS_NAME(IfRangeLengthError),
	KS_NAME(IfCRCError),
	KS_NAME(IfMidByteEllegal),
	KS_NAME(IfMidByteError),
	KS_NAME(IfLossPkts),


	KS_NAME(ifHOutPkts),
	KS_NAME(ifHOutGoodPkts),
	KS_NAME(ifHOutBroadcastPkts),
	KS_NAME(ifHOutMulticastPkts),
	KS_NAME(ifHOutUcastPkts),
	KS_NAME(ifHOutVlanPkts),
	KS_NAME(ifHOutJumboPkts),
	KS_NAME(ifHOutGoodOctets),
	KS_NAME(ifHOutOctets),
	KS_NAME(ifHOutDefer),
	KS_NAME(ifHOutPkts64Octets),

	KS_NAME(ifHTsoCounts),
	KS_NAME(ifHTsoErrorCounts),
	KS_NAME(IfHErrorIdleCounts),
	KS_NAME(IfHRFaultcounts),
	KS_NAME(IfHLFaultcounts),
	KS_NAME(IfHRPausecounts),
	KS_NAME(IfHTPausecounts),
	KS_NAME(IfHRControlcounts),
	KS_NAME(IfHTControlcounts),
	KS_NAME(IfHRXonCounts),
	KS_NAME(IfHTXonCounts),
	KS_NAME(IfHRXofCounts),
	KS_NAME(IfHTXofCounts),
	KS_NAME(IFHJabberCounts),


	{ KS_STATS_SIZE, NULL }
};

/*
 * Local datatype for defining tables of (Offset, Name) pairs
 */
static int
ixgb_statistics_update(kstat_t *ksp, int flag)
{
	uint32_t regno_low;
	uint32_t regno_high;
	uint64_t regval;
	ixgb_t *ixgbp;
	ixgb_statistics_t *istp;
	ixgb_hw_statistics_t *hw_stp;
	kstat_named_t *knp;
	const ixgb_ksindex_t *ksip;

	if (flag != KSTAT_READ)
		return (EACCES);

	regval = 0;
	ixgbp = ksp->ks_private;
	istp = &ixgbp->statistics;
	hw_stp = &istp->hw_statistics;
	knp = ksp->ks_data;

	/*
	 * Transfer the statistics values from the copy that the
	 * chip updates via DMA to the named-kstat structure.
	 *
	 * As above, we don't bother to sync or stop updates to the
	 * statistics, 'cos it doesn't really matter if they're a few
	 * microsends out of date or less than 100% consistent ...
	 */
	for (ksip = ixgb_statistics; ksip->name != NULL; ++knp, ++ksip) {
		regno_low = KS_BASE + ksip->index * sizeof (uint64_t);
		regno_high = regno_low + sizeof (uint32_t);
		regval = ixgb_reg_get32(ixgbp, regno_high);
		regval = (regval << 32) + ixgb_reg_get32(ixgbp, regno_low);
		hw_stp->a[ksip->index] += regval;
		knp->value.ui64 = hw_stp->a[ksip->index];
	}

	return (0);
}

static int
ixgb_params_update(kstat_t *ksp, int flag)
{
	ixgb_t *ixgbp = ksp->ks_private;
	kstat_named_t *knp;
	const nd_template_t *ndp;

	if (flag != KSTAT_READ)
		return (EACCES);

	for (ndp = nd_template, knp = ksp->ks_data; ndp->name; ndp++, knp++)
		knp->value.ui64 = ndp->getfn(ixgbp);

	return (0);
}

static const ixgb_ksindex_t ixgb_chipinfo[] = {
	{ 0,				"businfo"		},
	{ 1,				"command"		},
	{ 2,				"vendor_id"		},
	{ 3,				"device_id"		},
	{ 4,				"subsystem_vendor_id"	},
	{ 5,				"subsystem_device_id"	},
	{ 6,				"revision_id"		},
	{ 7,				"cache_line_size"	},
	{ 8,				"latency_timer"		},
	{ 9,				"&phy_type"		},
	{ 10,				"hw_mac_addr"		},
	{ 11,				"&bus_type"		},
	{ 12,				"&bus_speed"		},
	{ 13,				"&bus_size"		},
	{ -1,				NULL 			}
};

static const ixgb_ksindex_t ixgb_debuginfo[] = {
	{ 1,				"context_switch"	},
	{ 2,				"ip_hsum_err"		},
	{ 3,				"tcp_hsum_err"		},
	{ 4,				"tc_next"		},
	{ 5,				"tx_next"		},
	{ 6,				"tx_free"		},
	{ 7,				"tx_flow"		},
	{ 8,				"hdl_use"		},
	{ 9,				"hdl_free"		},
	{ 10,				"rx_next"		},
	{ 11,				"rx_tail"		},
	{ 12,				"rx_free"		},
	{ 13,				"rc_next"		},
	{ 14,				"rfree_next"		},
	{ 15,				"rx_bcopy"		},
	{ 16,				"chip_reset"		},
	{ -1,				NULL 			}
};
static const ixgb_ksindex_t ixgb_phy[] = {
	{IXGB_PHY_6005, "850nm, mm"},
	{IXGB_PHY_6104, "1310nm, sm"},
	{IXGB_PHY_TXN17201, "850nm, mm"},
	{IXGB_PHY_TXN17401, "1310nm, sm"}
};

static void
ixgb_set_char_kstat(kstat_named_t *knp, const char *s)
{
	(void) strncpy(knp->value.c, s, sizeof (knp->value.c));
}

static int
ixgb_chipinfo_update(kstat_t *ksp, int flag)
{
	ixgb_t *ixgbp;
	kstat_named_t *knp;
	chip_info_t *infop;
	uint64_t tmp;

	if (flag != KSTAT_READ)
		return (EACCES);

	ixgbp = ksp->ks_private;
	infop = &ixgbp->chipinfo;
	knp = ksp->ks_data;

	(knp++)->value.ui64 = infop->businfo;
	(knp++)->value.ui64 = infop->command;
	(knp++)->value.ui64 = infop->vendor;
	(knp++)->value.ui64 = infop->device;
	(knp++)->value.ui64 = infop->subven;
	(knp++)->value.ui64 = infop->subdev;
	(knp++)->value.ui64 = infop->revision;
	(knp++)->value.ui64 = infop->clsize;
	(knp++)->value.ui64 = infop->latency;

	tmp = infop->phy_type;
	ixgb_set_char_kstat(knp++, ixgb_phy[tmp].name);

	(knp++)->value.ui64 = infop->hw_mac_addr;

	/*
	 * Now we interpret some of the above into readable strings
	 */
	tmp = infop->businfo;
	ixgb_set_char_kstat(knp++,
	    tmp & IXGB_STATUS_PCIX_MODE ? "PCI-x" : "PCI");
	ixgb_set_char_kstat(knp++,
	    tmp & IXGB_STATUS_PCIX_SPD_MASK ? "fast" : "normal");
	ixgb_set_char_kstat(knp++,
	    tmp & IXGB_STATUS_BUS64 ? "64 bit" : "32 bit");

	return (0);
}

static int
ixgb_debuginfo_update(kstat_t *ksp, int flag)
{
	ixgb_t *ixgbp;
	kstat_named_t *knp;
	ixgb_sw_statistics_t *sw_stp;

	if (flag != KSTAT_READ)
		return (EACCES);

	ixgbp = ksp->ks_private;
	sw_stp = &ixgbp->statistics.sw_statistics;
	knp = ksp->ks_data;

	(knp++)->value.ui64 = sw_stp->load_context;
	(knp++)->value.ui64 = sw_stp->ip_hwsum_err;
	(knp++)->value.ui64 = sw_stp->tcp_hwsum_err;
	(knp++)->value.ui64 = ixgbp->send->tc_next;
	(knp++)->value.ui64 = ixgbp->send->tx_next;
	(knp++)->value.ui64 = ixgbp->send->tx_free;
	(knp++)->value.ui64 = ixgbp->send->tx_flow;
	(knp++)->value.ui64 = ixgbp->send->txhdl_queue.count;
	(knp++)->value.ui64 = ixgbp->send->freetxhdl_queue.count;
	(knp++)->value.ui64 = ixgbp->recv->rx_next;
	(knp++)->value.ui64 = ixgbp->recv->rx_tail;
	(knp++)->value.ui64 = ixgbp->buff->rx_free;
	(knp++)->value.ui64 = ixgbp->buff->rc_next;
	(knp++)->value.ui64 = ixgbp->buff->rfree_next;
	(knp++)->value.ui64 = ixgbp->buff->rx_bcopy;
	(knp++)->value.ui64 = ixgbp->chip_reset;

	return (0);
}

static kstat_t *
ixgb_setup_named_kstat(ixgb_t *ixgbp, int instance, char *name,
    const ixgb_ksindex_t *ksip, size_t size, int (*update)(kstat_t *, int))
{
	kstat_t *ksp;
	kstat_named_t *knp;
	char *np;
	int type;

	size /= sizeof (ixgb_ksindex_t);
	ksp = kstat_create(IXGB_DRIVER_NAME, instance, name, "net",
	    KSTAT_TYPE_NAMED, size-1, KSTAT_FLAG_PERSISTENT);
	if (ksp == NULL)
		return (NULL);

	ksp->ks_private = ixgbp;
	ksp->ks_update = update;
	for (knp = ksp->ks_data; (np = ksip->name) != NULL; ++knp, ++ksip) {
		switch (*np) {
		default:
			type = KSTAT_DATA_UINT64;
			break;
		case '$':
			np ++;
			type = KSTAT_DATA_STRING;
			break;
		case '&':
			np ++;
			type = KSTAT_DATA_CHAR;
			break;
		}
		kstat_named_init(knp, np, type);
	}
	kstat_install(ksp);

	return (ksp);
}

/*
 * Create kstats corresponding to NDD parameters
 */
static kstat_t *
ixgb_setup_params_kstat(ixgb_t *ixgbp, int instance, char *name,
    int (*update)(kstat_t *, int))
{
	kstat_t *ksp;
	kstat_named_t *knp;
	const nd_template_t *ndp;
	int count;

	/* count ND parameters */
	for (ndp = nd_template, count = 0; ndp->name; ndp++, count++)
		;

	ksp = kstat_create(IXGB_DRIVER_NAME, instance, name, "net",
	    KSTAT_TYPE_NAMED, count, KSTAT_FLAG_PERSISTENT);
	if (ksp != NULL) {
		ksp->ks_private = ixgbp;
		ksp->ks_update = update;
		for (ndp = nd_template, knp = ksp->ks_data; ndp->name;
		    ndp++, knp++)
			kstat_named_init(knp, ndp->name, KSTAT_DATA_UINT64);
		kstat_install(ksp);
	}

	return (ksp);
}

void
ixgb_init_kstats(ixgb_t *ixgbp, int instance)
{
	IXGB_TRACE(("ixgb_init_kstats($%p, %d)", (void *)ixgbp, instance));

	ixgbp->ixgb_kstats[IXGB_KSTAT_STATS] = ixgb_setup_named_kstat(ixgbp,
	    instance, "statistics", ixgb_statistics,
	    sizeof (ixgb_statistics), ixgb_statistics_update);

	ixgbp->ixgb_kstats[IXGB_KSTAT_CHIPID] = ixgb_setup_named_kstat(ixgbp,
	    instance, "chipinfo", ixgb_chipinfo,
	    sizeof (ixgb_chipinfo), ixgb_chipinfo_update);

	ixgbp->ixgb_kstats[IXGB_KSTAT_PARAMS] = ixgb_setup_params_kstat(ixgbp,
	    instance, "parameters", ixgb_params_update);

	ixgbp->ixgb_kstats[IXGB_KSTAT_DEBUG] = ixgb_setup_named_kstat(ixgbp,
	    instance, "driver-debug", ixgb_debuginfo,
	    sizeof (ixgb_debuginfo), ixgb_debuginfo_update);
}

void
ixgb_fini_kstats(ixgb_t *ixgbp)
{
	int i;

	IXGB_TRACE(("ixgb_fini_kstats($%p)", (void *)ixgbp));

	for (i = IXGB_KSTAT_COUNT; --i >= 0; )
		if (ixgbp->ixgb_kstats[i] != NULL)
			kstat_delete(ixgbp->ixgb_kstats[i]);
}
