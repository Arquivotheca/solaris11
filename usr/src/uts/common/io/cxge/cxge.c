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

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/strsubr.h>
#include <sys/cmn_err.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/callb.h>
#include <sys/strsun.h>
#include <sys/stat.h>
#include <sys/cpu.h>
#include <sys/kstat.h>
#include <inet/common.h>
#include <inet/mi.h>
#include <inet/nd.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <inet/tcp.h>
#include <sys/pci.h>
#include <sys/pattr.h>
#include <sys/multidata.h>
#include <inet/nd.h>

/*
 * Keep the design clean.  DO NOT include <sys/mac.h> or <sys/gld.h> in this
 * file or in cxge.h
 */

#include "cxge_common.h"
#include "cxge_regs.h"
#include "cxge_t3_cpl.h"
#include "cxge_version.h"
#include "cxgen.h"
#include "cxge.h"

#define	CXGE_TICKS(a) drv_usectohz((a)->params.linkpoll_period ? \
	(a)->params.linkpoll_period * 100000 : \
	(a)->params.stats_update_period * 1000000)

/*
 * Device Node Operation functions
 */
static int cxge_attach(dev_info_t *, ddi_attach_cmd_t);
static int cxge_detach(dev_info_t *, ddi_detach_cmd_t);
static int cxge_quiesce(dev_info_t *dev_info);

/*
 * Power Management
 */
int cxge_suspend(struct port_info *);
int cxge_resume(struct port_info *);

/*
 * Misc. helper functions.
 */
static int cxge_init_ops();
static int cxge_fini_ops();
static void cxge_cleanup(p_cxge_t, uint_t);
static void cxge_link_start(p_cxge_t);
static void cxge_ticker(void *);
static void cxge_tick_handler(void *);
int cxge_linkchange_noop(struct port_info *, int, int, int, int);
static int cxge_rx_noop(struct sge_qset *, mblk_t *);
static int cxge_tx_update_noop(struct sge_qset *);
static int cxge_register_callbacks(p_cxge_t);
static int cxge_register_port(p_cxge_t);
static int cxge_unregister_port(p_cxge_t);
static int cxge_setup_kstats(p_cxge_t);
static void cxge_set_prop_defaults(p_cxge_t);

DDI_DEFINE_STREAM_OPS(cxge_ops, nulldev, nulldev, cxge_attach, cxge_detach,
	nodev, NULL, D_MP, NULL, cxge_quiesce);
/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops,
	"CXGE Ethernet Driver " DRV_VERSION,
	&cxge_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * List of cxge softstate structures.
 */
void *cxge_list;

static int
cxge_init_ops()
{
	cxge_mac_init_ops(&cxge_ops);
	return (0);
}

static int
cxge_fini_ops()
{
	cxge_mac_fini_ops(&cxge_ops);
	return (0);
}

int
_init(void)
{
	int rc;

	rc = ddi_soft_state_init(&cxge_list, sizeof (cxge_t), 0);
	if (rc != DDI_SUCCESS)
		return (rc);

	/*
	 * Determine the mode we'll operate in, and setup the ops structure for
	 * that mode.
	 */
	if (cxge_init_ops())
		return (DDI_FAILURE);

	rc = mod_install(&modlinkage);
	if (rc != DDI_SUCCESS) {
		(void) cxge_fini_ops();
		ddi_soft_state_fini(&cxge_list);
		return (rc);
	}

	return (DDI_SUCCESS);
}

int
_fini(void)
{
	int rc;

	rc = mod_remove(&modlinkage);
	if (rc != DDI_SUCCESS)
		return (rc);

	(void) cxge_fini_ops();
	ddi_soft_state_fini(&cxge_list);

	return (DDI_SUCCESS);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

#define	CXGE_CLEANUP_SOFTSTATE	0x01
#define	CXGE_CLEANUP_KSTATS	0x02
#define	CXGE_CLEANUP_MUTEX	0x04
#define	CXGE_CLEANUP_REGPORT	0x08

static int
cxge_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance, status;
	uint_t cleanup = 0;
	p_cxge_t cxgep;

	ASSERT(dip);

	switch (cmd) {
	case DDI_ATTACH:
		break;
	default:
#ifdef DEBUG
		cmn_err(CE_NOTE, "%s: attach op %d unimplemented",
		    __func__, cmd);
#endif
		return (DDI_FAILURE);
	}

	/*
	 * Get the device instance since we'll need to setup
	 * or retrieve a soft state for this instance.
	 */
	instance = ddi_get_instance(dip);

	/*
	 * Allocate soft device data structure
	 */
	status = ddi_soft_state_zalloc(cxge_list, instance);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to allocate soft_state: %d", status);
		goto cxge_attach_exit;
	}
	cleanup |= CXGE_CLEANUP_SOFTSTATE;

	cxgep = ddi_get_soft_state(cxge_list, instance);
	ASSERT(cxgep);

	cxgep->instance = instance;
	cxgep->dip = dip;

	/* Ask the nexus for the port_info associated with this port */
	cxgep->pi = cxgen_get_portinfo(ddi_get_parent(dip), dip);
	ASSERT(cxgep->pi);

	/* Configuration */
	cxge_set_prop_defaults(cxgep);

	/*
	 * Init the flags mutex.  This must be done before the minor node is
	 * created (as it is used during cxge_ioctl).
	 */
	mutex_init(&cxgep->lock, NULL, MUTEX_DRIVER, NULL);
	cleanup |= CXGE_CLEANUP_MUTEX;

	/*
	 * Register the driver to the MAC
	 */
	status = cxge_register_port(cxgep);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to register port: %d", status);
		goto cxge_attach_exit;
	}
	cleanup |= CXGE_CLEANUP_REGPORT;

	/* Too few to merit a cxge_kstats.c file. */
	status = cxge_setup_kstats(cxgep);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to create config kstats: %d", status);
		goto cxge_attach_exit;
	}
	cleanup |= CXGE_CLEANUP_KSTATS;


	ASSERT(status == DDI_SUCCESS);
	ddi_report_dev(dip);

cxge_attach_exit:
	if (status != DDI_SUCCESS)
		(void) cxge_cleanup(cxgep, cleanup);

	return (status);
}

static int
cxge_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	p_cxge_t cxge = DIP_TO_CXGE(dip);
	timeout_id_t timer = 0;

	/* we only understand DETACH */
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ASSERT(cxge);
	ASSERT((cxge->flags & CXGE_PLUMBED) == 0);

	/* Stop the tick handler if it is running. */
	mutex_enter(&cxge->lock);
	if (cxge->flags & CXGE_TICKING) {
		cxge->flags &= ~CXGE_TICKING;
		timer = cxge->timer;
	}
	mutex_exit(&cxge->lock);
	if (timer) {
		(void) untimeout(timer);
		ddi_taskq_wait(cxge->pi->adapter->tq);
		ASSERT(cxge->timer == timer);
	}

	(void) cxge_cleanup(cxge, ~0);

	return (DDI_SUCCESS);
}

/*
 * cxge_suspend
 */
int
cxge_suspend(struct port_info *pi)
{
	p_cxge_t cxgep = DIP_TO_CXGE(pi->dip);

	ASSERT(cxgep);

	/*
	 * Nexus driver made sure that this port was plumbed.
	 * So no need to check for plumb status here again.
	 */
	cxge_uninit(cxgep);

	return (DDI_SUCCESS);
}

/*
 * cxge_resume
 */
int
cxge_resume(struct port_info *pi)
{
	p_cxge_t cxgep = DIP_TO_CXGE(pi->dip);

	ASSERT(cxgep);

	/*
	 * Nexus driver made sure that this port was plumbed before suspend.
	 * So no need to check for plumb status here again.
	 */
	return (cxge_init(cxgep));
}

/*
 * cxge_quiesce
 *
 * The function enables fast-reboot. It quiesces the device so that the device
 * no longer generates interrupts and modifies or accesses memory.
 */
/* ARGSUSED */
static int
cxge_quiesce(dev_info_t *dip)
{
	/* Nothing to do in cxge. Nexus driver takes care of it */

	return (DDI_SUCCESS);
}

static void
cxge_cleanup(p_cxge_t cxgep, uint_t mask)
{
	if (mask & CXGE_CLEANUP_REGPORT) {
		(void) cxge_unregister_port(cxgep);
	}

	if (mask & CXGE_CLEANUP_MUTEX)
		mutex_destroy(&cxgep->lock);


	if (mask & CXGE_CLEANUP_KSTATS) {
		kstat_delete(cxgep->config_ksp);
		cxgep->config_ksp = NULL;
	}

	if (mask & CXGE_CLEANUP_SOFTSTATE) {
		ddi_soft_state_free(cxge_list, cxgep->instance);
	}
}

/*
 *	link_start - enable a port
 *	@p: the port to enable
 *
 *	Performs the MAC and PHY actions needed to enable a port.
 */
static void
cxge_link_start(p_cxge_t cxgep)
{
	struct port_info *p = cxgep->pi;
	struct cmac *mac = &p->mac;
	int mtu;

	/*
	 * Max acceptable frame size = MTU + Ether header + VLAN tag + CRC
	 * t3_mac_set_mtu() adds Ethernet header size
	 */
	mtu = cxgep->mtu + VLAN_TAGSZ;

	if (!mac->multiport)
		(void) t3_mac_init(mac);
	(void) t3_mac_set_mtu(mac, mtu);
	rw_enter(&p->rxmode_lock, RW_READER);
	cxge_rx_mode(p);
	rw_exit(&p->rxmode_lock);
	t3_link_start(&p->phy, mac, &p->link_config);
	(void) t3_mac_enable(mac, MAC_DIRECTION_RX | MAC_DIRECTION_TX);
}

static void
cxge_ticker(void *arg)
{
	ddi_taskq_t *tq = ((p_cxge_t)arg)->pi->adapter->tq;

	(void) ddi_taskq_dispatch(tq, cxge_tick_handler, arg, DDI_SLEEP);
}

static inline int
link_poll_needed(struct port_info *p)
{
	struct cphy *phy = &p->phy;

	if (phy->caps & POLL_LINK_1ST_TIME) {
		p->phy.caps &= ~POLL_LINK_1ST_TIME;
		return (1);
	}

	return (p->link_fault || !(phy->caps & SUPPORTED_LINK_IRQ));
}

/*
 * Tick handler is started at plumb and once it has been started it will run
 * till cxge detach.  This means it can run when port is plumbed or unplumbed.
 * But we are guaranteed that if the tick handler is called, the port was
 * plumbed in the past.  This means first_port_up has run already.
 */
static void
cxge_tick_handler(void *arg)
{
	p_cxge_t cxgep = arg;
	struct port_info *pi = cxgep->pi;
	p_adapter_t cxgenp = pi->adapter;
	uint_t lpp = cxgenp->params.linkpoll_period;
	uint_t sup = cxgenp->params.stats_update_period;
	clock_t ticks;
	hrtime_t ts;

	/*
	 * If the interface is unplumbed, always report link down.  Examining
	 * flags without cxgep->lock is a benign race.
	 */
	if (cxgep->flags & CXGE_PLUMBED) {
		if (link_poll_needed(pi))
			t3_link_changed(cxgenp, pi->port_id);
	} else {
		/* This takes care of duplex and speed also.  See cxge_m_stat */
		pi->link_config.link_ok = 0;
		(pi->link_change)(pi, 0, 0, 0, 0, 0);
	}

	/* accumulate stats so they don't overflow */
	mutex_enter(&pi->lock);
	ts = gethrtime();
	if (ts - cxgep->last_stats_update >= sup * 1000000000) {
		cxgep->last_stats_update = ts;
		(void) t3_mac_update_stats(&pi->mac);
	}
	ts = cxgep->last_stats_update + (sup * 1000000000) - ts;
	mutex_exit(&pi->lock);

	/*
	 * This portion does not have bad races with plumb/unplumb/detach, even
	 * though we do not hold cxgep->lock while looking for PLUMBED.
	 *
	 * plumb -> unplumb is handled by cxge_uninit, which disables the tick
	 * handler while it's unplumbing the interface.  It re-enables the tick
	 * after doing its business.
	 *
	 * unplumb -> plumb is a benign race.  The worst that could happen is
	 * that we'll miss the PLUMBED flag this time, but things will work from
	 * the next tick.
	 *
	 * unplumb -> unattached is like plumb -> unplumb, just that cxge_detach
	 * kills the tick permanently.
	 */
	if (cxgenp->params.rev == T3_REV_B2 && pi->link_config.link_ok &&
	    (cxgep->flags & CXGE_PLUMBED)) {
		int status, mtu;
		struct cmac *mac = &pi->mac;

		status = t3b2_mac_watchdog_task(mac);
		if (status == 1)
			pi->mac.stats.num_toggled++;
		else if (status == 2) {
			/*
			 * t3_mac_set_mtu() adds Ethernet header size
			 */
			mtu = cxgep->mtu + VLAN_TAGSZ;

			(void) t3_mac_set_mtu(mac, mtu);

			rw_enter(&pi->rxmode_lock, RW_READER);
			cxge_rx_mode(pi);
			rw_exit(&pi->rxmode_lock);

			t3_link_start(&pi->phy, mac, &pi->link_config);
			(void) t3_mac_enable(mac, MAC_DIRECTION_RX |
			    MAC_DIRECTION_TX);
			t3_port_intr_enable(cxgenp, pi->port_id);

			pi->mac.stats.num_resets++;
		}
	}

	/*
	 * synchronization: see untimeout() in cxge_uninit and cxge_detach.
	 */
	mutex_enter(&cxgep->lock);
	if ((cxgep->flags & CXGE_TICKING) == 0)
		goto out;

	/* schedule another one */

	if (lpp)
		ticks = drv_usectohz(lpp * 100000);
	else
		ticks = drv_usectohz(ts / 1000);

	cxgep->timer = timeout(cxge_ticker, cxgep, ticks);
	ASSERT(cxgep->timer);
out:
	mutex_exit(&cxgep->lock);
}

/* ARGSUSED */
int
cxge_linkchange_noop(struct port_info *pi, int link_status, int speed,
	int duplex, int fc)
{
#ifdef DEBUG
	cmn_err(CE_CONT, "%s: race condition occurred.", __func__);
#endif
	return (0);
}

/* ARGSUSED */
static int
cxge_rx_noop(struct sge_qset *qs, mblk_t *m)
{
#ifdef DEBUG
	cmn_err(CE_CONT, "%s: race condition occurred.", __func__);
#endif
	freemsgchain(m);

	return (0);
}

/* ARGSUSED */
static int
cxge_tx_update_noop(struct sge_qset *qs)
{
#ifdef DEBUG
	cmn_err(CE_CONT, "%s: race condition occurred.", __func__);
#endif
	return (0);
}

static int
cxge_register_callbacks(p_cxge_t cxgep)
{
	struct port_info *pi = cxgep->pi;

	pi->link_change = cxge_mac_link_changed;
	pi->rx = cxge_mac_rx;
	pi->tx_update = cxge_mac_tx_update;
	pi->port_suspend = cxge_suspend;
	pi->port_resume = cxge_resume;

	return (0);
}

static int
cxge_register_port(p_cxge_t cxgep)
{
	return (cxge_register_mac(cxgep));
}

static int
cxge_unregister_port(p_cxge_t cxgep)
{
	return (cxge_unregister_mac(cxgep));
}

/* Called when a port is plumbed (eg from mac->mc_start) */
int
cxge_init(p_cxge_t cxgep)
{
	struct port_info *pi = cxgep->pi;
	p_adapter_t cxgenp = pi->adapter;
	int status = 0;

	/*
	 * This part with adapter lock held.
	 */
	mutex_enter(&cxgenp->lock);

	/* mc_start calls aren't idempotent. */
	ASSERT(!(cxgenp->open_device_map & (1 << pi->port_id)));

	if (cxgenp->open_device_map == 0 && first_port_up(cxgenp)) {
		mutex_exit(&cxgenp->lock);
		cmn_err(CE_WARN, "one time init failed.");
		status = EIO;
		goto cxge_init_exit;
	}
	if (cxgenp->open_device_map == 0) {
		t3_intr_clear(cxgenp);
	}
	cxgenp->open_device_map |= (1U << pi->port_id);
	mutex_exit(&cxgenp->lock);

	/* This part with the port lock held */
	mutex_enter(&pi->lock);
	bzero(&pi->mac.stats, sizeof (pi->mac.stats));
	mutex_exit(&pi->lock);

	cxgep->last_stats_update = 0;

	/* Callbacks that the nexus will make into the leaf. */
	(void) cxge_register_callbacks(cxgep);

	/* Bring up the phy + transceiver */
	cxge_link_start(cxgep);

	t3_port_intr_enable(cxgenp, pi->port_id);

	mutex_enter(&cxgep->lock);
	cxgep->flags |= CXGE_PLUMBED;
	mutex_exit(&cxgep->lock);

	/*
	 * Tick may already be running if this port was plumbed in the past.
	 * Start it if it's not.  Safe to do without cxgep->lock
	 */
	if (!(cxgep->flags & CXGE_TICKING)) {
		cxgep->flags |= CXGE_TICKING;
		cxgep->timer = timeout(cxge_ticker, cxgep, CXGE_TICKS(cxgenp));
	}

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
		status = EIO;
	}

cxge_init_exit:
	return (status);
}

/* Called when a port is unplumbed (eg from mac->mc_stop) */
void
cxge_uninit(p_cxge_t cxgep)
{
	struct port_info *pi = cxgep->pi;
	p_adapter_t cxgenp = pi->adapter;
	timeout_id_t timer;
	struct mcaddr_list *mcl, *next;
#ifdef DEBUG
	uint_t x, i;
#endif

	mutex_enter(&cxgep->lock);
	cxgep->flags &= ~CXGE_PLUMBED;
	cxgep->flags &= ~CXGE_TICKING; /* Stop the tick temporarily */
	timer = cxgep->timer;
	mutex_exit(&cxgep->lock);

	(void) untimeout(timer);
	ddi_taskq_wait(cxgenp->tq);
	ASSERT(cxgep->timer == timer);

	/* Free the mcaddr_list */
	rw_enter(&pi->rxmode_lock, RW_WRITER);
	mcl = pi->mcaddr_list;
#ifdef DEBUG
	x = pi->mcaddr_count;
#endif
	pi->mcaddr_list = NULL; /* emptied */
	pi->mcaddr_count = 0;
	rw_exit(&pi->rxmode_lock);
	while (mcl) {
#ifdef DEBUG
		for (i = 0; i < MC_BUCKET_SIZE; i++) {
			if (mcl->valid & (1 << i))
				x--;
		}
#endif
		next = mcl->next;
		kmem_free(mcl, sizeof (struct mcaddr_list));
		mcl = next;
	}
	ASSERT(x == 0); /* mcaddr_list logic broken if this occurs */

	t3_xgm_intr_disable(pi->adapter, pi->port_id);
	(void) t3_read_reg(pi->adapter, A_XGM_INT_STATUS + pi->mac.offset);
	t3_port_intr_disable(pi->adapter, pi->port_id);

	/* We set these to no-ops rather than NULL to avoid races. */
	(void) atomic_swap_ptr(&pi->rx, (void *)cxge_rx_noop);
	(void) atomic_swap_ptr(&pi->tx_update, (void *)cxge_tx_update_noop);

	/* disable pause frames */
	t3_set_reg_field(pi->adapter, A_XGM_TX_CFG + pi->mac.offset,
	    F_TXPAUSEEN, 0);

	/* Reset RX FIFO HWM */
	t3_set_reg_field(pi->adapter, A_XGM_RXFIFO_CFG +  pi->mac.offset,
	    V_RXFIFOPAUSEHWM(M_RXFIFOPAUSEHWM), 0);

	/* Early hint to sge_tx_data to stop tx */
	pi->link_config.link_ok = 0;

	/* Wait for TXFIFO empty */
	msleep(100);
	(void) t3_wait_op_done(pi->adapter, A_XGM_TXFIFO_CFG + pi->mac.offset,
	    F_TXFIFO_EMPTY, 1, 20, 5);

	msleep(100);
	(void) t3_mac_disable(&pi->mac, MAC_DIRECTION_RX);

	/* power down the transceiver */
	pi->phy.ops->power_down(&pi->phy, 1);

	/*
	 * This part with adapter lock held.
	 */
	mutex_enter(&cxgenp->lock);

	/* mc_stop calls aren't idempotent */
	ASSERT(cxgenp->open_device_map & (1 << pi->port_id));

	cxgenp->open_device_map &= ~(1U << pi->port_id);
	if (cxgenp->open_device_map == 0 && last_port_down(cxgenp))
		cmn_err(CE_WARN, "failed to release all resources.");

	mutex_exit(&cxgenp->lock);

	/*
	 * TODO: Horribly hack-ish (and unreliable) synchronization with the rx
	 * threads (MSI and INTx mode only).  The correct thing to do is to
	 * instruct the threads to quiesce, wait till they do so, and then
	 * proceed.  Or, to change the life cycle of the threads from
	 * first_port_up/cxgen_detach to to plumb/unplumb and kill them here.
	 *
	 * The synch. problem does not affect MSI-X and occurs rarely, so this
	 * duct tape code is sufficient for the time being.  Will fix the right
	 * way later.
	 */
	if ((cxgenp->flags & USING_MSIX) == 0)
		msleep(250);

	/* Set link state as MAC_LINK_UNKNOWN (-1) */
	(pi->link_change)(pi, -1, 0, 0, 0, 0);

	/* Restart the tick, don't need cxge->lock here */
	cxgep->flags |= CXGE_TICKING;
	cxgep->timer = timeout(cxge_ticker, cxgep, CXGE_TICKS(cxgenp));

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
	}
}

/* ARGSUSED */
void
cxge_ioctl(p_cxge_t cxgep, queue_t *wq, mblk_t *mp)
{
	/* Nothing as of now */
	miocnak(wq, mp, 0, EINVAL);
}

int
cxge_add_multicast(p_cxge_t cxgep, const uint8_t *addr)
{
	struct port_info *pi = cxgep->pi;
	struct mcaddr_list *m, *es_m = NULL, **nb;
	int i, empty_slot_found = 0, es_i = 0;

	/* must hold a write lock on rx_mode */
	ASSERT(rw_read_locked(&pi->rxmode_lock) == 0);

	m = pi->mcaddr_list;
	nb = &pi->mcaddr_list;
	while (m) {
		for (i = 0; i < MC_BUCKET_SIZE; i++) {
			if (m->valid & (1 << i)) {
				if (!(bcmp(addr, &m->addr[i], ETHERADDRL)))
					return (0); /* already in list */
			} else {
				if (!empty_slot_found) {
					empty_slot_found = 1;
					es_m = m;
					es_i = i;
				}
			}
		}
		nb = &m->next;
		m = m->next;
	}

	/* We didn't find the L2 mcast addr in our list, it has to be added */
	if (empty_slot_found) {
		/* Copy the address to the empty slot we'd found earlier */
		bcopy(addr, &es_m->addr[es_i], ETHERADDRL);
		es_m->valid |= (1 << es_i);
	} else {
		/* No empty slots, need to allocate a new bucket */
		ASSERT(*nb == NULL);
		*nb = kmem_zalloc(sizeof (struct mcaddr_list), KM_NOSLEEP);
		if (*nb == NULL) {
			cmn_err(CE_WARN, "add_multicast FAILED due to zalloc");
			return (0);
		}

		(*nb)->valid = 0x1;
		bcopy(addr, (*nb)->addr, ETHERADDRL);
	}
	pi->mcaddr_count++;

	return (1);
}

int
cxge_del_multicast(p_cxge_t cxgep, const uint8_t *addr)
{
	struct port_info *pi = cxgep->pi;
	struct mcaddr_list *m, *pb; /* previous bucket */
	int i;

	/* must hold a write lock on rx_mode */
	ASSERT(rw_read_locked(&pi->rxmode_lock) == 0);

	m = pi->mcaddr_list;
	pb = m;

	while (m) {
		ASSERT(m->valid);
		for (i = 0; i < MC_BUCKET_SIZE; i++) {
			if ((m->valid & (1 << i)) == 0)
				continue;

			if (!(bcmp(addr, &m->addr[i], ETHERADDRL))) {
				/* found */
				pi->mcaddr_count--;

				/* Mark entry invalid */
				m->valid &= ~(1 << i);
				if (m->valid) /* bucket not empty */
					return (1);

				/* the bucket that m points to is now empty */

				if (m == pi->mcaddr_list)
					pi->mcaddr_list = m->next;
				else
					pb->next = m->next;

				kmem_free(m, sizeof (struct mcaddr_list));

				return (1);
			}
		}
		pb = m;
		m = m->next;
	}

	/* Not found, no need to update rx_mode */
	return (0);
}

int
cxge_tx_lb_queue(p_cxge_t cxgep, mblk_t *mp)
{
	struct ether_header *eh = (void *)mp->b_rptr;

	ASSERT(MBLKL(mp) >= sizeof (struct ether_header));

	return (eh->ether_dhost.ether_addr_octet[5] % cxgep->pi->nqsets);
}

void
cxge_rx_mode(struct port_info *p)
{
	struct cmac *mac = &p->mac;
	struct t3_rx_mode rm;
	int i;

	/* must hold a read lock on rx_mode */
	ASSERT(rw_read_locked(&p->rxmode_lock));
	ASSERT(p->ucaddr_count <= EXACT_ADDR_FILTERS);

	if (p->ucaddr_count == 0) {
		(void) t3_mac_set_num_ucast(mac, 1);
		(void) t3_mac_set_address(mac, 0, p->hw_addr);
	} else {
		(void) t3_mac_set_num_ucast(mac, p->ucaddr_count);
		for (i = 0; i < p->ucaddr_count; i++)
			(void) t3_mac_set_address(mac, i, UCADDR(p, i));
	}
	t3_init_rx_mode(&rm, p);
	(void) t3_mac_set_rx_mode(mac, &rm);
}

typedef struct cxge_config_kstat_s {
	kstat_named_t controller;
	kstat_named_t first_qset;
	kstat_named_t nqsets;
	kstat_named_t factory_mac_address;
	kstat_named_t media;
	kstat_named_t mode;
} cxge_config_kstat_t, *p_cxge_config_kstat_t;

static int
cxge_setup_kstats(p_cxge_t cxgep)
{
	struct kstat *ksp;
	p_cxge_config_kstat_t kstatp;
	struct port_info *pi = cxgep->pi;
	dev_info_t *pdip = ddi_get_parent(cxgep->dip);
	int ndata;
	uint8_t *ma = &pi->hw_addr[0];

	ASSERT(cxgep->config_ksp == NULL);

	ndata = sizeof (cxge_config_kstat_t) / sizeof (kstat_named_t);
	ksp = kstat_create(CXGE_DEVNAME, cxgep->instance, "config", "port",
	    KSTAT_TYPE_NAMED, ndata, 0);
	if (ksp == NULL)
		return (DDI_FAILURE);

	kstatp = (p_cxge_config_kstat_t)ksp->ks_data;

	kstat_named_init(&kstatp->controller, "controller", KSTAT_DATA_CHAR);
	(void) snprintf(kstatp->controller.value.c, 16,  "%s%d",
	    ddi_driver_name(pdip), ddi_get_instance(pdip));

	kstat_named_init(&kstatp->first_qset, "first_qset", KSTAT_DATA_ULONG);
	kstatp->first_qset.value.ul = pi->first_qset;

	kstat_named_init(&kstatp->nqsets, "qsets", KSTAT_DATA_ULONG);
	kstatp->nqsets.value.ul = pi->nqsets;

	kstat_named_init(&kstatp->factory_mac_address, "factory_mac_address",
	    KSTAT_DATA_CHAR);
	(void) snprintf(kstatp->factory_mac_address.value.c, 16,
	    "%02x%02x%02x%02x%02x%02x",
	    ma[0], ma[1], ma[2], ma[3], ma[4], ma[5]);

	kstat_named_init(&kstatp->media, "media", KSTAT_DATA_CHAR);
	(void) snprintf(kstatp->media.value.c, 16, "%s", pi->phy.desc);

	kstat_install(ksp);
	cxgep->config_ksp = ksp;
	return (DDI_SUCCESS);
}

#define	CXGE_SET_ADV_PROP(prop) \
	do if (lc->supported & SUPPORTED_##prop) { \
		lc->advertising &= ~ADVERTISED_##prop; \
		_NOTE(CONSTCOND) \
	} while (0)

/* Set default properties */
static void
cxge_set_prop_defaults(p_cxge_t cxgep)
{
	struct link_config *lc = &cxgep->pi->link_config;

	/* jumbo frames: defaults to disabled */
	cxgep->mtu = ETHERMTU;

	/* hardware checksumming: defaults to enabled */
	cxgep->hw_csum = 1;

	/* LSO: defaults to enabled */
	cxgep->lso = 1;

	/* rx pause: defaults to enabled */
	lc->requested_fc |= PAUSE_RX;

	/* tx pause: defaults to enabled */
	lc->requested_fc |= PAUSE_TX;

	(void) cxge_set_coalesce(cxgep, 0);

	/*
	 * Advertisements are irrelevant when autonegotiation is not supported.
	 */
	if (lc->supported & SUPPORTED_Autoneg) {
		CXGE_SET_ADV_PROP(Autoneg);
		CXGE_SET_ADV_PROP(10000baseT_Full);
		CXGE_SET_ADV_PROP(1000baseT_Full);
		CXGE_SET_ADV_PROP(1000baseT_Half);
		CXGE_SET_ADV_PROP(Pause);
		CXGE_SET_ADV_PROP(Asym_Pause);
	}
}

int
cxge_set_coalesce(p_cxge_t cxgep, int v)
{
	struct port_info *pi = cxgep->pi;
	struct sge_qset *qs = &pi->adapter->sge.qs[pi->first_qset];
	int i;

	if (v < 1 || v > 250)
		return (EINVAL);

	for (i = 0; i < pi->nqsets; i++)
		qs[i].rspq.holdoff_tmr = max(v * 10, 1U);

	return (0);
}

int
cxge_set_desc_budget(p_cxge_t cxgep, int v)
{
	struct port_info *pi = cxgep->pi;
	struct sge_qset *qs = &pi->adapter->sge.qs[pi->first_qset];
	struct sge_rspq *q = &qs->rspq;
	int i;

	if (v < 1 || v > q->depth)
		return (EINVAL);

	for (i = 0; i < pi->nqsets; i++)
		qs[i].rspq.budget.descs = v;

	return (0);
}

int
cxge_set_frame_budget(p_cxge_t cxgep, int v)
{
	struct port_info *pi = cxgep->pi;
	struct sge_qset *qs = &pi->adapter->sge.qs[pi->first_qset];
	struct sge_rspq *q = &qs->rspq;
	int i;

	if (v < 1 || v > q->depth)
		return (EINVAL);

	for (i = 0; i < pi->nqsets; i++)
		qs[i].rspq.budget.frames = v;

	return (0);
}
