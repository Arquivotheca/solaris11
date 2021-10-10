/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * iprb -- Intel Pro100/B (8255x) Fast Ethernet Driver
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/dlpi.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/strsun.h>
#include <sys/stat.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>
#include <sys/note.h>
#include <sys/mii.h>
#include <sys/miiregs.h>


#include "iprb.h"
#include "rcvbundl.h"

/*
 * Declarations and Module Linkage
 */

#define	IDENT	"Intel 8255x Fast Ethernet"

#ifdef IPRBDEBUG
/* used for debugging */
int		iprbdebug = 0;
#endif

/* Required system entry points */
static		int iprbprobe(dev_info_t *);
static		int iprbattach(dev_info_t *, ddi_attach_cmd_t);
static		int iprbdetach(dev_info_t *, ddi_detach_cmd_t);
static		int iprbquiesce(dev_info_t *);
static		int iprbsuspend(dev_info_t *);
static		int iprbresume(dev_info_t *);

/* Required driver entry points for GLDv3 */
static int iprb_m_start(void *);
static void iprb_m_stop(void *);
static int iprb_m_unicst(void *, const uint8_t *);
static int iprb_m_multicst(void *, boolean_t, const uint8_t *);
static int iprb_m_promisc(void *, boolean_t);
static int iprb_m_getstat(void *, uint_t, uint64_t *);
static int iprb_m_getprop(void *, const char *, mac_prop_id_t, uint_t,
    void *);
static void iprb_m_propinfo(void *, const char *, mac_prop_id_t,
    mac_prop_info_handle_t);
static int iprb_m_setprop(void *, const char *, mac_prop_id_t, uint_t,
    const void *);
static void iprb_m_ioctl(void *, queue_t *, mblk_t *);

static mblk_t *iprb_m_tx(void *, mblk_t *);
static uint_t iprb_intr(caddr_t);

/* Functions to handle board command queue */
static	void	iprb_start(struct iprbinstance *);
static	void	iprb_stop(struct iprbinstance *);
static	mblk_t	*iprb_send(struct iprbinstance *, mblk_t *);
static	void	iprb_configure(struct iprbinstance *);
static	void	iprb_init_board(struct iprbinstance *);
static	void	iprb_add_command(struct iprbinstance *);
static	void	iprb_reap_commands(struct iprbinstance *, int reap_mode);
static	void	iprb_load_microcode(struct iprbinstance *, uchar_t);
static	boolean_t	iprb_diag_test(struct iprbinstance *);
static	boolean_t	iprb_self_test(struct iprbinstance *);
static	void	iprb_getprop(struct iprbinstance *iprbp);

/* Functions to R/W to PROM */
static	void
iprb_readia(struct iprbinstance *iprbp, uint16_t *addr,
		uint8_t offset);
static	void
iprb_shiftout(struct iprbinstance *iprbp, uint16_t data,
		uint8_t count);
static	void	iprb_raiseclock(struct iprbinstance *iprbp, uint8_t *eex);
static	void	iprb_lowerclock(struct iprbinstance *iprbp, uint8_t *eex);
static	uint16_t	iprb_shiftin(struct iprbinstance *iprbp);
static	void	iprb_eeclean(struct iprbinstance *iprbp);
static	void	iprb_writeia(struct iprbinstance *iprbp,
			ushort_t reg, ushort_t data);
static	void	iprb_update_checksum(struct iprbinstance *iprbp);
static	void	iprb_standby(struct iprbinstance *iprbp);
static	ushort_t iprb_wait_eeprom_cmd_done(struct iprbinstance *iprbp);


/* Functions to handle DMA memory resources */
static	int	iprb_alloc_dma_resources(struct iprbinstance *iprbp);
static	void	iprb_free_dma_resources(struct iprbinstance *iprbp);

/* Functions to handle Rx operation and 'Free List' manipulation */

static	mblk_t	*iprb_process_recv(struct iprbinstance *);
static	struct	iprb_rfd_info *iprb_get_buffer(struct iprbinstance *);
static	struct	iprb_rfd_info *iprb_alloc_buffer(struct iprbinstance *);
static	void	iprb_remove_buffer(struct iprbinstance *);
static	void	iprb_rcv_complete(struct iprb_rfd_info *);
static	void	iprb_release_mblks(struct iprbinstance *);

/* Misc. functions */
static void	iprb_update_stats(struct iprbinstance *);
static	void	iprb_RU_wdog(void *arg);
static void	iprb_hard_reset(struct iprbinstance *);
static void	iprb_get_ethaddr(struct iprbinstance *);
static boolean_t	iprb_prepare_xmit_buff(struct iprbinstance *,
    mblk_t *, size_t);
static ushort_t iprb_get_dev_type(ushort_t devid, ushort_t revid);

static	ushort_t iprb_get_eeprom_size(struct iprbinstance *);

#ifdef IPRBDEBUG
static void	iprb_print_board_state(struct iprbinstance *);
#endif

/* MII interface functions */

static void	iprb_phyinit(struct iprbinstance *);
static uint16_t	iprb_mii_readreg(void *, uint8_t phy, uint8_t reg);
static void	iprb_mii_writereg(void *, uint8_t phy, uint8_t reg, uint16_t);
static void	iprb_mii_notify(void *, link_state_t);

#define	GETPROP(iprbp, name, default)			\
	ddi_prop_get_int(DDI_DEV_T_NONE, iprbp->iprb_dip,	\
	    DDI_PROP_DONTPASS, name, default)

static mac_callbacks_t iprb_m_callbacks = {
	MC_IOCTL | MC_SETPROP | MC_GETPROP | MC_PROPINFO,
	iprb_m_getstat,
	iprb_m_start,
	iprb_m_stop,
	iprb_m_promisc,
	iprb_m_multicst,
	iprb_m_unicst,
	iprb_m_tx,
	NULL,
	iprb_m_ioctl,
	NULL,	/* mc_getcapab */
	NULL,	/* mc_open */
	NULL,	/* mc_close */
	iprb_m_setprop,
	iprb_m_getprop,
	iprb_m_propinfo
};

/* DMA attributes for a control command block */
static ddi_dma_attr_t control_cmd_dma_attr = {
	DMA_ATTR_V0,	/* version of this structure */
	0,		/* lowest usable address */
	0xffffffffU,	/* highest usable address */
	0x7fffffff,	/* maximum DMAable byte count */
	4,		/* alignment in bytes */
	0x100,		/* burst sizes (any?) */
	1,		/* minimum transfer */
	0xffffffffU,	/* maximum transfer */
	0xffffffffU,	/* maximum segment length */
	1,		/* maximum number of segments */
	1,		/* granularity */
	0,		/* flags (reserved) */
};

/*
 * for Dynamic TBDs, TBD buffer must be DWORD aligned. This is due to the
 * possibility of interruptions to the PCI bus operations (e.g. disconnect)
 * that may result in the device reading only half of the TX buffer pointer.
 * If this half was zero and the pointer is then updated by the driver before
 * the latter word is read by the device, device may use the invalid pointer.
 */

/* DMA attributes for a transmit buffer descriptor array */
static ddi_dma_attr_t tx_buffer_desc_dma_attr = {
	DMA_ATTR_V0,	/* version of this structure */
	0,		/* lowest usable address */
	0xffffffffU,	/* highest usable address */
	0x7fffffff,	/* maximum DMAable byte count */
	4,		/* alignment in bytes */
	0x100,		/* burst sizes (any?) */
	1,		/* minimum transfer */
	0xffffffffU,	/* maximum transfer */
	0xffffffffU,	/* maximum segment length */
	1,		/* maximum number of segments */
	1,		/* granularity */
	0,		/* flags (reserved) */
};

/* DMA attributes for a transmit buffer */
static ddi_dma_attr_t tx_buffer_dma_attr = {
	DMA_ATTR_V0,	/* version of this structure */
	0,		/* lowest usable address */
	0xffffffffU,	/* highest usable address */
	0x7fffffff,	/* maximum DMAable byte count */
	1,		/* alignment in bytes */
	0x100,		/* burst sizes (any?) */
	1,		/* minimum transfer */
	0xffffffffU,	/* maximum transfer */
	0xffffffffU,	/* maximum segment length */
	2,		/* maximum number of segments */
	1,		/* granularity */
	0,		/* flags (reserved) */
};


/* DMA attributes for a receive frame descriptor */
static ddi_dma_attr_t rfd_dma_attr = {
	DMA_ATTR_V0,	/* version of this structure */
	0,		/* lowest usable address */
	0xffffffffU,	/* highest usable address */
	0x7fffffff,	/* maximum DMAable byte count */
	4,		/* alignment in bytes */
	0x100,		/* burst sizes (any?) */
	1,		/* minimum transfer */
	0xffffffffU,	/* maximum transfer */
	0xffffffffU,	/* maximum segment length */
	1,		/* maximum number of segments */
	1,		/* granularity */
	0,		/* flags (reserved) */
};

/* DMA attributes for a statistics buffer */
static ddi_dma_attr_t stats_buffer_dma_attr = {
	DMA_ATTR_V0,	/* version of this structure */
	0,		/* lowest usable address */
	0xffffffffU,	/* highest usable address */
	0x7fffffff,	/* maximum DMAable byte count */
	16,		/* alignment in bytes */
	0x100,		/* burst sizes (any?) */
	1,		/* minimum transfer */
	0xffffffffU,	/* maximum transfer */
	0xffffffffU,	/* maximum segment length */
	1,		/* maximum number of segments */
	1,		/* granularity */
	0,		/* flags (reserved) */
};

/* DMA attributes for a self test buffer */
static ddi_dma_attr_t selftest_buffer_dma_attr = {
	DMA_ATTR_V0,	/* version of this structure */
	0,		/* lowest usable address */
	0xffffffffU,	/* highest usable address */
	0x7fffffff,	/* maximum DMAable byte count */
	16,		/* alignment in bytes */
	0x100,		/* burst sizes (any?) */
	1,		/* minimum transfer */
	0xffffffffU,	/* maximum transfer */
	0xffffffffU,	/* maximum segment length */
	1,		/* maximum number of segments */
	1,		/* granularity */
	0,		/* flags (reserved) */
};

/* DMA access attributes  <Little Endian Card> */
static ddi_device_acc_attr_t accattr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
};

static uchar_t iprb_bcast[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static struct dev_supp_features devsupf[] = DEV_SUPPORTED_FEATURES;

/* Standard Module linkage initialization for a Streams driver */

DDI_DEFINE_STREAM_OPS(iprb_dev_ops, nulldev, iprbprobe, iprbattach,
    iprbdetach,  NULL, NULL, D_MP, NULL, iprbquiesce);

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	IDENT,		/* short description */
	&iprb_dev_ops	/* driver specific ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};

int
_init(void)
{
	int	rv;
	mac_init_ops(&iprb_dev_ops, "iprb");
	if ((rv = mod_install(&modlinkage)) != DDI_SUCCESS) {
		mac_fini_ops(&iprb_dev_ops);
	}
	return (rv);
}

int
_fini(void)
{
	int 	rv;
	if ((rv = mod_remove(&modlinkage)) == DDI_SUCCESS) {
		mac_fini_ops(&iprb_dev_ops);
	}
	return (rv);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * DDI Entry Points
 */

static int
iprbprobe(dev_info_t *devinfo)
{
	ddi_acc_handle_t	handle;
	uint16_t		cmdreg;
	unsigned char		iprb_revision_id;
	uint16_t		iprb_device_id;
	ushort_t		iprb_mwi_enable;
	ushort_t		dev_type;

	if (pci_config_setup(devinfo, &handle) != DDI_SUCCESS)
		return (DDI_PROBE_FAILURE);

	cmdreg = pci_config_get16(handle, PCI_CONF_COMM);
	iprb_revision_id = pci_config_get8(handle, PCI_CONF_REVID);
	iprb_device_id = pci_config_get16(handle, PCI_CONF_DEVID);
	/* This code is needed to workaround a bug in the framework */
	iprb_mwi_enable = ddi_prop_get_int(DDI_DEV_T_ANY, devinfo,
	    DDI_PROP_DONTPASS, "MWIEnable", IPRB_DEFAULT_MWI_ENABLE);

	dev_type = iprb_get_dev_type(iprb_device_id, iprb_revision_id);

	/*
	 * MWI should only be enabled for D101A4 and above i.e. 82558 and
	 * above
	 */
	if (iprb_mwi_enable == 1 && devsupf[dev_type].mwi == 1)
		cmdreg |= PCI_COMM_MAE | PCI_COMM_ME |
		    PCI_COMM_MEMWR_INVAL | PCI_COMM_IO;
	else
		cmdreg |= PCI_COMM_MAE | PCI_COMM_ME | PCI_COMM_IO;

	/* Write it back to pci config register */
	pci_config_put16(handle, PCI_CONF_COMM, cmdreg);

	pci_config_teardown(&handle);
	return (DDI_PROBE_SUCCESS);
}

/*
 * attach(9E) -- Attach a device to the system
 *
 * Called once for each board successfully probed.
 */
static int
iprbattach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	mac_register_t	*macp;
	struct iprbinstance *iprbp;	/* Our private device info */
	uint16_t	i;
	ddi_acc_handle_t handle;
	ushort_t	word_A;

	switch (cmd) {
	case DDI_RESUME:
		return (iprbresume(devinfo));
	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	iprbp = kmem_zalloc(sizeof (*iprbp), KM_SLEEP);
	ddi_set_driver_private(devinfo, iprbp);

	iprbp->instance = ddi_get_instance(devinfo);
	iprbp->iprb_dip = devinfo;

	/*
	 * This is to access pci config space as probe and attach may be
	 * called by different threads for different instances of the NICs
	 */
	if (pci_config_setup(devinfo, &handle) != DDI_SUCCESS) {
		kmem_free(iprbp, sizeof (struct iprbinstance));
		return (DDI_FAILURE);
	}

	/* Initialize our private fields in iprbinstance */
	/* ExtractVVendor & device id */
	iprbp->iprb_vendor_id = pci_config_get16(handle, PCI_CONF_VENID);
	iprbp->iprb_device_id = pci_config_get16(handle, PCI_CONF_DEVID);
	/* Now we need to extract revision id from pci config space... */
	iprbp->iprb_revision_id = pci_config_get8(handle, PCI_CONF_REVID);
	iprbp->phy_id = 0;
	/* No access to pci config space beyond this point */
	pci_config_teardown(&handle);
	/* get all the parameters */
	iprb_getprop(iprbp);

	iprbp->iprb_dev_type = iprb_get_dev_type(iprbp->iprb_device_id,
	    iprbp->iprb_revision_id);

	if (ddi_regs_map_setup(devinfo, 1, (caddr_t *)&iprbp->port, 0, 0,
	    &accattr, &iprbp->iohandle) != DDI_SUCCESS) {
		kmem_free(iprbp, sizeof (struct iprbinstance));
		return (DDI_FAILURE);
	}
	/* Self Test */
	if ((ushort_t)iprbp->do_self_test == 1) {
		if (iprb_self_test(iprbp) != B_TRUE) {
			goto afail;
		}
	}

	/* Reset the hardware */
	iprb_hard_reset(iprbp);

	/* Disable Interrupts */
	DisableInterrupts(iprbp);

	/* Get the board's eeprom size */
	(void) iprb_get_eeprom_size(iprbp);

	/* Get the board's Sub vendor Id and device id */
	iprb_readia(iprbp, &(iprbp->iprb_sub_system_id), IPRB_SUB_SYSTEM_ID);
	iprb_readia(iprbp, &(iprbp->iprb_sub_vendor_id), IPRB_SUB_VENDOR_ID);

	/*
	 * ICH2 Fix. Only required for ICH2 based silicon.
	 * i.e. 815E, 810E, 820E...Do not use Speed as this happens
	 * earlier than speed detection process.
	 */

	if (iprbp->iprb_device_id == IPRB_PCI_DEVID_2449) {

		/* Clear Stand by power management for ICH2 platforms */
		iprb_readia(iprbp, &word_A, IPRB_EEPROM_WORDA);

		/* Clear bit 2 that puts controller in standby mode. */
		word_A = word_A & 0xFFFD;
		iprb_writeia(iprbp, IPRB_EEPROM_WORDA, word_A);
		iprb_update_checksum(iprbp);
	}

	/* Get the board's vendor-assigned hardware network address */
	(void) iprb_get_ethaddr(iprbp);

	/* Get the board's Compatabilty information from the EEPROM image */
	iprb_readia(iprbp, &i, IPRB_EEPROM_COMP_WORD);
	/* RU lockup affects this card ? */
	if ((i & 0x03) != 3)
		iprbp->RU_needed = 1;

	/*
	 * Do anything necessary to prepare the board for operation short of
	 * actually starting the board.
	 */
	if (ddi_get_iblock_cookie(devinfo, 0, &iprbp->icookie) != DDI_SUCCESS)
		goto afail;

	mutex_init(&iprbp->freelist_mutex, NULL, MUTEX_DRIVER, iprbp->icookie);
	mutex_init(&iprbp->cmdlock, NULL, MUTEX_DRIVER, iprbp->icookie);
	mutex_init(&iprbp->intrlock, NULL, MUTEX_DRIVER, iprbp->icookie);

	if (iprb_alloc_dma_resources(iprbp) == DDI_FAILURE)
		goto late_afail;

	/*
	 * This will prevent receiving interrupts before device is ready, as
	 * we are initializing device after setting the interrupts. So we
	 * will not get our interrupt handler invoked by OS while our device
	 * is still coming up or timer routines will not start till we are
	 * all set to process...
	 */

	/* Add the interrupt handler */
	(void) ddi_add_intr(devinfo, 0, NULL, NULL, iprb_intr, (caddr_t)iprbp);

	if (iprbp->max_rxbcopy > ETHERMTU + sizeof (struct ether_vlan_header))
		iprbp->max_rxbcopy = ETHERMTU +
		    sizeof (struct ether_vlan_header);
	if (iprbp->iprb_threshold > IPRB_MAX_THRESHOLD)
		iprbp->iprb_threshold = IPRB_MAX_THRESHOLD;
	if (iprbp->iprb_threshold < 0)
		iprbp->iprb_threshold = 0;

	iprb_init_board(iprbp);

	/* Configure physical layer */
	iprb_phyinit(iprbp);
	/* Do a Diag Test */
	if ((ushort_t)iprbp->do_diag_test == 1)
		if (iprb_diag_test(iprbp) != B_TRUE) {
			ddi_remove_intr(devinfo, 0, iprbp->icookie);
			goto late_afail;
		}
	/* Change for 82558 enhancement */
	/*
	 * If D101 and if the user has enabled flow control, set up the Flow
	 * Control Reg. in the CSR.
	 * Do not enable flow control for embedded 82559 (dev id 2449)
	 */
	if (devsupf[iprbp->iprb_dev_type].flowcntrl == 1) {
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_FC_THLD_REG),
		    IPRB_DEFAULT_FC_THLD);
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_FC_CMD_REG),
		    IPRB_DEFAULT_FC_CMD);
	}
	EnableInterrupts(iprbp);

#if defined(__lint) && defined(IPRBDEBUG)
	iprb_print_board_state(iprbp);
#endif


	if ((macp = mac_alloc(MAC_VERSION)) == NULL) {
		ddi_remove_intr(devinfo, 0, iprbp->icookie);
		goto late_afail;
	}

	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = iprbp;
	macp->m_dip = devinfo;
	macp->m_src_addr = iprbp->macaddr;
	macp->m_callbacks = &iprb_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = ETHERMTU;
	macp->m_margin = VLAN_TAGSZ;

	if (mac_register(macp, &iprbp->mh) == DDI_SUCCESS) {
		mac_free(macp);
		return (DDI_SUCCESS);
	}

	/* failed to register with mac */

	iprb_stop(iprbp);

	ddi_remove_intr(devinfo, 0, iprbp->icookie);

	/* failed to register with MAC */
	mac_free(macp);

late_afail:
	if (iprbp->mii)
		mii_free(iprbp->mii);
	iprb_free_dma_resources(iprbp);

	mutex_destroy(&iprbp->freelist_mutex);
	mutex_destroy(&iprbp->cmdlock);
	mutex_destroy(&iprbp->intrlock);

afail:
	ddi_regs_map_free(&iprbp->iohandle);
	kmem_free(iprbp, sizeof (struct iprbinstance));
	return (DDI_FAILURE);
}

/*
 * Called when the system is being halted to disable all hardware
 * interrupts and halt the receive unit.
 *
 * Note that we must not block at all, not even on mutexes.
 */
static int
iprbquiesce(dev_info_t *devinfo)
{
	struct	iprbinstance *iprbp;	/* Our private device info */

	iprbp = ddi_get_driver_private(devinfo);

	if (iprbp == NULL)
		return (DDI_FAILURE);

	/* Reset the hardware */
	iprb_hard_reset(iprbp);

	/* Disable Interrupts */
	DisableInterrupts(iprbp);

	return (DDI_SUCCESS);
}

static int
iprbsuspend(dev_info_t *devinfo)
{
	struct	iprbinstance *iprbp;	/* Our private device info */

	iprbp = ddi_get_driver_private(devinfo);

	if (iprbp == NULL)
		return (DDI_FAILURE);

	mii_suspend(iprbp->mii);

	mutex_enter(&iprbp->intrlock);
	mutex_enter(&iprbp->cmdlock);

	/* update statistics snapshot */
	iprb_update_stats(iprbp);

	/* stop the hardware completely */
	iprb_stop(iprbp);

	/* clean up any commands that didn't get reaped properly */
	iprb_reap_commands(iprbp, IPRB_REAP_ALL_CMDS);

	iprb_hard_reset(iprbp);
	DisableInterrupts(iprbp);

	iprbp->suspended = B_TRUE;

	mutex_exit(&iprbp->cmdlock);
	mutex_exit(&iprbp->intrlock);

	return (DDI_SUCCESS);
}

static int
iprbresume(dev_info_t *devinfo)
{
	struct	iprbinstance *iprbp;	/* Our private device info */

	iprbp = ddi_get_driver_private(devinfo);
	ASSERT(iprbp);

	/* Reset the hardware */
	iprb_hard_reset(iprbp);

	/* Disable Interrupts */
	DisableInterrupts(iprbp);

	iprb_init_board(iprbp);

	/* Change for 82558 enhancement */
	/*
	 * If D101 and if the user has enabled flow control, set up the Flow
	 * Control Reg. in the CSR.
	 * Do not enable flow control for embedded 82559 (dev id 2449)
	 */
	if (devsupf[iprbp->iprb_dev_type].flowcntrl == 1) {
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_FC_THLD_REG),
		    IPRB_DEFAULT_FC_THLD);
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_FC_CMD_REG),
		    IPRB_DEFAULT_FC_CMD);
	}

	mutex_enter(&iprbp->intrlock);
	mutex_enter(&iprbp->cmdlock);

	iprbp->suspended = B_FALSE;
	if (iprbp->iprb_receive_enabled) {
		iprb_start(iprbp);
	}
	mutex_exit(&iprbp->cmdlock);
	mutex_exit(&iprbp->intrlock);

	/* Reset MII layer */
	mii_resume(iprbp->mii);

	return (DDI_SUCCESS);
}

void
iprb_hard_reset(struct iprbinstance *iprbp)
{
	int	i = 10;

	/* As per 82557/8 spec. , issue selective reset first */
	ddi_put32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PORT), IPRB_PORT_SEL_RESET);
	/* Wait for PORT register to clear */
	do {
		drv_usecwait(10);
		if (ddi_get32(iprbp->iohandle,
		    REG32(iprbp->port, IPRB_SCB_PORT)) == 0)
			break;
	} while (--i > 0);
#ifdef IPRBDEBUG
	if (i == 0)
		cmn_err(CE_WARN, "iprb: Selective reset failed");
#endif
	/* Issue software reset */
	ddi_put32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PORT), IPRB_PORT_SW_RESET);
	drv_usecwait(10);	/* As per specs - RSS */
}

/* detach(9E) -- Detach a device from the system */

static int
iprbdetach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	struct	iprbinstance *iprbp;	/* Our private device info */
	int	i;
#define	WAITTIME 10000			/* usecs to wait */
#define	MAX_TIMEOUT_RETRY_CNT	(30 *(1000000 / WAITTIME))	/* 30 seconds */

	switch (cmd) {
	case DDI_SUSPEND:
		return (iprbsuspend(devinfo));
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	/* Get the driver private structure */
	iprbp = ddi_get_driver_private(devinfo);

	/* Wait for all receive buffers to be returned */
	i = MAX_TIMEOUT_RETRY_CNT;

	mutex_enter(&iprbp->freelist_mutex);
	while (iprbp->rfds_outstanding > 0) {
		mutex_exit(&iprbp->freelist_mutex);
		delay(drv_usectohz(WAITTIME));

		if (--i == 0) {
			cmn_err(CE_WARN, "iprb: never reclaimed all the "
			    "receive buffers.  Still have %d "
			    "buffers outstanding.",
			    iprbp->rfds_outstanding);
			return (DDI_FAILURE);
		}
		mutex_enter(&iprbp->freelist_mutex);
	}

	/*
	 *  Unregister ourselves from the GLD interface
	 *
	 *  mac_unregister will:
	 *	remove the minor node;
	 *	unlink us from the GLD module.
	 */
	if (mac_unregister(iprbp->mh) != DDI_SUCCESS) {
		mutex_exit(&iprbp->freelist_mutex);
		return (DDI_FAILURE);
	}

	/* Stop the board if it is running */
	iprb_hard_reset(iprbp);

	mutex_exit(&iprbp->freelist_mutex);

	ddi_remove_intr(devinfo, 0, iprbp->icookie);

	/* Release any pending xmit mblks */
	iprb_release_mblks(iprbp);

	/* Free up MII resources */
	if (iprbp->mii)
		mii_free(iprbp->mii);

	/* Release all DMA resources */
	iprb_free_dma_resources(iprbp);

	mutex_destroy(&iprbp->freelist_mutex);
	mutex_destroy(&iprbp->cmdlock);
	mutex_destroy(&iprbp->intrlock);

	/* Unmap our register set */
	ddi_regs_map_free(&iprbp->iohandle);

	kmem_free(iprbp, sizeof (struct iprbinstance));

	return (DDI_SUCCESS);
}

/*
 * GLDv3 Entry Points
 */

/*
 * iprb_init_board() -- initialize the specified network board.
 */
static void
iprb_init_board(struct iprbinstance *iprbp)
{
	mutex_enter(&iprbp->intrlock);
	mutex_enter(&iprbp->cmdlock);

	iprbp->iprb_first_rfd = 0;	/* First RFD index in list */
	iprbp->iprb_last_rfd = iprbp->iprb_nrecvs - 1;	/* Last RFD index */
	iprbp->iprb_current_rfd = 0;	/* Next RFD index to be filled */

	/*
	 * iprb_first_cmd is the first command used but not yet processed by
	 * the D100.  -1 means we don't have any commands pending.
	 *
	 * iprb_last_cmd is the last command used but not yet processed by the
	 * D100.
	 *
	 * iprb_current_cmd is the one we can next use.
	 */
	iprbp->iprb_first_cmd = -1;
	iprbp->iprb_last_cmd = 0;
	iprbp->iprb_current_cmd = 0;

	/* Note that the next command will be the first command */
	iprbp->iprb_initial_cmd = 1;
	/* Set general pointer to 0 */
	/* LANCEWOOD Fix */
	/* Could not Reclaim all command buffers msg fix... */
	/*
	 * Always reinitialize gen ptr. and load CUbase and RUbase
	 */
	ddi_put32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PTR), (uint32_t)0);
	ddi_put8(iprbp->iohandle,
	    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_LOAD_CUBASE);
	IPRB_SCBWAIT(iprbp);
	ddi_put32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PTR), (uint32_t)0);
	ddi_put8(iprbp->iohandle,
	    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_LOAD_RUBASE);
	IPRB_SCBWAIT(iprbp);
	/* End of LANCEWOOD Fix */

	mutex_exit(&iprbp->cmdlock);
	mutex_exit(&iprbp->intrlock);
}

/*
 * Read the EEPROM (one bit at a time!!!) to get the ethernet address
 */
void
iprb_get_ethaddr(struct iprbinstance *iprbp)
{
	int    i;

	for (i = 0; i < (ETHERADDRL / sizeof (uint16_t)); i++) {
		iprb_readia(iprbp,
		    (void *)&iprbp->macaddr[i *sizeof (uint16_t)], i);
	}
}

/*
 * iprb_m_unicst() -- set the physical network address on the board
 */
int
iprb_m_unicst(void *arg, const uint8_t *macaddr)
{
	struct iprbinstance *iprbp =	arg;

	mutex_enter(&iprbp->cmdlock);
	bcopy(macaddr, iprbp->macaddr, ETHERADDRL);
	if (!iprbp->suspended) {
		iprb_configure(iprbp);
	}
	mutex_exit(&iprbp->cmdlock);
	return (0);
}

/*
 * Reap commands... and try hard by waiting up to a second for them to
 * complete.  If this fails to succeed, then the NIC is probably hosed.
 */
static int
iprb_reap_wait(struct iprbinstance *iprbp)
{
	int when;

	/* XXX Handle Promisc multicast etc */
	iprb_reap_commands(iprbp, IPRB_REAP_COMPLETE_CMDS);
	when = 1000000;	/* spin for up to a second for hardware to finish */
	while ((iprbp->iprb_current_cmd == iprbp->iprb_first_cmd) && (when)) {
		drv_usecwait(10);
		when -= 10;
		iprb_reap_commands(iprbp, IPRB_REAP_COMPLETE_CMDS);
	}
	if (iprbp->iprb_current_cmd == iprbp->iprb_first_cmd) {
		cmn_err(CE_WARN, "iprb%d: config failed, out of resources.",
		    iprbp->instance);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * Set up the configuration of the D100.  If promiscuous mode is to be on, we
 * change certain things.
 */
static void
iprb_configure(struct iprbinstance *iprbp)
{
	struct iprb_cfg_cmd *cfg;
	struct iprb_ias_cmd *ias;
	struct iprb_mcs_cmd *mcs;
	int s, i;

	int	phy = 0;	/* Will let us know about MII Phy0 */
				/* or Phy1 and will be -1 in case  */
				/* of 503 */

	ASSERT(mutex_owned(&iprbp->cmdlock));

	/*
	 * Do unicast first
	 */
	if (iprb_reap_wait(iprbp) != DDI_SUCCESS) {
		return;
	}

	ias = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->ias_cmd;
	ias->ias_cmd = IPRB_IAS_CMD;

	/* Get the ethernet address from the macaddr */
	for (i = 0; i < ETHERADDRL; i++)
		ias->addr[i] = iprbp->macaddr[i];

	iprb_add_command(iprbp);
	IPRB_SCBWAIT(iprbp);

	/* XXX Handle Promisc multicast etc */

	/*
	 * Next do various hardware specific configuration, including
	 * promiscuous support, etc.
	 */
	if (iprb_reap_wait(iprbp) != DDI_SUCCESS) {
		return;
	}

	cfg = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->cfg_cmd;
	cfg->cfg_cmd = IPRB_CFG_CMD;
	cfg->cfg_byte0 = IPRB_CFG_B0;
	cfg->cfg_byte1 = IPRB_CFG_B1;

	if (iprbp->Aifs >= 0) {
		cfg->cfg_byte2 = (unsigned char) iprbp->Aifs;
	} else
		cfg->cfg_byte2 = IPRB_CFG_B2;

	cfg->cfg_byte3 = IPRB_CFG_B3;

	/*
	 * MWI should only be enabled for D101A4 and above i.e. 82558 and
	 * above, including embedded 82559
	 */
	/* Read Align should only be available for 82558 and above */
	/*
	 * MWIEnable is already read in iprbprobe routine for a completly
	 * different purpose, here it is being reread only because I don't
	 * want to make use of probe routine values as probe and attach and
	 * configure can be called in any order by different threads
	 */
	if (devsupf[iprbp->iprb_dev_type].mwi == 1) {
		if (GETPROP(iprbp, "MWIEnable", IPRB_DEFAULT_MWI_ENABLE) == 1)
			cfg->cfg_byte3 |= IPRB_CFG_MWI_ENABLE;
		if (iprbp->read_align == 1)
			cfg->cfg_byte3 |=
			    IPRB_CFG_READAL_ENABLE | IPRB_CFG_TERMWCL_ENABLE;
	}
	cfg->cfg_byte4 = IPRB_CFG_B4;	/* Rx DMA Max. Byte Count */
	cfg->cfg_byte5 = IPRB_CFG_B5;	/* Tx DMA Max. Byte Count */

	/*
	 * We save the bad packets (as we were doing for promiscuous),
	 * because we need to accept larger VLAN sized frames.
	 */
	cfg->cfg_byte6 = IPRB_CFG_B6 | IPRB_CFG_B6PROM;
	if (iprbp->promisc) {
		cfg->cfg_byte7 = IPRB_CFG_B7PROM;
	} else {
		cfg->cfg_byte7 = IPRB_CFG_B7NOPROM;
	}

	/* Setup number of retries after under run */
	/*
	 * Bit 2-1 of Byte 7 represent no. of retries..
	 */
	cfg->cfg_byte7 = ((cfg->cfg_byte7 & (~IPRB_CFG_URUN_RETRY)) |
	    ((iprbp->tx_ur_retry) << 1));

	phy = mii_get_addr(iprbp->mii);	/* Returns -1 in case of 503 */

	cfg->cfg_byte8 = IS_503(phy) ? IPRB_CFG_B8_503 : IPRB_CFG_B8_MII;

	cfg->cfg_byte9 = IPRB_CFG_B9;
	cfg->cfg_byte10 = IPRB_CFG_B10;
	cfg->cfg_byte11 = IPRB_CFG_B11;

	if (iprbp->ifs >= 0) {
		cfg->cfg_byte12 = (unsigned char) (iprbp->ifs << 4);
	} else
		cfg->cfg_byte12 = IPRB_CFG_B12;

	cfg->cfg_byte13 = IPRB_CFG_B13;
	cfg->cfg_byte14 = IPRB_CFG_B14;

	if (iprbp->promisc)
		cfg->cfg_byte15 = IPRB_CFG_B15 | IPRB_CFG_B15_PROM;
	else
		cfg->cfg_byte15 = IPRB_CFG_B15;

	/* WAW enable/disable  */
	if ((iprbp->coll_backoff == 1) &&
	    devsupf[iprbp->iprb_dev_type].collbackoff) {
		cfg->cfg_byte15 |= IPRB_CFG_WAW_ENABLE;
	}

	/*
	 * The CRS+CDT bit should only be set when NIC is operating in 503
	 * mode (Configuration Byte 8) Recommendation: This should be 1 for
	 * 503 based 82557 designs 0 for MII based 82557 designs 0 for 82558
	 * or 82559 designs.
	 */

	/* phy == -1 only in case of 503 , see get_active_mii */
	if (IS_503(phy)) {
		cfg->cfg_byte15 |= IPRB_CFG_CRS_OR_CDT;
	}

	/*
	 * Enabling Flow Control only if 82558 or 82559
	 * No flow control for embedded 82559 device - device_id == 0x2449
	 * Flow control for PHY 1 - Please see the note below.
	 */
	if (devsupf[iprbp->iprb_dev_type].flowcntrl == 1) {
		switch (mii_get_flowctrl(iprbp->mii)) {
		case LINK_FLOWCTRL_TX:
		case LINK_FLOWCTRL_BI:
		/*
		 * A fix added to disable flow control.
		 */
			cfg->cfg_byte16 = IPRB_CFG_FC_DELAY_LSB;
			cfg->cfg_byte17 = IPRB_CFG_FC_DELAY_MSB;
			cfg->cfg_byte18 = IPRB_CFG_B18;
			cfg->cfg_byte19 = (IPRB_CFG_B19 |
			    IPRB_CFG_FC_RESTOP |
			    IPRB_CFG_FC_RESTART |
			    IPRB_CFG_REJECT_FC);
			break;
		default:
			/* iprbp->flow_control == 0 */
			cfg->cfg_byte16 = IPRB_CFG_B16;
			cfg->cfg_byte17 = IPRB_CFG_B17;
			cfg->cfg_byte18 = IPRB_CFG_B18;
			cfg->cfg_byte19 = (IPRB_CFG_B19 |
			    IPRB_CFG_TX_FC_DISABLE |
			    IPRB_CFG_REJECT_FC);
			break;
		}
	} else {
		cfg->cfg_byte16 = IPRB_CFG_B16;
		cfg->cfg_byte17 = IPRB_CFG_B17;
		cfg->cfg_byte18 = IPRB_CFG_B18;
		if (iprbp->flow_control == 0) {
			cfg->cfg_byte19 = (IPRB_CFG_B19 |
			    IPRB_CFG_TX_FC_DISABLE);
		} else {
			/* iprbp->flow_control == 1 */
			cfg->cfg_byte19 = IPRB_CFG_B19;
		}
	}

	/*
	 * We must force full duplex, if we are using PHY 0 and we are
	 * supposed to run in FDX mode. We will have to do it as IPRB has
	 * only one FDX# input pin. This pin is connected to PHY 1 Some
	 * testing is required to see the SPLASH full duplex operation.
	 */
	cfg->cfg_byte20 = IPRB_CFG_B20;

	cfg->cfg_byte21 = IPRB_CFG_B21;
	cfg->cfg_byte22 = IPRB_CFG_B22;
	cfg->cfg_byte23 = IPRB_CFG_B23;

	iprb_add_command(iprbp);

	/*
	 * Finally, do the multicast.
	 */
	if (iprb_reap_wait(iprbp) != DDI_SUCCESS) {
		return;
	}

	mcs = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->mcs_cmd;
	mcs->mcs_cmd = IPRB_MCS_CMD;

	for (i = 0, s = 0; s < IPRB_MAXMCSN; s++) {
		if (iprbp->iprb_mcs_addrval[s] != 0) {
			bcopy(&iprbp->iprb_mcs_addrs[s],
			    &mcs->mcs_bytes[i * ETHERADDRL], ETHERADDRL);
			i++;
		}
	}
	mcs->mcs_count = i *ETHERADDRL;

	iprb_add_command(iprbp);

	ASSERT(mutex_owned(&iprbp->cmdlock));
}

/*
 * iprb_m_multicst() -- set (enable) or disable a multicast address
 *
 * Program the hardware to enable/disable the multicast address in "mcast".
 * Enable if "op" is non-zero, disable if zero.
 *
 * We keep a list of multicast addresses, because the D100 requires that the
 * entire list be uploaded every time a change is made.
 */
int
iprb_m_multicst(void *arg, boolean_t add, const uint8_t *mcast)
{
	struct iprbinstance *iprbp =	arg;

	int	s;
	int	found = 0;
	int	free_index = -1;

	mutex_enter(&iprbp->cmdlock);

	for (s = 0; s < IPRB_MAXMCSN; s++) {
		if (iprbp->iprb_mcs_addrval[s] == 0) {
			if (free_index == -1)
				free_index = s;
		} else {
			if (bcmp(&(iprbp->iprb_mcs_addrs[s]),
			    mcast, ETHERADDRL) == 0) {
				found = 1;
				break;
			}
		}
	}

	if (add != B_TRUE) {
		/* We want to disable the multicast address. */
		if (found) {
			iprbp->iprb_mcs_addrval[s] = 0;
		} else {
			/* Trying to remove non-existant mcast addr */
			mutex_exit(&iprbp->cmdlock);
			return (0);
		}
	} else {
		/* Enable a mcast addr */
		if (!found) {
			if (free_index == -1) {
				mutex_exit(&iprbp->cmdlock);
				return (ENOMEM);
			}
			bcopy(mcast, &iprbp->iprb_mcs_addrs[free_index],
			    ETHERADDRL);
			iprbp->iprb_mcs_addrval[free_index] = 1;
		} else {
			/* already enabled */
			mutex_exit(&iprbp->cmdlock);
			return (0);
		}
	}

	if (!iprbp->suspended) {
		iprb_configure(iprbp);
	}

	mutex_exit(&iprbp->cmdlock);
	return (0);
}

/*
 * iprb_m_promisc() -- set or reset promiscuous mode on the board
 *
 * Program the hardware to enable/disable promiscuous mode. Enable if "on" is
 * non-zero, disable if zero.
 */
static int
iprb_m_promisc(void *arg, boolean_t on)
{
	struct iprbinstance *iprbp = arg;

	mutex_enter(&iprbp->cmdlock);
	iprbp->promisc = on;
	if (!iprbp->suspended)
		iprb_configure(iprbp);
	mutex_exit(&iprbp->cmdlock);
	return (0);
}

static void
iprb_update_stats(struct iprbinstance *iprbp)
{
	int			warning = 1000;
	ddi_acc_handle_t	acch = iprbp->stats_acch;
	struct iprb_stats	*stats = iprbp->stats;

	ASSERT(mutex_owned(&iprbp->cmdlock));

	if (iprbp->suspended)
		return;

	ddi_put32(acch, &stats->iprb_stat_chkword, 0);
	(void) ddi_dma_sync(iprbp->stats_dmah, 0, 0, DDI_DMA_SYNC_FORKERNEL);

	IPRB_SCBWAIT(iprbp);
	ddi_put32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PTR), iprbp->stats_paddr);
	ddi_put8(iprbp->iohandle,
	    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_CU_LOAD_DUMP_ADDR);

	IPRB_SCBWAIT(iprbp);
	ddi_put8(iprbp->iohandle,
	    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_CU_DUMPSTAT_RESET);

	do {
		(void) ddi_dma_sync(iprbp->stats_dmah, 0, 0,
		    DDI_DMA_SYNC_FORKERNEL);
		if (ddi_get32(acch, &stats->iprb_stat_chkword) ==
		    IPRB_STAT_RST_COMPLETE)
			break;
		drv_usecwait(10);
	} while (--warning > 0);

#define	GETSTAT(x)	\
	iprbp->iprb_stat_##x = ddi_get32(acch, &stats->iprb_stat_##x)

	GETSTAT(xunderrun);
	GETSTAT(roverrun);
	GETSTAT(crc);
	GETSTAT(crs);
	GETSTAT(align);
	GETSTAT(resource);
	GETSTAT(short);
	GETSTAT(maxcol);
	GETSTAT(latecol);
	GETSTAT(totcoll);
	GETSTAT(onecoll);
	GETSTAT(multicoll);
	GETSTAT(defer);

	if (warning == 0) {
		cmn_err(CE_WARN, "iprb%d: Statistics update failed.",
		    iprbp->instance);
	}
}

/*
 * iprb_m_stat() -- update statistics
 */
static int
iprb_m_getstat(void *arg, uint_t stat, uint64_t *val)
{
	struct iprbinstance *iprbp = arg;

	int	rv = 0;

	if (stat == MAC_STAT_IFSPEED) {
		mutex_enter(&iprbp->cmdlock);
		iprb_update_stats(iprbp);
		mutex_exit(&iprbp->cmdlock);
	}

	if (mii_m_getstat(iprbp->mii, stat, val) == 0) {
		return (0);
	}

	switch (stat) {
	case MAC_STAT_MULTIRCV:
		*val = iprbp->iprb_stat_multircv;
		break;
	case MAC_STAT_BRDCSTRCV:
		*val = iprbp->iprb_stat_brdcstrcv;
		break;
	case MAC_STAT_MULTIXMT:
		*val = iprbp->iprb_stat_multixmt;
		break;
	case MAC_STAT_BRDCSTXMT:
		*val = iprbp->iprb_stat_brdcstxmt;
		break;
	case MAC_STAT_UNDERFLOWS:
		*val = iprbp->iprb_stat_xunderrun;
		break;
	case MAC_STAT_OVERFLOWS:
		*val = iprbp->iprb_stat_roverrun;
		break;

	case MAC_STAT_IERRORS:
		*val = iprbp->iprb_stat_crc +
		    iprbp->iprb_stat_align +
		    iprbp->iprb_stat_short +
		    iprbp->iprb_stat_crs +
		    iprbp->iprb_stat_resource +
		    iprbp->iprb_stat_roverrun;
		break;
	case MAC_STAT_OERRORS:
		*val = iprbp->iprb_stat_maxcol +
		    iprbp->iprb_stat_latecol +
		    iprbp->iprb_stat_xunderrun;
		break;
	case MAC_STAT_OPACKETS:
		*val = iprbp->iprb_stat_opackets;
		break;
	case MAC_STAT_OBYTES:
		*val = iprbp->iprb_stat_obytes;
		break;
	case MAC_STAT_IPACKETS:
		*val = iprbp->iprb_stat_ipackets;
		break;
	case MAC_STAT_RBYTES:
		*val = iprbp->iprb_stat_rbytes;
		break;
	case MAC_STAT_NORCVBUF:
		*val = iprbp->iprb_stat_norcvbuf;
		break;
	case MAC_STAT_NOXMTBUF:
		*val = iprbp->iprb_stat_noxmtbuf;
		break;
	case MAC_STAT_COLLISIONS:
		*val = iprbp->iprb_stat_totcoll;
		break;
	case ETHER_STAT_ALIGN_ERRORS:
		*val = iprbp->iprb_stat_align;
		break;
	case ETHER_STAT_FCS_ERRORS:
		*val = iprbp->iprb_stat_crc;
		break;
	case ETHER_STAT_DEFER_XMTS:
		*val = iprbp->iprb_stat_defer;
		break;
	case ETHER_STAT_FIRST_COLLISIONS:
		*val = iprbp->iprb_stat_onecoll;
		break;
	case ETHER_STAT_MULTI_COLLISIONS:
		*val = iprbp->iprb_stat_multicoll;
		break;
	case ETHER_STAT_TX_LATE_COLLISIONS:
		*val = iprbp->iprb_stat_latecol;
		break;
	case ETHER_STAT_EX_COLLISIONS:
		*val = iprbp->iprb_stat_maxcol;
		break;
	case ETHER_STAT_CARRIER_ERRORS:
		*val = iprbp->iprb_stat_crs;
		break;
	case ETHER_STAT_TOOSHORT_ERRORS:
		*val = iprbp->iprb_stat_short;
		break;
	case ETHER_STAT_TOOLONG_ERRORS:
		*val = iprbp->iprb_stat_frame_toolong;
		break;
	case ETHER_STAT_MACRCV_ERRORS:
		*val = iprbp->RU_stat_count;
		break;
	default:
		rv = EINVAL;
		break;
	}

	return (rv);
}

void
iprb_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	struct iprbinstance *iprbp = arg;

	if (mii_m_loop_ioctl(iprbp->mii, wq, mp))
		return;

	miocnak(wq, mp, 0, EINVAL);
}

int
iprb_m_getprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz,
    void *val)
{
	struct iprbinstance *iprbp = arg;
	int rv;

	_NOTE(ARGUNUSED(name));

	rv = mii_m_getprop(iprbp->mii, name, num, sz, val);
	return (rv);
}

static void
iprb_m_propinfo(void *arg, const char *name, mac_prop_id_t num,
    mac_prop_info_handle_t mph)
{
	struct iprbinstance *iprbp = arg;

	mii_m_propinfo(iprbp->mii, name, num, mph);
}

int
iprb_m_setprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz,
    const void *val)
{
	struct iprbinstance *iprbp = arg;
	int rv;

	_NOTE(ARGUNUSED(name));

	rv = mii_m_setprop(iprbp->mii, name, num, sz, val);
	return (rv);
}

/*
 * iprb_reap_commands() - reap commands already processed
 */
static void
iprb_reap_commands(struct iprbinstance *iprbp, int mode)
{
	int16_t reaper, last_reaped, i;
	volatile struct iprb_gen_cmd *gcmd;

	/* Any commands to be processed ? */
	if (iprbp->iprb_first_cmd == -1)	/* no */
		return;
	reaper = iprbp->iprb_first_cmd;
	last_reaped = -1;

	do {
		/* Get the command to be reaped */
		gcmd = &iprbp->cmd_blk[reaper]->gen_cmd;

		/* If we aren't reaping all cmds */
		if (mode != IPRB_REAP_ALL_CMDS)
			/* Is it done? */
			if (!(gcmd->gen_status & IPRB_CMD_COMPLETE))
				break;	/* No */

		/* If this was a Tx command, free all the resources. */
		if (iprbp->Txbuf_info[reaper].mp != NULL) {
			freemsg(iprbp->Txbuf_info[reaper].mp);
			iprbp->Txbuf_info[reaper].mp = NULL;

			for (i = 0;
			    i < iprbp->Txbuf_info[reaper].frag_nhandles; i++)
				(void) ddi_dma_unbind_handle(
				    iprbp->Txbuf_info[reaper].
				    frag_dma_handle[i]);
		}

		/*
		 * Increment to next command to be processed, looping around
		 * if necessary.
		 */
		last_reaped = reaper++;
		if (reaper == iprbp->iprb_nxmits)
			reaper = 0;
	} while (last_reaped != iprbp->iprb_last_cmd);

	/* Did we get them all? */
	if (last_reaped == iprbp->iprb_last_cmd)
		iprbp->iprb_first_cmd = -1;	/* Yes */
	else
		iprbp->iprb_first_cmd = reaper;	/* No */
}

/*
 * iprb_add_command() - Modify the command chain so that the current command
 * is known to be ready.
 */
static void
iprb_add_command(struct iprbinstance *iprbp)
{
	volatile struct iprb_gen_cmd *current_cmd, *last_cmd;

	current_cmd = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->gen_cmd;
	last_cmd = &iprbp->cmd_blk[iprbp->iprb_last_cmd]->gen_cmd;

	current_cmd->gen_status = 0;
	/* Make it so the D100 will suspend upon completion of this cmd */
	current_cmd->gen_cmd |= IPRB_SUSPEND;

	/* This one is the new last command */
	iprbp->iprb_last_cmd = iprbp->iprb_current_cmd;

	/* If there were previously no commands, this one is first */
	if (iprbp->iprb_first_cmd == -1)
		iprbp->iprb_first_cmd = iprbp->iprb_current_cmd;

	/* Make a new current command, looping around if necessary */
	++iprbp->iprb_current_cmd;

	if (iprbp->iprb_current_cmd == iprbp->iprb_nxmits)
		iprbp->iprb_current_cmd = 0;

	/*
	 * If we think we are about to run out of resources, have this
	 * command generate a software interrupt on completion, so we will
	 * ping gld to try again.
	 */
	if (iprbp->iprb_current_cmd == iprbp->iprb_first_cmd)
		current_cmd->gen_cmd |= IPRB_INTR;

	/* Make it so that the D100 will no longer suspend on last command */
	if (current_cmd != last_cmd)
		last_cmd->gen_cmd &= ~IPRB_SUSPEND;

	IPRB_SCBWAIT(iprbp);

	/*
	 * RESUME the D100.  RESUME will be ignored if the CU is either IDLE
	 * or ACTIVE, leaving the state IDLE or Active, respectively; after
	 * the first command, the CU will be either ACTIVE or SUSPENDED.
	 */
	if (iprbp->iprb_initial_cmd) {
		iprbp->iprb_initial_cmd = 0;
		ddi_put32(iprbp->iohandle,
		    REG32(iprbp->port, IPRB_SCB_PTR), (uint32_t)
		    iprbp->cmd_blk_info[iprbp->iprb_last_cmd].cmd_physaddr);
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_CU_START);
	} else {
		/*
		 * Embedded LAN controller have some timing problem causing
		 * CU lockup. Suggested fix:
		 * To avoid CU lockup on 10BaseT Half Duplex, send NOP
		 * and wait 1us (0.2% performance decrease)
		 */
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_CU_RESUME);
	}
}

/*
 * iprb_send() -- send a packet
 *
 * Called when a packet is ready to be transmitted. A pointer to an M_DATA
 * message that contains the packet is passed to this routine. The complete
 * LLC header is contained in the message's first message block, and the
 * remainder of the packet is contained within additional M_DATA message
 * blocks linked to the first message block.
 */
static mblk_t *
iprb_send(struct iprbinstance *iprbp, mblk_t *mp)
{
	int		i;
	size_t		pktlen = 0;
	mblk_t		*lmp;
	boolean_t	ismulti, isbcast;

	ASSERT(mp != NULL);

	/* Count the number of mblks in list, and the total pkt length */
	for (i = 0, lmp = mp; lmp != NULL; lmp = lmp->b_cont, ++i)
		pktlen += MBLKL(lmp);

	/* Make sure packet isn't too large */
	if (pktlen > (ETHERMTU + sizeof (struct ether_vlan_header))) {
		iprbp->iprb_stat_frame_toolong++;
		freemsg(mp);
		return (NULL);
	}

	mutex_enter(&iprbp->cmdlock);
	if (iprbp->suspended) {
		mutex_exit(&iprbp->cmdlock);
		freemsg(mp);
		return (NULL);
	}

	/* Free up any processed command buffers */
	iprb_reap_commands(iprbp, IPRB_REAP_COMPLETE_CMDS);

	/* If there are no xmit control blocks available, return */
	if (iprbp->iprb_current_cmd == iprbp->iprb_first_cmd) {
		mutex_exit(&iprbp->cmdlock);
		return (mp);
	}

	/* Now assume worst case, that each mblk crosses (one) page boundry */
	i <<= 1;
	if ((i > IPRB_MAX_FRAGS) || MBLKL(mp) < sizeof (struct ether_header)) {
		lmp = msgpullup(mp, -1);
		freemsg(mp);
		if (lmp == NULL) {
			iprbp->iprb_stat_noxmtbuf++;
			return (NULL);
		}
		mp = lmp;
	}

	/* figure multicast/broadcast */
	if (mp->b_rptr[0] & 0x1) {
		if (bcmp(mp->b_rptr, iprb_bcast, ETHERADDRL) == 0) {
			isbcast = B_TRUE;
			ismulti = B_FALSE;
		} else {
			isbcast = B_FALSE;
			ismulti = B_TRUE;
		}
	} else {
		isbcast = B_FALSE;
		ismulti = B_FALSE;
	}

	/* Prepare message for transmission */

	/*
	 * We use the vanilla 82557 xmit... the 82558 mode offers only
	 * slight performance improvement (one less PCI transaction) at
	 * considerable added complexity to the host driver.  Some day
	 * we might want to rethink this if we ever want to use the advanced
	 * features of the 82550 (IP checksum and LSO offload).  However,
	 * since these are still 10/100 devices, it seems unlikely to offer
	 * enough to be worth the complexity at this time.
	 */

	/*
	 * We hold the lock across the call for now.  its an open
	 * question whether this hurts performance or not.
	 *
	 * The iprb_prepare_xmit_buff will release the lock on return.
	 */
	if (iprb_prepare_xmit_buff(iprbp, mp, pktlen) == B_FALSE) {
		/* a failure here is not reschedulable */
		return (NULL);
	}

	if (ismulti)
		iprbp->iprb_stat_multixmt++;
	else if (isbcast)
		iprbp->iprb_stat_brdcstxmt++;

	iprbp->iprb_stat_opackets++;
	iprbp->iprb_stat_obytes += pktlen;

	return (NULL);	/* successful transmit attempt */
}

mblk_t *
iprb_m_tx(void *arg, mblk_t *mp)
{
	struct iprbinstance *iprbp = arg;

	while (mp != NULL) {
		mblk_t *nmp = mp->b_next;
		mp->b_next = NULL;

		if ((mp = iprb_send(iprbp, mp)) != NULL) {
			mp->b_next = nmp;
			break;
		}
		mp = nmp;
	}

	return (mp);
}

/*
 * Mutex iprbp->cmdlock must be held before calling this function
 * Mutex will be released by this function.
 */
boolean_t
iprb_prepare_xmit_buff(struct iprbinstance *iprbp, mblk_t *mp, size_t pktlen)
{
	mblk_t		*lmp;
	int		len, i;
	uint8_t		num_frags;
	int		used_len = 0;
	int		offset;
	int		num_frags_in_stash = 0;
	uint_t		ncookies;
	ddi_dma_cookie_t	dma_cookie;
	ddi_dma_handle_t	dmah;

	struct	iprb_cmd_info xcmd_info;
	struct	iprb_xmit_cmd *xcmd;
	struct	iprb_Txbuf_info *new_info;
	struct	iprb_Txbuf_desc *new_txbuf_desc;

	ASSERT(mutex_owned(&iprbp->cmdlock));
	xcmd_info = iprbp->cmd_blk_info[iprbp->iprb_current_cmd];
	xcmd = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->xmit_cmd;
	new_txbuf_desc = (struct iprb_Txbuf_desc *)
	    iprbp->Txbuf_desc[iprbp->iprb_current_cmd];
	new_info = &iprbp->Txbuf_info[iprbp->iprb_current_cmd];

	/* Set up transmit buffers */
	i = 0;
	num_frags = 0;

	/*
	 * Starting offset into stash.  Compute from end to allow
	 * for the structure padding.
	 */

	for (lmp = mp; lmp != NULL; lmp = lmp->b_cont)  {
		len = MBLKL(lmp);

		ASSERT(len >= 0);
		if (len == 0)
			continue;

		/* space left in stash? */
		if ((used_len + len) <= IPRB_STASH_SIZE) {
			/*
			 * Compute offset from end - allows for structure
			 * padding)
			 */
			offset = (sizeof (struct iprb_xmit_cmd))
			    - (IPRB_STASH_SIZE - used_len);

			bcopy(lmp->b_rptr,
			    ((xcmd->data).gen_tcb.data_stash + used_len), len);

			new_txbuf_desc->frag[num_frags].data =
			    xcmd_info.cmd_physaddr + offset;

			new_txbuf_desc->frag[num_frags].frag_len = len;
			used_len += len;

			num_frags++;
			num_frags_in_stash++;
			continue;
		}

		/* not enough room to copy, so just DMA it */
		dmah = new_info->frag_dma_handle[i];

		if (ddi_dma_addr_bind_handle(dmah, NULL, (caddr_t)lmp->b_rptr,
		    len, DDI_DMA_WRITE | DDI_DMA_STREAMING, DDI_DMA_DONTWAIT,
		    0, &dma_cookie, &ncookies) != DDI_DMA_MAPPED) {
			while (i-- > 0)
				(void) ddi_dma_unbind_handle(
				    new_info->frag_dma_handle[i]);
			mutex_exit(&iprbp->cmdlock);
			freemsg(mp);
			return (B_FALSE);
		}
		/* Worst case scenario is crossing 1 page boundary */
		ASSERT(ncookies == 1 || ncookies == 2);
		for (;;) {
			new_txbuf_desc->frag[num_frags].data =
			    dma_cookie.dmac_address;
			new_txbuf_desc->frag[num_frags].frag_len =
			    (uint32_t)dma_cookie.dmac_size;
			num_frags++;
			if (--ncookies == 0)
				break;
			ddi_dma_nextcookie(dmah, &dma_cookie);
		}
		i++;
	}

	ASSERT(num_frags <= IPRB_MAX_FRAGS);

	if (num_frags == num_frags_in_stash) {
		/* We bcopied whole packet */
		freemsg(mp);
		new_info->mp = NULL;
	} else
		new_info->mp = mp;	/* mblk to free when complete */

	/* No. of DMA handles to unbind */
	new_info->frag_nhandles = (uint8_t)i;

	xcmd->xmit_tbd = new_info->tbd_physaddr;
	xcmd->xmit_count = 0;
	xcmd->xmit_threshold = pktlen >> iprbp->iprb_threshold;
	xcmd->xmit_tbdnum = num_frags;	/* No. of fragments to process */
	xcmd->xmit_cmd = IPRB_XMIT_CMD | IPRB_SF;
	iprb_add_command(iprbp);
	mutex_exit(&iprbp->cmdlock);
	return (B_TRUE);
}

/*
 * iprb_intr() -- interrupt from board to inform us that a receive or
 * transmit has completed.
 */

/*
 * IMPORTANT NOTE - Due to a bug in the D100 controller, the suspend/resume
 * functionality for receive is not reliable.  We are therefore using the EL
 * bit, along with RU_START.  In order to do this properly, it is very
 * important that the order of interrupt servicing remains intact; more
 * specifically, it is important that the FR interrupt bit is serviced before
 * the RNR interrupt bit is serviced, because both bits will be set when an
 * RNR condition occurs, and we must process the received frames before we
 * restart the receive unit.
 */
static uint_t
iprb_intr(caddr_t arg)
{
	struct iprbinstance *iprbp = (void *)arg;
	boolean_t	txupdate = B_FALSE;
	mblk_t		*rxmp = NULL;

	int	intr_status;

	mutex_enter(&iprbp->intrlock);

	if (iprbp->suspended) {
		mutex_exit(&iprbp->intrlock);
		return (DDI_INTR_UNCLAIMED);
	}

	/* Get interrupt bits */
	intr_status = ddi_get16(iprbp->iohandle,
	    REG16(iprbp->port, IPRB_SCB_STATUS)) & IPRB_SCB_INTR_MASK;

	if (intr_status == 0) {
		mutex_exit(&iprbp->intrlock);
		return (DDI_INTR_UNCLAIMED);	/* was NOT our interrupt */
	}
	/* Acknowledge all interrupts */
	ddi_put16(iprbp->iohandle,
	    REG16(iprbp->port, IPRB_SCB_STATUS), intr_status);

	/* Frame received */
	if (intr_status & IPRB_INTR_FR) {
		iprbp->RUwdog_lbolt = ddi_get_lbolt();
		rxmp = iprb_process_recv(iprbp);
	}
	/* Out of resources on receive condition */
	if (intr_status & IPRB_INTR_RNR) {
		iprbp->RUwdog_lbolt = ddi_get_lbolt();

		/* Reset End-of-List */
		iprbp->rfd[iprbp->iprb_last_rfd]->rfd_control &= ~IPRB_RFD_EL;
		iprbp->rfd[iprbp->iprb_nrecvs - 1]->rfd_control |= IPRB_RFD_EL;

		/* Reset driver's pointers */
		iprbp->iprb_first_rfd = 0;
		iprbp->iprb_last_rfd = iprbp->iprb_nrecvs - 1;
		iprbp->iprb_current_rfd = 0;

		/* and start at first RFD again */
		mutex_enter(&iprbp->cmdlock);
		IPRB_SCBWAIT(iprbp);
		ddi_put32(iprbp->iohandle,
		    REG32(iprbp->port, IPRB_SCB_PTR),
		    iprbp->rfd_info[iprbp->iprb_current_rfd]->rfd_physaddr);
		IPRB_SCBWAIT(iprbp);
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_RU_START);
		mutex_exit(&iprbp->cmdlock);
	}
	if (intr_status & IPRB_INTR_CXTNO)
		txupdate = B_TRUE;

	/* Should never get this interrupt */
#ifdef IPRBDEBUG
	if (iprbdebug & IPRBINT)
		if (intr_status & IPRB_INTR_MDI)
			cmn_err(CE_WARN, "iprb: Received MDI interrupt.");
#endif
	iprbp->iprb_stat_intr++;
	mutex_exit(&iprbp->intrlock);

	if (rxmp != NULL)
		mac_rx(iprbp->mh, NULL, rxmp);

	if (txupdate)
		mac_tx_update(iprbp->mh);

	return (DDI_INTR_CLAIMED);	/* Indicate it WAS our interrupt */
}


/*
 * iprb_start() -- start the board receiving.
 *
 * NB: Called with both the intrlock and cmdlock held.
 */

static void
iprb_start(struct iprbinstance *iprbp)
{
	/* Disable Interrupts */
	DisableInterrupts(iprbp);

	iprb_configure(iprbp);

	/* Loading appropriate microcode... */
	if (devsupf[iprbp->iprb_dev_type].cpucs == 1) {
		iprb_load_microcode(iprbp, iprbp->iprb_revision_id);
	}

	iprbp->iprb_receive_enabled = 1;

	/* Reset End-of-List */
	iprbp->rfd[iprbp->iprb_last_rfd]->rfd_control &= ~IPRB_RFD_EL;
	iprbp->rfd[iprbp->iprb_nrecvs - 1]->rfd_control |= IPRB_RFD_EL;

	/* Reset driver's pointers */
	iprbp->iprb_first_rfd = 0;
	iprbp->iprb_last_rfd = iprbp->iprb_nrecvs - 1;
	iprbp->iprb_current_rfd = 0;
	/*
	 * Initialise the Watch Dog timer to be used to fix hardware RU
	 * lockup. This lockup will not notify S/W. Intel recommends a 2
	 * second watch dog to free the RU lockup.
	 */
	if (iprbp->RU_needed) {
		iprbp->RUwdog_lbolt = 0;
		iprbp->RUwdogID = timeout(iprb_RU_wdog,
		    (void *)iprbp, (clock_t)RUWDOGTICKS);
	}
	/* and start at first RFD again */

	IPRB_SCBWAIT(iprbp);
	ddi_put32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PTR),
	    iprbp->rfd_info[iprbp->iprb_first_rfd]->rfd_physaddr);
	IPRB_SCBWAIT(iprbp);
	ddi_put8(iprbp->iohandle,
	    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_RU_START);

	EnableInterrupts(iprbp);
}

/*
 * iprb_stop() - disable receiving
 *
 * NB: Called with both the intrlock and cmdlock held (exception:
 * during failure in attach, when the driver is single threaded).
 */
static void
iprb_stop(struct iprbinstance *iprbp)
{
	int	i = 1000000;

	/*
	 * We don't want to do this as we may lose the link in between and no
	 * one will inform OS...
	 */
	if (iprbp->RU_needed)
		(void) untimeout(iprbp->RUwdogID);
	DisableInterrupts(iprbp);
	/* Turn off receiving unit */
	IPRB_SCBWAIT(iprbp);
	ddi_put8(iprbp->iohandle,
	    REG8(iprbp->port, IPRB_SCB_CMD), IPRB_RU_ABORT);

	/* Try to reap all commands which are complete */
	iprb_reap_commands(iprbp, IPRB_REAP_COMPLETE_CMDS);
	/* Give enough time to clear full command queue */
	while (iprbp->iprb_first_cmd != -1) {
		drv_usecwait(10);
		iprb_reap_commands(iprbp, IPRB_REAP_COMPLETE_CMDS);
		i--;
		if (i == 0) {
			cmn_err(CE_WARN, "iprb%d: could not reclaim all"
			    " command buffers", iprbp->instance);
			break;
		}
	}
}

static int
iprb_m_start(void *arg)
{
	struct iprbinstance *iprbp =	arg;

	mutex_enter(&iprbp->intrlock);
	mutex_enter(&iprbp->cmdlock);

	iprbp->iprb_receive_enabled = 1;

	if (!iprbp->suspended)
		iprb_start(iprbp);

	mutex_exit(&iprbp->cmdlock);
	mutex_exit(&iprbp->intrlock);

	mii_start(iprbp->mii);

	return (0);
}

static void
iprb_m_stop(void *arg)
{
	struct iprbinstance *iprbp =	arg;

	mii_stop(iprbp->mii);

	mutex_enter(&iprbp->intrlock);
	mutex_enter(&iprbp->cmdlock);

	iprbp->iprb_receive_enabled = 0;

	if (!iprbp->suspended)
		iprb_stop(iprbp);

	mutex_exit(&iprbp->cmdlock);
	mutex_exit(&iprbp->intrlock);
}


/* Code to process all of the receive packets */
static mblk_t *
iprb_process_recv(struct iprbinstance *iprbp)
{
	int16_t		current;
	mblk_t		*head = NULL;
	mblk_t		*mp = NULL;
	mblk_t		**mpp;
	unsigned short	rcv_len;
	struct iprb_rfd	*rfd, *rfd_end;
	struct iprb_rfd_info	*newrx, *currx;

	mpp = &head;

	/* while we don't come across command not complete */
	for (;;) {
		mp = NULL;

		/* Start with the current one */
		current = iprbp->iprb_current_rfd;
		rfd = iprbp->rfd[current];

		/* Is it complete? */
		if (!(rfd->rfd_status & IPRB_RFD_COMPLETE))
			break;

		if (!(rfd->rfd_status & IPRB_RFD_OK)) {

			if (rfd->rfd_status &
			    (IPRB_RFD_CRC_ERR | IPRB_RFD_ALIGN_ERR |
			    IPRB_RFD_NO_BUF_ERR | IPRB_RFD_DMA_OVERRUN |
			    IPRB_RFD_SHORT_ERR | IPRB_RFD_PHY_ERR |
			    IPRB_RFD_COLLISION))
				goto failed_receive;
			/*
			 * other reasons -- specifically the length error,
			 * might be due to a legal VLAN frame
			 */
		}
		/* Get the length from the RFD */
		rcv_len = rfd->rfd_count & IPRB_RFD_CNTMSK;

		if (!iprbp->iprb_receive_enabled)
			goto failed_receive;

		currx = iprbp->rfd_info[current];

		mutex_enter(&iprbp->freelist_mutex);

		if ((rcv_len < iprbp->max_rxbcopy) ||
		    ((newrx = iprb_get_buffer(iprbp)) == NULL)) {

			mutex_exit(&iprbp->freelist_mutex);
			/*
			 * If packet Rx'd is smaller than iprbp->max_rxbcopy
			 * or no buffers available on free list, then we
			 * bcopy the packet.
			 */
			if ((mp = allocb(rcv_len + 2, BPRI_MED)) == NULL) {
				iprbp->iprb_stat_norcvbuf++;
				goto failed_receive;
			}

			/*
			 * Note that at this time we go ahead and align
			 * the packet so that the IP header will be aligned.
			 */
			mp->b_rptr += 2;
			mp->b_wptr += 2;
			bcopy(currx->rfd_virtaddr + IPRB_RFD_OFFSET,
			    mp->b_wptr, rcv_len);
		} else {

			iprbp->rfds_outstanding++;

			mutex_exit(&iprbp->freelist_mutex);

			if ((mp = desballoc((uchar_t *)currx->rfd_virtaddr +
			    IPRB_RFD_OFFSET, rcv_len, 0,
			    (frtn_t *)currx)) == NULL) {
				iprbp->iprb_stat_norcvbuf++;

				mutex_enter(&iprbp->freelist_mutex);
				newrx->next = iprbp->free_list;
				iprbp->free_list = newrx;
				iprbp->free_buf_cnt++;
				iprbp->rfds_outstanding--;
				mutex_exit(&iprbp->freelist_mutex);

				goto failed_receive;
			}
			/* Link in the new rfd into the chain */
			iprbp->rfd_info[current] = newrx;
			rfd = (void *)newrx->rfd_virtaddr;

			iprbp->rfd[iprbp->iprb_last_rfd]->rfd_next =
			    newrx->rfd_physaddr;
			rfd->rfd_next = iprbp->rfd[current]->rfd_next;
			iprbp->rfd[current] = rfd;
		}

		mp->b_wptr += rcv_len;
		*mpp = mp;
		mpp = &mp->b_next;

		/* calculate multicast/broadcast stats */
		if (mp->b_rptr[0] & 0x1) {
			if (bcmp(mp->b_rptr, iprb_bcast, ETHERADDRL) == 0)
				iprbp->iprb_stat_brdcstrcv++;
			else
				iprbp->iprb_stat_multircv++;
		}
		iprbp->iprb_stat_ipackets++;
		iprbp->iprb_stat_rbytes += rcv_len;

failed_receive:
		rfd_end = iprbp->rfd[iprbp->iprb_last_rfd];

		/* This one becomes the new end-of-list */
		rfd->rfd_control |= IPRB_RFD_EL;
		rfd->rfd_count = 0;
		rfd->rfd_status = 0;

		iprbp->iprb_last_rfd = current;

		/* Turn off EL bit on old end-of-list (if not same) */
		if (rfd_end != rfd)
			rfd_end->rfd_control &= ~IPRB_RFD_EL;

		/* Current one moves up, looping around if necessary */
		iprbp->iprb_current_rfd++;
		if (iprbp->iprb_current_rfd == iprbp->iprb_nrecvs)
			iprbp->iprb_current_rfd = 0;

	}

	return (head);
}

/*
 * Code directly from Intel spec to read EEPROM data for the ethernet
 * address, one bit at a time. The read can only be a 16-bit read.
 */
static void
iprb_readia(struct iprbinstance *iprbp, uint16_t *addr, uint8_t offset)
{
	uint8_t		eex;

	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	eex &= ~(IPRB_EEDI | IPRB_EEDO | IPRB_EESK);
	eex |= IPRB_EECS;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);
	iprb_shiftout(iprbp, IPRB_EEPROM_READ, 3);
	iprb_shiftout(iprbp, offset, iprbp->iprb_eeprom_address_length);
	*addr = iprb_shiftin(iprbp);
	iprb_eeclean(iprbp);
}

/*
 * Procedure:   iprb_writeia
 *
 * Description: This routine writes a word to a specific EEPROM location.
 *
 * Arguments:
 *      iprbp - Ptr to this card's instance pointer.
 *      reg - The EEPROM word that we are going to write to.
 *      data - The data (word) that we are going to write to the EEPROM.
 *
 * Returns: (none)
 */
static void
iprb_writeia(struct iprbinstance *iprbp, ushort_t reg, ushort_t data)
{
	uint8_t eex;

	/* select EEPROM, mask off ASIC and reset bits, set EECS */
	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	eex &= ~(IPRB_EEDI | IPRB_EEDO | IPRB_EESK);
	eex |= IPRB_EECS;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);

	iprb_shiftout(iprbp, IPRB_EEPROM_EWEN_OPCODE, 5);
	iprb_shiftout(iprbp, reg, (iprbp->iprb_eeprom_address_length - 2));

	iprb_standby(iprbp);

	/*
	 * Erase this particular word.  Write the erase opcode and register
	 * number in that order. The opcode is 3bits in length; reg is
	 * 'address_length' bits long.
	 */
	iprb_shiftout(iprbp, IPRB_EEPROM_ERASE_OPCODE, 3);
	iprb_shiftout(iprbp, reg, iprbp->iprb_eeprom_address_length);

	if (iprb_wait_eeprom_cmd_done(iprbp) == B_FALSE)
		return;

	iprb_standby(iprbp);

	/* write the new word to the EEPROM */

	/* send the write opcode the EEPORM */
	iprb_shiftout(iprbp, IPRB_EEPROM_WRITE_OPCODE, 3);

	/* select which word in the EEPROM that we are writing to. */
	iprb_shiftout(iprbp, reg, iprbp->iprb_eeprom_address_length);

	/* write the data to the selected EEPROM word. */
	iprb_shiftout(iprbp, data, 16);

	if (iprb_wait_eeprom_cmd_done(iprbp) == B_FALSE)
		return;

	iprb_standby(iprbp);

	iprb_shiftout(iprbp, IPRB_EEPROM_EWDISABLE_OPCODE, 5);
	iprb_shiftout(iprbp, reg, (iprbp->iprb_eeprom_address_length - 2));

	iprb_eeclean(iprbp);
}

/*
 * Procedure:   iprb_standby
 *
 * Description: This routine lowers the EEPROM chip select (EECS) for a few
 *              milliseconds.
 *
 * Arguments:
 *      iprbp - Ptr to this card's iprb instance structure
 *
 * Returns: (none)
 */
static void
iprb_standby(struct iprbinstance *iprbp)
{
	uint8_t    eex;

	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	eex &= ~(IPRB_EECS | IPRB_EESK);
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);

	/* Wait for 4 ms */
	drv_usecwait(4000);

	eex |= IPRB_EECS;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);
}

/*
 * Procedure:   iprb_wait_eeprom_cmd_done
 *
 * Description: This routine waits for the the EEPROM to finish its command.
 *              Specifically, it waits for EEDO (data out) to go high.
 *
 * Arguments:
 *      iprbp - Ptr to this card's iprb instance structure
 *
 * Returns:
 *      B_TRUE - If the command finished
 *      B_FALSE - If the command never finished (EEDO stayed low)
 */
static ushort_t
iprb_wait_eeprom_cmd_done(struct iprbinstance *iprbp)
{
	ushort_t eex, i;

	iprb_standby(iprbp);

	for (i = 0; i < 200; i++) {
		eex = ddi_get8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_EECTL));

		if (eex & IPRB_EEDO)
			return (B_TRUE);

		drv_usecwait(1000);
	}
	return (B_FALSE);
}

/*
 * Procedure:   iprb_update_checksum
 *
 * Description: Calculates the checksum and writes it to the EEProm.  This
 *              routine assumes that the checksum word is the last word in
 *              a 64 word EEPROM.  It calculates the checksum accroding to
 *              the formula: Checksum = 0xBABA - (sum of first 63 words).
 *
 * Arguments:
 *      iprbp - Ptr to this card's iprb instance structure
 *
 * Returns: (none)
 */
static void
iprb_update_checksum(struct iprbinstance *iprbp)
{
	uint16_t	Checksum = 0;
	ushort_t	Iter, EepromSize;
	uint16_t	address;

	EepromSize = iprb_get_eeprom_size(iprbp);

	for (Iter = 0; Iter < (EepromSize - 1); Iter++) {
		iprb_readia(iprbp, &address, Iter);
		Checksum += address;
	}

	Checksum = (ushort_t)0xBABA - Checksum;
	iprb_writeia(iprbp, (EepromSize - 1), Checksum);

}

static void
iprb_shiftout(struct iprbinstance *iprbp, uint16_t data, uint8_t count)
{
	uint8_t		eex;
	uint16_t	mask;

	mask = 0x01 << (count - 1);
	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	eex &= ~(IPRB_EEDO | IPRB_EEDI);

	do {
		eex &= ~IPRB_EEDI;
		if (data & mask)
			eex |= IPRB_EEDI;

		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_EECTL), eex);
		drv_usecwait(100);
		iprb_raiseclock(iprbp, (uint8_t *)&eex);
		iprb_lowerclock(iprbp, (uint8_t *)&eex);
		mask = mask >> 1;
	} while (mask);

	eex &= ~IPRB_EEDI;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);
}

static void
iprb_raiseclock(struct iprbinstance *iprbp, uint8_t *eex)
{
	*eex = *eex | IPRB_EESK;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), *eex);
	drv_usecwait(100);
}

static void
iprb_lowerclock(struct iprbinstance *iprbp, uint8_t *eex)
{
	*eex = *eex & ~IPRB_EESK;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), *eex);
	drv_usecwait(100);
}

uint16_t
iprb_shiftin(struct iprbinstance *iprbp)
{
	uint16_t	d;
	uint8_t		x, i;

	x = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	x &= ~(IPRB_EEDO | IPRB_EEDI);
	d = 0;

	for (i = 0; i < 16; i++) {
		d = d << 1;
		iprb_raiseclock(iprbp, &x);
		x = ddi_get8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_EECTL));
		x &= ~(IPRB_EEDI);
		if (x & IPRB_EEDO)
			d |= 1;

		iprb_lowerclock(iprbp, &x);
	}

	return (d);
}

static void
iprb_eeclean(struct iprbinstance *iprbp)
{
	uint8_t		eex;

	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	eex &= ~(IPRB_EECS | IPRB_EEDI);
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);

	iprb_raiseclock(iprbp, &eex);
	iprb_lowerclock(iprbp, &eex);
}

/*
 * Acquire all DMA resources for driver operation.
 */
static int
iprb_alloc_dma_resources(struct iprbinstance *iprbp)
{
	dev_info_t	*devinfo = iprbp->iprb_dip;
	register int	i, j;
	ddi_dma_cookie_t	dma_cookie;
	size_t		len;
	uint_t		ncookies;
	uint32_t	last_dma_addr;
	struct	iprb_rfd_info *rfd_info_free;

	/*
	 * How many Rx descriptors do I need ?
	 *
	 * Every Rx Descriptor equates to a Rx buffer.
	 */

	/*
	 * Allocate all the DMA structures that need to be freed when the
	 * driver is detached using iprb_free_dma_resources().
	 */

	/*
	 * Command Blocks (Tx) Memory First Command Blocks are used to tell
	 * the adapter what to do, which is mostly to transmit packets. So
	 * the amount of Command blocks is equivalent to the amount of Tx
	 * descriptors (they are one and the same).
	 */

	last_dma_addr = 0;
	for (i = iprbp->iprb_nxmits - 1; i >= 0; i--) {

		/* Allocate a DMA handle for all Command Blocks (Tx) */
		if (ddi_dma_alloc_handle(devinfo, &control_cmd_dma_attr,
		    DDI_DMA_SLEEP, 0,
		    &iprbp->cmd_blk_info[i].cmd_dma_handle) != DDI_SUCCESS)
			goto error;

		/* Allocate the Command Block itself */
		if (ddi_dma_mem_alloc(iprbp->cmd_blk_info[i].cmd_dma_handle,
		    sizeof (union iprb_generic_cmd), &accattr,
		    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
		    (caddr_t *)&iprbp->cmd_blk_info[i].cmd_virtaddr, &len,
		    &iprbp->cmd_blk_info[i].cmd_acc_handle) != DDI_SUCCESS) {
			ddi_dma_free_handle(
			    &iprbp->cmd_blk_info[i].cmd_dma_handle);
			iprbp->cmd_blk_info[i].cmd_dma_handle = NULL;
			goto error;
		}
		/* Bind the Handle and the memory together */
		if (ddi_dma_addr_bind_handle(
		    iprbp->cmd_blk_info[i].cmd_dma_handle,
		    NULL, (caddr_t)iprbp->cmd_blk_info[i].cmd_virtaddr,
		    sizeof (union iprb_generic_cmd), DDI_DMA_RDWR |
		    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0, &dma_cookie,
		    &ncookies) != DDI_DMA_MAPPED) {
			ddi_dma_mem_free(
			    &iprbp->cmd_blk_info[i].cmd_acc_handle);
			ddi_dma_free_handle(
			    &iprbp->cmd_blk_info[i].cmd_dma_handle);
			iprbp->cmd_blk_info[i].cmd_acc_handle = NULL;
			iprbp->cmd_blk_info[i].cmd_dma_handle = NULL;
			goto error;
		}
		ASSERT(ncookies == 1);
		/* Zero out the Command Blocks (Tx) for use */
		bzero(iprbp->cmd_blk_info[i].cmd_virtaddr,
		    sizeof (union iprb_generic_cmd));
		iprbp->cmd_blk_info[i].cmd_physaddr = dma_cookie.dmac_address;
		iprbp->cmd_blk[i] =
		    (void *)iprbp->cmd_blk_info[i].cmd_virtaddr;

		/* Initialise the Command Blocks for operation */
		iprbp->cmd_blk[i]->xmit_cmd.xmit_next = last_dma_addr;
		iprbp->cmd_blk[i]->xmit_cmd.xmit_cmd = IPRB_XMIT_CMD | IPRB_SF;
		last_dma_addr = dma_cookie.dmac_address;
	}
	iprbp->cmd_blk[iprbp->iprb_nxmits - 1]->xmit_cmd.xmit_next =
	    last_dma_addr;

	/* PreAllocate Tx Buffer descriptors. (One per Command Block) */
	for (i = iprbp->iprb_nxmits - 1; i >= 0; i--) {
		/* Allocate a handle for the Tx Buffer Descriptor */
		if (ddi_dma_alloc_handle(devinfo, &tx_buffer_desc_dma_attr,
		    DDI_DMA_SLEEP, 0,
		    &iprbp->Txbuf_info[i].tbd_dma_handle) != DDI_SUCCESS)
			goto error;

		/* Allocate the Tx Buffer Descriptor itself */
		if (ddi_dma_mem_alloc(iprbp->Txbuf_info[i].tbd_dma_handle,
		    sizeof (struct iprb_Txbuf_desc), &accattr,
		    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
		    (caddr_t *)&iprbp->Txbuf_info[i].tbd_virtaddr, &len,
		    &iprbp->Txbuf_info[i].tbd_acc_handle) != DDI_SUCCESS) {
			ddi_dma_free_handle(
			    &iprbp->Txbuf_info[i].tbd_dma_handle);
			iprbp->Txbuf_info[i].tbd_dma_handle = NULL;
			goto error;
		}
		/* Bind the Handle and the memory together */
		if (ddi_dma_addr_bind_handle(
		    iprbp->Txbuf_info[i].tbd_dma_handle,
		    NULL, (caddr_t)iprbp->Txbuf_info[i].tbd_virtaddr,
		    sizeof (struct iprb_Txbuf_desc), DDI_DMA_RDWR |
		    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0, &dma_cookie,
		    &ncookies) != DDI_DMA_MAPPED) {
			ddi_dma_mem_free(
			    &iprbp->Txbuf_info[i].tbd_acc_handle);
			ddi_dma_free_handle(
			    &iprbp->Txbuf_info[i].tbd_dma_handle);
			iprbp->Txbuf_info[i].tbd_acc_handle = NULL;
			iprbp->Txbuf_info[i].tbd_dma_handle = NULL;
			goto error;
		}
		ASSERT(ncookies == 1);

		/* Zero out the Tx Buffer descriptors for use */
		bzero(iprbp->Txbuf_info[i].tbd_virtaddr,
		    sizeof (struct iprb_Txbuf_desc));
		iprbp->Txbuf_info[i].tbd_physaddr = dma_cookie.dmac_address;
		iprbp->Txbuf_desc[i] =
		    (void *)iprbp->Txbuf_info[i].tbd_virtaddr;
	}

	/* PreAllocate handles for all the Tx Fragments held by TxBuf desc. */
	for (i = 0; i < iprbp->iprb_nxmits; i++)
		for (j = 0; j < IPRB_MAX_FRAGS; j++)
			if (ddi_dma_alloc_handle(devinfo,
			    &tx_buffer_dma_attr, DDI_DMA_SLEEP, 0,
			    &iprbp->Txbuf_info[i].frag_dma_handle[j]) !=
			    DDI_SUCCESS)
				goto error;

	/*
	 * Pre-allocate all the required Rx descriptors/buffers.
	 */

	/* Create the "free list" of Rx buffers. */
	for (i = 0; i < iprbp->min_rxbuf; i++) {
		rfd_info_free = iprb_alloc_buffer(iprbp);
		if (rfd_info_free == NULL)
			goto error;

		rfd_info_free->next = iprbp->free_list;
		iprbp->free_list = rfd_info_free;
		iprbp->free_buf_cnt++;
	}

	/* Take a buffer for each descriptor */
	ASSERT(iprbp->iprb_nrecvs <= IPRB_MAX_RECVS);
	ASSERT(iprbp->free_buf_cnt >= iprbp->iprb_nrecvs);

	last_dma_addr = IPRB_NULL_PTR;
	for (i = iprbp->iprb_nrecvs - 1; i >= 0; i--) {
		mutex_enter(&iprbp->freelist_mutex);
		rfd_info_free = iprb_get_buffer(iprbp);
		mutex_exit(&iprbp->freelist_mutex);

		/* Initialise the Rx descriptors into a list */
		iprbp->rfd_info[i] = rfd_info_free;
		iprbp->rfd[i] = (void *)rfd_info_free->rfd_virtaddr;
		iprbp->rfd[i]->rfd_next = last_dma_addr;
		iprbp->rfd[i]->rfd_rbd = IPRB_NULL_PTR;
		iprbp->rfd[i]->rfd_size = ETHERMTU +
		    sizeof (struct ether_vlan_header);
		last_dma_addr = rfd_info_free->rfd_physaddr;
	}
	/* This is the end of Rx descriptor list */
	iprbp->rfd[iprbp->iprb_nrecvs - 1]->rfd_next = last_dma_addr;
	iprbp->rfd[iprbp->iprb_nrecvs - 1]->rfd_control |= (IPRB_RFD_EL);

	/* Allocate a DMA handle for the statistics buffer */
	if (ddi_dma_alloc_handle(devinfo, &stats_buffer_dma_attr,
	    DDI_DMA_SLEEP, 0, &iprbp->stats_dmah) != DDI_SUCCESS) {
		goto error;
	}
	/* Now allocate memory for the statistics buffer */
	if (ddi_dma_mem_alloc(iprbp->stats_dmah, sizeof (struct iprb_stats),
	    &accattr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
	    (caddr_t *)&iprbp->stats, &len, &iprbp->stats_acch) !=
	    DDI_SUCCESS) {
		goto error;
	}
	bzero((void *)iprbp->stats, sizeof (struct iprb_stats));
	/* and finally get the DMA address associated with the buffer */
	if (ddi_dma_addr_bind_handle(iprbp->stats_dmah, NULL,
	    (caddr_t)iprbp->stats, sizeof (struct iprb_stats),
	    DDI_DMA_READ | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
	    &dma_cookie, &ncookies) != DDI_DMA_MAPPED) {
		goto error;
	}
	ASSERT(ncookies == 1);
	iprbp->stats_paddr = dma_cookie.dmac_address;

	return (DDI_SUCCESS);

error:
	cmn_err(CE_WARN, "iprb: could not allocate enough DMA memory.");
	iprb_free_dma_resources(iprbp);
	return (DDI_FAILURE);
}

/*
 * Release all DMA resources. In the opposite order from
 * iprb_alloc_dma_resources().
 */
static void
iprb_free_dma_resources(struct iprbinstance *iprbp)
{
	int i, j;

	/*
	 * Free all Rx resources and the Rx Free list.
	 */
	ASSERT(iprbp->iprb_receive_enabled == 0);
	ASSERT(iprbp->rfds_outstanding == 0);

	/*
	 * Free all the Rx Desc/buffers currently in use, place them on the
	 * 'free list' to be freed up.
	 */
	for (i = 0; i < iprbp->iprb_nrecvs; i++) {
		if (iprbp->rfd[i] == NULL)
			continue;
		ASSERT(iprbp->rfd_info[i] != NULL);
		(iprbp->rfd_info[i])->next = iprbp->free_list;
		iprbp->free_list = iprbp->rfd_info[i];
		iprbp->free_buf_cnt++;
		iprbp->rfd[i] = NULL;
		iprbp->rfd_info[i] = NULL;
	}

	/* Free Rx buffers on the "free list" */
	for (; iprbp->free_list; )
		iprb_remove_buffer(iprbp);
	ASSERT(iprbp->free_buf_cnt == 0);
	ASSERT(iprbp->rfd_cnt == 0);
	/*
	 * Free all Tx resources and all Command Blocks used for the adapter.
	 */

	/* Free the Tx Fragment Handles */
	for (i = 0; i < iprbp->iprb_nxmits; i++)
		for (j = 0; j < IPRB_MAX_FRAGS; j++)
			if (iprbp->Txbuf_info[i].frag_dma_handle[j] != NULL) {
				ddi_dma_free_handle(
				    &iprbp->Txbuf_info[i].frag_dma_handle[j]);
				iprbp->Txbuf_info[i].frag_dma_handle[j] = NULL;
			}
	/* Free the Tx Buffer Descriptors */
	for (i = 0; i < iprbp->iprb_nxmits; i++)
		if (iprbp->Txbuf_info[i].tbd_acc_handle != NULL) {
			(void) ddi_dma_unbind_handle(
			    iprbp->Txbuf_info[i].tbd_dma_handle);
			ddi_dma_mem_free(&iprbp->Txbuf_info[i].tbd_acc_handle);
			ddi_dma_free_handle(
			    &iprbp->Txbuf_info[i].tbd_dma_handle);

			iprbp->Txbuf_info[i].tbd_dma_handle = NULL;
			iprbp->Txbuf_info[i].tbd_acc_handle = NULL;
			iprbp->Txbuf_desc[i] = NULL;
		}
	/* Free the Command Blocks (Tx) */
	for (i = 0; i < iprbp->iprb_nxmits; i++)
		if (iprbp->cmd_blk_info[i].cmd_acc_handle != NULL) {
			(void) ddi_dma_unbind_handle(
			    iprbp->cmd_blk_info[i].cmd_dma_handle);
			ddi_dma_mem_free(
			    &iprbp->cmd_blk_info[i].cmd_acc_handle);
			ddi_dma_free_handle(
			    &iprbp->cmd_blk_info[i].cmd_dma_handle);
			iprbp->cmd_blk_info[i].cmd_acc_handle = NULL;
			iprbp->cmd_blk_info[i].cmd_dma_handle = NULL;
			iprbp->cmd_blk[i] = NULL;
		}

	/* Free the Stats buffer */
	if (iprbp->stats_paddr != 0)
		(void) ddi_dma_unbind_handle(iprbp->stats_dmah);
	if (iprbp->stats_acch != NULL)
		ddi_dma_mem_free(&iprbp->stats_acch);
	if (iprbp->stats_dmah != NULL)
		ddi_dma_free_handle(&iprbp->stats_dmah);
}

/*
 * Alloc a DMA-able data buffer and the decriptor which allow the driver to
 * track it.
 */
static struct iprb_rfd_info *
iprb_alloc_buffer(struct iprbinstance *iprbp)
{
	dev_info_t	*devinfo = iprbp->iprb_dip;
	struct iprb_rfd_info *rfd_info;
	struct iprb_rfd *rfd;
	uint_t		ncookies, how_far = 0;
	ddi_dma_cookie_t dma_cookie;
	size_t		len;

	if (iprbp->rfd_cnt >= iprbp->max_rxbuf)
		return (NULL);

	if ((rfd_info = kmem_zalloc(sizeof (struct iprb_rfd_info),
	    KM_NOSLEEP)) == NULL)
		return (NULL);

	if (ddi_dma_alloc_handle(devinfo, &rfd_dma_attr,
	    DDI_DMA_SLEEP, 0, &rfd_info->rfd_dma_handle) != DDI_SUCCESS)
		goto fail;

	how_far++;

	/* Now Attempt to allocate the data buffer itself!! */
	if (ddi_dma_mem_alloc(rfd_info->rfd_dma_handle,
	    sizeof (struct iprb_rfd), &accattr, DDI_DMA_STREAMING, 0, 0,
	    (caddr_t *)&rfd_info->rfd_virtaddr, &len,
	    &rfd_info->rfd_acc_handle) != DDI_SUCCESS)
		goto fail;

	how_far++;

	/* Bind the address of the data buffer to a handle */
	if (ddi_dma_addr_bind_handle(rfd_info->rfd_dma_handle, NULL,
	    (caddr_t)rfd_info->rfd_virtaddr, sizeof (struct iprb_rfd),
	    DDI_DMA_RDWR | DDI_DMA_STREAMING, DDI_DMA_SLEEP, 0, &dma_cookie,
	    &ncookies) != DDI_DMA_MAPPED)
		goto fail;

	/* Initialise RFD for Operation */
	rfd = (void *)rfd_info->rfd_virtaddr;
	bzero(rfd, sizeof (struct iprb_rfd));
	rfd->rfd_size = ETHERMTU + sizeof (struct ether_vlan_header);
	rfd->rfd_rbd = IPRB_NULL_PTR;

	rfd_info->rfd_physaddr = dma_cookie.dmac_address;
	rfd_info->iprbp = iprbp;
	rfd_info->free_rtn.free_func = iprb_rcv_complete;
	rfd_info->free_rtn.free_arg = (char *)rfd_info;
	iprbp->rfd_cnt++;
	return (rfd_info);

fail:
	switch (how_far) {
	case 2:		/* FALLTHROUGH */
		ddi_dma_mem_free(&rfd_info->rfd_acc_handle);
	case 1:		/* FALLTHROUGH */
		ddi_dma_free_handle(&rfd_info->rfd_dma_handle);
	case 0:		/* FALLTHROUGH */
		kmem_free(rfd_info, sizeof (struct iprb_rfd_info));
	default:
		return (NULL);
	}
}

/*
 * Shrink the Rx "Free List".
 */
static void
iprb_remove_buffer(struct iprbinstance *iprbp)
{
	struct iprb_rfd_info *rfd_info;

	if (iprbp->free_list == NULL)
		return;

	ASSERT(iprbp->free_buf_cnt > 0);
	/* Unlink buffer to be freed from free list */
	rfd_info = iprbp->free_list;
	iprbp->free_list = iprbp->free_list->next;
	iprbp->rfd_cnt--;
	iprbp->free_buf_cnt--;

	/* Kick that buffer out of here */
	(void) ddi_dma_unbind_handle(rfd_info->rfd_dma_handle);

	ddi_dma_mem_free(&rfd_info->rfd_acc_handle);
	ddi_dma_free_handle(&rfd_info->rfd_dma_handle);

	kmem_free(rfd_info, sizeof (struct iprb_rfd_info));
}

static struct iprb_rfd_info *
iprb_get_buffer(struct iprbinstance *iprbp)
{
	struct iprb_rfd_info *rfd_info;

	rfd_info = iprbp->free_list;
	if (rfd_info != NULL) {
		iprbp->free_list = iprbp->free_list->next;
		iprbp->free_buf_cnt--;
	} else
		rfd_info = iprb_alloc_buffer(iprbp);

	return (rfd_info);
}

static void
iprb_rcv_complete(struct iprb_rfd_info *rfd_info)
{
	struct iprbinstance *iprbp = rfd_info->iprbp;

	/* One less outstanding receive buffer */
	mutex_enter(&iprbp->freelist_mutex);
	ASSERT(iprbp->rfds_outstanding > 0);
	iprbp->rfds_outstanding--;
	/* Return buffer to free list */
	rfd_info->next = iprbp->free_list;
	iprbp->free_list = rfd_info;
	iprbp->free_buf_cnt++;
	mutex_exit(&iprbp->freelist_mutex);
}

static void
iprb_release_mblks(register struct iprbinstance *iprbp)
{
	register	int	i, j;

	for (i = 0; i < iprbp->iprb_nxmits; ++i) {
		if (iprbp->Txbuf_info[i].mp != NULL) {
			freemsg(iprbp->Txbuf_info[i].mp);
			iprbp->Txbuf_info[i].mp = NULL;
			for (j = 0; j < iprbp->Txbuf_info[i].frag_nhandles; j++)
				(void) ddi_dma_unbind_handle(
				    iprbp->Txbuf_info[i].frag_dma_handle[j]);
		}
	}
}

#ifdef IPRBDEBUG
static void
iprb_print_board_state(struct iprbinstance *iprbp)
{
	uint32_t scb_csr;
	uint32_t scb_ptr;

	scb_csr = ddi_get32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_STATUS));
	scb_ptr = ddi_get32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PTR));

	cmn_err(CE_WARN, "iprb: scb csr 0x%x scb ptr 0x%x", scb_csr, scb_ptr);
}
#endif

/*
 * Wdog Timer : Called every 2 seconds. If a receive operation has not been
 * completed within the two seconds this timer will break the RU lock
 * condition by sending a multicast setup command, which resets the RU and
 * releases it from the lock condition [ See Intel Confidential : Technical
 * Reference Manual PRO/100B 82557 ] [ 6.2 82557 B-step Errata, 6.2.2 Receive
 * Lockup ]
 */
static void
iprb_RU_wdog(void *arg)
{
	struct iprbinstance *iprbp = arg;
	unsigned long	val;
	volatile struct iprb_mcs_cmd *mcmd;
	int	i, s;

	mutex_enter(&iprbp->cmdlock);
	val = ddi_get_lbolt();

	if (val - iprbp->RUwdog_lbolt > RUTIMEOUT) {
		/* RU in Locked State , try to free it up */
		iprb_reap_commands(iprbp, IPRB_REAP_COMPLETE_CMDS);

		if (iprbp->iprb_current_cmd == iprbp->iprb_first_cmd) {
			/* command queue is full */
			iprb_reap_commands(iprbp, IPRB_REAP_ALL_CMDS);
		}


		/* construct Multicast Setup command */
		/*
		 * Receive Lockup only happens at 10 Mbps. This fix is only
		 * required if card is operating at 10 Mbps, ignore this fix
		 * for 100 Mbps networks.
		 */
		if (mii_get_speed(iprbp->mii) == 10) {
			mcmd =
			    &iprbp->cmd_blk[iprbp->iprb_current_cmd]->mcs_cmd;
			mcmd->mcs_cmd = IPRB_MCS_CMD;
			for (s = 0, i = 0; s < IPRB_MAXMCSN; s++) {
				if (iprbp->iprb_mcs_addrval[s] != 0) {
					bcopy(&iprbp->iprb_mcs_addrs[s],
					    (void *)
					    &mcmd->mcs_bytes[i * ETHERADDRL],
					    ETHERADDRL);
					i++;
				}
			}
			mcmd->mcs_count = i * ETHERADDRL;

			/* Send the command to the 82557 */
			iprb_add_command(iprbp);
			iprbp->RUwdog_lbolt = ddi_get_lbolt();
			iprbp->RU_stat_count++;
		}
	}
	if (iprbp->iprb_receive_enabled)
		iprbp->RUwdogID = timeout(iprb_RU_wdog, iprbp,
		    (clock_t)RUWDOGTICKS);

	mutex_exit(&iprbp->cmdlock);
}

/*
 * Based on the PHY dection routine in the PRO/100B tecnical reference manual
 * At startup we work out what phys are present, and when we attempt to
 * configure the device, we check what one to use
 */

static void
iprb_phyinit(struct iprbinstance *iprbp)
{
	struct mii_ops ops = {
		MII_OPS_VERSION,
		iprb_mii_readreg,
		iprb_mii_writereg,
		iprb_mii_notify,
		NULL
	};

	iprbp->mii = mii_alloc(iprbp, iprbp->iprb_dip, &ops);
	if (iprbp->mii == NULL) {
		cmn_err(CE_CONT, "!iprb: Cannot initialise MII instance");
		return;
	}
	/* 82558 and later support 100 Mbps symmetric pause */
	if (devsupf[iprbp->iprb_dev_type].flowcntrl == 1) {
		mii_set_pauseable(iprbp->mii, B_TRUE, B_FALSE);
	} else {
		mii_set_pauseable(iprbp->mii, B_FALSE, B_FALSE);
	}
}

/* Callbacks for MII code */
static uint16_t
iprb_mii_readreg(void *arg, uint8_t phy_addr, uint8_t reg_addr)
{
	struct iprbinstance	*iprbp = arg;
	uint32_t		cmd = 0;
	uint32_t		out_data = 0;
	int			timeout = 4;

	mutex_enter(&iprbp->cmdlock);
	cmd = IPRB_MDI_READFRAME(phy_addr, reg_addr);

	ddi_put32(iprbp->iohandle, REG32(iprbp->port, IPRB_SCB_MDICTL), cmd);

	/* max of 64 microseconds, as per PRO/100B HRM 3.3.6.3 */

	for (;;) {
		out_data = ddi_get32(iprbp->iohandle,
		    REG32(iprbp->port, IPRB_SCB_MDICTL));
		if (out_data & IPRB_MDI_READY || timeout-- == 0)
			break;
		drv_usecwait(16);
	}
	mutex_exit(&iprbp->cmdlock);
	return (out_data & 0xffff);
}

static void
iprb_mii_writereg(void *arg, uint8_t phy_addr, uint8_t reg_addr, uint16_t data)
{
	struct iprbinstance	*iprbp = arg;
	uint32_t		cmd;
	int			timeout = 4;

	mutex_enter(&iprbp->cmdlock);

	cmd = IPRB_MDI_WRITEFRAME(phy_addr, reg_addr, data);

	ddi_put32(iprbp->iohandle, REG32(iprbp->port, IPRB_SCB_MDICTL), cmd);

	/* Wait a max of 64 microseconds, as per note in section 3.3.6.2 */
	while (!(ddi_get32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_MDICTL)) & IPRB_MDI_READY) && timeout--)
		drv_usecwait(100);

	mutex_exit(&iprbp->cmdlock);
	if (timeout == -1)
		cmn_err(CE_WARN, "!iprb%d: timeout writing MDI frame",
		    iprbp->instance);

}

static void
iprb_mii_notify(void *arg, link_state_t state)
{
	struct iprbinstance *iprbp = arg;

	if (state == LINK_STATE_UP) {
		/* iprb_configure works out which port to select now */
		mutex_enter(&iprbp->cmdlock);
		iprb_configure(iprbp);
		mutex_exit(&iprbp->cmdlock);
	}

	mac_link_update(iprbp->mh, state);
}

/*
 * Procedure : iprb_get_eeprom_size
 *
 * Description :  This function will calculate the size of EEPROM as it can vary
 * from 82557 (64 registers) to 82558/82559 which supports 64 and 256
 * registers. No of bits required to address EEPROM registers will also change
 * and it will be 6 bits for 64 registers, 7 bits for 128 registers and 8
 * bits for 256 registers.
 *
 * Arguments : iprbp 	- pointer to struct iprbinstance
 *
 * Returns : size 	- EEPROM size in bytes.
 */

static ushort_t
iprb_get_eeprom_size(struct iprbinstance *iprbp)
{
	ushort_t	eex;		/* This is the manipulation bit */
	ushort_t	size = 1;	/* Must be 1 to accumulate product */
	ushort_t	address_length = 0;	/* EEPROM address length */
	/*
	 * The algorithm used to enable is dummy zero mechanism From the
	 * 82558/82559 Technical Reference Manual
	 * 1.	First activate EEPROM by writing a '1' to the EECS bit.
	 * 2.	Write the read opcode including the start bit (110b), one bit
	 *	`at a time, starting with the Msb('1').
	 * 2.1.	`Write the opcode bit to the EEDI bit.
	 * 2.2.	Write a '1' to a '1' to EESK bit and then wait for the
	 *	minimum SK high time.
	 * 2.3.	Write a '0' to EESK bit and then wait for the min SK low time.
	 * 2.4.	Repeat steps 3 to 5 for next 2 opcode bits
	 * 3.	Write the address field, one bit at a time, keeping track of
	 *	the number of bits shifted in, starting with MSB.
	 * 3.1.	Write the address bit to the EEDI bit.
	 * 3.2.	Write a '1' to EESK bit and then wait for min SK high time.
	 * 3.3.	Write a '0' to EESK bit and then wait for min SK low time.
	 * 3.4	Read the EEDO bit and check for "dummy zero bit".
	 * 3.5	Repeat steps 3.1 to 3.4 unttk the EEDO bit is set to '0'.
	 *	The number of loop operations performed will be equal to
	 *	number of bits in the address field.
	 * 4.	Read a 16 bit word from the EEPROM one bit at a time, starting
	 *	with the MSB, to complete the transaction.
	 * 4.1	Write a '1' to EESK bit and then wait for min SK high time.
	 * 4.2	Read data bit from the EEDO bit.
	 * 4.3	Write a '0' to EESK bit and then wait for min SK low time.
	 * 4.4	Repeat steps 4.1 to 4.3 for next 15 times.
	 * 5.	Deactivate the EEPROM by writing a 0 codeto EECS bit.
	 */
	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));

	eex &= ~(IPRB_EEDI | IPRB_EEDO | IPRB_EESK);
	eex |= IPRB_EECS;
	ddi_put8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL), eex);
	/* Write the read opcode */
	iprb_shiftout(iprbp, IPRB_EEPROM_READ, 3);

	/*
	 * experiment to discover the size of the eeprom.  request register
	 * zero and wait for the eeprom to tell us it has accepted the entire
	 * address.
	 */

	eex = ddi_get8(iprbp->iohandle, REG8(iprbp->port, IPRB_SCB_EECTL));
	do {
		size *= 2;	/* each bit of address doubles eeprom size */
		address_length++;
		eex |= IPRB_EEDO;	/* set bit to detect "dummy zero" */
		eex &= ~IPRB_EEDI;	/* address consists of all zeros */
		ddi_put8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_EECTL), eex);
		drv_usecwait(100);
		iprb_raiseclock(iprbp, (uint8_t *)&eex);
		iprb_lowerclock(iprbp, (uint8_t *)&eex);

		/* check for "dummy zero" */
		eex = (ushort_t)ddi_get8(iprbp->iohandle,
		    REG8(iprbp->port, IPRB_SCB_EECTL));
		if (size > 256) {
			size = 0;
			cmn_err(CE_WARN, "EEPROM size is ZERO");
			break;
		}
	} while (eex & IPRB_EEDO);

	/* read in the value requested */

	if (size) {
		(void) iprb_shiftin(iprbp);
		iprb_eeclean(iprbp);
	}
	iprbp->iprb_eeprom_address_length = address_length;

	return (size);
}



/*
 * Procedure: 	iprb_load_microcode
 *
 * Description: This routine downloads microcode on to the controller. This
 * microcode is available for the D101A, D101B0 and D101M. The microcode
 * reduces the number of receive interrupts by "bundling" them. The amount of
 * reduction in interrupts is configurable thru a iprb.conf file parameter
 * called CpuCycleSaver.
 *
 * Returns: B_TRUE - Success B_FALSE - Failure
 */
void
iprb_load_microcode(struct iprbinstance *iprbp, uchar_t revision_id)
{
	uint_t		i, microcode_length;
	uint32_t	d101a_microcode[] = D101_A_RCVBUNDLE_UCODE;
	/* Microcode for 82558 A Step */
	uint32_t	d101b0_microcode[] = D101_B0_RCVBUNDLE_UCODE;
	/* Microcode for 82558 B Step */
	uint32_t	d101ma_microcode[] = D101M_B_RCVBUNDLE_UCODE;
	/* Microcode for 82559 A Step */
	uint32_t	d101s_microcode[] = D101S_RCVBUNDLE_UCODE;

	uint32_t	d102_microcode[] = D102_B_RCVBUNDLE_UCODE;
	uint32_t	d102c_microcode[] = D102_C_RCVBUNDLE_UCODE;
	uint32_t	d102e_microcode[] = D102_E_RCVBUNDLE_UCODE;

	/* Microcode for 82559 S */
	uint32_t	*mlong;
	uint16_t	*mshort;

	int		cpusaver_dword = 0;
	uint32_t	cpusaver_dword_val = 0;
	struct iprb_ldmc_cmd *ldmc;

	/*
	 * If microcode loading is disabled, punt, but under no
	 * circumstances do so for Revision 12 iprb's, as the
	 * microcode download for that rev contains a *serious*
	 * bug fix, without which the interface can easily hang
	 * until reboot.
	 */
	if (iprbp->cpu_cycle_saver_dword_val == 0 &&
	    revision_id != D102_B_REV_ID)
		return;

#define	SETUCODE(u)						\
	mlong = u;						\
	mshort = (uint16_t *)u;					\
	microcode_length = sizeof (u) / sizeof (int32_t)

	/* Decide which microcode to use by looking at the board's rev_id */
	if (revision_id == D101A4_REV_ID) {
		SETUCODE(d101a_microcode);
		cpusaver_dword = D101_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00080600;
	} else if (revision_id == D101B0_REV_ID) {
		SETUCODE(d101b0_microcode);
		cpusaver_dword = D101_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00080600;
	} else if (revision_id == D101MA_REV_ID) {
		SETUCODE(d101ma_microcode);
		cpusaver_dword = D101M_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00080800;
	} else if (revision_id == D101S_REV_ID) {
		SETUCODE(d101s_microcode);
		cpusaver_dword = D101S_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00080600;
	} else if (revision_id == D102_B_REV_ID) {
		SETUCODE(d102_microcode);
		cpusaver_dword = D102_B_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00200600;
	} else if (revision_id == D102_C_REV_ID) {
		SETUCODE(d102c_microcode);
		cpusaver_dword = D102_C_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00200600;
	} else if (revision_id == D102_E_REV_ID) {
		SETUCODE(d102e_microcode);
		cpusaver_dword = D102_E_CPUSAVER_DWORD;
		cpusaver_dword_val = 0x00200600;

	} else {
		/* we don't have microcode for this board */
		return;
	}

	if (revision_id != D102_B_REV_ID) {
		/* Check microcode */
		if ((cpusaver_dword != 0) &&
		    (mlong[cpusaver_dword] != cpusaver_dword_val)) {
			cmn_err(CE_CONT,
			    "iprb_load_microcode: Invalid microcode");
			cmn_err(CE_CONT,
			    "mlong value %lu and it should be %lu/n",
			    (long)mlong[cpusaver_dword],
			    (long)cpusaver_dword_val);
			return;
		}

		/* Tune microcode for cpu saver */
		mshort[cpusaver_dword *2] =
		    (iprbp->cpu_cycle_saver_dword_val < 1) ? 0x0600 :
		    ((iprbp->cpu_cycle_saver_dword_val > 0xc000) ? 0x0600 :
		    iprbp->cpu_cycle_saver_dword_val);
	}

	/* Make available any command buffers already processed */
	if (iprb_reap_wait(iprbp) != DDI_SUCCESS)
		return;

	ldmc = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->ldmc_cmd;
	ldmc->ldmc_cmd = IPRB_LDMC_CMD;

	/* Copy in the microcode */
	for (i = 0; i < microcode_length; i++)
		ldmc->microcode[i] = mlong[i];

	/*
	 * Submit the Load microcode command to the chip, and wait for it to
	 * complete.
	 */
	iprb_add_command(iprbp);
}

static boolean_t
iprb_diag_test(struct iprbinstance *iprbp)
{
	boolean_t	status;
	struct	iprb_diag_cmd *diag;

	mutex_enter(&iprbp->cmdlock);

	/* Make available any command buffers already processed */
	iprb_reap_commands(iprbp, IPRB_REAP_ALL_CMDS);

	/* Any command buffers left? */
	if (iprbp->iprb_current_cmd == iprbp->iprb_first_cmd) {
		mutex_exit(&iprbp->cmdlock);
		return (B_FALSE);
	}
	diag = &iprbp->cmd_blk[iprbp->iprb_current_cmd]->diag_cmd;
	diag->diag_cmd = IPRB_DIAG_CMD;

	iprb_add_command(iprbp);
	drv_usecwait(5000);
	IPRB_SCBWAIT(iprbp);
	mutex_exit(&iprbp->cmdlock);

	if (((~(diag->diag_bits)) & 0x800) == 0x800) {
#ifdef IPRBDEBUG
		if (iprbdebug & IPRBTEST)
			cmn_err(CE_CONT, "iprb%d: Diagnostics Successful...",
			    iprbp->instance);
#endif
		status = B_TRUE;
	} else {
		cmn_err(CE_WARN, "iprb%d: Diagnostics failed...",
		    iprbp->instance);
		status = B_FALSE;
	}
	return (status);
}

static boolean_t
iprb_self_test(struct iprbinstance *iprbp)
{
	ddi_dma_handle_t dma_handle_selftest;
	volatile struct iprb_self_test_cmd *selftest;
	ddi_acc_handle_t selftest_dma_acchdl;
	uint32_t	self_test_cmd;
	size_t		len;
	boolean_t	status;
	dev_info_t	*devinfo = iprbp->iprb_dip;
	ddi_dma_cookie_t dma_cookie;
	int		i = 50;
	uint_t		ncookies;

	/* This command requires 16 byte aligned address */
	/* Allocate a DMA handle for the self test buffer */
	if (ddi_dma_alloc_handle(devinfo, &selftest_buffer_dma_attr,
	    DDI_DMA_SLEEP, 0, &dma_handle_selftest) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "iprb%d: could not allocate self test dma handle",
		    iprbp->instance);
		status = B_FALSE;
	} else {
		/* Now allocate memory for the self test buffer */
		if (ddi_dma_mem_alloc(dma_handle_selftest,
		    sizeof (struct iprb_self_test_cmd),
		    &accattr, DDI_DMA_STREAMING, DDI_DMA_SLEEP, 0,
		    (caddr_t *)&selftest, &len,
		    &selftest_dma_acchdl) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "iprb%d: could not allocate memory "
			    "for self test buffer", iprbp->instance);
			ddi_dma_free_handle(&dma_handle_selftest);
			status = B_FALSE;
		} else {
			bzero((void *)selftest, sizeof (*selftest));
			/*
			 * and finally get the DMA address associated with
			 * the buffer
			 */
			if (ddi_dma_addr_bind_handle(dma_handle_selftest,
			    NULL, (caddr_t)selftest,
			    sizeof (struct iprb_self_test_cmd),
			    DDI_DMA_READ, DDI_DMA_SLEEP, 0,
			    &dma_cookie, &ncookies) != DDI_DMA_MAPPED) {
				cmn_err(CE_WARN,
				    "iprb%d: could not get dma address for "
				    "self test buffer", iprbp->instance);
				ddi_dma_mem_free(&selftest_dma_acchdl);
				ddi_dma_free_handle(&dma_handle_selftest);
				status = B_FALSE;
			} else {
				ASSERT(ncookies == 1);
				self_test_cmd = dma_cookie.dmac_address;
				self_test_cmd |= IPRB_PORT_SELF_TEST;
				selftest->st_sign = 0;
				selftest->st_result = 0xffffffff;

				ddi_put32(iprbp->iohandle,
				    REG32(iprbp->port, IPRB_SCB_PORT),
				    (uint32_t)self_test_cmd);
				drv_usecwait(5000);
				/* Wait for PORT register to clear */
				do {
					drv_usecwait(10);
					if (ddi_get32(iprbp->iohandle,
					    REG32(iprbp->port, IPRB_SCB_PORT))
					    == 0)
						break;
				} while (--i > 0);

				if (ddi_get32(iprbp->iohandle,
				    REG32(iprbp->port, IPRB_SCB_PORT)) != 0) {
					cmn_err(CE_WARN,
					    "iprb%d: Port is not clear,"
					    " Self test command is not "
					    " completed yet...",
					    iprbp->instance);
				}
				if ((selftest->st_sign == 0) ||
				    (selftest->st_result != 0)) {
					cmn_err(CE_WARN,
					    "iprb%d: Selftest failed "
					    "Sig = %x Result = %x",
					    iprbp->instance,
					    selftest->st_sign,
					    selftest->st_result);
					status = B_FALSE;
				} else
					status = B_TRUE;
			}	/* else of ddi_dma_addr_bind_handle */
		}		/* else of ddi_dma_mem_alloc */
	}			/* else of ddi_dma_alloc_handle */
	/* Issue software reset */

	/*
	 * Port reset command should not be used under normal operation when
	 * device is active. This will reset the device unconditionally. In
	 * some cases this will hang the pci bus. First issue selective
	 * reset, wait for port register to be cleared (completion of
	 * selective reset command) and then issue a port Reset command.
	 */
	/* Memory Cleanup */

#ifdef IPRBDEBUG
	if (status == B_TRUE)
		if (iprbdebug & IPRBTEST)
			cmn_err(CE_CONT,
			    "iprb%d: Selftest Successful Sig = %x Result = %x",
			    iprbp->instance,
			    selftest->st_sign, selftest->st_result);
#endif
	(void) ddi_dma_unbind_handle(dma_handle_selftest);
	ddi_dma_mem_free(&selftest_dma_acchdl);
	ddi_dma_free_handle(&dma_handle_selftest);
	/* Now let us setup the device unconditionally */
	ddi_put32(iprbp->iohandle, REG32(iprbp->port, IPRB_SCB_PORT),
	    IPRB_PORT_SW_RESET);
	drv_usecwait(50);
	/* Wait for PORT register to clear */
	i = 10;
	do {
		drv_usecwait(10);
		if (ddi_get32(iprbp->iohandle,
		    REG32(iprbp->port, IPRB_SCB_PORT)) == 0)
			break;
	} while (--i > 0);

	if (ddi_get32(iprbp->iohandle,
	    REG32(iprbp->port, IPRB_SCB_PORT)) != 0) {
		cmn_err(CE_WARN, "iprb%d: Port is not clear,"
		    " SW reset command is not completed yet...",
		    iprbp->instance);
	}
	return (status);
}

static void
iprb_getprop(struct iprbinstance *iprbp)
{
	iprbp->max_rxbcopy = GETPROP(iprbp, "max-rxbcopy", IPRB_MAX_RXBCOPY);
	iprbp->iprb_threshold = GETPROP(iprbp,
	    "xmit-threshold", IPRB_DEFAULT_THRESHOLD);
	iprbp->iprb_nxmits = GETPROP(iprbp, "num-xmit-bufs", IPRB_MAX_XMITS);

	if (iprbp->iprb_nxmits < 3)
		iprbp->iprb_nxmits = IPRB_DEFAULT_XMITS;
	if (iprbp->iprb_nxmits > IPRB_MAX_XMITS)
		iprbp->iprb_nxmits = IPRB_MAX_XMITS;

	iprbp->iprb_nrecvs = GETPROP(iprbp,
	    "num-recv-bufs", IPRB_DEFAULT_RECVS);

	if (iprbp->iprb_nrecvs < 2)
		iprbp->iprb_nrecvs = IPRB_DEFAULT_RECVS;
	if (iprbp->iprb_nrecvs > IPRB_MAX_RECVS)
		iprbp->iprb_nrecvs = IPRB_MAX_RECVS;

	iprbp->min_rxbuf = GETPROP(iprbp, "min-recv-bufs", IPRB_FREELIST_SIZE);

	/* Must have at least 'nrecvs' */
	if (iprbp->min_rxbuf < iprbp->iprb_nrecvs)
		iprbp->min_rxbuf = iprbp->iprb_nrecvs;

	iprbp->max_rxbuf = GETPROP(iprbp, "max-recv-bufs", IPRB_MAX_RECVS);

	if (iprbp->max_rxbuf <= iprbp->min_rxbuf)
		iprbp->max_rxbuf = IPRB_MAX_RECVS;

	iprbp->do_self_test = GETPROP(iprbp,
	    "DoSelfTest", IPRB_DEFAULT_SELF_TEST);
	iprbp->do_diag_test = GETPROP(iprbp,
	    "DoDiagnosticsTest", IPRB_DEFAULT_DIAGNOSTICS_TEST);
	iprbp->Aifs = GETPROP(iprbp, "Adaptive-IFS", -1);

	if (iprbp->Aifs > 255)
		iprbp->Aifs = 255;

	iprbp->read_align = GETPROP(iprbp,
	    "ReadAlign", IPRB_DEFAULT_READAL_ENABLE);
	iprbp->tx_ur_retry = GETPROP(iprbp,
	    "TxURRetry", IPRB_DEFAULT_TXURRETRY);
	iprbp->ifs = GETPROP(iprbp, "inter-frame-spacing", -1);

	if (iprbp->ifs > 15)
		iprbp->ifs = 15;

	iprbp->coll_backoff = GETPROP(iprbp,
	    "CollisionBackOffModification", IPRB_DEFAULT_WAW);
	iprbp->flow_control = GETPROP(iprbp,
	    "FlowControl", IPRB_DEFAULT_FLOW_CONTROL);
	iprbp->curr_cna_backoff = GETPROP(iprbp,
	    "CurrentCNABackoff", IPRB_DEFAULT_CNA_BACKOFF);
	iprbp->cpu_cycle_saver_dword_val = GETPROP(iprbp,
	    "CpuCycleSaver", IPRB_DEFAULT_CPU_CYCLE_SAVER);
}

static ushort_t
iprb_get_dev_type(ushort_t devid, ushort_t revid)
{
	ushort_t type = DEV_TYPE_1229;	/* default */

	switch (devid) {
	case IPRB_PCI_DEVID_1229:
		if (revid >= D101A4_REV_ID)
			type = DEV_TYPE_1229_SERVER;
		break;
	case IPRB_PCI_DEVID_1029:
		type = DEV_TYPE_1229;
		break;
	case IPRB_PCI_DEVID_1030:
		type = DEV_TYPE_1030;
		break;
	case IPRB_PCI_DEVID_2449:
		type = DEV_TYPE_2449;
		break;
	}
	return (type);
}
