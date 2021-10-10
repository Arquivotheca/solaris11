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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */


/*
 * kstat helpers.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>

#include "cxge_regs.h"
#include "cxge_common.h"

#include "cxge_version.h"
#include "cxgen.h"

/*
 * cxgen:X:config
 */
typedef struct cxgen_config_kstat_s {
	kstat_named_t chip_ver;
	kstat_named_t mc_vers;
	kstat_named_t fw_vers;
	kstat_named_t driver_version;
	kstat_named_t bus_type;
	kstat_named_t bus_width;
	kstat_named_t bus_frequency;
	kstat_named_t pci_vendor_id;
	kstat_named_t pci_device_id;
	kstat_named_t core_clock;
	kstat_named_t up_clock;
	kstat_named_t port_cnt;
	kstat_named_t port_type;
	kstat_named_t pmtx_clock;
	kstat_named_t pmtx_memtype;
	kstat_named_t pmtx_size;
	kstat_named_t pmrx_clock;
	kstat_named_t pmrx_memtype;
	kstat_named_t pmrx_size;
	kstat_named_t cm_clock;
	kstat_named_t cm_memtype;
	kstat_named_t cm_size;
	kstat_named_t tcam_mode;
	kstat_named_t tcam_size;
	kstat_named_t tcam_servers;
	kstat_named_t tcam_filters;
	kstat_named_t tcam_routes;
	kstat_named_t tp_win_scale_mode;
	kstat_named_t tp_timestamps_mode;
	kstat_named_t tp_chan_tx_size;
	kstat_named_t tp_chan_rx_size;
	kstat_named_t tp_tx_pg_size;
	kstat_named_t tp_tx_num_pgs;
	kstat_named_t tp_rx_pg_size;
	kstat_named_t tp_rx_num_pgs;
	kstat_named_t cim_sdram_base;
} cxgen_config_kstat_t, *p_cxgen_config_kstat_t;

/*
 * cxgen:X:sge
 */
typedef struct cxgen_sge_kstat_s {
	kstat_named_t parity_errors;
	kstat_named_t framing_errors;
	kstat_named_t rspq_starved;
	kstat_named_t fl_empty;
	kstat_named_t rspq_credit_overflows;
	kstat_named_t rspq_disabled;
	kstat_named_t lo_db_empty;
	kstat_named_t lo_db_full;
	kstat_named_t hi_db_empty;
	kstat_named_t hi_db_full;
	kstat_named_t lo_credit_underflows;
	kstat_named_t hi_credit_underflows;
} cxgen_sge_kstat_t, *p_cxgen_sge_kstat_t;

/*
 * cxgen:X:qsY_rspq
 */
typedef struct cxgen_rspq_kstat_s {
	/* See struct rspq_stats */
	kstat_named_t rx_imm_data;
	kstat_named_t rx_buf_data;
	kstat_named_t nomem;
	kstat_named_t starved;
	kstat_named_t disabled;
	kstat_named_t pure;
	kstat_named_t txq_unblocked;
	kstat_named_t max_descs_processed;
	kstat_named_t desc_budget_met;
	kstat_named_t max_frames_chained;
	kstat_named_t chain_budget_met;
} cxgen_rspq_kstat_t, *p_cxgen_rspq_kstat_t;

/*
 * cxgen:X:qsY_flZ
 */
typedef struct cxgen_fl_kstat_s {
	/* See struct fl_stats */
	kstat_named_t spareq_hit;
	kstat_named_t spareq_miss;
	kstat_named_t nomem_kalloc;
	kstat_named_t nomem_mblk;
	kstat_named_t nomem_meta_hdl;
	kstat_named_t nomem_meta_mem;
	kstat_named_t nomem_meta_bind;
	kstat_named_t nomem_meta_mblk;
	kstat_named_t empty;
} cxgen_fl_kstat_t, *p_cxgen_fl_kstat_t;

/*
 * cxgen:X:qsY_txqZ
 */
typedef struct cxgen_txq_kstat_s {
	/* See struct txq_stats */
	kstat_named_t tx_imm_data;
	kstat_named_t tx_lso;
	kstat_named_t tx_pkt;
	kstat_named_t used_small_buf;
	kstat_named_t used_big_buf;
	kstat_named_t txq_blocked;
	kstat_named_t pullup;
	kstat_named_t pullup_failed;
	kstat_named_t dma_map_failed;
	kstat_named_t max_frame_len;
	kstat_named_t max_mblks_in_frame;
	kstat_named_t max_dma_segs_in_mblk;
	kstat_named_t max_dma_segs_in_frame;
} cxgen_txq_kstat_t, *p_cxgen_txq_kstat_t;

static char *cxgen_mem_type[] = {
	"not-present", "ddr2-533-3-3-3", "ddr2-533-4-4-4",
	"ddr2-533-5-5-5", "ddr2-400-3-3-3", "ddr2-400-4-4-4"
};

static char *cxgen_tcam_mode[] = {
	"not-present", "144 Bit", "72 Bit"
};

/*
 * Utility macros to aid readability and to avoid typos + boredom while filling
 * up kstats.  Use KS_U for ul (unsigned long), KS_C for c (char[16]).
 */
#define	KS_UINIT(x)	kstat_named_init(&kstatp->x, #x, KSTAT_DATA_ULONG)
#define	KS_CINIT(x)	kstat_named_init(&kstatp->x, #x, KSTAT_DATA_CHAR)
#define	KS_U_SET(x, y)	kstatp->x.value.ul = (y)
#define	KS_U_FROM(x, y)	kstatp->x.value.ul = y->x
#define	KS_C_SET(x, ...) \
			(void) snprintf(kstatp->x.value.c, 16,  __VA_ARGS__)

/*
 * Helpers.
 */
static int cxgen_setup_config_kstats(p_adapter_t);
static void cxgen_destroy_config_kstats(p_adapter_t);

static int cxgen_setup_sge_kstats(p_adapter_t);
static int cxgen_update_sge_kstats(kstat_t *, int);
static void cxgen_destroy_sge_kstats(p_adapter_t);

static int cxgen_setup_rspq_kstats(struct sge_qset *);
static int cxgen_update_rspq_kstats(kstat_t *, int);
static void cxgen_destroy_rspq_kstats(struct sge_qset *);

static int cxgen_setup_fl_kstats(struct sge_qset *, int);
static int cxgen_update_fl_kstats(kstat_t *, int);
static void cxgen_destroy_fl_kstats(struct sge_qset *, int);

static int cxgen_setup_txq_kstats(struct sge_qset *, int);
static int cxgen_update_txq_kstats(kstat_t *, int);
static void cxgen_destroy_txq_kstats(struct sge_qset *, int);

int
cxgen_setup_kstats(p_adapter_t cxgenp)
{
	int rc = DDI_FAILURE;

	if (cxgen_setup_config_kstats(cxgenp))
		goto cxgen_setup_kstats_exit;

	if (cxgen_setup_sge_kstats(cxgenp))
		goto cxgen_setup_kstats_exit;

	rc = DDI_SUCCESS;

cxgen_setup_kstats_exit:
	if (rc != DDI_SUCCESS)
		cxgen_destroy_kstats(cxgenp);

	return (rc);
}

void
cxgen_destroy_kstats(p_adapter_t cxgenp)
{
	if (cxgenp->sge_ksp)
		cxgen_destroy_sge_kstats(cxgenp);

	if (cxgenp->config_ksp)
		cxgen_destroy_config_kstats(cxgenp);
}

static int
cxgen_setup_config_kstats(p_adapter_t cxgenp)
{
	struct kstat *ksp;
	p_cxgen_config_kstat_t kstatp;
	int ndata;
	struct pci_params *p = &cxgenp->params.pci;
	struct vpd_params *v = &cxgenp->params.vpd;
	struct tp_params *t = &cxgenp->params.tp;
	struct mc5 *m = &cxgenp->mc5;
	struct mc5_params *mp = &cxgenp->params.mc5;

	ndata = sizeof (cxgen_config_kstat_t) / sizeof (kstat_named_t);

	if ((ksp = kstat_create(CXGEN_DEVNAME, cxgenp->instance, "config",
	    "nexus", KSTAT_TYPE_NAMED, ndata, 0)) == NULL) {
		cmn_err(CE_WARN, "Failed to initialize config kstats.");
		return (DDI_FAILURE);
	}

	kstatp = (p_cxgen_config_kstat_t)ksp->ks_data;

	KS_UINIT(chip_ver);
	KS_CINIT(mc_vers);
	KS_CINIT(fw_vers);
	KS_CINIT(driver_version);

	KS_CINIT(bus_type);
	KS_CINIT(bus_width);
	KS_CINIT(bus_frequency);
	KS_CINIT(pci_vendor_id);
	KS_CINIT(pci_device_id);

	KS_UINIT(core_clock);
	KS_UINIT(up_clock);

	KS_UINIT(port_cnt);
	KS_UINIT(port_type);

	KS_UINIT(pmtx_clock);
	KS_CINIT(pmtx_memtype);
	KS_UINIT(pmtx_size);

	KS_UINIT(pmrx_clock);
	KS_CINIT(pmrx_memtype);
	KS_UINIT(pmrx_size);

	KS_UINIT(cm_clock);
	KS_CINIT(cm_memtype);
	KS_UINIT(cm_size);

	KS_CINIT(tcam_mode);
	KS_UINIT(tcam_size);
	KS_UINIT(tcam_servers);
	KS_UINIT(tcam_filters);
	KS_UINIT(tcam_routes);

	KS_UINIT(tp_win_scale_mode);
	KS_UINIT(tp_timestamps_mode);
	KS_UINIT(tp_chan_tx_size);
	KS_UINIT(tp_chan_rx_size);
	KS_UINIT(tp_tx_pg_size);
	KS_UINIT(tp_tx_num_pgs);
	KS_UINIT(tp_rx_pg_size);
	KS_UINIT(tp_rx_num_pgs);

	KS_UINIT(cim_sdram_base);

	/*
	 * The config kstats do not change.  Most of them are set here.  Others
	 * are set in cxgen_finalize_config_kstats, which is called during
	 * first_port_up.
	 *
	 * No need for any bzero here as kstat_create zeroes out all data.
	 */

	/* chip_ver, port_cnt, port_type, firmware_ver, microcode_ver, etc. */
	KS_U_SET(chip_ver, cxgenp->params.rev);
	KS_U_SET(port_cnt, cxgenp->params.nports);
	KS_U_SET(port_type, cxgenp->params.vpd.port_type[0]);
	KS_C_SET(fw_vers, cxgenp->fw_vers);
	KS_C_SET(mc_vers, cxgenp->mc_vers);
	KS_C_SET(driver_version, DRV_VERSION);

	/* pci stats: bus_type, bus_width, bus_frequency */
	KS_C_SET(pci_vendor_id, "0x%x", cxgenp->pci_vendor);
	KS_C_SET(pci_device_id, "0x%x", cxgenp->pci_device);
	KS_C_SET(bus_width, "%d Bit", p->width);
	KS_C_SET(bus_frequency, "%dMhz", p->speed);
	switch (p->variant) {
	case PCI_VARIANT_PCIE:
		KS_C_SET(bus_type, "pci-express");
		KS_C_SET(bus_width, "x%d lanes", p->width);
		KS_C_SET(bus_frequency, "not-applicable");
		break;
	case PCI_VARIANT_PCI:
		KS_C_SET(bus_type, "pci");
		break;
	case PCI_VARIANT_PCIX_MODE1_PARITY:
	case PCI_VARIANT_PCIX_MODE1_ECC:
	case PCI_VARIANT_PCIX_266_MODE2:
		KS_C_SET(bus_type, "pci-x");
		break;
	default:
		ASSERT(0);
		KS_C_SET(bus_type, "unknown");
		KS_C_SET(bus_width, "unknown");
		KS_C_SET(bus_frequency, "unknown");
		break;
	}

	/* clocks: core_clock, pmtx_clock, pmrx_clock, cm_clock, up_clock */
	KS_U_SET(core_clock, v->cclk);
	KS_U_SET(pmtx_clock, v->mclk);
	KS_U_SET(pmrx_clock, v->mclk);
	KS_U_SET(cm_clock, v->mclk);
	KS_U_SET(up_clock, v->uclk);

	/* memtypes: pmtx_memtype, pmrx_memtype, cm_memtype */
	KS_C_SET(pmtx_memtype, cxgen_mem_type[v->mem_timing]);
	KS_C_SET(pmrx_memtype, cxgen_mem_type[v->mem_timing]);
	KS_C_SET(cm_memtype, cxgen_mem_type[v->mem_timing]);

	/* pmtx_size, pmrx_size, cm_size */
	KS_U_FROM(pmtx_size, t);
	KS_U_FROM(pmrx_size, t);
	KS_U_FROM(cm_size, t);

	/* tcam_mode, tcam_size, tcam_servers, tcam_filters, tcam_routes */
	KS_C_SET(tcam_mode, cxgen_tcam_mode[m->mode]);
	KS_U_FROM(tcam_size, m);
	KS_U_SET(tcam_servers, mp->nservers);
	KS_U_SET(tcam_filters, mp->nfilters);
	KS_U_SET(tcam_routes, mp->nroutes);

	/* tp_chan_rx_size, tp_chan_tx_size */
	KS_U_SET(tp_chan_rx_size, t->chan_rx_size);
	KS_U_SET(tp_chan_tx_size, t->chan_tx_size);

	/* tp_rx_pg_size, tp_rx_num_pgs, tp_tx_pg_size, tp_tx_num_pgs */
	KS_U_SET(tp_rx_pg_size, t->rx_pg_size);
	KS_U_SET(tp_rx_num_pgs, t->rx_num_pgs);
	KS_U_SET(tp_tx_pg_size, t->tx_pg_size);
	KS_U_SET(tp_tx_num_pgs, t->tx_num_pgs);

	/* Must match what's in tp_config() */
	KS_U_SET(tp_win_scale_mode, 1);
	KS_U_SET(tp_timestamps_mode, 1);

	/* Do NOT set ksp->ks_update.  config kstats do not change. */

	/* Install the kstat */
	ksp->ks_private = (void *)cxgenp;
	kstat_install(ksp);
	cxgenp->config_ksp = ksp;

	return (DDI_SUCCESS);
}

/*
 * This is an inelegant hack.  We don't want to have a ks_update that runs again
 * and again because config kstats don't change.  But at least one stat is
 * available only after first_port_up has been called.  We use this function to
 * update that stat.
 */
void
cxgen_finalize_config_kstats(p_adapter_t cxgenp)
{
	struct kstat *ksp = cxgenp->config_ksp;
	p_cxgen_config_kstat_t kstatp = (p_cxgen_config_kstat_t)ksp->ks_data;

	/* Put here because partition_mem needs to run first */
	KS_U_SET(cim_sdram_base, t3_read_reg(cxgenp, A_CIM_SDRAM_BASE_ADDR));
}

static void
cxgen_destroy_config_kstats(p_adapter_t cxgenp)
{
	kstat_delete(cxgenp->config_ksp);
	cxgenp->config_ksp = NULL;
}

static int
cxgen_setup_sge_kstats(p_adapter_t cxgenp)
{
	struct kstat *ksp;
	p_cxgen_sge_kstat_t kstatp;
	int ndata;

	ndata = sizeof (cxgen_sge_kstat_t) / sizeof (kstat_named_t);

	if ((ksp = kstat_create(CXGEN_DEVNAME, cxgenp->instance, "sge",
	    "nexus", KSTAT_TYPE_NAMED, ndata, 0)) == NULL) {
		cmn_err(CE_WARN, "Failed to initialize sge kstats.");
		return (DDI_FAILURE);
	}

	kstatp = (p_cxgen_sge_kstat_t)ksp->ks_data;

	KS_UINIT(parity_errors);
	KS_UINIT(framing_errors);
	KS_UINIT(rspq_starved);
	KS_UINIT(fl_empty);
	KS_UINIT(rspq_credit_overflows);
	KS_UINIT(rspq_disabled);
	KS_UINIT(lo_db_empty);
	KS_UINIT(lo_db_full);
	KS_UINIT(hi_db_empty);
	KS_UINIT(hi_db_full);
	KS_UINIT(lo_credit_underflows);
	KS_UINIT(hi_credit_underflows);

	/* Install the kstat */
	ksp->ks_update = cxgen_update_sge_kstats;
	ksp->ks_private = (void *) &cxgenp->sge_stats;
	kstat_install(ksp);
	cxgenp->sge_ksp = ksp;

	return (DDI_SUCCESS);
}

static int
cxgen_update_sge_kstats(kstat_t *ksp, int rw)
{
	p_cxgen_sge_kstat_t kstatp = (p_cxgen_sge_kstat_t)ksp->ks_data;
	struct cxgen_sge_stats *sge_stats = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (0);

	KS_U_FROM(parity_errors, sge_stats);
	KS_U_FROM(framing_errors, sge_stats);
	KS_U_FROM(rspq_starved, sge_stats);
	KS_U_FROM(fl_empty, sge_stats);
	KS_U_FROM(rspq_credit_overflows, sge_stats);
	KS_U_FROM(rspq_disabled, sge_stats);
	KS_U_FROM(lo_db_empty, sge_stats);
	KS_U_FROM(lo_db_full, sge_stats);
	KS_U_FROM(hi_db_empty, sge_stats);
	KS_U_FROM(hi_db_full, sge_stats);
	KS_U_FROM(lo_credit_underflows, sge_stats);
	KS_U_FROM(hi_credit_underflows, sge_stats);

	return (0);
}

static void
cxgen_destroy_sge_kstats(p_adapter_t cxgenp)
{
	kstat_delete(cxgenp->sge_ksp);
	cxgenp->sge_ksp = NULL;
}

static int
cxgen_setup_rspq_kstats(struct sge_qset *qs)
{
	struct kstat *ksp;
	struct sge_rspq *q = &qs->rspq;
	p_adapter_t cxgenp = qs->port->adapter;
	p_cxgen_rspq_kstat_t kstatp;
	int ndata;
	char str[16];

	/* This queue or qset is not active */
	if (q->cleanup == 0)
		return (DDI_SUCCESS);

	ASSERT(q->ksp == NULL);

	ndata = sizeof (cxgen_rspq_kstat_t) / sizeof (kstat_named_t);

	(void) snprintf(str, sizeof (str), "qs%u_rspq", qs->idx);

	ksp = kstat_create(CXGEN_DEVNAME, cxgenp->instance, str,
	    "rspq", KSTAT_TYPE_NAMED, ndata, 0);
	if (ksp == NULL) {
		cmn_err(CE_WARN, "%s: failed to initialize kstats for "
		    "qset%d.", __func__, qs->idx);
		return (DDI_FAILURE);
	}

	kstatp = (p_cxgen_rspq_kstat_t)ksp->ks_data;

	KS_UINIT(rx_imm_data);
	KS_UINIT(rx_buf_data);
	KS_UINIT(starved);
	KS_UINIT(disabled);
	KS_UINIT(nomem);
	KS_UINIT(pure);
	KS_UINIT(txq_unblocked);
	KS_UINIT(max_descs_processed);
	KS_UINIT(desc_budget_met);
	KS_UINIT(max_frames_chained);
	KS_UINIT(chain_budget_met);

	ksp->ks_update = cxgen_update_rspq_kstats;
	ksp->ks_private = (void *) &q->stats;
	kstat_install(ksp);
	q->ksp = ksp;

	return (DDI_SUCCESS);
}

static int
cxgen_update_rspq_kstats(kstat_t *ksp, int rw)
{
	p_cxgen_rspq_kstat_t kstatp = (p_cxgen_rspq_kstat_t)ksp->ks_data;
	struct rspq_stats *stats = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (0);

	KS_U_FROM(rx_imm_data, stats);
	KS_U_FROM(rx_buf_data, stats);
	KS_U_FROM(starved, stats);
	KS_U_FROM(disabled, stats);
	KS_U_FROM(nomem, stats);
	KS_U_FROM(pure, stats);
	KS_U_FROM(txq_unblocked, stats);
	KS_U_FROM(max_descs_processed, stats);
	KS_U_FROM(desc_budget_met, stats);
	KS_U_FROM(max_frames_chained, stats);
	KS_U_FROM(chain_budget_met, stats);

	return (0);
}

static void
cxgen_destroy_rspq_kstats(struct sge_qset *qs)
{
	struct sge_rspq *q = &qs->rspq;

	if (q->ksp) {
		kstat_delete(q->ksp);
		q->ksp = NULL;
	}
}

static int
cxgen_setup_fl_kstats(struct sge_qset *qs, int idx)
{
	struct kstat *ksp;
	struct sge_fl *q = &qs->fl[idx];
	p_adapter_t cxgenp = qs->port->adapter;
	p_cxgen_fl_kstat_t kstatp;
	int ndata;
	char str[16];

	/* This queue or qset is not active */
	if (q->cleanup == 0)
		return (DDI_SUCCESS);

	ASSERT(q->ksp == NULL);

	ndata = sizeof (cxgen_fl_kstat_t) / sizeof (kstat_named_t);

	(void) snprintf(str, sizeof (str), "qs%u_fl%d", qs->idx, idx);

	ksp = kstat_create(CXGEN_DEVNAME, cxgenp->instance, str,
	    "fl", KSTAT_TYPE_NAMED, ndata, 0);
	if (ksp == NULL) {
		cmn_err(CE_WARN, "%s: failed to initialize kstats for "
		    "qset%d fl%d.", __func__, qs->idx, idx);
		return (DDI_FAILURE);
	}

	kstatp = (p_cxgen_fl_kstat_t)ksp->ks_data;

	KS_UINIT(spareq_hit);
	KS_UINIT(spareq_miss);
	KS_UINIT(nomem_kalloc);
	KS_UINIT(nomem_mblk);
	KS_UINIT(nomem_meta_hdl);
	KS_UINIT(nomem_meta_mem);
	KS_UINIT(nomem_meta_bind);
	KS_UINIT(nomem_meta_mblk);
	KS_UINIT(empty);

	ksp->ks_update = cxgen_update_fl_kstats;
	ksp->ks_private = (void *) &q->stats;
	kstat_install(ksp);
	q->ksp = ksp;

	return (DDI_SUCCESS);
}

static int
cxgen_update_fl_kstats(kstat_t *ksp, int rw)
{
	p_cxgen_fl_kstat_t kstatp = (p_cxgen_fl_kstat_t)ksp->ks_data;
	struct fl_stats *stats = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (0);

	KS_U_FROM(spareq_hit, stats);
	KS_U_FROM(spareq_miss, stats);
	KS_U_FROM(nomem_kalloc, stats);
	KS_U_FROM(nomem_mblk, stats);
	KS_U_FROM(nomem_meta_hdl, stats);
	KS_U_FROM(nomem_meta_mem, stats);
	KS_U_FROM(nomem_meta_bind, stats);
	KS_U_FROM(nomem_meta_mblk, stats);
	KS_U_FROM(empty, stats);

	return (0);
}

static void
cxgen_destroy_fl_kstats(struct sge_qset *qs, int idx)
{
	struct sge_fl *q = &qs->fl[idx];

	if (q->ksp) {
		kstat_delete(q->ksp);
		q->ksp = NULL;
	}
}

static int
cxgen_setup_txq_kstats(struct sge_qset *qs, int idx)
{
	struct kstat *ksp;
	struct sge_txq *q = &qs->txq[idx];
	p_adapter_t cxgenp = qs->port->adapter;
	p_cxgen_txq_kstat_t kstatp;
	int ndata;
	char str[16];

	/* This queue or qset is not active */
	if (q->cleanup == 0)
		return (DDI_SUCCESS);

	ASSERT(q->ksp == NULL);

	ndata = sizeof (cxgen_txq_kstat_t) / sizeof (kstat_named_t);

	(void) snprintf(str, sizeof (str), "qs%u_txq%d", qs->idx, idx);

	ksp = kstat_create(CXGEN_DEVNAME, cxgenp->instance, str,
	    "txq", KSTAT_TYPE_NAMED, ndata, 0);
	if (ksp == NULL) {
		cmn_err(CE_WARN, "%s: failed to initialize kstats for "
		    "qset%d txq%d.", __func__, qs->idx, idx);
		return (DDI_FAILURE);
	}

	kstatp = (p_cxgen_txq_kstat_t)ksp->ks_data;

	KS_UINIT(tx_imm_data);
	KS_UINIT(tx_lso);
	KS_UINIT(tx_pkt);
	KS_UINIT(used_small_buf);
	KS_UINIT(used_big_buf);
	KS_UINIT(txq_blocked);
	KS_UINIT(pullup);
	KS_UINIT(pullup_failed);
	KS_UINIT(dma_map_failed);
	KS_UINIT(max_frame_len);
	KS_UINIT(max_mblks_in_frame);
	KS_UINIT(max_dma_segs_in_mblk);
	KS_UINIT(max_dma_segs_in_frame);

	ksp->ks_update = cxgen_update_txq_kstats;
	ksp->ks_private = (void *) &q->stats;
	kstat_install(ksp);
	q->ksp = ksp;

	return (DDI_SUCCESS);
}

static int
cxgen_update_txq_kstats(kstat_t *ksp, int rw)
{
	p_cxgen_txq_kstat_t kstatp = (p_cxgen_txq_kstat_t)ksp->ks_data;
	struct txq_stats *stats = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (0);

	KS_U_FROM(tx_imm_data, stats);
	KS_U_FROM(tx_lso, stats);
	KS_U_FROM(tx_pkt, stats);
	KS_U_FROM(used_small_buf, stats);
	KS_U_FROM(used_big_buf, stats);
	KS_U_FROM(txq_blocked, stats);
	KS_U_FROM(pullup, stats);
	KS_U_FROM(pullup_failed, stats);
	KS_U_FROM(dma_map_failed, stats);
	KS_U_FROM(max_frame_len, stats);
	KS_U_FROM(max_mblks_in_frame, stats);
	KS_U_FROM(max_dma_segs_in_mblk, stats);
	KS_U_FROM(max_dma_segs_in_frame, stats);

	return (0);
}

static void
cxgen_destroy_txq_kstats(struct sge_qset *qs, int idx)
{
	struct sge_txq *q = &qs->txq[idx];

	if (q->ksp) {
		kstat_delete(q->ksp);
		q->ksp = NULL;
	}
}

void
cxgen_setup_qset_kstats(p_adapter_t cxgenp, int idx)
{
	struct sge_qset *qs = &cxgenp->sge.qs[idx];
	int i;

	/* Response Queue */
	(void) cxgen_setup_rspq_kstats(qs);

	/* The two freelists */
	for (i = 0; i < SGE_RXQ_PER_SET; i++)
		(void) cxgen_setup_fl_kstats(qs, i);

	/* The three tx queues */
	for (i = 0; i < SGE_TXQ_PER_SET; i++)
		(void) cxgen_setup_txq_kstats(qs, i);
}

void
cxgen_destroy_qset_kstats(p_adapter_t cxgenp, int idx)
{
	struct sge_qset *qs = &cxgenp->sge.qs[idx];
	int i;

	/* Response Queue */
	cxgen_destroy_rspq_kstats(qs);

	/* The two freelists */
	for (i = 0; i < SGE_RXQ_PER_SET; i++)
		cxgen_destroy_fl_kstats(qs, i);

	/* The three tx queues */
	for (i = 0; i < SGE_TXQ_PER_SET; i++)
		cxgen_destroy_txq_kstats(qs, i);
}
