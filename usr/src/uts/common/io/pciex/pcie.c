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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/promif.h>
#include <sys/disp.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/conf.h>
#include <sys/pci_cap.h>
#include <sys/pci_impl.h>
#include <sys/pcie_impl.h>
#include <sys/ndi_impldefs.h>
#include <sys/iovcfg.h>
#include <sys/pciev_impl.h>
#include <sys/hotplug/pci/pcie_hp.h>
#include <sys/hotplug/pci/pcicfg.h>
#include <sys/hotplug/pci/pcishpc.h>
#include <sys/hotplug/pci/pcicfg.h>
#include <sys/pci_cfgacc.h>
#include <sys/kobj.h>
#include <sys/pciconf.h>
#include <sys/pcirm.h>
#include <sys/pcirm_impl.h>
#include <util/sscanf.h>

/* enable boot rebalance */
#define	PCIE_BR_ENABLE		0x0001

#define	TIME_TO_COMPLETE_FLR	1100

/*
 * force boot rebalance on all fabrics. this flag intended for debug
 * usage only, not for general purpose.
 */
#define	PCIE_BR_ALL_FABRICS	0x0002

/* Enable boot rebalance by default, can be disabled by setting to 0 */
int pcie_br_flags = PCIE_BR_ENABLE;

#define	MAX_RETRIES_FOR_CLASS_CONFIG_COMPLETION 5
#define	DELAY_FOR_CLASS_CONFIG_COMPLETION 20

#define	PCICFG_MAKE_REG_HIGH(busnum, devnum, funcnum, register)\
	(\
	((ulong_t)(busnum & 0xff) << 16)    |\
	((ulong_t)(devnum & 0x1f) << 11)    |\
	((ulong_t)(funcnum & 0x7) <<  8)    |\
	((ulong_t)(register & 0x3f)))

/* Local functions prototypes */
static void pcie_init_pfd(dev_info_t *);
static void pcie_fini_pfd(dev_info_t *);

#ifdef	__amd64
static void pcie_check_io_mem_range(ddi_acc_handle_t, boolean_t *, boolean_t *);
#endif /* __amd64 */

#ifdef DEBUG
uint_t pcie_debug_flags = 0;
uint_t pcie_debug = 0;
static void pcie_print_bus(pcie_bus_t *bus_p);
void pcie_dbg(char *fmt, ...);
#define	ERR_MSG(x)\
	(err_msg = x);
#else
#define	ERR_MSG(x)
#endif /* DEBUG */

/* Variable to control default PCI-Express config settings */
ushort_t pcie_command_default =
    PCI_COMM_SERR_ENABLE |
    PCI_COMM_WAIT_CYC_ENAB |
    PCI_COMM_PARITY_DETECT |
    PCI_COMM_ME |
    PCI_COMM_MAE |
    PCI_COMM_IO;

/* xxx_fw are bits that are controlled by FW and should not be modified */
ushort_t pcie_command_default_fw =
    PCI_COMM_SPEC_CYC |
    PCI_COMM_MEMWR_INVAL |
    PCI_COMM_PALETTE_SNOOP |
    PCI_COMM_WAIT_CYC_ENAB |
    0xF800; /* Reserved Bits */

ushort_t pcie_bdg_command_default_fw =
    PCI_BCNF_BCNTRL_ISA_ENABLE |
    PCI_BCNF_BCNTRL_VGA_ENABLE |
    0xF000; /* Reserved Bits */

/* PCI-Express Base error defaults */
ushort_t pcie_base_err_default =
    PCIE_DEVCTL_CE_REPORTING_EN |
    PCIE_DEVCTL_NFE_REPORTING_EN |
    PCIE_DEVCTL_FE_REPORTING_EN |
    PCIE_DEVCTL_UR_REPORTING_EN;

extern uint16_t pcie_devctl_default;

/* PCI-Express AER Root Control Register */
#define	PCIE_ROOT_SYS_ERR	(PCIE_ROOTCTL_SYS_ERR_ON_CE_EN | \
				PCIE_ROOTCTL_SYS_ERR_ON_NFE_EN | \
				PCIE_ROOTCTL_SYS_ERR_ON_FE_EN)

ushort_t pcie_root_ctrl_default =
    PCIE_ROOTCTL_SYS_ERR_ON_CE_EN |
    PCIE_ROOTCTL_SYS_ERR_ON_NFE_EN |
    PCIE_ROOTCTL_SYS_ERR_ON_FE_EN;

/* PCI-Express Root Error Command Register */
ushort_t pcie_root_error_cmd_default =
    PCIE_AER_RE_CMD_CE_REP_EN |
    PCIE_AER_RE_CMD_NFE_REP_EN |
    PCIE_AER_RE_CMD_FE_REP_EN;

/* ECRC settings in the PCIe AER Control Register */
uint32_t pcie_ecrc_value =
    PCIE_AER_CTL_ECRC_GEN_ENA |
    PCIE_AER_CTL_ECRC_CHECK_ENA;

/*
 * If a particular platform wants to disable certain errors such as UR/MA,
 * instead of using #defines have the platform's PCIe Root Complex driver set
 * these masks using the pcie_get_XXX_mask and pcie_set_XXX_mask functions.  For
 * x86 the closest thing to a PCIe root complex driver is NPE.	For SPARC the
 * closest PCIe root complex driver is PX.
 *
 * pcie_serr_disable_flag : disable SERR only (in RCR and command reg) x86
 * systems may want to disable SERR in general.  For root ports, enabling SERR
 * causes NMIs which are not handled and results in a watchdog timeout error.
 */
uint32_t pcie_aer_uce_mask = 0;		/* AER UE Mask */
uint32_t pcie_aer_ce_mask = 0;		/* AER CE Mask */
uint32_t pcie_aer_suce_mask = 0;	/* AER Secondary UE Mask */
uint32_t pcie_serr_disable_flag = 0;	/* Disable SERR */

/* Default severities needed for eversholt.  Error handling doesn't care */
uint32_t pcie_aer_uce_severity = PCIE_AER_UCE_MTLP | PCIE_AER_UCE_RO | \
    PCIE_AER_UCE_FCP | PCIE_AER_UCE_SD | PCIE_AER_UCE_DLP | \
    PCIE_AER_UCE_TRAINING;
uint32_t pcie_aer_suce_severity = PCIE_AER_SUCE_SERR_ASSERT | \
    PCIE_AER_SUCE_UC_ADDR_ERR | PCIE_AER_SUCE_UC_ATTR_ERR | \
    PCIE_AER_SUCE_USC_MSG_DATA_ERR;

int pcie_disable_ari = 0;

/* Round up to 8-byte aligned */
#define	PCIE_RUP(a) P2ROUNDUP(a, 8)

#define	PCIE_RUP_ADD(a, b) ((a) + PCIE_RUP(b))

dev_info_t *pcie_get_boot_usb_hc();
dev_info_t *pcie_get_boot_disk();

extern int (*pciv_get_pf_list_p)(nvlist_t **);
extern int pcie_get_pf_list(nvlist_t **);
extern int (*pciv_get_vf_list_p)(char *, nvlist_t **);
extern int pcie_get_vf_list(char *, nvlist_t **);
extern int (*pciv_get_numvfs_p)(char *, uint_t *);
extern int pcie_get_numvfs(char *, uint_t *);
extern int pcie_param_get(char *, nvlist_t **);
extern int (*pciv_param_get_p)(char *, nvlist_t **);
extern int pcie_plist_lookup(pci_plist_t, va_list);
extern int (*pciv_plist_lookup_p)(pci_plist_t, va_list);
extern int (*pciv_plist_getvf_p)(nvlist_t *, uint16_t, pci_plist_t *);
extern int pcie_plist_getvf(nvlist_t *, uint16_t, pci_plist_t *);
extern void (*pciv_class_config_completed_p)(char *);
void pcie_class_config_completed(char *);

#define	HI32(x) ((uint32_t)(((uint64_t)(x)) >> 32))
#define	LO32(x) ((uint32_t)(x))
#define	HI16(x) ((uint16_t)(((uint32_t)(x)) >> 16))
#define	LO16(x) ((uint16_t)(x))
#define	HI8(x) ((uint8_t)(((uint16_t)(x)) >> 8))

#define	PCIE_MEMGRAN 0x100000
#define	PCIE_IOGRAN 0x1000

static void
pcie_dump_sriov_config(dev_info_t *dip)
{
	uint32_t		value;
	int			bar_index;
	dev_info_t		*rcdip;
	pcie_req_id_t		bdf = 0;
	uint16_t		sriov_cap_ptr;

	if (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_SRIOV),
	    &sriov_cap_ptr) != DDI_SUCCESS)
		return;
	if (pcie_get_bdf_from_dip(dip, &bdf) != DDI_SUCCESS)
		return;
	rcdip = pcie_get_rc_dip(dip);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
	cmn_err(CE_CONT, " SRIOV_CONTROL = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET);
	cmn_err(CE_CONT, " SRIOV_INITIAL_VFS = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET);
	cmn_err(CE_CONT, " SRIOV_TOTAL_VFS = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET);
	cmn_err(CE_CONT, " SRIOV_NUM_VFS = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	cmn_err(CE_CONT, " SRIOV_First_VF_OFFSET = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	cmn_err(CE_CONT, " SRIOV_VF_STRIDE = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_VF_DEV_ID_OFFSET);
	cmn_err(CE_CONT, " SRIOV_VF_DEVICE_ID = [0x%x]\n", value);
	value = pci_cfgacc_get16(rcdip, bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_SYSTEM_PAGE_SIZE_OFFSET);
	cmn_err(CE_CONT, " SRIOV_SYSTEM_PAGESIZE = [0x%x]\n", value);
	for (bar_index = PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET;
	    bar_index <= PCIE_EXT_CAP_SRIOV_VF_BAR5_OFFSET;
	    bar_index += 4) {
		value = pci_cfgacc_get32(rcdip, bdf,
		    (bar_index + sriov_cap_ptr));
		cmn_err(CE_CONT, " SRIOV_VF_BAR = [0x%x]\n", value);
	}
}
/*
 * Execute the call back registerd with dip.
 * If there is no callback registered, it is OK, return success.
 */
int
pcie_cb_execute(dev_info_t *dip, ddi_cb_action_t action, void *cbarg)
{
	ddi_cb_t	*cbp;

	ASSERT(dip != NULL);
	if (dip == NULL)
		return (DDI_EINVAL);
	cbp = DEVI(dip)->devi_cb_p;
	if (cbp == NULL) {
		/*
		 * There is no call back registered.
		 * return success.
		 */
		return (DDI_SUCCESS);
	}
	if (cbp->cb_func == NULL)
		return (DDI_EINVAL);
	if (cbp->cb_dip != dip)
		return (DDI_EINVAL);
	return (cbp->cb_func(dip, action, cbarg, cbp->cb_arg1, cbp->cb_arg2));
}

int
pcie_get_bus_dev_type(dev_info_t *dip, uint16_t *pcie_off_p)
{
	uint16_t	status, base;
	dev_info_t	*rcdip;
	pcie_bus_t	*bus_p;
	pcie_req_id_t	bus_bdf = 0;
	uint8_t		bus_hdr_type;
	uint16_t	num_cap = 3;
	int		bus_dev_type;
	uint32_t	capid;

	if (pcie_off_p == NULL)
		return (DDI_FAILURE);
	*pcie_off_p = 0;
	bus_p = PCIE_DIP2BUS(dip);
	if (bus_p) {
		*pcie_off_p = bus_p->bus_pcie_off;
		return ((int)bus_p->bus_dev_type);
	}
	if (pcie_get_bdf_from_dip(dip, &bus_bdf) != DDI_SUCCESS)
		return (DDI_FAILURE);
	rcdip = pcie_get_rc_dip(dip);
	bus_hdr_type = pci_cfgacc_get8(rcdip, bus_bdf, PCI_CONF_HEADER);
	bus_hdr_type &= PCI_HEADER_TYPE_M;
	status = pci_cfgacc_get16(rcdip, bus_bdf, PCI_CONF_STAT);
	if (status == PCI_CAP_EINVAL16 || !(status & PCI_STAT_CAP))
		return (DDI_FAILURE); /* capability not supported */
	switch (bus_hdr_type) {
	case PCI_HEADER_ZERO:
		base = PCI_CONF_CAP_PTR;
		break;
	case PCI_HEADER_PPB:
		base = PCI_BCNF_CAP_PTR;
		break;
	case PCI_HEADER_CARDBUS:
		base = PCI_CBUS_CAP_PTR;
		break;
	default:
		cmn_err(CE_WARN, "%s: unexpected pci header type:%x",
		    __func__, bus_hdr_type);
		return (DDI_FAILURE);
	}
	bus_dev_type = PCIE_PCIECAP_DEV_TYPE_PCI_PSEUDO;
	for (base = pci_cfgacc_get8(rcdip, bus_bdf, base);
	    base && num_cap;
	    base = pci_cfgacc_get8(rcdip, bus_bdf,
	    base + PCI_CAP_NEXT_PTR)) {
		capid = pci_cfgacc_get8(rcdip, bus_bdf, base);
		switch (capid) {
		case PCI_CAP_ID_PCI_E:
			*pcie_off_p = base;
			bus_dev_type = (int)pci_cfgacc_get16(rcdip, bus_bdf,
			    base + PCIE_PCIECAP) & PCIE_PCIECAP_DEV_TYPE_MASK;
			return (bus_dev_type);
		default:
			num_cap--;
			break;
		}
	}
	return (bus_dev_type);
}

int
pcie_get_secbus(dev_info_t *dip)
{
	dev_info_t	*rcdip;
	dev_info_t	*pdip;
	int		secbus;
	pcie_req_id_t	bdf;

	/*
	 * Do not attempt to use bus_bdg_secbus because
	 * this may not be set even if we find a non NULL bus_p
	 * bus_bdg_secbus is not set during PCIE_BUS_INITIAL
	 */
	ASSERT(dip != NULL);
	pdip = ddi_get_parent(dip);
	rcdip = pcie_get_rc_dip(pdip);
	ASSERT(rcdip != NULL);
	if (pcie_get_bdf_from_dip(pdip, &bdf) != DDI_SUCCESS)
		return (DDI_FAILURE);
	secbus = pci_cfgacc_get8(rcdip, bdf, PCI_BCNF_SECBUS);
	return (secbus);
}

int
pcie_get_dev_func_num(dev_info_t *dip, int *dev_num, int *func)
{
	pcie_bus_t	*bus_p;
	int		secbus;
	pcie_req_id_t	sec_bdf, bdf;
	pcie_fn_type_t	func_type;
	uint16_t	dev_type;
	uint16_t	pcie_off;

	ASSERT(dip != NULL);
	ASSERT(dev_num != NULL);
	ASSERT(func != NULL);
	if (pcie_get_bdf_from_dip(dip, &bdf) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	*func = (bdf & PCIE_REQ_ID_FUNC_MASK);
	*dev_num = (bdf & PCIE_REQ_ID_DEV_MASK) >> PCIE_REQ_ID_DEV_SHIFT;
	bus_p = PCIE_DIP2BUS(dip);
	func_type = FUNC_TYPE_REGULAR;
	if (bus_p != NULL) {
		func_type = bus_p->bus_func_type;
		dev_type = bus_p->bus_dev_type;
	} else {
		dev_type = pcie_get_bus_dev_type(dip, &pcie_off);
		if ((dev_type == PCIE_PCIECAP_DEV_TYPE_PCIE_DEV) &&
		    (pcie_is_vf(dip)))
			func_type = FUNC_TYPE_VF;
	}
	if ((func_type == FUNC_TYPE_VF) ||
	    ((dev_type == PCIE_PCIECAP_DEV_TYPE_PCIE_DEV) &&
	    (pcie_ari_is_enabled(ddi_get_parent(dip))) ==
	    PCIE_ARI_FORW_ENABLED)) {
		secbus = pcie_get_secbus(dip);
		sec_bdf = (secbus << 8);
		*func = bdf - sec_bdf;
		*dev_num = 0;
	}
	return (DDI_SUCCESS);
}

int
pcie_get_unit_addr(dev_info_t *dip, char *namep)
{
	int		func;
	int		device;
	char		*addrname;
	char		none = '\0';

	ASSERT(dip != NULL);
	ASSERT(namep != NULL);
	namep[0] = '\0';
	if (i_ddi_node_state(dip) < DS_BOUND) {
		addrname = &none;
	} else {
		addrname = ddi_get_name_addr(dip);
		if (addrname == NULL)
			addrname = &none;
	}
	if (*addrname != '\0') {
		(void) strcpy(namep, addrname);
		return (DDI_SUCCESS);
	}
	if (pcie_get_dev_func_num(dip, &device, &func) != DDI_SUCCESS)
		return (DDI_FAILURE);
	if (func)
		(void) sprintf(namep, "%x,%x", device, func);
	else
		(void) sprintf(namep, "%x", device);
	return (DDI_SUCCESS);
}

static int
pcie_deviname(dev_info_t *dip, char *namep)
{
	char		unit_addr[32];

	ASSERT(dip != NULL);
	ASSERT(namep != NULL);
	if (dip == ddi_root_node()) {
		*namep = '\0';
		return (0);
	}
	(void) ddi_pathname(dip, namep);
	if (pcie_get_unit_addr(dip, unit_addr) != DDI_SUCCESS)
		return (DDI_FAILURE);
	(void) sprintf(namep, "/%s@%s", ddi_node_name(dip), unit_addr);
	return (0);
}

int
pcie_pathname(dev_info_t *dip, char *pathnamep)
{
	char	*bp;
	int	err;

	ASSERT(dip != NULL);
	if (dip == ddi_root_node()) {
		*pathnamep = '\0';
		return (0);
	}
	(void) pcie_pathname(ddi_get_parent(dip), pathnamep);
	bp = pathnamep + strlen(pathnamep);
	err = pcie_deviname(dip, bp);
	return (err);
}

int
pcie_cap_locate(dev_info_t *dip, uint32_t id, uint16_t *base_p)
{
	uint16_t	status, base;
	dev_info_t	*rcdip;
	pcie_bus_t	*bus_p;
	uint32_t	xcaps_hdr;
	pcie_req_id_t	bus_bdf = 0;
	uint8_t		bus_hdr_type;
	uint16_t	bus_pcie_off = NULL;
	uint32_t	capid;
	uint16_t	num_cap = 3;

	*base_p = 0;
	bus_p = PCIE_DIP2BUS(dip);
	rcdip = pcie_get_rc_dip(dip);
	if (rcdip == NULL)
		return (DDI_FAILURE);
	if (bus_p == NULL) {
		if (pcie_get_bdf_from_dip(dip, &bus_bdf) != DDI_SUCCESS)
			return (DDI_FAILURE);
		bus_hdr_type = pci_cfgacc_get8(rcdip, bus_bdf, PCI_CONF_HEADER);
		bus_hdr_type &= PCI_HEADER_TYPE_M;
	} else {
		bus_bdf = bus_p->bus_bdf;
		bus_hdr_type = bus_p->bus_hdr_type;
	}
	status = pci_cfgacc_get16(rcdip, bus_bdf, PCI_CONF_STAT);
	if (status == PCI_CAP_EINVAL16 || !(status & PCI_STAT_CAP))
		return (DDI_FAILURE); /* capability not supported */
	switch (bus_hdr_type) {
	case PCI_HEADER_ZERO:
		base = PCI_CONF_CAP_PTR;
		break;
	case PCI_HEADER_PPB:
		base = PCI_BCNF_CAP_PTR;
		break;
	case PCI_HEADER_CARDBUS:
		base = PCI_CBUS_CAP_PTR;
		break;
	default:
		cmn_err(CE_WARN, "%s: unexpected pci header type:%x",
		    __func__, bus_hdr_type);
		return (DDI_FAILURE);
	}
	if (id & PCI_CAP_XCFG_FLAG) {
		if (bus_p == NULL) {
			for (base = pci_cfgacc_get8(rcdip, bus_bdf, base);
			    base && num_cap;
			    base = pci_cfgacc_get8(rcdip, bus_bdf,
			    base + PCI_CAP_NEXT_PTR)) {
				capid = pci_cfgacc_get8(rcdip, bus_bdf, base);
				switch (capid) {
				case PCI_CAP_ID_PCI_E:
					bus_pcie_off = base;
					num_cap--;
					break;
				case PCI_CAP_ID_PCIX:
					num_cap--;
					break;
				case PCI_CAP_ID_MSI:
					num_cap--;
					break;
				default:
					break;
				}
				if (bus_pcie_off) break;
			}
		} else {
			bus_pcie_off = bus_p->bus_pcie_off;
		}
		if (bus_pcie_off == NULL)
			return (DDI_FAILURE);
		for (base = PCIE_EXT_CAP; base;
		    base = (xcaps_hdr >> PCIE_EXT_CAP_NEXT_PTR_SHIFT) &
		    PCIE_EXT_CAP_NEXT_PTR_MASK) {
			if ((xcaps_hdr = pci_cfgacc_get32(rcdip, bus_bdf,
			    base)) == PCI_CAP_EINVAL32)
				break;
			if (((xcaps_hdr >> PCIE_EXT_CAP_ID_SHIFT) &
			    PCIE_EXT_CAP_ID_MASK) == (uint16_t)id) {
				*base_p = base;
				return (DDI_SUCCESS);
			}
		}
	} else {
		for (base = pci_cfgacc_get8(rcdip, bus_bdf, base); base;
		    base = pci_cfgacc_get8(rcdip, bus_bdf,
		    base + PCI_CAP_NEXT_PTR)) {
			if (pci_cfgacc_get8(rcdip, bus_bdf, base)
			    == (uint8_t)id) {
				*base_p = base;
				return (DDI_SUCCESS);
			}
		}
	}
	*base_p = PCI_CAP_NEXT_PTR_NULL;
	return (DDI_FAILURE);
}

int
pcie_flr(dev_info_t *dip)
{
	pcie_bus_t	*bus_p;
	uint16_t	reg16;

	ASSERT(dip != NULL);
	bus_p = PCIE_DIP2UPBUS(dip);
	if (bus_p == NULL)
		return (DDI_EINVAL);
	if (pci_save_config_regs(dip) != DDI_SUCCESS) {
		PCIE_DBG("%s(%d): pcie_flr: failed to save "
		    "config space regs\n", ddi_driver_name(dip),
		    ddi_get_instance(dip));
		return (DDI_FAILURE);
	}
#ifdef DEBUG
	if (pcie_debug)
		pcie_dump_sriov_config(dip);
#endif
	reg16 = PCIE_CAP_GET(16, bus_p, PCIE_DEVCTL);
	reg16 |= PCIE_DEVCTL_INITIATE_FLR;
	PCIE_CAP_PUT(16, bus_p, PCIE_DEVCTL, reg16);
	delay(MSEC_TO_TICK(TIME_TO_COMPLETE_FLR));
	if (pci_restore_config_regs(dip) != DDI_SUCCESS) {
		PCIE_DBG("%s(%d): pcie_flr: failed to restore "
		    "config space regs\n", ddi_driver_name(dip),
		    ddi_get_instance(dip));
		return (DDI_FAILURE);
	}
#ifdef DEBUG
	if (pcie_debug)
		pcie_dump_sriov_config(dip);
#endif
	return (DDI_SUCCESS);
}

int
pciv_vf_config(dev_info_t *dip, pciv_config_vf_t *vfcfg_p)
{
	int			ret;
	pcie_bus_t		*bus_p;
	int			value;
	ddi_acc_handle_t	config_handle;
#ifdef DEBUG
	char			*path, *err_msg;
#endif

	ASSERT(dip != NULL);
	bus_p = PCIE_DIP2UPBUS(dip);
	if ((bus_p == NULL) || (vfcfg_p == NULL))
		return (DDI_EINVAL);
	config_handle = bus_p->bus_cfg_hdl;
	/*
	 * Make sure that dip is SRIOV capable
	 */
	if (bus_p->sriov_cap_ptr == 0) {
		ERR_MSG("device is not SRIOV capable");
		ret = DDI_EINVAL;
		goto exit;
	}
	switch (vfcfg_p->cmd) {
	case PCIV_VFCFG_PARAM:
		vfcfg_p->num_vf = bus_p->num_vf;
		vfcfg_p->page_size = PCICFG_SRIOV_SYSTEM_PAGE_SIZE;
		if (bus_p->bus_ari == B_TRUE) {
			vfcfg_p->ari_cap = B_TRUE;
		} else {
			vfcfg_p->ari_cap = B_FALSE;
		}
		value = PCI_XCAP_GET16(config_handle, NULL,
		    bus_p->sriov_cap_ptr,
		    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
		if (value == PCI_CAP_EINVAL32) {
			ERR_MSG("unable to get VF offset");
			ret = DDI_FAILURE;
			goto exit;
		}
		if (value == 0) {
			ERR_MSG("VF offset is 0 !!");
			ret = DDI_FAILURE;
			goto exit;
		}
		vfcfg_p->first_vf_offset = (uint16_t)value;
		value = PCI_XCAP_GET16(config_handle, NULL,
		    bus_p->sriov_cap_ptr,
		    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
		if (value == PCI_CAP_EINVAL32) {
			ERR_MSG("unable to get VF stride");
			ret = DDI_FAILURE;
			goto exit;
		}
		vfcfg_p->vf_stride = (uint16_t)value;
		return (DDI_SUCCESS);
	case PCIV_VF_ENABLE:
		if ((bus_p->num_vf == 0) || (bus_p->num_vf != vfcfg_p->num_vf))
			return (DDI_EINVAL);
		ret = pcicfg_config_vf(dip);
		return (ret);
	case PCIV_VF_DISABLE:
		if (bus_p->num_vf == 0)
			return (DDI_EINVAL);
		ret = pcicfg_unconfig_vf(dip);
		return (ret);
	default:
		return (DDI_EINVAL);
	}
exit:
#ifdef DEBUG
	if (pcie_debug) {
		path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
		(void) ddi_pathname(dip, path);
		cmn_err(CE_WARN, "pciv_vf_config:%s:%s\n", path, err_msg);
		kmem_free(path, MAXPATHLEN);
	}
#endif
	return (ret);
}


/*
 * Getting PF AER register value for a VF
 * this only works for root domain
 */
uint32_t
pcie_get_PF_aer_reg(pcie_bus_t *vf_bus_p, off_t offset)
{
	dev_info_t *pf_dip = PCIE_GET_PF_DIP(vf_bus_p);
	pcie_bus_t *pf_bus_p;
	uint32_t rval;

	ASSERT(pf_dip);

	pf_bus_p = PCIE_DIP2BUS(pf_dip);
	rval = PCIE_AER_GET(32, pf_bus_p, offset);
	return (rval);
}

/*
 * modload support
 */

static struct modlmisc modlmisc	= {
	&mod_miscops,	/* Type	of module */
	"PCI Express Framework Module"
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void	*)&modlmisc,
	NULL
};

/*
 * Global Variables needed for a non-atomic version of ddi_fm_ereport_post.
 * Currently used to send the pci.fabric ereports whose payload depends on the
 * type of PCI device it is being sent for.
 */
char		*pcie_nv_buf;
nv_alloc_t	*pcie_nvap;
nvlist_t	*pcie_nvl;

int
_init(void)
{
	int rval;

	pcie_nv_buf = kmem_alloc(ERPT_DATA_SZ, KM_SLEEP);
	pcie_nvap = fm_nva_xcreate(pcie_nv_buf, ERPT_DATA_SZ);
	pcie_nvl = fm_nvlist_create(pcie_nvap);
	pciv_get_pf_list_p = pcie_get_pf_list;
	pciv_get_vf_list_p = pcie_get_vf_list;
	pciv_get_numvfs_p = pcie_get_numvfs;
	pciv_param_get_p = pcie_param_get;
	pciv_plist_lookup_p = pcie_plist_lookup;
	pciv_plist_getvf_p = pcie_plist_getvf;
	pciv_class_config_completed_p = pcie_class_config_completed;

	(void) pciconf_parse_now(PCICONF_FILE);
	if (pciconf_file != (struct _buf *)-1) {
		/*
		 * clean up the buffer.
		 */
		if (pciconf_file->_base != NULL)
			kmem_free(pciconf_file->_base, MAX_PCICONF_FILE_SIZE);
		kmem_free(pciconf_file->_name, strlen(pciconf_file->_name)+1);
		kmem_free(pciconf_file, sizeof (struct _buf));
		pciconf_file = (struct _buf *)-1;
	}
	pciev_init();
	pciv_proxy_init();

	if ((rval = mod_install(&modlinkage)) != 0) {
		fm_nvlist_destroy(pcie_nvl, FM_NVA_RETAIN);
		fm_nva_xdestroy(pcie_nvap);
		kmem_free(pcie_nv_buf, ERPT_DATA_SZ);
	}
	return (rval);
}

int
_fini()
{
	int		rval;

	pciv_proxy_fini();
	pciev_fini();
	clean_pciconf_data();

	if ((rval = mod_remove(&modlinkage)) == 0) {
		fm_nvlist_destroy(pcie_nvl, FM_NVA_RETAIN);
		fm_nva_xdestroy(pcie_nvap);
		kmem_free(pcie_nv_buf, ERPT_DATA_SZ);
	}
	return (rval);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

void
pcie_class_config_completed(char *pf_path)
{
	dev_info_t	*pf_dip, *pdip, *dip;
	pcie_bus_t	*bus_p;
	int		circ, ret;

	pf_dip = pcie_find_dip_by_unit_addr(pf_path);
	if (pf_dip == NULL)
		return;
	bus_p = PCIE_DIP2BUS(pf_dip);
	if (bus_p == NULL)
		return;
	if (bus_p->iovcfg_class_config_completed)
		return;
	bus_p->iovcfg_class_config_completed = B_TRUE;
	pdip = ddi_get_parent(pf_dip);
	for (dip = ddi_get_child(pdip); dip; dip = ddi_get_next_sibling(dip)) {
		bus_p = PCIE_DIP2UPBUS(dip);
		if (!bus_p || (bus_p->bus_pf_dip != pf_dip))
			continue;
		if (DEVI_IS_ASSIGNED(dip)) {
			(void) ddi_ctlops(dip, dip, DDI_CTLOPS_REPORTDEV,
			    NULL, NULL);
		} else {
			if (!i_ddi_devi_attached(dip)) {
				ndi_devi_enter(pdip, &circ);
				/*
				 * Attach the VF device
				 */
				ret = i_ndi_config_node(dip, DS_READY, 0);
#ifdef DEBUG
				char	*path;

				if (pcie_debug && (ret != DDI_SUCCESS)) {
					path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
					(void) ddi_pathname(dip, path);
					cmn_err(CE_WARN,
					    "Failed to attach VF %s\n", path);
					kmem_free(path, MAXPATHLEN);
				}
#endif
				ndi_devi_exit(pdip, circ);
			}
		}
	}
}

int
pcie_resume_vf_devices(dev_info_t *pf_dip)
{
	dev_info_t	*pdip, *dip;
	pcie_bus_t	*bus_p;
	int		ret, rv;
#ifdef DEBUG
	char		*path = NULL;
#endif

	if (pf_dip == NULL)
		return (DDI_FAILURE);
	bus_p = PCIE_DIP2BUS(pf_dip);
	if (bus_p == NULL)
		return (DDI_FAILURE);
	pdip = ddi_get_parent(pf_dip);
	ret = DDI_SUCCESS;
	for (dip = ddi_get_child(pdip); dip; dip = ddi_get_next_sibling(dip)) {
		bus_p = PCIE_DIP2UPBUS(dip);
		if (!bus_p || (bus_p->bus_pf_dip != pf_dip))
			continue;
		if (!DEVI_IS_ASSIGNED(dip) &&
		    (bus_p->iov_dev_state & IOV_DEV_SUSPENDED)) {
#ifdef DEBUG
			if (pcie_debug) {
				if (path == NULL)
					path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
				(void) ddi_pathname(dip, path);
				cmn_err(CE_NOTE, "resuming VF device %s\n",
				    path);
			}
#endif
			rv = devi_attach(dip, DDI_RESUME);
			if (rv == DDI_SUCCESS) {
				bus_p->iov_dev_state &= (~IOV_DEV_SUSPENDED);
			} else {
#ifdef DEBUG
				if (pcie_debug) {
					cmn_err(CE_NOTE,
					"resuming failed for VF device %s\n",
					    path);
				}
#endif
				ret = rv;	/* saved failed condition */
			}
		}
	}
#ifdef DEBUG
	if (path)
		kmem_free(path, MAXPATHLEN);
#endif
	return (ret);
}

int
pcie_suspend_vf_devices(dev_info_t *pf_dip)
{
	dev_info_t	*pdip, *dip;
	pcie_bus_t	*bus_p;
	int		ret;
#ifdef DEBUG
	char		*path = NULL;
#endif

	if (pf_dip == NULL)
		return (DDI_FAILURE);
	bus_p = PCIE_DIP2BUS(pf_dip);
	if (bus_p == NULL)
		return (DDI_FAILURE);
	pdip = ddi_get_parent(pf_dip);
	for (dip = ddi_get_child(pdip); dip; dip = ddi_get_next_sibling(dip)) {
		bus_p = PCIE_DIP2UPBUS(dip);
		if (!bus_p || (bus_p->bus_pf_dip != pf_dip))
			continue;

		/*
		 * suspend devices that are assigned to root domain
		 * and are in attached state
		 */

		if (!DEVI_IS_ASSIGNED(dip) && i_ddi_devi_attached(dip)) {
			ret = devi_detach(dip, DDI_SUSPEND);
			if (ret)
				break;
			bus_p->iov_dev_state |= IOV_DEV_SUSPENDED;
#ifdef DEBUG
			if (pcie_debug) {
				if (path == NULL)
					path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
				(void) ddi_pathname(dip, path);
				cmn_err(CE_NOTE, "suspended VF device %s\n",
				    path);
			}
#endif
		}
	}
#ifdef DEBUG
	if (path)
		kmem_free(path, MAXPATHLEN);
#endif
	if (ret == DDI_SUCCESS)
		return (DDI_SUCCESS);
	/*
	 * could not suspend all Vf devices.
	 * resume the devices suspended.
	 */
	return (pcie_resume_vf_devices(pf_dip));
}

/* ARGSUSED */
int
pcie_init(dev_info_t *dip, caddr_t arg)
{
	int	ret = DDI_SUCCESS;

#ifdef	DEBUG
	if (pcie_debug) {
		char *path;

		path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
		(void) ddi_pathname(dip, path);
		cmn_err(CE_NOTE, "pcie_init called for path %s\n", path);
		kmem_free(path, MAXPATHLEN);
	}
#endif
	/*
	 * Create a "devctl" minor node to support DEVCTL_DEVICE_*
	 * and DEVCTL_BUS_* ioctls to this bus.
	 */
	if ((ret = ddi_create_minor_node(dip, "devctl", S_IFCHR,
	    PCI_MINOR_NUM(ddi_get_instance(dip), PCI_DEVCTL_MINOR),
	    DDI_NT_NEXUS, 0)) != DDI_SUCCESS) {
		PCIE_DBG("Failed to create devctl minor node for %s%d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));

		return (ret);
	}

	if ((ret = pcie_hp_init(dip, arg)) != DDI_SUCCESS) {
		/*
		 * On a few x86 platforms, we observed unexpected hotplug
		 * initialization failures in recent years. Continue with
		 * a message printed because we don't want to stop PCI
		 * driver attach and system boot because of this hotplug
		 * initialization failure before we address all those issues.
		 */
		PCIE_DBG("%s%d: Failed setting hotplug framework\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));

#if defined(__sparc)
		ddi_remove_minor_node(dip, "devctl");

		return (ret);
#endif /* defined(__sparc) */
	}

	return (DDI_SUCCESS);
}

/* ARGSUSED */
int
pcie_uninit(dev_info_t *dip)
{
	int	ret = DDI_SUCCESS;

	if (pcie_ari_is_enabled(dip) == PCIE_ARI_FORW_ENABLED)
		(void) pcie_ari_disable(dip);

	if ((ret = pcie_hp_uninit(dip)) != DDI_SUCCESS) {
		PCIE_DBG("Failed to uninitialize hotplug for %s%d\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));

		return (ret);
	}

	ddi_remove_minor_node(dip, "devctl");

	return (ret);
}

/*
 * PCIe module interface for enabling hotplug interrupt.
 *
 * It should be called after pcie_init() is done and bus driver's
 * interrupt handlers have being attached.
 */
int
pcie_hpintr_enable(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pcie_hp_ctrl_t	*ctrl_p = PCIE_GET_HP_CTRL(dip);

	if (PCIE_IS_PCIE_HOTPLUG_ENABLED(bus_p)) {
		(void) (ctrl_p->hc_ops.enable_hpc_intr)(ctrl_p);
	} else if (PCIE_IS_PCI_HOTPLUG_ENABLED(bus_p)) {
		(void) pcishpc_enable_irqs(ctrl_p);
	}
	return (DDI_SUCCESS);
}

/*
 * PCIe module interface for disabling hotplug interrupt.
 *
 * It should be called before pcie_uninit() is called and bus driver's
 * interrupt handlers is dettached.
 */
int
pcie_hpintr_disable(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pcie_hp_ctrl_t	*ctrl_p = PCIE_GET_HP_CTRL(dip);

	if (PCIE_IS_PCIE_HOTPLUG_ENABLED(bus_p)) {
		(void) (ctrl_p->hc_ops.disable_hpc_intr)(ctrl_p);
	} else if (PCIE_IS_PCI_HOTPLUG_ENABLED(bus_p)) {
		(void) pcishpc_disable_irqs(ctrl_p);
	}
	return (DDI_SUCCESS);
}


/* ARGSUSED */
int
pcie_intr(dev_info_t *dip)
{
	return (pcie_hp_intr(dip) | pcie_lbn_intr(dip));
}

/* ARGSUSED */
int
pcie_open(dev_info_t *dip, dev_t *devp, int flags, int otyp, cred_t *credp)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	/*
	 * Make sure the open is for the right file type.
	 */
	if (otyp != OTYP_CHR)
		return (EINVAL);

	/*
	 * Handle the open by tracking the device state.
	 */
	if ((bus_p->bus_soft_state == PCI_SOFT_STATE_OPEN_EXCL) ||
	    ((flags & FEXCL) &&
	    (bus_p->bus_soft_state != PCI_SOFT_STATE_CLOSED))) {
		return (EBUSY);
	}

	if (flags & FEXCL)
		bus_p->bus_soft_state = PCI_SOFT_STATE_OPEN_EXCL;
	else
		bus_p->bus_soft_state = PCI_SOFT_STATE_OPEN;

	return (0);
}

/* ARGSUSED */
int
pcie_close(dev_info_t *dip, dev_t dev, int flags, int otyp, cred_t *credp)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	bus_p->bus_soft_state = PCI_SOFT_STATE_CLOSED;

	return (0);
}

/* ARGSUSED */
int
pcie_ioctl(dev_info_t *dip, dev_t dev, int cmd, intptr_t arg, int mode,
    cred_t *credp, int *rvalp)
{
	struct devctl_iocdata	*dcp;
	uint_t			bus_state;
	int			rv = DDI_SUCCESS;

	/*
	 * We can use the generic implementation for devctl ioctl
	 */
	switch (cmd) {
	case DEVCTL_DEVICE_GETSTATE:
	case DEVCTL_DEVICE_ONLINE:
	case DEVCTL_DEVICE_OFFLINE:
	case DEVCTL_BUS_GETSTATE:
		return (ndi_devctl_ioctl(dip, cmd, arg, mode, 0));
	default:
		break;
	}

	/*
	 * read devctl ioctl data
	 */
	if (ndi_dc_allochdl((void *)arg, &dcp) != NDI_SUCCESS)
		return (EFAULT);

	switch (cmd) {
	case DEVCTL_BUS_QUIESCE:
		if (ndi_get_bus_state(dip, &bus_state) == NDI_SUCCESS)
			if (bus_state == BUS_QUIESCED)
				break;
		(void) ndi_set_bus_state(dip, BUS_QUIESCED);
		break;
	case DEVCTL_BUS_UNQUIESCE:
		if (ndi_get_bus_state(dip, &bus_state) == NDI_SUCCESS)
			if (bus_state == BUS_ACTIVE)
				break;
		(void) ndi_set_bus_state(dip, BUS_ACTIVE);
		break;
	case DEVCTL_BUS_RESET:
	case DEVCTL_BUS_RESETALL:
	case DEVCTL_DEVICE_RESET:
		rv = ENOTSUP;
		break;
	default:
		rv = ENOTTY;
	}

	ndi_dc_freehdl(dcp);
	return (rv);
}

/* ARGSUSED */
int
pcie_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
    int flags, char *name, caddr_t valuep, int *lengthp)
{
	if (dev == DDI_DEV_T_ANY)
		goto skip;

	if (PCIE_IS_HOTPLUG_CAPABLE(dip) &&
	    strcmp(name, "pci-occupant") == 0) {
		int	pci_dev = PCI_MINOR_NUM_TO_PCI_DEVNUM(getminor(dev));

		pcie_hp_create_occupant_props(dip, dev, pci_dev);
	}

skip:
	return (ddi_prop_op(dev, dip, prop_op, flags, name, valuep, lengthp));
}

int
pcie_init_cfghdl(dev_info_t *cdip)
{
	pcie_bus_t		*bus_p;
	ddi_acc_handle_t	eh = NULL;

	bus_p = PCIE_DIP2BUS(cdip);
	if (bus_p == NULL)
		return (DDI_FAILURE);

	/* Create an config access special to error handling */
	if (pci_config_setup(cdip, &eh) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Cannot setup config access"
		    " for BDF 0x%x\n", bus_p->bus_bdf);
		return (DDI_FAILURE);
	}

	bus_p->bus_cfg_hdl = eh;
	return (DDI_SUCCESS);
}

void
pcie_fini_cfghdl(dev_info_t *cdip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(cdip);

	pci_config_teardown(&bus_p->bus_cfg_hdl);
}

#define	PCICFG_LADDR(lo, hi)	(((uint64_t)(hi) << 32) | (uint32_t)(lo))

/*
 * PCI-Express child device initialization.
 * This function enables generic pci-express interrupts and error
 * handling.
 *
 * @param pdip		root dip (root nexus's dip)
 * @param cdip		child's dip (device's dip)
 * @return		DDI_SUCCESS or DDI_FAILURE
 */
/* ARGSUSED */
int
pcie_initchild(dev_info_t *cdip)
{
	uint16_t		tmp16, reg16;
	pcie_bus_t		*bus_p;
	int			value;
	char			*path;
	dev_info_t		*rcdip;

	bus_p = PCIE_DIP2BUS(cdip);
	if (bus_p == NULL) {
		PCIE_DBG("%s: BUS not found.\n",
		    ddi_driver_name(cdip));

		return (DDI_FAILURE);
	}

	if (pcie_init_cfghdl(cdip) != DDI_SUCCESS)
		return (DDI_FAILURE);
	if (bus_p->sriov_cap_ptr) {
		rcdip = pcie_get_rc_dip(cdip);
		/*
		 * If the VF enable bit is set issue a FLR to clear the VF
		 * enable. This happens if a reboot command is issued on x86
		 * system that had the VF enabled prior to reboot.
		 */
		value = pci_cfgacc_get16(rcdip, bus_p->bus_bdf,
		    (bus_p->sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET));
		if ((value & PCIE_SRIOV_CONTROL_VF_ENABLE)) {
			path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) pcie_pathname(cdip, path);
			pci_cfgacc_put16(rcdip, bus_p->bus_bdf,
			    (bus_p->sriov_cap_ptr +
			    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET),
			    0);
			pci_cfgacc_put16(rcdip, bus_p->bus_bdf,
			    (bus_p->sriov_cap_ptr +
			    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET),
			    0);
			kmem_free(path, MAXPATHLEN);
		}
	}

	/* Clear the device's status register */
	reg16 = PCIE_GET(16, bus_p, PCI_CONF_STAT);
	PCIE_PUT(16, bus_p, PCI_CONF_STAT, reg16);

	/* Setup the device's command register */
	reg16 = PCIE_GET(16, bus_p, PCI_CONF_COMM);
	tmp16 = reg16 & pcie_command_default_fw;
	if (!PCIE_IS_VF(bus_p))
		tmp16 |= pcie_command_default;
	else
		tmp16 |= (pcie_command_default &
		    (~(PCI_COMM_PARITY_DETECT | PCI_COMM_SERR_ENABLE)));

#ifdef	__amd64
	boolean_t empty_io_range = B_FALSE;
	boolean_t empty_mem_range = B_FALSE;
	/*
	 * Check for empty IO and Mem ranges on bridges. If so disable IO/Mem
	 * access as it can cause a hang if enabled.
	 */
	pcie_check_io_mem_range(bus_p->bus_cfg_hdl, &empty_io_range,
	    &empty_mem_range);
	if ((empty_io_range == B_TRUE) &&
	    (pcie_command_default & PCI_COMM_IO)) {
		tmp16 &= ~PCI_COMM_IO;
		PCIE_DBG("No I/O range found for %s, bdf 0x%x\n",
		    ddi_driver_name(cdip), bus_p->bus_bdf);
	}
	if ((empty_mem_range == B_TRUE) &&
	    (pcie_command_default & PCI_COMM_MAE)) {
		tmp16 &= ~PCI_COMM_MAE;
		PCIE_DBG("No Mem range found for %s, bdf 0x%x\n",
		    ddi_driver_name(cdip), bus_p->bus_bdf);
	}
#endif /* __amd64 */

	if (pcie_serr_disable_flag && PCIE_IS_PCIE(bus_p) &&
	    (!PCIE_IS_VF(bus_p)))
		tmp16 &= ~PCI_COMM_SERR_ENABLE;

	PCIE_PUT(16, bus_p, PCI_CONF_COMM, tmp16);
	PCIE_DBG_CFG(cdip, bus_p, "COMMAND", 16, PCI_CONF_COMM, reg16);

	/*
	 * If the device has a bus control register then program it
	 * based on the settings in the command register.
	 */
	if (PCIE_IS_BDG(bus_p)) {
		/* Clear the device's secondary status register */
		reg16 = PCIE_GET(16, bus_p, PCI_BCNF_SEC_STATUS);
		PCIE_PUT(16, bus_p, PCI_BCNF_SEC_STATUS, reg16);

		/* Setup the device's secondary command register */
		reg16 = PCIE_GET(16, bus_p, PCI_BCNF_BCNTRL);
		tmp16 = (reg16 & pcie_bdg_command_default_fw);

		tmp16 |= PCI_BCNF_BCNTRL_SERR_ENABLE;
		/*
		 * Workaround for this Nvidia bridge. Don't enable the SERR
		 * enable bit in the bridge control register as it could lead to
		 * bogus NMIs.
		 */
		if (bus_p->bus_dev_ven_id == 0x037010DE)
			tmp16 &= ~PCI_BCNF_BCNTRL_SERR_ENABLE;

		if (pcie_command_default & PCI_COMM_PARITY_DETECT)
			tmp16 |= PCI_BCNF_BCNTRL_PARITY_ENABLE;

		/*
		 * Enable Master Abort Mode only if URs have not been masked.
		 * For PCI and PCIe-PCI bridges, enabling this bit causes a
		 * Master Aborts/UR to be forwarded as a UR/TA or SERR.  If this
		 * bit is masked, posted requests are dropped and non-posted
		 * requests are returned with -1.
		 */
		if (pcie_aer_uce_mask & PCIE_AER_UCE_UR)
			tmp16 &= ~PCI_BCNF_BCNTRL_MAST_AB_MODE;
		else
			tmp16 |= PCI_BCNF_BCNTRL_MAST_AB_MODE;
		PCIE_PUT(16, bus_p, PCI_BCNF_BCNTRL, tmp16);
		PCIE_DBG_CFG(cdip, bus_p, "SEC CMD", 16, PCI_BCNF_BCNTRL,
		    reg16);
	}

	if (PCIE_IS_PCIE(bus_p)) {
		/* Enable PCIe errors */
		pcie_enable_errors(cdip);
	}

	bus_p->bus_ari = B_FALSE;
	if ((pcie_ari_is_enabled(ddi_get_parent(cdip))
	    == PCIE_ARI_FORW_ENABLED) && (pcie_ari_device(cdip)
	    == PCIE_ARI_DEVICE)) {
		bus_p->bus_ari = B_TRUE;
		if (bus_p->bus_func_type == FUNC_TYPE_PF) {
			value = PCI_XCAP_GET16(bus_p->bus_cfg_hdl, NULL,
			    bus_p->sriov_cap_ptr,
			    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
			value |= PCIE_SRIOV_CONTROL_ARI_CAPABLE_HEIRARCHY;
			PCI_XCAP_PUT16(bus_p->bus_cfg_hdl, NULL,
			    bus_p->sriov_cap_ptr,
			    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET, value);
		}
	}

	pf_init(cdip, NULL, DDI_ATTACH);

	/*
	 * VFs that are assigned are handled in pcie_postattach()
	 */
	if (DEVI_IS_ASSIGNED(cdip) && (bus_p->bus_func_type != FUNC_TYPE_VF)) {
		(void) ddi_ctlops(cdip, cdip, DDI_CTLOPS_REPORTDEV,
		    NULL, NULL);
	}

	return (DDI_SUCCESS);
}

static void
pcie_init_pfd(dev_info_t *dip)
{
	size_t len = sizeof (pf_data_t);
	pf_data_t *pfd_p;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	uint8_t *start;
	size_t off_aff_dev = 0, off_root_eh_src = 0;
	size_t off_block_hdr = 0, off_pci = 0, off_pci_bdg = 0;
	size_t off_pcie = 0, off_pcie_adv = 0, off_pcie_rp = 0;
	size_t off_pcie_adv_rp = 0, off_pcie_adv_bdg = 0;
	size_t off_pcix_bdg = 0, off_pcix_ecc_0 = 0, off_pcix_ecc_1 = 0;
	size_t off_pcix = 0, off_pcix_ecc = 0;
	size_t off_pf_info = 0, off_root_err_info = 0;

	/*
	 * Decide buffer size depending on the device type
	 * Each struct should be 8-byte aligned
	 */
	len = PCIE_RUP(len);

	/* Data not passed across domains should be initialized first */
	off_aff_dev = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_affected_dev_t));

	if (PCIE_IS_ROOT(bus_p)) {
		off_root_eh_src = len;
		len = PCIE_RUP_ADD(len, sizeof (pf_root_eh_src_t));
	}

	/* Initialize data that are to be passed across domains */
	off_block_hdr = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_blk_hdr_t) * PF_MAX_REG_BLOCK);

	off_pci = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pci_err_regs_t));

	if (PCIE_IS_BDG(bus_p)) {
		off_pci_bdg = len;
		len = PCIE_RUP_ADD(len, sizeof (pf_pci_bdg_err_regs_t));
	}

	if (PCIE_IS_PCIE(bus_p)) {
		off_pcie = len;
		len = PCIE_RUP_ADD(len, sizeof (pf_pcie_err_regs_t));
		off_pcie_adv = len;
		len = PCIE_RUP_ADD(len, sizeof (pf_pcie_adv_err_regs_t));

		if (PCIE_IS_RP(bus_p)) {
			off_pcie_rp = len;
			len = PCIE_RUP_ADD(len, sizeof (pf_pcie_rp_err_regs_t));
			off_pcie_adv_rp = len;
			len = PCIE_RUP_ADD(len,
			    sizeof (pf_pcie_adv_rp_err_regs_t));
		} else if (PCIE_IS_PCIE_BDG(bus_p)) {
			off_pcie_adv_bdg = len;
			len = PCIE_RUP_ADD(len,
			    sizeof (pf_pcie_adv_bdg_err_regs_t));
			if (PCIE_IS_PCIX(bus_p)) {
				off_pcix_bdg = len;
				len = PCIE_RUP_ADD(len,
				    sizeof (pf_pcix_bdg_err_regs_t));
				if (PCIX_ECC_VERSION_CHECK(bus_p)) {
					off_pcix_ecc_0 = len;
					len = PCIE_RUP_ADD(len,
					    sizeof (pf_pcix_ecc_regs_t));
					off_pcix_ecc_1 = len;
					len = PCIE_RUP_ADD(len,
					    sizeof (pf_pcix_ecc_regs_t));
				}
			}
		}
	} else if (PCIE_IS_PCIX(bus_p)) {
		if (PCIE_IS_BDG(bus_p)) {
			off_pcix_bdg = len;
			len = PCIE_RUP_ADD(len,
			    sizeof (pf_pcix_bdg_err_regs_t));
			if (PCIX_ECC_VERSION_CHECK(bus_p)) {
				off_pcix_ecc_0 = len;
				len = PCIE_RUP_ADD(len,
				    sizeof (pf_pcix_ecc_regs_t));
				off_pcix_ecc_1 = len;
				len = PCIE_RUP_ADD(len,
				    sizeof (pf_pcix_ecc_regs_t));
			}
		} else {
			off_pcix = len;
			len = PCIE_RUP_ADD(len, sizeof (pf_pcix_err_regs_t));
			if (PCIX_ECC_VERSION_CHECK(bus_p)) {
				off_pcix_ecc = len;
				len = PCIE_RUP_ADD(len,
				    sizeof (pf_pcix_ecc_regs_t));
			}
		}
	}

	if (PCIE_IS_VF(bus_p)) {
		off_pf_info = len;
		len = PCIE_RUP_ADD(len, sizeof (pf_pcie_PF_aer_t));
	}

	if (PCIE_IS_ROOT(bus_p)) {
		off_root_err_info = len;
		len = PCIE_RUP_ADD(len, sizeof (pf_root_err_info_t));
	}

	pfd_p = (pf_data_t *)kmem_zalloc(len, KM_SLEEP);

	/* Setup pfd structure depending on the device type */
	PCIE_DIP2PFD(dip) = pfd_p;
	pfd_p->pe_len = len;

	pfd_p->pe_bus_p = bus_p;
	pfd_p->pe_severity_flags = 0;
	pfd_p->pe_orig_severity_flags = 0;
	pfd_p->pe_lock = B_FALSE;
	pfd_p->pe_valid = B_FALSE;

	start = (uint8_t *)pfd_p;

	PFD_AFFECTED_DEV(pfd_p) =
	    (pf_affected_dev_t *)((uintptr_t)(start + off_aff_dev));
	PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf = PCIE_INVALID_BDF;

	if (off_root_eh_src) {
		PCIE_ROOT_EH_SRC(pfd_p) = (pf_root_eh_src_t *)(
		    (uintptr_t)(start + off_root_eh_src));
	}

	PFD_BLK_HDR(pfd_p) = (pf_blk_hdr_t *)(
	    (uintptr_t)(start + off_block_hdr));

	PF_OFFSET(pfd_p, PF_IDX_PCI_REG) = off_pci - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCI_REG) = PCIE_RUP(sizeof (pf_pci_err_regs_t));

	if (off_pci_bdg) {
		PF_OFFSET(pfd_p, PF_IDX_PCI_BDG_REG) =
		    off_pci_bdg - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCI_BDG_REG) =
		    PCIE_RUP(sizeof (pf_pci_bdg_err_regs_t));
	}

	if (off_pcie) {
		PF_OFFSET(pfd_p, PF_IDX_PCIE_REG) = off_pcie - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIE_REG) =
		    PCIE_RUP(sizeof (pf_pcie_err_regs_t));
	}

	if (off_pcie_adv) {
		PF_OFFSET(pfd_p, PF_IDX_PCIE_AER_REG) =
		    off_pcie_adv - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIE_AER_REG) =
		    PCIE_RUP(sizeof (pf_pcie_adv_err_regs_t));
		PCIE_ADV_REG(pfd_p)->pcie_ue_tgt_bdf = PCIE_INVALID_BDF;
	}

	if (off_pcie_rp) {
		PF_OFFSET(pfd_p, PF_IDX_PCIE_RP_REG) =
		    off_pcie_rp - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIE_RP_REG) =
		    PCIE_RUP(sizeof (pf_pcie_rp_err_regs_t));
	}

	if (off_pcie_adv_rp) {
		PF_OFFSET(pfd_p, PF_IDX_PCIE_RP_AER_REG) =
		    off_pcie_adv_rp - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIE_RP_AER_REG) =
		    PCIE_RUP(sizeof (pf_pcie_adv_rp_err_regs_t));
		PCIE_ADV_RP_REG(pfd_p)->pcie_rp_ce_src_id = PCIE_INVALID_BDF;
		PCIE_ADV_RP_REG(pfd_p)->pcie_rp_ue_src_id = PCIE_INVALID_BDF;
	}

	if (off_pcie_adv_bdg) {
		PF_OFFSET(pfd_p, PF_IDX_PCIE_BDG_AER_REG) =
		    off_pcie_adv_bdg - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIE_BDG_AER_REG) =
		    PCIE_RUP(sizeof (pf_pcie_adv_bdg_err_regs_t));
		PCIE_ADV_BDG_REG(pfd_p)->pcie_sue_tgt_bdf = PCIE_INVALID_BDF;
	}

	if (off_pcix_bdg) {
		PF_OFFSET(pfd_p, PF_IDX_PCIX_BDG_REG) =
		    off_pcix_bdg - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIX_BDG_REG) =
		    PCIE_RUP(sizeof (pf_pcix_bdg_err_regs_t));
	}

	if (off_pcix_ecc_0) {
		PF_OFFSET(pfd_p, PF_IDX_PCIX_ECC_REG_0) =
		    off_pcix_ecc_0 - off_block_hdr;
		PF_OFFSET(pfd_p, PF_IDX_PCIX_ECC_REG_1) =
		    off_pcix_ecc_1 - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIX_ECC_REG_0) =
		    PCIE_RUP(sizeof (pf_pcix_ecc_regs_t));
		PF_LEN(pfd_p, PF_IDX_PCIX_ECC_REG_1) =
		    PCIE_RUP(sizeof (pf_pcix_ecc_regs_t));
	}

	if (off_pcix) {
		PF_OFFSET(pfd_p, PF_IDX_PCIX_REG) = off_pcix - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIX_REG) =
		    PCIE_RUP(sizeof (pf_pcix_err_regs_t));
	}

	if (off_pcix_ecc) {
		PF_OFFSET(pfd_p, PF_IDX_PCIX_ECC_REG_0) =
		    off_pcix_ecc - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PCIX_ECC_REG_0) =
		    PCIE_RUP(sizeof (pf_pcix_ecc_regs_t));
	}

	if (off_pf_info) {
		PF_OFFSET(pfd_p, PF_IDX_PF_INFO) = off_pf_info - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_PF_INFO) =
		    PCIE_RUP(sizeof (pf_pcie_PF_aer_t));
	}

	if (off_root_err_info) {
		PF_OFFSET(pfd_p, PF_IDX_ROOT_ERR_INFO) =
		    off_root_err_info - off_block_hdr;
		PF_LEN(pfd_p, PF_IDX_ROOT_ERR_INFO) =
		    PCIE_RUP(sizeof (pf_root_err_info_t));
		PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf = PCIE_INVALID_BDF;
	}
}

static void
pcie_fini_pfd(dev_info_t *dip)
{
	pf_data_t	*pfd_p = PCIE_DIP2PFD(dip);
	size_t		len = pfd_p->pe_len;

	kmem_free(pfd_p, len);

	PCIE_DIP2PFD(dip) = NULL;
}


/*
 * Special functions to allocate pf_data_t's for PCIe root complexes.
 * Note: Root Complex not Root Port
 */
void
pcie_rc_init_pfd(dev_info_t *dip, pf_data_t **pfd_pp)
{
	size_t len = sizeof (pf_data_t);
	pf_data_t *pfd_p;
	uint8_t *start;
	pcie_bus_t *bus_p;
	size_t off_pci, off_aff_dev, off_pci_bdg, off_pcie, off_pcie_adv,
	    off_pcie_rp, off_pcie_adv_rp, off_root_eh_src, off_root_err_info,
	    off_block_hdr;

	len = PCIE_RUP(len);

	off_aff_dev = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_affected_dev_t));

	off_root_eh_src = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_root_eh_src_t));

	off_block_hdr = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_blk_hdr_t) * PF_MAX_REG_BLOCK);

	off_pci = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pci_err_regs_t));

	off_pci_bdg = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pci_bdg_err_regs_t));

	off_pcie = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pcie_err_regs_t));

	off_pcie_adv = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pcie_adv_err_regs_t));

	off_pcie_rp = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pcie_rp_err_regs_t));

	off_pcie_adv_rp = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_pcie_adv_rp_err_regs_t));

	off_root_err_info = len;
	len = PCIE_RUP_ADD(len, sizeof (pf_root_err_info_t));

	pfd_p = (pf_data_t *)kmem_zalloc(len, KM_SLEEP);

	*pfd_pp = pfd_p;
	pfd_p->pe_len = len;

	bus_p = PCIE_DIP2DOWNBUS(dip);
	pfd_p->pe_bus_p = bus_p;
	pfd_p->pe_severity_flags = 0;
	pfd_p->pe_orig_severity_flags = 0;
	pfd_p->pe_lock = B_FALSE;
	pfd_p->pe_valid = B_FALSE;

	start = (uint8_t *)pfd_p;

	PFD_AFFECTED_DEV(pfd_p) = (pf_affected_dev_t *)(
	    (uintptr_t)(start + off_aff_dev));
	PFD_AFFECTED_DEV(pfd_p)->pe_affected_bdf = PCIE_INVALID_BDF;

	PCIE_ROOT_EH_SRC(pfd_p) = (pf_root_eh_src_t *)(
	    (uintptr_t)(start + off_root_eh_src));

	PFD_BLK_HDR(pfd_p) = (pf_blk_hdr_t *)(
	    (uintptr_t)(start + off_block_hdr));

	PF_OFFSET(pfd_p, PF_IDX_PCI_REG) = off_pci - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCI_REG) = PCIE_RUP(sizeof (pf_pci_err_regs_t));

	PF_OFFSET(pfd_p, PF_IDX_PCI_BDG_REG) = off_pci_bdg - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCI_BDG_REG) =
	    PCIE_RUP(sizeof (pf_pci_bdg_err_regs_t));

	PF_OFFSET(pfd_p, PF_IDX_PCIE_REG) = off_pcie - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCIE_REG) =
	    PCIE_RUP(sizeof (pf_pcie_err_regs_t));

	PF_OFFSET(pfd_p, PF_IDX_PCIE_AER_REG) = off_pcie_adv - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCIE_AER_REG) =
	    PCIE_RUP(sizeof (pf_pcie_adv_err_regs_t));
	PCIE_ADV_REG(pfd_p)->pcie_ue_tgt_bdf = PCIE_INVALID_BDF;
	PCIE_ADV_REG(pfd_p)->pcie_ue_sev = pcie_aer_uce_severity;

	PF_OFFSET(pfd_p, PF_IDX_PCIE_RP_REG) = off_pcie_rp - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCIE_RP_REG) =
	    PCIE_RUP(sizeof (pf_pcie_rp_err_regs_t));

	PF_OFFSET(pfd_p, PF_IDX_PCIE_RP_AER_REG) =
	    off_pcie_adv_rp - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_PCIE_RP_AER_REG) =
	    PCIE_RUP(sizeof (pf_pcie_adv_rp_err_regs_t));
	PCIE_ADV_RP_REG(pfd_p)->pcie_rp_ce_src_id = PCIE_INVALID_BDF;
	PCIE_ADV_RP_REG(pfd_p)->pcie_rp_ue_src_id = PCIE_INVALID_BDF;

	PF_OFFSET(pfd_p, PF_IDX_ROOT_ERR_INFO) =
	    off_root_err_info - off_block_hdr;
	PF_LEN(pfd_p, PF_IDX_ROOT_ERR_INFO) =
	    PCIE_RUP(sizeof (pf_root_err_info_t));
	PFD_ROOT_ERR_INFO(pfd_p)->scan_bdf = PCIE_INVALID_BDF;
}

void
pcie_rc_fini_pfd(pf_data_t *pfd_p)
{
	size_t		len = pfd_p->pe_len;

	kmem_free(pfd_p, len);
}

/*
 * init pcie_bus_t for root complex
 *
 * This routine is invoked during boot, before enumerate PCI devices under
 * the root complex(x86 case) or during px driver attach (sparc case);
 * Only a few of the fields in bus_t is valid for root complex.
 *
 * The fields that are bracketed are initialized if flag PCIE_BUS_INITIAL
 * is set:
 *
 * dev_info_t *		<bus_dip>
 * dev_info_t *		bus_rp_dip
 * ddi_acc_handle_t	bus_cfg_hdl
 * uint_t		<bus_fm_flags>
 * pcie_req_id_t	bus_bdf
 * pcie_req_id_t	bus_rp_bdf
 * uint32_t		bus_dev_ven_id
 * uint8_t		bus_rev_id
 * uint8_t		<bus_hdr_type>
 * uint16_t		<bus_dev_type>
 * uint8_t		bus_bdg_secbus
 * uint16_t		bus_pcie_off
 * uint16_t		<bus_aer_off>
 * uint16_t		bus_pcix_off
 * uint16_t		bus_ecc_ver
 * pci_bus_range_t	bus_bus_range
 * ppb_ranges_t	*	bus_addr_ranges
 * int			bus_addr_entries
 * pci_regspec_t *	bus_assigned_addr
 * int			bus_assigned_entries
 * pciv_tx_taskq_t *	taskq
 * pf_data_t *		bus_pfd
 * pf_impl_t *		<bus_root_pf_impl>
 * pcie_domain_t *	<bus_dom>
 * pcie_link_t *	<bus_link_hdl>
 * int			bus_mps
 * uint64_t		bus_cfgacc_base
 * void	*		bus_plat_private
 *
 * The fields that are bracketed are initialized if flag PCIE_BUS_FINAL
 * is set:
 *
 * dev_info_t *		bus_dip
 * dev_info_t *		bus_rp_dip
 * ddi_acc_handle_t	bus_cfg_hdl
 * uint_t		bus_fm_flags
 * pcie_req_id_t	bus_bdf
 * pcie_req_id_t	bus_rp_bdf
 * uint32_t		bus_dev_ven_id
 * uint8_t		bus_rev_id
 * uint8_t		bus_hdr_type
 * uint16_t		bus_dev_type
 * uint8_t		bus_bdg_secbus
 * uint16_t		bus_pcie_off
 * uint16_t		bus_aer_off
 * uint16_t		bus_pcix_off
 * uint16_t		bus_ecc_ver
 * pci_bus_range_t	<bus_bus_range>
 * ppb_ranges_t	*	bus_addr_ranges
 * int			bus_addr_entries
 * pci_regspec_t *	bus_assigned_addr
 * int			bus_assigned_entries
 * pciv_tx_taskq_t *	<taskq>
 * boolean_t		<virtual_fabric>
 * pf_data_t *		bus_pfd
 * pf_impl_t *		bus_root_pf_impl
 * pcie_domain_t *	bus_dom
 * pcie_link_t *	bus_link_hdl
 * int			bus_mps
 * uint64_t		bus_cfgacc_base
 * void	*		bus_plat_private
 */

void
pcie_rc_init_bus(dev_info_t *dip, uint8_t flags)
{
	pcie_bus_t *bus_p;
	pcie_domain_t *dom_p;
	pcie_link_t	*link_p;
	char name[32];
	int range_size;
	const char *errstr = NULL;

	if (!(flags & PCIE_BUS_INITIAL))
		goto initial_done;

	bus_p = (pcie_bus_t *)kmem_zalloc(sizeof (pcie_bus_t), KM_SLEEP);
	bus_p->bus_dip = dip;
	bus_p->bus_rc_dip = dip;
	bus_p->bus_dev_type = PCIE_PCIECAP_DEV_TYPE_RC_PSEUDO;
	bus_p->bus_hdr_type = PCI_HEADER_ONE;
	bus_p->bus_cfgacc_base = INVALID_CFGACC_BASE;

	/* Fake that there are AER logs */
	bus_p->bus_aer_off = (uint16_t)-1;

	/* Needed only for handle lookup */
	bus_p->bus_fm_flags |= PF_FM_READY;

	bus_p->bus_rc_rbl = kmem_zalloc(sizeof (pcirm_rc_rbl_t), KM_SLEEP);
	bus_p->bus_rbl = kmem_zalloc(sizeof (pcirm_rbl_t), KM_SLEEP);
	ndi_set_bus_private(dip, B_FALSE, DEVI_PORT_TYPE_PCI, bus_p);

	bus_p->bus_root_pf_impl = PCIE_ZALLOC(pf_impl_t);

	dom_p = PCIE_ZALLOC(pcie_domain_t);
	PCIE_BUS2DOM(bus_p) = dom_p;
	(void) sprintf(name, "%s%d domain list hash",
	    ddi_driver_name(dip), ddi_get_instance(dip));
	dom_p->dom_hashp = PCIE_ZALLOC(pcie_dom_hash_t);
	pciev_domain_list_init(dom_p->dom_hashp, name);

	link_p = kmem_zalloc(sizeof (pcie_link_t), KM_SLEEP);
	bus_p->bus_link_hdl = link_p;
	mutex_init(&link_p->link_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&link_p->link_cv, NULL, CV_DRIVER, NULL);
	link_p->link_flags = PCIE_LINK_UP;

initial_done:
	if (!(flags & PCIE_BUS_FINAL))
		return;

	bus_p = PCIE_DIP2BUS(dip);
	ASSERT(bus_p != NULL);

	/* get "bus_range" property */
	range_size = sizeof (pci_bus_range_t);
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&bus_p->bus_bus_range, &range_size)
	    != DDI_PROP_SUCCESS) {
		errstr = "Cannot find \"bus-range\" property";
		cmn_err(CE_WARN,
		    "PCIE init RC dip %p failed: %s\n",
		    (void *)dip, errstr);
	}

	/*
	 * If a "virtual-root-complex" property exists, the fabric is
	 * virtual. To be compatible with SDIO firmware, also need to
	 * check "pci-intx-not-supported".
	 */
	bus_p->virtual_fabric =
	    ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "virtual-root-complex") ||
	    ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "pci-intx-not-supported") ? B_TRUE : B_FALSE;

	/* Create per-RC taskq */
	if (pcie_rc_taskq_create(dip) != DDI_SUCCESS) {
		errstr = "Cannot create taskq thread";
		cmn_err(CE_WARN,
		    "PCIE init RC dip %p failed: %s\n",
		    (void *)dip, errstr);
		pcie_rc_taskq_destroy(dip);
	}
}

void
pcie_rc_fini_bus(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2DOWNBUS(dip);
	pcie_domain_t *dom_p = PCIE_BUS2DOM(bus_p);
	pcie_link_t	*link_p = bus_p->bus_link_hdl;

	pcie_rc_taskq_destroy(dip);

	ndi_set_bus_private(dip, B_FALSE, NULL, NULL);

	cv_destroy(&link_p->link_cv);
	mutex_destroy(&link_p->link_lock);

	kmem_free(link_p, sizeof (pcie_link_t));
	kmem_free(bus_p->bus_rbl, sizeof (pcirm_rbl_t));
	kmem_free(bus_p->bus_rc_rbl, sizeof (pcirm_rc_rbl_t));

	pciev_domain_list_fini(dom_p->dom_hashp);
	kmem_free(dom_p->dom_hashp, sizeof (pcie_dom_hash_t));
	kmem_free(dom_p, sizeof (pcie_domain_t));
	kmem_free(bus_p->bus_root_pf_impl, sizeof (pf_impl_t));
	kmem_free(bus_p, sizeof (pcie_bus_t));
}

/*
 * Initial property copies in pcie_bus_t.
 */
void
pcie_init_bus_props(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	dev_info_t *rcdip;
	char *errstr = NULL;
	int range_size;

	/* save the range information if device is a switch/bridge */
	if (PCIE_IS_BDG(bus_p)) {
		/* get "bus_range" property */
		range_size = sizeof (pci_bus_range_t);
		if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "bus-range", (caddr_t)&bus_p->bus_bus_range, &range_size)
		    != DDI_PROP_SUCCESS) {
			errstr = "cannot find \"bus-range\" property";
			cmn_err(CE_WARN,
			    "pcie init err info failed BDF 0x%x:%s\n",
			    bus_p->bus_bdf, errstr);
		}

		/* get secondary bus number */
		rcdip = pcie_get_rc_dip(dip);
		ASSERT(rcdip != NULL);

		bus_p->bus_bdg_secbus = pci_cfgacc_get8(rcdip,
		    bus_p->bus_bdf, PCI_BCNF_SECBUS);

		/* get "ranges" property */
		if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "ranges", (caddr_t)&bus_p->bus_addr_ranges,
		    &bus_p->bus_addr_entries) != DDI_PROP_SUCCESS)
			bus_p->bus_addr_entries = 0;
		bus_p->bus_addr_entries /= sizeof (ppb_ranges_t);
	}

	/* save "assigned-addresses" property array, ignore failues */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&bus_p->bus_assigned_addr,
	    &bus_p->bus_assigned_entries) == DDI_PROP_SUCCESS)
		bus_p->bus_assigned_entries /= sizeof (pci_regspec_t);
	else
		bus_p->bus_assigned_entries = 0;

	bus_p->bus_props_inited = B_TRUE;
}

void
pcie_fini_bus_props(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);

	kmem_free(bus_p->bus_assigned_addr,
	    (sizeof (pci_regspec_t) * bus_p->bus_assigned_entries));
	kmem_free(bus_p->bus_addr_ranges,
	    (sizeof (ppb_ranges_t) * bus_p->bus_addr_entries));

	/* zero out the fields that have been destroyed */
	bus_p->bus_assigned_addr = NULL;
	bus_p->bus_addr_ranges = NULL;
	bus_p->bus_assigned_entries = 0;
	bus_p->bus_addr_entries = 0;
	bus_p->bus_props_inited = B_FALSE;
}

/*
 * partially init pcie_bus_t for device (dip,bdf) for accessing pci
 * config space
 *
 * This routine is invoked during boot, either after creating a devinfo node
 * (x86 case) or during px driver attach (sparc case); it is also invoked
 * in hotplug context after a devinfo node is created.
 *
 * The fields that are bracketed are initialized if flag PCIE_BUS_INITIAL
 * is set:
 *
 * dev_info_t *		<bus_dip>
 * dev_info_t *		<bus_rp_dip>
 * ddi_acc_handle_t	bus_cfg_hdl
 * uint_t		bus_fm_flags
 * pcie_req_id_t	<bus_bdf>
 * pcie_req_id_t	<bus_rp_bdf>
 * uint32_t		<bus_dev_ven_id>
 * uint8_t		<bus_rev_id>
 * uint8_t		<bus_hdr_type>
 * uint16_t		<bus_dev_type>
 * uint8_t		<bus_bdg_secbus
 * int			bus_pcie_ver
 * uint16_t		<bus_pcie_off>
 * uint16_t		<bus_aer_off>
 * uint16_t		<bus_ari_off>
 * uint16_t		<bus_acs_off>
 * uint16_t		<bus_ext2_off>
 * uint16_t		<bus_pcix_off>
 * uint16_t		<bus_ecc_ver>
 * pci_bus_range_t	bus_bus_range
 * ppb_ranges_t	*	bus_addr_ranges
 * int			bus_addr_entries
 * pci_regspec_t *	bus_assigned_addr
 * int			bus_assigned_entries
 * pciv_tx_taskq_t *	taskq
 * pf_data_t *		bus_pfd
 * pf_impl_t *		bus_root_pf_impl
 * pcie_domain_t *	bus_dom
 * int			bus_mps
 * uint64_t		bus_cfgacc_base
 * pcie_linkbw		bus_linkbw
 * void	*		bus_plat_private
 *
 * The fields that are bracketed are initialized if flag PCIE_BUS_FINAL
 * is set:
 *
 * dev_info_t *		bus_dip
 * dev_info_t *		bus_rp_dip
 * ddi_acc_handle_t	bus_cfg_hdl
 * uint_t		bus_fm_flags
 * pcie_req_id_t	bus_bdf
 * pcie_req_id_t	bus_rp_bdf
 * uint32_t		bus_dev_ven_id
 * uint8_t		bus_rev_id
 * uint8_t		bus_hdr_type
 * uint16_t		bus_dev_type
 * uint8_t		<bus_bdg_secbus>
 * int			<bus_pcie_ver>
 * uint16_t		bus_pcie_off
 * uint16_t		bus_aer_off
 * uint16_t		bus_ari_off
 * uint16_t		bus_acs_off
 * uint16_t		bus_ext2_off
 * uint16_t		bus_pcix_off
 * uint16_t		bus_ecc_ver
 * pci_bus_range_t	<bus_bus_range>
 * ppb_ranges_t	*	<bus_addr_ranges>
 * int			<bus_addr_entries>
 * pci_regspec_t *	<bus_assigned_addr>
 * int			<bus_assigned_entries>
 * pciv_tx_taskq_t *	taskq
 * pf_data_t *		<bus_pfd>
 * pf_impl_t *		<bus_root_pf_impl>
 * int			<bus_mps>
 * pcie_domain_t *	<bus_dom>
 * uint64_t		bus_cfgacc_base
 * pcie_linkbw		<bus_linkbw>
 * void	*		<bus_plat_private>
 */

pcie_bus_t *
pcie_init_bus(dev_info_t *dip, pcie_req_id_t bdf, uint8_t flags)
{
	uint16_t	status, base;
	pcie_bus_t	*bus_p;
	dev_info_t	*rcdip;
	dev_info_t	*pdip;
#if defined(__sparc)
	uint32_t	devid, venid;
#endif
	uint16_t	sriov_cap_ptr;
	uint16_t	value;

#ifdef DEBUG
	char *path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) pcie_pathname(dip, path);
#endif
	if (!(flags & PCIE_BUS_INITIAL)) {
		bus_p = PCIE_DIP2BUS(dip);
		goto initial_done;
	}

	bus_p = kmem_zalloc(sizeof (pcie_bus_t), KM_SLEEP);

	ndi_set_bus_private(dip, B_TRUE, DEVI_PORT_TYPE_PCI, (void *)bus_p);

	bus_p->bus_dip = dip;
	bus_p->bus_bdf = bdf;
	bus_p->bus_initial_bus = bdf >> 8;

	/* Save RC dip */
	if (PCIE_IS_RC(bus_p)) {
		/* shouldn't happen, but just in case */
		bus_p->bus_rc_dip = dip;
	} else {
		for (pdip = ddi_get_parent(dip); pdip;
		    pdip = ddi_get_parent(pdip)) {
			if (PCIE_IS_RC(PCIE_DIP2BUS(pdip))) {
				bus_p->bus_rc_dip = pdip;
				break;
			}
		}
	}

	rcdip = bus_p->bus_rc_dip;
	ASSERT(rcdip != NULL);

#if defined(__sparc)
	devid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);
	venid = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);
	bus_p->bus_dev_ven_id = (devid << 16) | (venid & 0xffff);
	bus_p->bus_rev_id = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "revision-id", -1);
	/*
	 * Hotplugged devices might not have their "device-id", "vendor-id"
	 * .etc properties populated when pcie_init_bus() is called, thus
	 * config access is needed to fetch these.
	 */
	if (bus_p->bus_dev_ven_id == (uint32_t)-1)
		bus_p->bus_dev_ven_id = pci_cfgacc_get32(rcdip,
		    bdf, PCI_CONF_VENID);
	if (bus_p->bus_rev_id == (uint8_t)-1)
		bus_p->bus_rev_id = pci_cfgacc_get8(rcdip,
		    bdf, PCI_CONF_REVID);

#else
	bus_p->bus_dev_ven_id = pci_cfgacc_get32(rcdip, bdf, PCI_CONF_VENID);
	bus_p->bus_rev_id = pci_cfgacc_get8(rcdip, bdf, PCI_CONF_REVID);
#endif /* __sparc */

	/* Save the Header Type */
	bus_p->bus_hdr_type = pci_cfgacc_get8(rcdip, bdf, PCI_CONF_HEADER);
	bus_p->bus_hdr_type &= PCI_HEADER_TYPE_M;

	/*
	 * Figure out the device type and all the relevant capability offsets
	 */
	/* set default value */
	bus_p->bus_dev_type = PCIE_PCIECAP_DEV_TYPE_PCI_PSEUDO;
	bus_p->num_vf = 0;
	bus_p->bus_ari_off = 0;
	bus_p->bus_acs_off = 0;
	bus_p->bus_ext2_off = 0;

	status = pci_cfgacc_get16(rcdip, bdf, PCI_CONF_STAT);
	if (status == PCI_CAP_EINVAL16 || !(status & PCI_STAT_CAP))
		goto caps_done; /* capability not supported */

	/* Relevant conventional capabilities first */

	/* Conventional caps: PCI_CAP_ID_PCI_E, PCI_CAP_ID_PCIX */
	if (pcie_cap_locate(dip, PCI_CAP_ID_PCI_E, &base) == DDI_SUCCESS) {
		bus_p->bus_pcie_off = base;
		bus_p->bus_dev_type = pci_cfgacc_get16(rcdip, bdf,
		    base + PCIE_PCIECAP) & PCIE_PCIECAP_DEV_TYPE_MASK;

		/* Check and save PCIe hotplug capability information */
		if ((PCIE_IS_RP(bus_p) || PCIE_IS_SWD(bus_p)) &&
		    (pci_cfgacc_get16(rcdip, bdf, base + PCIE_PCIECAP)
		    & PCIE_PCIECAP_SLOT_IMPL) &&
		    (pci_cfgacc_get32(rcdip, bdf, base + PCIE_SLOTCAP)
		    & PCIE_SLOTCAP_HP_CAPABLE))
			bus_p->bus_hp_sup_modes |= PCIE_NATIVE_HP_MODE;
	}

	if (pcie_cap_locate(dip, PCI_CAP_ID_PCIX, &base) == DDI_SUCCESS) {
		bus_p->bus_pcix_off = base;
		if (PCIE_IS_BDG(bus_p))
			bus_p->bus_ecc_ver =
			    pci_cfgacc_get16(rcdip, bdf, base +
			    PCI_PCIX_SEC_STATUS) & PCI_PCIX_VER_MASK;
		else
			bus_p->bus_ecc_ver =
			    pci_cfgacc_get16(rcdip, bdf, base +
			    PCI_PCIX_COMMAND) & PCI_PCIX_VER_MASK;
	}

	if (pcie_cap_locate(dip, PCI_CAP_ID_MSI, &base) == DDI_SUCCESS)
		bus_p->bus_msi_off = base;

	/* Check and save PCI hotplug (SHPC) capability information */
	if (PCIE_IS_BDG(bus_p)) {
		if (pcie_cap_locate(dip, PCI_CAP_ID_PCI_HOTPLUG,
		    &base) == DDI_SUCCESS) {
			bus_p->bus_pci_hp_off = base;
			bus_p->bus_hp_sup_modes |= PCIE_PCI_HP_MODE;
		}
	}

	/* Then, relevant extended capabilities */

	if (!PCIE_IS_PCIE(bus_p))
		goto caps_done;

	/* Extended caps: PCIE_EXT_CAP_ID_AER, PCIE_EXT_CAP_ID_SRIOV */
	if (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_AER),
	    &base) == DDI_SUCCESS)
		bus_p->bus_aer_off = base;

	/* Extended caps: PCIE_EXT_CAP_ID_ARI */
	if (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_ARI),
	    &base) == DDI_SUCCESS)
		bus_p->bus_ari_off = base;

	/* Extended caps: PCIE_EXT_CAP_ID_ACS */
	if (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_ACS),
	    &base) == DDI_SUCCESS)
		bus_p->bus_acs_off = base;

	/* Extended caps: PCIE_EXT_CAP_ID_EXT2 */
	if (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_EXT2),
	    &base) == DDI_SUCCESS)
		bus_p->bus_ext2_off = base;

caps_done:
	if ((bus_p->bus_dev_type == PCIE_PCIECAP_DEV_TYPE_PCIE_DEV) &&
	    (pcie_is_vf(dip)))
		bus_p->bus_func_type = FUNC_TYPE_VF;
	/* save RP dip and RP bdf */
	if (PCIE_IS_RP(bus_p)) {
		bus_p->bus_rp_dip = dip;
		bus_p->bus_rp_bdf = bus_p->bus_bdf;
	} else {
		for (pdip = ddi_get_parent(dip); pdip;
		    pdip = ddi_get_parent(pdip)) {
			pcie_bus_t *parent_bus_p = PCIE_DIP2BUS(pdip);

			/*
			 * If RP dip and RP bdf in parent's bus_t have
			 * been initialized, simply use these instead of
			 * continuing up to the RC.
			 */
			if (parent_bus_p->bus_rp_dip != NULL) {
				bus_p->bus_rp_dip = parent_bus_p->bus_rp_dip;
				bus_p->bus_rp_bdf = parent_bus_p->bus_rp_bdf;
				break;
			}

			/*
			 * When debugging be aware that some NVIDIA x86
			 * architectures have 2 nodes for each RP, One at Bus
			 * 0x0 and one at Bus 0x80.  The requester is from Bus
			 * 0x80
			 */
			if (PCIE_IS_ROOT(parent_bus_p)) {
				bus_p->bus_rp_dip = pdip;
				bus_p->bus_rp_bdf = parent_bus_p->bus_bdf;
				break;
			}
		}
	}

	/*
	 * Initializing bus_mps as -1 allows the OS to set appropriate MPS
	 * for the device or whole fabric.
	 * Setting it to any other value may result in incorrect MPS value
	 * for the device or fabric.
	 */

	bus_p->bus_mps = -1;
	bus_p->bus_pcie_ver = 0;
	bus_p->bus_rbl = kmem_zalloc(sizeof (pcirm_rbl_t), KM_SLEEP);

	if (PCIE_IS_HOTPLUG_CAPABLE(dip))
		(void) ndi_prop_create_boolean(DDI_DEV_T_NONE, dip,
		    "hotplug-capable");
	if ((bus_p->bus_pcie_off == NULL) ||
	    (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_SRIOV),
	    &sriov_cap_ptr) != DDI_SUCCESS))
		goto initial_done;
	/*
	 * drops here if SR-IOV capable device is found.
	 */
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET));
	bus_p->initial_num_vf = value;
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET));
	bus_p->total_num_vf = value;
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET));
	bus_p->first_vf_offset = value;
	value = pci_cfgacc_get16(rcdip, bdf,
	    (sriov_cap_ptr + PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET));
	bus_p->vf_stride = value;

#ifdef DEBUG
	if (pcie_debug)
		cmn_err(CE_NOTE,
		    "SRIOV capable device found. path is %s initial #vf = %d\n",
		    path, value);
#endif
	bus_p->bus_func_type = FUNC_TYPE_PF;
	bus_p->sriov_cap_ptr = sriov_cap_ptr;
initial_done:
	if (!(flags & PCIE_BUS_FINAL))
		goto final_done;

	apply_pciconf_configs(dip);
	pcie_init_bus_props(dip);

	if (PCIE_IS_ROOT(bus_p))
		bus_p->bus_root_pf_impl = PCIE_ZALLOC(pf_impl_t);

	pcie_init_pfd(dip);

	pcie_init_plat(dip);

	pcie_init_dom(dip);

	pcie_init_regs(dip);

final_done:

	PCIE_DBG("Add %s(dip 0x%p, bdf 0x%x, secbus 0x%x)\n",
	    ddi_driver_name(dip), (void *)dip, bus_p->bus_bdf,
	    bus_p->bus_bdg_secbus);
#ifdef DEBUG
	pcie_print_bus(bus_p);
	if (path)
		kmem_free(path, MAXPATHLEN);
#endif
	return (bus_p);
}

/*
 * Invoked before destroying devinfo node, mostly during hotplug
 * operation to free pcie_bus_t data structure
 */
/* ARGSUSED */
void
pcie_fini_bus(dev_info_t *dip, uint8_t flags)
{
	pcie_bus_t *bus_p = PCIE_DIP2UPBUS(dip);
	ASSERT(bus_p);

	if (flags & PCIE_BUS_INITIAL) {
		pcie_hp_fini_state_priv(dip);
		pcie_fini_dom(dip);
		pcie_fini_plat(dip);
		pcie_fini_pfd(dip);
		if (bus_p->bus_root_pf_impl)
			kmem_free(bus_p->bus_root_pf_impl, sizeof (pf_impl_t));

		pcie_fini_bus_props(dip);
	}

	if (flags & PCIE_BUS_FINAL) {
		if (PCIE_IS_HOTPLUG_CAPABLE(dip)) {
			(void) ndi_prop_remove(DDI_DEV_T_NONE, dip,
			    "hotplug-capable");
		}

		ndi_set_bus_private(dip, B_TRUE, NULL, NULL);
		kmem_free(bus_p->bus_rbl, sizeof (pcirm_rbl_t));
		kmem_free(bus_p, sizeof (pcie_bus_t));
	}
}

int
pcie_preattach_child(dev_info_t *cdip, struct attachspec *asp)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(cdip);
	dev_info_t	*pf_dip;
	int		retries;

	if (!bus_p)
		return (DDI_FAILURE);
	pf_dip = bus_p->bus_pf_dip;
	switch (asp->cmd) {
	case DDI_ATTACH:

		if ((bus_p->bus_func_type != FUNC_TYPE_VF) || (pf_dip == NULL))
			break;
		/*
		 * VF device detected.
		 * wait here for a reasonable period(100 ms)
		 * for class config to be completed.
		 */
		bus_p = PCIE_DIP2BUS(pf_dip);
		retries = MAX_RETRIES_FOR_CLASS_CONFIG_COMPLETION;
		while (!bus_p->iovcfg_class_config_completed && retries--) {
			delay(DELAY_FOR_CLASS_CONFIG_COMPLETION);
		}
		if (bus_p->iovcfg_class_config_completed)
			return (DDI_SUCCESS);
		else
			return (DDI_FAILURE);
	case DDI_RESUME:
	default:
		break;
	}
	return (DDI_SUCCESS);
}

int
pcie_postattach_child(dev_info_t *cdip, struct attachspec *asp)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(cdip);
	char		*path = NULL;
	ddi_cb_t	*cbp;
	int		ret = DDI_SUCCESS;

	if (!bus_p)
		return (DDI_FAILURE);
	if (asp->result != DDI_SUCCESS)
		/*
		 * ATTACH/RESUME failed so we have nothing to do here
		 * We return success indicating the result of
		 * postattach operation
		 */
		return (DDI_SUCCESS);
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) ddi_pathname(cdip, path);
	switch (asp->cmd) {

	case DDI_RESUME:

		if ((bus_p->bus_func_type == FUNC_TYPE_PF) &&
		    (bus_p->num_vf > 0) &&
		    (bus_p->bus_vf_bar_ptr)) {
			/*
			 * SRIOV device with VFs configured.
			 * resume the VFs
			 */
			bus_p->iov_dev_state &= (~IOV_DEV_SUSPENDED);
#ifdef DEBUG
			if (pcie_debug)
				cmn_err(CE_NOTE,
				    "resuming PF device %s\n", path);
#endif
			/*
			 * resume the VF devices
			 */
			ret = pcie_resume_vf_devices(cdip);
		}
		break;

	case DDI_ATTACH:
		/*
		 * check if we have SRIOV capable driver.
		 */
		cbp = DEVI(cdip)->devi_cb_p;
		if (cbp && cbp->cb_flags & DDI_CB_FLAG_SRIOV)
			bus_p->has_iov_capable_driver = B_TRUE;
		if (bus_p->bus_func_type == FUNC_TYPE_PF)
			bus_p->iov_dev_state |= IOV_DEV_ATTACHED;
		if ((bus_p->bus_func_type != FUNC_TYPE_PF) ||
		    (bus_p->num_vf <= 0))
			/*
			 * Not a SRIOV device
			 * OR
			 * a SRIOV device with no VFs configured.
			 */
			break;
		if (bus_p->has_iov_capable_driver &&
		    (bus_p->bus_vf_bar_ptr == NULL)) {
			/*
			 * SRIOV device that does not have VFs
			 * configured after attach.
			 * The framework will try to configure VFs
			 */
#ifdef DEBUG
			if (pcie_debug)
				cmn_err(CE_NOTE,
			    "configuring VFs for PF device %s..\n", path);
#endif
			ret = pcicfg_config_vf(cdip);
			if (ret != DDI_SUCCESS) {
				cmn_err(CE_WARN,
			    "Failed to configure VFs for the device %s\n",
				    path);
				break;
			}

		}

		/* Scan the assigned VF before VF drivers attach */

		pcie_scan_assigned_devs(cdip);

		/*
		 * The call below will initiate the process of class
		 * configuration for the PF device.
		 * A return code of DDI_EPENDING indictes that the call
		 * has not completed and the  caller has to wait for
		 * iovcfg module calling pcie_class_config_completed()
		 *
		 * An example is the setup MAC addresses for the VFs
		 * The completion of class configraion is indicated by
		 * iovcfg module calling pcie_class_config_completed()
		 * The HV is sent a READY only after the class
		 * configuration is completed.
		 */
		ret = iovcfg_configure_pf_class(path);
		if (ret != DDI_EPENDING)
			pcie_class_config_completed(path);
		break;
	default:
		break;
	}	/* end of switch */
	if (path)
		kmem_free(path, MAXPATHLEN);
	return (pcie_enable_ce(cdip));
}

int
pcie_predetach_child(dev_info_t *cdip, struct detachspec *dsp)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(cdip);
#ifdef DEBUG
	char		*path;
#endif
	int		ret = DDI_SUCCESS;

	if (!bus_p)
		return (DDI_FAILURE);

	/*
	 * if device is not a SRIOV PF function
	 * OR
	 * a SRIOV device with no VFs configured
	 * simply return.
	 */
	if ((bus_p->bus_func_type != FUNC_TYPE_PF) || (bus_p->num_vf <= 0) ||
	    (bus_p->has_iov_capable_driver == B_FALSE) ||
	    (bus_p->bus_vf_bar_ptr == NULL))
		return (DDI_SUCCESS);
	/*
	 * Found a PF device with configured VF devices.
	 */

	/*
	 * Fail the detach if class config is in progress.
	 */
	if (!bus_p->iovcfg_class_config_completed)
		return (DDI_FAILURE);

	switch (dsp->cmd) {
	case DDI_DETACH:
	case DDI_HOTPLUG_DETACH:
#ifdef DEBUG
		if (pcie_debug) {
			path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) ddi_pathname(cdip, path);
			cmn_err(CE_NOTE,
			    "unconfiguring VFs for PF device %s..\n",
			    path);
			kmem_free(path, MAXPATHLEN);
		}
#endif
		ret = pcicfg_unconfig_vf(cdip);
		return (ret);
	case DDI_SUSPEND:
#ifdef DEBUG
		if (pcie_debug) {
			path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) ddi_pathname(cdip, path);
			cmn_err(CE_NOTE,
			    "PRE suspending of PF device %s...\n",
			    path);
			kmem_free(path, MAXPATHLEN);
		}
#endif
		/*
		 * suspend the VF devices
		 */
		ret = pcie_suspend_vf_devices(cdip);
		return (ret);
	default:
		return (DDI_SUCCESS);
	}	/* end of switch */
}

int
pcie_postdetach_child(dev_info_t *cdip, struct detachspec *dsp)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(cdip);

	if ((bus_p->bus_func_type != FUNC_TYPE_PF) || (bus_p->num_vf <= 0) ||
	    (bus_p->has_iov_capable_driver == B_FALSE) ||
	    (bus_p->bus_vf_bar_ptr == NULL))
		return (DDI_SUCCESS);

	/*
	 * Found a PF device with configured VF devices.
	 */
	switch (dsp->cmd) {
	case DDI_SUSPEND:
		if (dsp->result == DDI_SUCCESS) {
			bus_p->iov_dev_state |= IOV_DEV_SUSPENDED;
			break;
		}
		/*
		 * SUSPEND of PF failed.
		 * resume the suspended VF devices.
		 */
#ifdef DEBUG
		char	*path;

		if (pcie_debug) {
			path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) ddi_pathname(cdip, path);
			cmn_err(CE_NOTE,
"suspend of PF device %s failed. resuming VF devices suspended...\n",
			    path);
			kmem_free(path, MAXPATHLEN);
		}
#endif
		return (pcie_resume_vf_devices(cdip));
	case DDI_DETACH:
		if (dsp->result == DDI_SUCCESS)
			bus_p->iov_dev_state &= (~IOV_DEV_ATTACHED);
		break;

	default:
		break;
	}	/* end of switch */
	return (DDI_SUCCESS);
}

/*
 * PCI-Express child device de-initialization.
 * This function disables generic pci-express interrupts and error
 * handling.
 */
void
pcie_uninitchild(dev_info_t *cdip)
{
	pf_fini(cdip, DDI_DETACH);
	pcie_disable_errors(cdip);
	pcie_fini_cfghdl(cdip);
}

/*
 * Find the root complex dip
 */
dev_info_t *
pcie_get_rc_dip(dev_info_t *dip)
{
	dev_info_t *rcdip = PCIE_GET_RC_DIP(PCIE_DIP2BUS(dip));
	pcie_bus_t *bus_p;

	/* Check RC dip cache in requester's bus_p */
	if (rcdip != NULL)
		return (rcdip);

	/* Check parent dip */
	for (rcdip = dip; rcdip; rcdip = ddi_get_parent(rcdip)) {
		bus_p = PCIE_DIP2BUS(rcdip);
		if (bus_p && PCIE_IS_RC(bus_p))
			break;
	}

	return (rcdip);
}

boolean_t
pcie_is_pci_nexus(dev_info_t *dip)
{
	char		*device_type;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device_type", &device_type) != DDI_PROP_SUCCESS)
		return (B_FALSE);

	if (strcmp(device_type, "pciex") != 0 &&
	    strcmp(device_type, "pci") != 0) {
		ddi_prop_free(device_type);
		return (B_FALSE);
	}

	ddi_prop_free(device_type);
	return (B_TRUE);
}

boolean_t
pcie_is_pci_device(dev_info_t *dip)
{
	dev_info_t	*pdip;

	pdip = ddi_get_parent(dip);
	ASSERT(pdip);

	return (pcie_is_pci_nexus(pdip));
}

typedef struct {
	boolean_t	init;
	uint8_t		flags;
} pcie_bus_arg_t;

/*ARGSUSED*/
static int
pcie_fab_do_init_fini(dev_info_t *dip, void *arg)
{
	pcie_req_id_t	bdf;
	pcie_bus_arg_t	*bus_arg = (pcie_bus_arg_t *)arg;

	/*
	 * Check if we're still in the PCIe fabric.
	 * Cannot use PCIE_IS_PCI_DEVICE() hare as the bus_t structure
	 * might not be created yet if we're doing an init with flag
	 * PCIE_BUS_INITIAL.
	 */
	if (!pcie_is_pci_device(dip))
		goto out;

	if (bus_arg->init) {
		if (pcie_get_bdf_from_dip(dip, &bdf) != DDI_SUCCESS)
			goto out;

		(void) pcie_init_bus(dip, bdf, bus_arg->flags);
	} else {
		(void) pcie_fini_bus(dip, bus_arg->flags);
	}

	return (DDI_WALK_CONTINUE);

out:
	return (DDI_WALK_PRUNECHILD);
}

void
pcie_fab_init_bus(dev_info_t *rcdip, uint8_t flags)
{
	int		circular_count;
	dev_info_t	*dip = ddi_get_child(rcdip);
	pcie_bus_arg_t	arg;

	arg.init = B_TRUE;
	arg.flags = flags;

	ndi_devi_enter(rcdip, &circular_count);
	ddi_walk_devs(dip, pcie_fab_do_init_fini, &arg);
	ndi_devi_exit(rcdip, circular_count);
	(void) iovcfg_update_pflist();
}

void
pcie_fab_fini_bus(dev_info_t *rcdip, uint8_t flags)
{
	int		circular_count;
	dev_info_t	*dip = ddi_get_child(rcdip);
	pcie_bus_arg_t	arg;

	arg.init = B_FALSE;
	arg.flags = flags;

	ndi_devi_enter(rcdip, &circular_count);
	ddi_walk_devs(dip, pcie_fab_do_init_fini, &arg);
	ndi_devi_exit(rcdip, circular_count);
}

void
pcie_enable_errors(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	reg16, tmp16;
	uint32_t	reg32, tmp32;

	ASSERT(bus_p);

	/*
	 * Clear any pending errors
	 */
	pcie_clear_errors(dip);

	if (!PCIE_IS_PCIE(bus_p))
		return;

	/*
	 * VF errors are enabled through PF, not through itself
	 */
	if (PCIE_IS_VF(bus_p))
		return;

	/*
	 * Enable Baseline Error Handling but leave CE reporting off (poweron
	 * default).
	 */
	if ((reg16 = PCIE_CAP_GET(16, bus_p, PCIE_DEVCTL)) !=
	    PCI_CAP_EINVAL16) {
		tmp16 = (reg16 & (PCIE_DEVCTL_MAX_READ_REQ_MASK |
		    PCIE_DEVCTL_MAX_PAYLOAD_MASK)) |
		    (pcie_devctl_default & ~(PCIE_DEVCTL_MAX_READ_REQ_MASK |
		    PCIE_DEVCTL_MAX_PAYLOAD_MASK)) |
		    (pcie_base_err_default & (~PCIE_DEVCTL_CE_REPORTING_EN));

		PCIE_CAP_PUT(16, bus_p, PCIE_DEVCTL, tmp16);
		PCIE_DBG_CAP(dip, bus_p, "DEVCTL", 16, PCIE_DEVCTL, reg16);
	}

	/* Enable Root Port Baseline Error Receiving */
	if (PCIE_IS_ROOT(bus_p) &&
	    (reg16 = PCIE_CAP_GET(16, bus_p, PCIE_ROOTCTL)) !=
	    PCI_CAP_EINVAL16) {

		tmp16 = pcie_serr_disable_flag ?
		    (pcie_root_ctrl_default & ~PCIE_ROOT_SYS_ERR) :
		    pcie_root_ctrl_default;
		PCIE_CAP_PUT(16, bus_p, PCIE_ROOTCTL, tmp16);
		PCIE_DBG_CAP(dip, bus_p, "ROOT DEVCTL", 16, PCIE_ROOTCTL,
		    reg16);
	}

	/*
	 * Enable PCI-Express Advanced Error Handling if Exists
	 */
	if (!PCIE_HAS_AER(bus_p))
		return;

	/* Set Uncorrectable Severity */
	if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_UCE_SERV)) !=
	    PCI_CAP_EINVAL32) {
		tmp32 = pcie_aer_uce_severity;

		PCIE_AER_PUT(32, bus_p, PCIE_AER_UCE_SERV, tmp32);
		PCIE_DBG_AER(dip, bus_p, "AER UCE SEV", 32, PCIE_AER_UCE_SERV,
		    reg32);
	}

	/* Enable Uncorrectable errors */
	if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_UCE_MASK)) !=
	    PCI_CAP_EINVAL32) {
		tmp32 = pcie_aer_uce_mask;

		PCIE_AER_PUT(32, bus_p, PCIE_AER_UCE_MASK, tmp32);
		PCIE_DBG_AER(dip, bus_p, "AER UCE MASK", 32, PCIE_AER_UCE_MASK,
		    reg32);
	}

	/* Enable ECRC generation and checking */
	if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_CTL)) !=
	    PCI_CAP_EINVAL32) {
		tmp32 = reg32 | pcie_ecrc_value;
		PCIE_AER_PUT(32, bus_p, PCIE_AER_CTL, tmp32);
		PCIE_DBG_AER(dip, bus_p, "AER CTL", 32, PCIE_AER_CTL, reg32);
	}

	/* Enable Secondary Uncorrectable errors if this is a bridge */
	if (!PCIE_IS_PCIE_BDG(bus_p))
		goto root;

	/* Set Uncorrectable Severity */
	if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_SUCE_SERV)) !=
	    PCI_CAP_EINVAL32) {
		tmp32 = pcie_aer_suce_severity;

		PCIE_AER_PUT(32, bus_p, PCIE_AER_SUCE_SERV, tmp32);
		PCIE_DBG_AER(dip, bus_p, "AER SUCE SEV", 32, PCIE_AER_SUCE_SERV,
		    reg32);
	}

	if ((reg32 = PCIE_AER_GET(32, bus_p, PCIE_AER_SUCE_MASK)) !=
	    PCI_CAP_EINVAL32) {
		PCIE_AER_PUT(32, bus_p, PCIE_AER_SUCE_MASK, pcie_aer_suce_mask);
		PCIE_DBG_AER(dip, bus_p, "AER SUCE MASK", 32,
		    PCIE_AER_SUCE_MASK, reg32);
	}

root:
	/*
	 * Enable Root Control this is a Root device
	 */
	if (!PCIE_IS_ROOT(bus_p))
		return;

	if ((reg16 = PCIE_AER_GET(16, bus_p, PCIE_AER_RE_CMD)) !=
	    PCI_CAP_EINVAL16) {
		PCIE_AER_PUT(16, bus_p, PCIE_AER_RE_CMD,
		    pcie_root_error_cmd_default);
		PCIE_DBG_AER(dip, bus_p, "AER Root Err Cmd", 16,
		    PCIE_AER_RE_CMD, reg16);
	}
}

/*
 * This function is used for enabling CE reporting and setting the AER CE mask.
 * When called from outside the pcie module it should always be preceded by
 * a call to pcie_enable_errors.
 */
int
pcie_enable_ce(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	device_sts, device_ctl;
	uint32_t	tmp_pcie_aer_ce_mask;

	if (!PCIE_IS_PCIE(bus_p))
		return (DDI_SUCCESS);

	/*
	 * VF errors are enabled through PF, not through itself
	 */
	if (PCIE_IS_VF(bus_p))
		return (DDI_SUCCESS);

	/*
	 * The "pcie_ce_mask" property is used to control both the CE reporting
	 * enable field in the device control register and the AER CE mask. We
	 * leave CE reporting disabled if pcie_ce_mask is set to -1.
	 */

	tmp_pcie_aer_ce_mask = (uint32_t)ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "pcie_ce_mask", pcie_aer_ce_mask);

	if (tmp_pcie_aer_ce_mask == (uint32_t)-1) {
		/*
		 * Nothing to do since CE reporting has already been disabled.
		 */
		return (DDI_SUCCESS);
	}

	if (PCIE_HAS_AER(bus_p)) {
		/* Enable AER CE */
		PCIE_AER_PUT(32, bus_p, PCIE_AER_CE_MASK, tmp_pcie_aer_ce_mask);
		PCIE_DBG_AER(dip, bus_p, "AER CE MASK", 32, PCIE_AER_CE_MASK,
		    0);

		/* Clear any pending AER CE errors */
		PCIE_AER_PUT(32, bus_p, PCIE_AER_CE_STS, -1);
	}

	/* clear any pending CE errors */
	if ((device_sts = PCIE_CAP_GET(16, bus_p, PCIE_DEVSTS)) !=
	    PCI_CAP_EINVAL16)
		PCIE_CAP_PUT(16, bus_p, PCIE_DEVSTS,
		    device_sts & (~PCIE_DEVSTS_CE_DETECTED));

	/* Enable CE reporting */
	device_ctl = PCIE_CAP_GET(16, bus_p, PCIE_DEVCTL);
	PCIE_CAP_PUT(16, bus_p, PCIE_DEVCTL,
	    (device_ctl & (~PCIE_DEVCTL_ERR_MASK)) | pcie_base_err_default);
	PCIE_DBG_CAP(dip, bus_p, "DEVCTL", 16, PCIE_DEVCTL, device_ctl);

	return (DDI_SUCCESS);
}

/* ARGSUSED */
void
pcie_disable_errors(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	device_ctl;
	uint32_t	aer_reg;

	if (!PCIE_IS_PCIE(bus_p))
		return;

	/*
	 * VF errors are disabled through PF, not through itself
	 */
	if (PCIE_IS_VF(bus_p))
		return;

	/*
	 * Disable PCI-Express Baseline Error Handling
	 */
	device_ctl = PCIE_CAP_GET(16, bus_p, PCIE_DEVCTL);
	device_ctl &= ~PCIE_DEVCTL_ERR_MASK;
	PCIE_CAP_PUT(16, bus_p, PCIE_DEVCTL, device_ctl);

	/*
	 * Disable PCI-Express Advanced Error Handling if Exists
	 */
	if (!PCIE_HAS_AER(bus_p))
		goto root;

	/*
	 * Disable Uncorrectable errors
	 *
	 * Add a workaround to disable ACS Violation error, this code
	 * will be removed in ACS FMA project
	 */
	PCIE_AER_PUT(32, bus_p, PCIE_AER_UCE_MASK, PCIE_AER_UCE_BITS |
	    PCIE_AER_UCE_ACS);

	/* Disable Correctable errors */
	PCIE_AER_PUT(32, bus_p, PCIE_AER_CE_MASK, PCIE_AER_CE_BITS);

	/* Disable ECRC generation and checking */
	if ((aer_reg = PCIE_AER_GET(32, bus_p, PCIE_AER_CTL)) !=
	    PCI_CAP_EINVAL32) {
		aer_reg &= ~(PCIE_AER_CTL_ECRC_GEN_ENA |
		    PCIE_AER_CTL_ECRC_CHECK_ENA);

		PCIE_AER_PUT(32, bus_p, PCIE_AER_CTL, aer_reg);
	}
	/*
	 * Disable Secondary Uncorrectable errors if this is a bridge
	 */
	if (!PCIE_IS_PCIE_BDG(bus_p))
		goto root;

	PCIE_AER_PUT(32, bus_p, PCIE_AER_SUCE_MASK, PCIE_AER_SUCE_BITS);

root:
	/*
	 * disable Root Control this is a Root device
	 */
	if (!PCIE_IS_ROOT(bus_p))
		return;

	if (!pcie_serr_disable_flag) {
		device_ctl = PCIE_CAP_GET(16, bus_p, PCIE_ROOTCTL);
		device_ctl &= ~PCIE_ROOT_SYS_ERR;
		PCIE_CAP_PUT(16, bus_p, PCIE_ROOTCTL, device_ctl);
	}

	if (!PCIE_HAS_AER(bus_p))
		return;

	if ((device_ctl = PCIE_CAP_GET(16, bus_p, PCIE_AER_RE_CMD)) !=
	    PCI_CAP_EINVAL16) {
		device_ctl &= ~pcie_root_error_cmd_default;
		PCIE_CAP_PUT(16, bus_p, PCIE_AER_RE_CMD, device_ctl);
	}
}

/*
 * Extract bdf from bus_t or "reg" property.
 */
int
pcie_get_bdf_from_dip(dev_info_t *dip, pcie_req_id_t *bdf)
{
	pci_regspec_t	*regspec;
	int		reglen;
	dev_info_t	*rcdip;
	pcie_bus_t	*bus_p;

	ASSERT(dip != NULL);
	bus_p = PCIE_DIP2BUS(dip);
	if (bus_p) {
		*bdf = bus_p->bus_bdf;
		return (DDI_SUCCESS);
	}
	rcdip = pcie_get_rc_dip(dip);
	if (rcdip == dip)
		return (DDI_FAILURE);
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (int **)&regspec, (uint_t *)&reglen) != DDI_SUCCESS)
		return (DDI_FAILURE);

	if (reglen < (sizeof (pci_regspec_t) / sizeof (int))) {
		ddi_prop_free(regspec);
		return (DDI_FAILURE);
	}

	/* Get phys_hi from first element.  All have same bdf. */
	*bdf = (regspec->pci_phys_hi & (PCI_REG_BDFR_M ^ PCI_REG_REG_M)) >> 8;

	ddi_prop_free(regspec);
	return (DDI_SUCCESS);
}

dev_info_t *
pcie_get_my_childs_dip(dev_info_t *dip, dev_info_t *rdip)
{
	dev_info_t *cdip = rdip;

	for (; ddi_get_parent(cdip) != dip; cdip = ddi_get_parent(cdip))
		;

	return (cdip);
}

uint32_t
pcie_get_bdf_for_dma_xfer(dev_info_t *dip, dev_info_t *rdip)
{
	dev_info_t *cdip;

	/*
	 * As part of the probing, the PCI fcode interpreter may setup a DMA
	 * request if a given card has a fcode on it using dip and rdip of the
	 * hotplug connector i.e, dip and rdip of px/pcieb driver. In this
	 * case, return a invalid value for the bdf since we cannot get to the
	 * bdf value of the actual device which will be initiating this DMA.
	 */
	if (rdip == dip)
		return (PCIE_INVALID_BDF);

	cdip = pcie_get_my_childs_dip(dip, rdip);

	/*
	 * For a given rdip, return the bdf value of dip's (px or pcieb)
	 * immediate child or secondary bus-id if dip is a PCIe2PCI bridge.
	 *
	 * XXX - For now, return a invalid bdf value for all PCI and PCI-X
	 * devices since this needs more work.
	 */
	return (PCI_GET_PCIE2PCI_SECBUS(cdip) ?
	    PCIE_INVALID_BDF : PCI_GET_BDF(cdip));
}

uint32_t
pcie_get_aer_uce_mask() {
	return (pcie_aer_uce_mask);
}
uint32_t
pcie_get_aer_ce_mask() {
	return (pcie_aer_ce_mask);
}
uint32_t
pcie_get_aer_suce_mask() {
	return (pcie_aer_suce_mask);
}
uint32_t
pcie_get_serr_mask() {
	return (pcie_serr_disable_flag);
}

void
pcie_set_aer_uce_mask(uint32_t mask) {
	pcie_aer_uce_mask = mask;
	if (mask & PCIE_AER_UCE_UR)
		pcie_base_err_default &= ~PCIE_DEVCTL_UR_REPORTING_EN;
	else
		pcie_base_err_default |= PCIE_DEVCTL_UR_REPORTING_EN;

	if (mask & PCIE_AER_UCE_ECRC)
		pcie_ecrc_value = 0;
}

void
pcie_set_aer_ce_mask(uint32_t mask) {
	pcie_aer_ce_mask = mask;
}
void
pcie_set_aer_suce_mask(uint32_t mask) {
	pcie_aer_suce_mask = mask;
}
void
pcie_set_serr_mask(uint32_t mask) {
	pcie_serr_disable_flag = mask;
}

/*
 * Is the rdip a child of dip.	Used for checking certain CTLOPS from bubbling
 * up erronously.  Ex.	ISA ctlops to a PCI-PCI Bridge.
 */
boolean_t
pcie_is_child(dev_info_t *dip, dev_info_t *rdip)
{
	dev_info_t	*cdip = ddi_get_child(dip);
	for (; cdip; cdip = ddi_get_next_sibling(cdip))
		if (cdip == rdip)
			break;
	return (cdip != NULL);
}

boolean_t
pcie_is_link_disabled(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);

	if (PCIE_IS_PCIE(bus_p)) {
		if (PCIE_CAP_GET(16, bus_p, PCIE_LINKCTL) &
		    PCIE_LINKCTL_LINK_DISABLE)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Determines if there are any root ports attached to a root complex.
 *
 * dip - dip of root complex
 *
 * Returns - DDI_SUCCESS if there is at least one root port otherwise
 *	     DDI_FAILURE.
 */
int
pcie_root_port(dev_info_t *dip)
{
	int port_type;
	uint16_t cap_ptr;
	ddi_acc_handle_t config_handle;
	dev_info_t *cdip = ddi_get_child(dip);

	/*
	 * Determine if any of the children of the passed in dip
	 * are root ports.
	 */
	for (; cdip; cdip = ddi_get_next_sibling(cdip)) {

		if (pci_config_setup(cdip, &config_handle) != DDI_SUCCESS)
			continue;

		if ((PCI_CAP_LOCATE(config_handle, PCI_CAP_ID_PCI_E,
		    &cap_ptr)) == DDI_FAILURE) {
			pci_config_teardown(&config_handle);
			continue;
		}

		port_type = PCI_CAP_GET16(config_handle, NULL, cap_ptr,
		    PCIE_PCIECAP) & PCIE_PCIECAP_DEV_TYPE_MASK;

		pci_config_teardown(&config_handle);

		if (port_type == PCIE_PCIECAP_DEV_TYPE_ROOT)
			return (DDI_SUCCESS);
	}

	/* No root ports were found */

	return (DDI_FAILURE);
}

/*
 * Function that determines if a device a PCIe device.
 *
 * dip - dip of device.
 *
 * returns - DDI_SUCCESS if device is a PCIe device, otherwise DDI_FAILURE.
 */
int
pcie_dev(dev_info_t *dip)
{
	/* get parent device's device_type property */
	char *device_type;
	int rc = DDI_FAILURE;
	dev_info_t *pdip = ddi_get_parent(dip);

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, pdip,
	    DDI_PROP_DONTPASS, "device_type", &device_type)
	    != DDI_PROP_SUCCESS) {
		return (DDI_FAILURE);
	}

	if (strcmp(device_type, "pciex") == 0)
		rc = DDI_SUCCESS;
	else
		rc = DDI_FAILURE;

	ddi_prop_free(device_type);
	return (rc);
}

void
pcie_set_rber_fatal(dev_info_t *dip, boolean_t val)
{
	pcie_bus_t *bus_p = PCIE_DIP2UPBUS(dip);
	bus_p->bus_pfd->pe_rber_fatal = val;
}

/*
 * Return parent Root Port's pe_rber_fatal value.
 */
boolean_t
pcie_get_rber_fatal(dev_info_t *dip)
{
	pcie_bus_t *bus_p = PCIE_DIP2UPBUS(dip);
	pcie_bus_t *rp_bus_p = PCIE_DIP2UPBUS(bus_p->bus_rp_dip);
	return (rp_bus_p->bus_pfd->pe_rber_fatal);
}

int
pcie_ari_supported(dev_info_t *dip)
{
	uint32_t devcap2;
	uint16_t pciecap;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	uint8_t dev_type;
	dev_info_t	*rcdip;
	pcie_req_id_t   bdf;
	uint16_t	pcie_off;
	int		rval;

	PCIE_DBG("pcie_ari_supported: dip=%p\n", dip);
	if (bus_p && bus_p->bus_cfg_hdl) {
		dev_type = bus_p->bus_dev_type;
	} else {
		rval = pcie_get_bus_dev_type(dip, &pcie_off);
		if (rval == DDI_FAILURE)
			return (PCIE_ARI_FORW_NOT_SUPPORTED);
		dev_type = (uint8_t)rval;
		if (pcie_get_bdf_from_dip(dip, &bdf) == DDI_FAILURE)
			return (PCIE_ARI_FORW_NOT_SUPPORTED);
	}
	if ((dev_type != PCIE_PCIECAP_DEV_TYPE_DOWN) &&
	    (dev_type != PCIE_PCIECAP_DEV_TYPE_ROOT))
		return (PCIE_ARI_FORW_NOT_SUPPORTED);

	if (pcie_disable_ari) {
		PCIE_DBG("pcie_ari_supported: dip=%p: ARI Disabled\n", dip);
		return (PCIE_ARI_FORW_NOT_SUPPORTED);
	}
	rcdip = pcie_get_rc_dip(dip);
	if (bus_p && (bus_p->bus_cfg_hdl != NULL)) {
		pciecap = PCIE_CAP_GET(16, bus_p, PCIE_PCIECAP);
	} else
		pciecap = pci_cfgacc_get16(rcdip, bdf, pcie_off + PCIE_PCIECAP);

	if ((pciecap & PCIE_PCIECAP_VER_MASK) < PCIE_PCIECAP_VER_2_0) {
		PCIE_DBG("pcie_ari_supported: dip=%p: Not 2.0\n", dip);
		return (PCIE_ARI_FORW_NOT_SUPPORTED);
	}
	if (bus_p && (bus_p->bus_cfg_hdl != NULL)) {
		devcap2 = PCIE_CAP_GET(32, bus_p, PCIE_DEVCAP2);
	} else
		devcap2 = pci_cfgacc_get32(rcdip, bdf, pcie_off + PCIE_DEVCAP2);

	PCIE_DBG("pcie_ari_supported: dip=%p: DevCap2=0x%x\n",
	    dip, devcap2);

	if (devcap2 & PCIE_DEVCAP2_ARI_FORWARD) {
		PCIE_DBG("pcie_ari_supported: "
		    "dip=%p: ARI Forwarding is supported\n", dip);
		return (PCIE_ARI_FORW_SUPPORTED);
	}
	return (PCIE_ARI_FORW_NOT_SUPPORTED);
}

/*
 * This routine may be called when
 * bus_cfg_hdl is not initialized.
 */
int
pcie_ari_enable(dev_info_t *dip)
{
	uint16_t devctl2;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	dev_info_t	*rcdip;
	pcie_req_id_t   bdf;
	uint16_t	pcie_off;
	int	rval;

	PCIE_DBG("pcie_ari_enable: dip=%p\n", dip);

	if (pcie_ari_supported(dip) == PCIE_ARI_FORW_NOT_SUPPORTED)
		return (DDI_FAILURE);

	if (bus_p && (bus_p->bus_cfg_hdl != NULL)) {
		devctl2 = PCIE_CAP_GET(16, bus_p, PCIE_DEVCTL2);
		devctl2 |= PCIE_DEVCTL2_ARI_FORWARD_EN;
		PCIE_CAP_PUT(16, bus_p, PCIE_DEVCTL2, devctl2);
	} else {
		rval = pcie_get_bus_dev_type(dip, &pcie_off);
		if (rval == DDI_FAILURE)
			return (DDI_FAILURE);
		rcdip = pcie_get_rc_dip(dip);
		if (pcie_get_bdf_from_dip(dip, &bdf) == DDI_FAILURE)
			return (DDI_FAILURE);
		devctl2 = pci_cfgacc_get16(rcdip, bdf,
		    pcie_off + PCIE_DEVCTL2);
		pci_cfgacc_put16(rcdip, bdf,
		    pcie_off + PCIE_DEVCTL2,
		    devctl2 | PCIE_DEVCTL2_ARI_FORWARD_EN);
	}

	PCIE_DBG("pcie_ari_enable: dip=%p: writing 0x%x to DevCtl2\n",
	    dip, devctl2);

	return (DDI_SUCCESS);
}

int
pcie_ari_disable(dev_info_t *dip)
{
	uint16_t devctl2;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);

	PCIE_DBG("pcie_ari_disable: dip=%p\n", dip);

	if (pcie_ari_supported(dip) == PCIE_ARI_FORW_NOT_SUPPORTED)
		return (DDI_FAILURE);

	devctl2 = PCIE_CAP_GET(16, bus_p, PCIE_DEVCTL2);
	devctl2 &= ~PCIE_DEVCTL2_ARI_FORWARD_EN;
	PCIE_CAP_PUT(16, bus_p, PCIE_DEVCTL2, devctl2);

	PCIE_DBG("pcie_ari_disable: dip=%p: writing 0x%x to DevCtl2\n",
	    dip, devctl2);

	return (DDI_SUCCESS);
}

int
pcie_ari_is_enabled(dev_info_t *dip)
{
	uint16_t devctl2;
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	dev_info_t	*rcdip;
	int	rval;
	pcie_req_id_t	bdf;
	uint16_t	pcie_off;

	PCIE_DBG("pcie_ari_is_enabled: dip=%p\n", dip);

	if (pcie_ari_supported(dip) == PCIE_ARI_FORW_NOT_SUPPORTED)
		return (PCIE_ARI_FORW_DISABLED);
	if (bus_p && (bus_p->bus_cfg_hdl != NULL)) {
		devctl2 = PCIE_CAP_GET(32, bus_p, PCIE_DEVCTL2);
	} else {
		rval = pcie_get_bus_dev_type(dip, &pcie_off);
		if (rval == DDI_FAILURE)
			return (PCIE_ARI_FORW_NOT_SUPPORTED);
		if (pcie_get_bdf_from_dip(dip, &bdf) == DDI_FAILURE)
			return (PCIE_ARI_FORW_NOT_SUPPORTED);
		rcdip = pcie_get_rc_dip(dip);
		devctl2 = pci_cfgacc_get32(rcdip, bdf, pcie_off + PCIE_DEVCTL2);
	}

	PCIE_DBG("pcie_ari_is_enabled: dip=%p: DevCtl2=0x%x\n",
	    dip, devctl2);

	if (devctl2 & PCIE_DEVCTL2_ARI_FORWARD_EN) {
		PCIE_DBG("pcie_ari_is_enabled: "
		    "dip=%p: ARI Forwarding is enabled\n", dip);
		return (PCIE_ARI_FORW_ENABLED);
	}

	return (PCIE_ARI_FORW_DISABLED);
}

int
pcie_ari_device(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	uint16_t	base;

	PCIE_DBG("pcie_ari_device: dip=%p\n", dip);
	if (bus_p)
		return (bus_p->bus_ari_off ? PCIE_ARI_DEVICE :
		    PCIE_NOT_ARI_DEVICE);

	/* Locate the ARI Capability */

	if (pcie_cap_locate(dip, PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_ARI),
	    &base) == DDI_SUCCESS) {
		/* ARI Capability was found so it must be a ARI device */
		PCIE_DBG("pcie_ari_device: ARI Device dip=%p\n", dip);
		return (PCIE_ARI_DEVICE);
	}
	return (PCIE_NOT_ARI_DEVICE);
}

int
pcie_ari_get_next_function(dev_info_t *dip, int *func)
{
	uint32_t val;
	uint16_t cap_ptr, next_function;
	ddi_acc_handle_t handle;

	/*
	 * XXX - This function may be called before the bus_p structure
	 * has been populated.  This code can be changed to remove
	 * pci_config_setup()/pci_config_teardown() when the RFE
	 * to populate the bus_p structures early in boot is putback.
	 */

	if (pci_config_setup(dip, &handle) != DDI_SUCCESS)
		return (DDI_FAILURE);

	if ((PCI_CAP_LOCATE(handle,
	    PCI_CAP_XCFG_SPC(PCIE_EXT_CAP_ID_ARI), &cap_ptr)) == DDI_FAILURE) {
		pci_config_teardown(&handle);
		return (DDI_FAILURE);
	}

	val = PCI_CAP_GET32(handle, NULL, cap_ptr, PCIE_ARI_CAP);

	next_function = (val >> PCIE_ARI_CAP_NEXT_FUNC_SHIFT) &
	    PCIE_ARI_CAP_NEXT_FUNC_MASK;

	pci_config_teardown(&handle);

	*func = next_function;

	return (DDI_SUCCESS);
}

dev_info_t *
pcie_func_to_dip(dev_info_t *dip, pcie_req_id_t function)
{
	pcie_req_id_t child_bdf, func_mask = PCIE_REQ_ID_FUNC_MASK;
	dev_info_t *cdip;

	if (dip == NULL)
		return (NULL);

	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip)) {

		if (pcie_get_bdf_from_dip(cdip, &child_bdf) ==
		    DDI_FAILURE)
			return (NULL);

		if ((pcie_ari_is_enabled(ddi_get_parent(dip))
		    == PCIE_ARI_FORW_ENABLED) && (pcie_ari_device(dip)
		    == PCIE_ARI_DEVICE))
			func_mask = PCIE_REQ_ID_ARI_FUNC_MASK;

		if ((child_bdf & func_mask) == function)
			return (cdip);
	}
	return (NULL);
}

/* Check if a node is a pci/pcie bridge node */
boolean_t
pcie_is_pci_bridge(dev_info_t *dip)
{
	char *device_type;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "device_type", &device_type)
	    != DDI_SUCCESS) {
		return (B_FALSE);
	}

	/* it is not a pci/pci-ex bus type */
	if ((strcmp(device_type, "pci") != 0) &&
	    (strcmp(device_type, "pciex") != 0)) {
		ddi_prop_free(device_type);
		return (B_FALSE);
	}

	ddi_prop_free(device_type);
	return (B_TRUE);
}

/* Check if the pdip is the parent/grandpanrent of dip */
boolean_t
pcie_is_ancestor(dev_info_t *dip, dev_info_t *ancestor_dip)
{
	for (; dip && dip != ancestor_dip; dip = ddi_get_parent(dip))
		;
	return (dip ? B_TRUE : B_FALSE);
}

uint16_t
pcie_get_vf_busnum(dev_info_t *pf_dip)
{
	pcie_bus_t		*bus_p = PCIE_DIP2UPBUS(pf_dip);
	pcie_req_id_t		pf_bdf = bus_p->bus_bdf;
	dev_info_t		*rcdip = pcie_get_rc_dip(pf_dip);
	uint16_t		sriov_cap_ptr = bus_p->sriov_cap_ptr;
	uint16_t		num_vf, vf1_off, stride;
	uint_t			pf_bus, max_bus;

	num_vf = pci_cfgacc_get16(rcdip, pf_bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET);
	vf1_off = pci_cfgacc_get16(rcdip, pf_bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	stride = pci_cfgacc_get16(rcdip, pf_bdf, sriov_cap_ptr +
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);

	if (num_vf == PCI_CAP_EINVAL16 || stride == PCI_CAP_EINVAL16 ||
	    vf1_off == 0 || vf1_off == PCI_CAP_EINVAL16 ||
	    (num_vf > 1 && stride == 0))
		return (PCI_CAP_EINVAL16);

	/*
	 * compute the maximum bus num required to support VFs.
	 */
	PCIE_DBG("pf bdf = 0x%x offset = 0x%x stride = 0x%x\n",
	    pf_bdf, vf1_off, stride);
	pf_bus = pf_bdf >> PCIE_REQ_ID_BUS_SHIFT;
	max_bus = ((pf_bdf + vf1_off + (stride * (num_vf -1)))
	    >> PCIE_REQ_ID_BUS_SHIFT);

	return (max_bus - pf_bus);
}

/*
 * Get the VF bar resource requests into an array pointed to by vf_bars.
 * Caller is expected to pass in an array of size sizeof(pci_regspec_t) *
 * PCI_BASE_NUM and this array will be filled in upon successful return
 * of this function.
 */
int
pcie_get_vf_bars(dev_info_t *pf_dip, pci_regspec_t *vf_bars,
    int *vf_cnt, boolean_t totalVF)
{
	uint32_t 		mem_base, hiword;
	uint32_t 		m64, size;
	uint32_t		mem_size;
	pci_regspec_t		phys_spec, *reg;
	int 			i, bar_num, rv, rlen;
	int			reg_offset, offset;
	pcie_bus_t		*bus_p = PCIE_DIP2UPBUS(pf_dip);
	dev_info_t		*rcdip = pcie_get_rc_dip(pf_dip);
	uint16_t		bdf = bus_p->bus_bdf;
	uint16_t		num_vf;

	ASSERT(rcdip);

	if (bus_p->sriov_cap_ptr == 0)
		return (DDI_FAILURE);

	num_vf = pci_cfgacc_get16(rcdip, bdf, bus_p->sriov_cap_ptr +
	    (totalVF ? PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET :
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET));
	if (num_vf == PCI_CAP_EINVAL16 || num_vf == 0) {
		PCIE_DBG("pcie_get_vf_bars: num_vf is invalid,"
		    "pf dip=%p", pf_dip);
		return (DDI_FAILURE);
	}
	rv = ddi_getlongprop(DDI_DEV_T_ANY, pf_dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&reg, &rlen);
	if (rv != DDI_PROP_SUCCESS)
		return (DDI_FAILURE);

	*vf_cnt = num_vf;
	bzero(vf_bars, sizeof (pci_regspec_t) * PCI_BASE_NUM);
	/* Process each VF BAR */
	for (i = PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET,
	    reg_offset = PCI_CONF_BASE0, bar_num = 0;
	    i <= PCIE_EXT_CAP_SRIOV_VF_BAR5_OFFSET;
	    i += (m64 ? 8 : 4), reg_offset += (m64 ? 8 : 4),
	    bar_num += (m64 ? 2 :1)) {
		/*
		 * First read the low order 32 bits to determine if the BARs
		 * are 32 bit or 64 bit. Must restore the BAR after sizing it
		 * in case the BAR has been programmed already.
		 */
		offset = i + bus_p->sriov_cap_ptr;
		mem_base = pci_cfgacc_get32(rcdip, bdf, offset);
		pci_cfgacc_put32(rcdip, bdf, offset, -1u);

		m64 = ((mem_base & PCI_BASE_TYPE_M) == PCI_BASE_TYPE_ALL);
		if (m64) {
			pci_cfgacc_put32(rcdip, bdf, offset + 4, -1u);
		}
		mem_size = pci_cfgacc_get32(rcdip, bdf, offset);
		if (mem_size == 0)
			continue;

		/* Build the phys_spec for this VF BAR */
		hiword = PCICFG_MAKE_REG_HIGH(PCI_REG_BUS_G(reg->pci_phys_hi),
		    PCI_REG_DEV_G(reg->pci_phys_hi),
		    PCI_REG_FUNC_G(reg->pci_phys_hi),
		    bar_num);
		size = (~(PCI_BASE_M_ADDR_M & mem_size)) + 1;
		if (size == 0) {
			PCIE_DBG("pcie_get_VF_mem_req: "
			    "VF base register [0x%x] asks for 0", reg_offset);
			continue;
		}
		hiword |= m64 ? PCI_ADDR_MEM64 : PCI_ADDR_MEM32;
		hiword |= mem_base & PCI_BASE_PREF_M ? PCI_REG_PF_M : 0;
		phys_spec.pci_phys_hi = (hiword | PCI_REG_REL_M);
		phys_spec.pci_phys_mid = 0;
		phys_spec.pci_phys_low = 0;
		phys_spec.pci_size_hi = 0;
		phys_spec.pci_size_low = (size * num_vf);
		bcopy(&phys_spec, vf_bars + bar_num, sizeof (pci_regspec_t));
	}
	kmem_free(reg, rlen);

	return (DDI_SUCCESS);
}

static boolean_t
pcie_match_device_func(dev_info_t *dip, uint_t device, uint_t func)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);

	if (device == PCIRM_ALL_DEVICES)
		return (B_TRUE);

	return ((bus_p->bus_bdf & 0xff) == (device << 3 | func));
}

/* Find the common parent of two dip nodes */
dev_info_t *
pcie_find_common_parent(dev_info_t *dip1, dev_info_t *dip2)
{
	dev_info_t *pdip = NULL;

	if (dip1 == dip2)
		return (dip1);
	if (pcie_is_ancestor(dip1, dip2))
		return (dip2);
	if (pcie_is_ancestor(dip2, dip1))
		return (dip1);

	pdip = ddi_get_parent(dip2);
	for (; pdip; pdip = ddi_get_parent(pdip)) {
		if (pcie_is_ancestor(dip1, pdip)) {
			return (pdip);
		}
	}
	return (pdip);
}

/* Currently only IOV requires resource intervention */
static boolean_t
pcie_res_is_reserved(dev_info_t *dip)
{
	pci_regspec_t vf_bars[PCI_BASE_NUM];
	pci_regspec_t *phys_spec;
	pcirm_req_t request;
	pcirm_res_t allocated;
	pci_regspec_t *avail;
	dev_info_t *cdip;
	boolean_t is_enough = B_TRUE;
	int i, alen, nbus, tmpbus;
	int num_vf;

	/* bus number */
	nbus = 0;
	cdip = ddi_get_child(dip);
	for (; cdip; cdip = ddi_get_next_sibling(cdip)) {
		if (PCIE_IS_IOV_PF(cdip)) {
			if ((tmpbus = pcie_get_vf_busnum(cdip)) !=
			    PCI_CAP_EINVAL16) {
				nbus += tmpbus;
			}
		}
	}

	if (nbus != 0) {
		bzero(&request, sizeof (pcirm_req_t));
		request.type = PCIRM_TYPE_BUS;
		request.len = nbus;

		if (pcirm_alloc_resource(NULL, dip,
		    &request, &allocated) != PCIRM_SUCCESS) {
			return (B_FALSE);
		}
	}

	/* memory */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, "available", (caddr_t)&avail,
	    &alen) != DDI_SUCCESS) {
		return (B_FALSE);
	}

	cdip = ddi_get_child(dip);
	for (; cdip; cdip = ddi_get_next_sibling(cdip)) {
		if (!PCIE_IS_IOV_PF(cdip))
			continue;

		if (pcie_get_vf_bars(cdip, vf_bars, &num_vf, B_TRUE) !=
		    PCIRM_SUCCESS)
			continue;

		for (i = 0; i < PCI_BASE_NUM; i++) {
			phys_spec = vf_bars + i;
			if (phys_spec->pci_size_low == 0)
				continue;

			bzero(&request, sizeof (pcirm_req_t));
			request.type = PCIRM_REQTYPE(
			    phys_spec->pci_phys_hi);
			request.len = phys_spec->pci_size_low;
			request.align_mask = request.len/num_vf - 1;

			if (pcirm_alloc_resource(NULL, dip, &request,
			    &allocated) != PCIRM_SUCCESS) {
				is_enough = B_FALSE;
				goto restore;
			}
		}
	}

restore:
	/*
	 * restore "available" prop, this actually rolls back the
	 * allocations done above.
	 */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "available", (int *)avail, alen / sizeof (int));
	kmem_free(avail, alen);

	return (is_enough);
}

#if defined(__i386) || defined(__amd64)
static boolean_t
pcie_check_all_regs_assigned(dev_info_t *dip)
{
	pci_regspec_t *regs, *assigned;
	int rlen, rcount;
	int alen, acount;
	int i, j;
	boolean_t is_assigned = B_TRUE;

	/* read "reg" property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&regs, &rlen) != DDI_SUCCESS) {
		return (B_TRUE);
	}
	rcount = rlen / sizeof (pci_regspec_t);

	/* read "assigned-addresses" property */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned, &alen) != DDI_SUCCESS) {
		kmem_free(regs, rlen);
		return (rcount == 1);
	}
	acount = alen / sizeof (pci_regspec_t);

	/*
	 * Use "assigned-addresses" as a basis to calculate device node
	 * resource requirements, but also cross check "reg" property
	 * for non-relocatable bit .etc, if avaiable.
	 */
	for (i = 1; i < rcount; i++) {
		uint32_t reg_type = PCI_REG_ADDR_G(regs[i].pci_phys_hi);
		uint32_t reg_addr = PCI_REG_BDFR_G(regs[i].pci_phys_hi);

		/* match with type */
		if (PCIRM_REQTYPE(regs[i].pci_phys_hi) != PCIRM_TYPE_MEM &&
		    PCIRM_REQTYPE(regs[i].pci_phys_hi) != PCIRM_TYPE_PMEM) {
			continue;
		}

		for (j = 0; j < acount; j++) {
			uint32_t assign_type = PCI_REG_ADDR_G(
			    assigned[j].pci_phys_hi);
			uint32_t assign_addr = PCI_REG_BDFR_G(
			    assigned[j].pci_phys_hi);

			if (reg_addr != assign_addr)
				continue;

			if (reg_type == assign_type ||
			    (reg_type == PCI_ADDR_MEM64 &&
			    assign_type == PCI_ADDR_MEM32)) {
				/*
				 * Found matched reg entry
				 */
				if (REG_TO_SIZE(assigned[j]) <
				    REG_TO_SIZE(regs[i])) {
					is_assigned = B_FALSE;
					goto done;
				}
				break;
			}
		}

		if (j == acount) {
			is_assigned = B_FALSE;
			goto done;
		}
	}
done:
	kmem_free(assigned, alen);
	kmem_free(regs, rlen);
	return (is_assigned);
}
#endif

typedef struct check_res_arg {
	dev_info_t *topdip;
} check_res_arg_t;

static int
pcie_do_check_all_devices_assigned(dev_info_t *dip, void *arg)
{
	check_res_arg_t	*res_arg = (check_res_arg_t *)arg;

	if (!PCIE_IS_PCI_DEVICE(dip))
		return (DDI_WALK_PRUNECHILD);

#if defined(__i386) || defined(__amd64)
	/* Check if all the regs for this dip are assigned properly */
	if (!pcie_check_all_regs_assigned(dip)) {
		/* regs not assigned */
		dip = ddi_get_parent(dip);
		goto found;
	}
#endif
	if (pcie_is_pci_bridge(dip)) {
		dev_info_t *cdip;

		for (cdip = ddi_get_child(dip); cdip && !PCIE_IS_IOV_PF(cdip);
		    cdip = ddi_get_next_sibling(cdip)) {
			/* iov PF? */
		}
		if (!cdip)
			return (DDI_WALK_CONTINUE);

		if (!pcie_res_is_reserved(dip))
			goto found; /* insufficient resources */
	}
	return (DDI_WALK_CONTINUE);
found:
	res_arg->topdip = res_arg->topdip?
	    pcie_find_common_parent(dip, res_arg->topdip) : dip;
	return (DDI_WALK_CONTINUE);
}

/*
 * Check if all devices in this fabric are assigned required resources
 */
static int
pcie_check_all_devices_assigned(dev_info_t *rcdip, dev_info_t **topdip)
{
	int		circular_count;
	dev_info_t	*dip = ddi_get_child(rcdip);
	check_res_arg_t arg;

	arg.topdip = NULL;

	ndi_devi_enter(rcdip, &circular_count);
	ddi_walk_devs(dip, pcie_do_check_all_devices_assigned, &arg);
	ndi_devi_exit(rcdip, circular_count);

	*topdip = arg.topdip;

	return (arg.topdip == NULL);
}

static void
pcie_set_bridge_bars(dev_info_t *rcdip, uint16_t bdf, pcirm_type_t type,
    uint64_t new_base, uint64_t new_len)
{
	/*
	 * Program PCI bridge registers
	 */
	switch (type) {
	case PCIRM_TYPE_BUS:
		/* reprogram secondary/sub bus number */

		pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_SECBUS,
		    new_base);
		pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_SUBBUS,
		    new_base + new_len - 1);

		ASSERT(pci_cfgacc_get8(rcdip, bdf, PCI_BCNF_SECBUS)
		    == new_base);
		ASSERT(pci_cfgacc_get8(rcdip, bdf, PCI_BCNF_SUBBUS)
		    == new_base + new_len - 1);
		break;

	case PCIRM_TYPE_PMEM:
		ASSERT(P2ALIGN((new_base + new_len), PCIE_MEMGRAN) ==
		    new_base + new_len);

		if (new_len == 0) {
			/* disable forwarding any PMEM transactions */
			pci_cfgacc_put16(rcdip, bdf,
			    PCI_BCNF_PF_BASE_LOW, 0xfff0);
			pci_cfgacc_put32(rcdip, bdf,
			    PCI_BCNF_PF_BASE_HIGH, -1u);
			pci_cfgacc_put16(rcdip, bdf,
			    PCI_BCNF_PF_LIMIT_LOW, 0);
			pci_cfgacc_put32(rcdip, bdf,
			    PCI_BCNF_PF_LIMIT_HIGH, 0);
			break;
		}

		/*
		 * Program the PF memory base register with the
		 * start of the memory range
		 */
		pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_PF_BASE_LOW,
		    HI16(LO32(new_base)));
		pci_cfgacc_put32(rcdip, bdf, PCI_BCNF_PF_BASE_HIGH,
		    HI32(new_base));
		/*
		 * Program the PF memory limit register with the
		 * end of the memory range.
		 */
		pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_PF_LIMIT_LOW,
		    HI16(LO32(P2ALIGN((new_base + new_len),
		    PCIE_MEMGRAN) - 1)));
		pci_cfgacc_put32(rcdip, bdf, PCI_BCNF_PF_LIMIT_HIGH,
		    HI32(P2ALIGN((new_base + new_len),
		    PCIE_MEMGRAN) - 1));
		break;

	case PCIRM_TYPE_MEM:
		ASSERT(P2ALIGN((new_base + new_len), PCIE_MEMGRAN) ==
		    new_base + new_len);

		if (new_len == 0) {
			/* disable forwarding any MEM transactions */
			pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_MEM_BASE, 0xffff);
			pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_MEM_LIMIT, 0);
			break;
		}

		/*
		 * Program the memory base register with the
		 * start of the memory range
		 */
		pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_MEM_BASE,
		    HI16(LO32(new_base)));
		/*
		 * Program the memory limit register with the
		 * end of the memory range.
		 */
		pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_MEM_LIMIT,
		    HI16(LO32(P2ALIGN((new_base + new_len),
		    PCIE_MEMGRAN) - 1)));

		break;

	case PCIRM_TYPE_IO:
		ASSERT(P2ALIGN(new_base + new_len, PCIE_IOGRAN)
		    == new_base + new_len);

		if (new_len == 0) {
			/* disable forwarding any IO transactions */
			pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_IO_LIMIT_LOW, 0);
			pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_IO_LIMIT_HI, 0);
			pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_IO_BASE_LOW, 0xff);
			pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_IO_BASE_HI, 0);
			break;
		}

		/*
		 * Program the I/O Space Base
		 */
		pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_IO_BASE_LOW,
		    HI8(LO16(LO32(new_base))));
		pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_IO_BASE_HI,
		    HI16(LO32(new_base)));
		/*
		 * Program the I/O Space Limit
		 */
		pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_IO_LIMIT_LOW,
		    HI8(LO16(LO32(P2ALIGN(new_base + new_len,
		    PCIE_IOGRAN)))) - 1);
		pci_cfgacc_put16(rcdip, bdf, PCI_BCNF_IO_LIMIT_HI,
		    HI16(LO32(P2ALIGN(new_base + new_len,
		    PCIE_IOGRAN))) - 1);
		break;

	default:
		break;
	} /* switch */
}

/* ARGSUSED */
static void
pcie_set_device_bars(dev_info_t *rcdip, uint16_t bdf, pcirm_type_t type,
    uint64_t new_base, uint64_t new_len, int offset)
{
	/*
	 * Program device BARs
	 */
	switch (type) {
	case PCIRM_TYPE_PMEM:
	case PCIRM_TYPE_MEM:
#if DEBUG
		/* check for not present BAR */
		pci_cfgacc_put32(rcdip, bdf, offset, -1u);
		if (pci_cfgacc_get32(rcdip, bdf, offset) == 0)
			break;
#endif
		/* program the low word */
		pci_cfgacc_put32(rcdip, bdf, offset,
		    LO32(new_base));

		if (offset == 0x30) {
			/* ROM BAR is 32 bit */
			ASSERT((pci_cfgacc_get32(rcdip, bdf, offset) &
			    PCI_BASE_ROM_ADDR_M) == LO32(new_base));
			break;
		}

		ASSERT((pci_cfgacc_get32(rcdip, bdf, offset) &
		    PCI_BASE_M_ADDR_M) == LO32(new_base));
		/*
		 * program the high word if this is a 64bit BAR,
		 * note that "reg" property of ROM bar has different
		 * meanings from other memory BARs.
		 */
		if ((pci_cfgacc_get32(rcdip, bdf, offset) & PCI_BASE_TYPE_M)
		    == PCI_BASE_TYPE_ALL) {
			/* high word */
			pci_cfgacc_put32(rcdip, bdf, offset + 4,
			    HI32(new_base));
			ASSERT(pci_cfgacc_get32(rcdip, bdf, offset + 4) ==
			    HI32(new_base));
		}
		break;

	case PCIRM_TYPE_IO:
		/* program the low word */
		pci_cfgacc_put32(rcdip, bdf, offset,
		    LO32(new_base));
		ASSERT((pci_cfgacc_get32(rcdip, bdf, offset) &
		    PCI_BASE_IO_ADDR_M) == LO32(new_base));
		break;

	default:
		break;
	} /* switch */
}

static void
pcie_set_bars(dev_info_t *dip, pcirm_type_t type, uint64_t new_base,
    uint64_t new_len, int offset)
{

	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	dev_info_t *rcdip = pcie_get_rc_dip(dip);
	uint16_t bdf = bus_p->bus_bdf;

	if (offset == PCIRM_BAR_OFF_BUS_NUM) {
		/*
		 * The bus number of this device has changed.
		 * Do a config space write to let it memorize its new
		 * bus number. For a PCI bridge reprogram its primary
		 * bus number accordingly.
		 */
		if (PCIE_IS_BDG(bus_p)) {
			/* program child bridge's primary bus */
			pci_cfgacc_put8(rcdip, bdf, PCI_BCNF_PRIBUS,
			    new_base);
		} else {
			pci_cfgacc_put8(rcdip, bdf, PCI_CONF_VENID, 0);
		}

		ASSERT(pci_cfgacc_get32(rcdip, bdf, PCI_CONF_VENID) ==
		    bus_p->bus_dev_ven_id);

		return;
	}

	if (!PCIE_IS_BDG(bus_p) || offset >= 0) {
		pcie_set_device_bars(rcdip, bdf, type,
		    new_base, new_len, offset);
	} else {
		ASSERT(offset == -1);
		pcie_set_bridge_bars(rcdip, bdf, type,
		    new_base, new_len);
	}
}

/*
 * This function assumes bus_t has been updated after rebalance
 * before calling this func
 */
/* ARGSUSED */
static int
pcie_reprogram_resource(dev_info_t *dip, pcirm_rbl_map_t *rbl, void *arg)
{
	for (; rbl != NULL; rbl = rbl->next) {
		pcie_set_bars(dip, rbl->type, rbl->new_base,
		    rbl->new_len, rbl->bar_off);
	}

	return (DDI_SUCCESS);
}

/*
 * Reserve necessary resources for SR-IOV devices so that VFs can be used,
 * if firmware hasn't assigned enough resources already. We reserve enough
 * resources (bus number, memory) for all VFs in each IOV device.
 */
boolean_t
pcie_boot_rebalance(dev_info_t *rcdip)
{
	/* Find all IOV devices in this fabric */
	pcirm_handle_t handle;
	dev_info_t *topdip;
	int ret;

	if (!pcie_plat_rbl_enabled())
		pcie_br_flags = 0;

	if (!(pcie_br_flags & PCIE_BR_ENABLE))
		return (B_FALSE);

	if (pcie_br_flags & PCIE_BR_ALL_FABRICS) {
		/* force rebalance of entire fabric */
		topdip = rcdip;
	} else {
		/* check for devices which require rebalance */
		if (pcie_check_all_devices_assigned(rcdip, &topdip))
			return (B_FALSE);
	}

	/* Find common parent of all IOV devices */
	ret = pcirm_find_resource(topdip, PCIRM_ALL_DEVICES,
	    PCIRM_TYPE_BUS_M | PCIRM_TYPE_MEM_M | PCIRM_TYPE_PMEM_M,
	    &handle);

	if (ret >= 0) {
		if (pcirm_commit_resource(handle) == PCIRM_SUCCESS) {
			(void) pcirm_walk_resource_map(handle,
			    pcie_reprogram_resource, NULL);
		}
		(void) pcirm_free_handle(handle);
	}

	return (B_TRUE);
}

/*
 * Fix "assigned-addresses" property if:
 * - some "reg" entries are missing from "assigned-addresses";
 * - it contains non-existing entries.
 */
static void
pcie_fix_assigned_prop(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	dev_info_t	*rcdip = pcie_get_rc_dip(dip);
	uint16_t	bdf = bus_p->bus_bdf;
	pci_regspec_t	*regs, *assigned, *newregs;
	int		rlen, alen;
	uint_t		i, j, rcount, acount;
	int		offset;
	uint32_t	oldval, val;
	pci_regspec_t	buf[PCI_BASE_NUM+1];

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&regs, &rlen) != DDI_SUCCESS) {
		return;
	}
	rcount = rlen / sizeof (pci_regspec_t);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned, &alen)
	    != DDI_SUCCESS) {
		/* not found */
		alen = 0;
	}
	acount = alen / sizeof (pci_regspec_t);

	if (acount > sizeof (buf)/sizeof (pci_regspec_t))
		return;

	bcopy(assigned, buf, alen);
	/*
	 * Cross check "reg" and "assigned-addresses" properties, and
	 * add missing entries into "assigned-addresses" if any.
	 */
	for (i = 1; i < rcount; i++) {
		for (j = 0; j < acount; j++) {
			if (PCI_REG_REG_G(regs[i].pci_phys_hi) ==
			    PCI_REG_REG_G(buf[j].pci_phys_hi)) {
				break;
			}
		}

		if (j < acount)
			continue;

		/*
		 * There is a "reg" entry that isn't included
		 * in "assigned-addresses", so add it.
		 */
		regs[i].pci_phys_hi |= PCI_REG_REL_M;

		offset = PCI_REG_REG_G(regs[i].pci_phys_hi);
		val = pci_cfgacc_get32(rcdip, bdf, offset);
		if (offset == PCI_CONF_ROM) {
			/* ROM bar */
			regs[i].pci_phys_low = val &
			    PCI_BASE_ROM_ADDR_M;
			goto update;
		}
		if ((val & PCI_BASE_SPACE_M) == PCI_BASE_SPACE_IO) {
			/* IO bar */
			regs[i].pci_phys_low = val &
			    PCI_BASE_IO_ADDR_M;
			goto update;
		}

		/* Mem bar */
		regs[i].pci_phys_low = val & PCI_BASE_M_ADDR_M;
		if ((val & PCI_BASE_TYPE_M) == PCI_BASE_TYPE_ALL) {
			val = pci_cfgacc_get32(rcdip, bdf, offset+4);
			regs[i].pci_phys_mid = val;
		}

update:
		bcopy(&regs[i], buf + acount, sizeof (regs[i]));
		acount++;
	}

	kmem_free((caddr_t)regs, rlen);
	if (alen) {
		kmem_free((caddr_t)assigned, alen);
	}

	/* No "assigned-addresses" */
	if (acount == 0)
		return;

	/*
	 * Remove entries in "assigned-addresses" if BAR doesn't exist.
	 */
	alen = acount * sizeof (pci_regspec_t);
	newregs = kmem_zalloc(alen, KM_SLEEP);
	for (i = 0, j = 0; i < acount; i++) {
		if (buf[i].pci_phys_mid == 0 &&
		    buf[i].pci_phys_low == 0) {
			/* base addr is 0, likely a non-existing bar */
			offset = PCI_REG_REG_G(buf[i].pci_phys_hi);

			/* Size the bar to see if it exists */
			oldval = pci_cfgacc_get32(rcdip, bdf, offset);
			pci_cfgacc_put32(rcdip, bdf, offset, -1u);
			val = pci_cfgacc_get32(rcdip, bdf, offset);
			pci_cfgacc_put32(rcdip, bdf, offset, oldval);

			if (val == 0) {
				/* Remove this non-existing bar */
				continue;
			}
		}

		newregs[j] = buf[i];
		j++;
	}

	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "assigned-addresses", (int *)newregs,
	    (j * sizeof (pci_regspec_t))/sizeof (int));
	kmem_free((caddr_t)newregs, alen);
}

static void
pcie_dup_prop(dev_info_t *dip, char *propname)
{
	int	*prop;
	int	plen;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    propname, (caddr_t)&prop, &plen) == DDI_SUCCESS) {

		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    propname, (int *)prop, plen / sizeof (int));
		kmem_free(prop, plen);
	}
}

/*
 * Copy "ranges" and "available" properties from PROM to OS.
 * As boot rebalance could re-arrange device resource assignments,
 * if "ranges" or "available" property gets removed as a result
 * of rebalance, ddi_getlongprop() will return values from PROM.
 * The intent here is to copy the values from PROM to OS, and
 * update the necessary consumers to lookup these props from OS
 * (DDI_PROP_NOTPROM).
 */
static void
pcie_dup_props(dev_info_t *dip)
{
	pcie_dup_prop(dip, "bus-range");
	pcie_dup_prop(dip, "ranges");
	pcie_dup_prop(dip, "available");
	pcie_dup_prop(dip, "reg");
	pcie_dup_prop(dip, "assigned-addresses");
}

/* ARGSUSED */
static int
pcie_fab_do_copy_fix_addr_prop(dev_info_t *dip, void *arg)
{
	if (!PCIE_IS_PCI_DEVICE(dip))
		return (DDI_WALK_PRUNECHILD);

	pcie_dup_props(dip);
	pcie_fix_assigned_prop(dip);
	return (DDI_WALK_CONTINUE);
}

void
pcie_fab_copy_fix_addr_prop(dev_info_t *rcdip)
{
	dev_info_t *dip = ddi_get_child(rcdip);
	int circular_count;

	pcie_dup_props(rcdip);

	ndi_devi_enter(rcdip, &circular_count);
	ddi_walk_devs(dip, pcie_fab_do_copy_fix_addr_prop, NULL);
	ndi_devi_exit(rcdip, circular_count);
}

/*
 * Get the USB host controller dip under which the boot keyboard resides.
 */
static dev_info_t *
pcie_get_boot_device(char *option)
{
	dev_info_t *pdip, *root, *cdip, *ccdip;
	char *input, *value, *temp, *dev_name, *path;
	int  ilen, klen, plen, device, func;
	boolean_t boot_kb = B_FALSE;
	boolean_t boot_disk = B_FALSE;

	if (strcmp(option, "input-device") == 0)
		boot_kb = B_TRUE;
	else if (strcmp(option, "boot-device") == 0)
		boot_disk = B_TRUE;
	else
		return (NULL);

	/* Get 'options' node from device tree */
	root = ddi_root_node();
	for (cdip = ddi_get_child(root); cdip != NULL;
	    cdip = ddi_get_next_sibling(cdip)) {
		if (strcmp(ddi_get_name(cdip), "options") == 0)
			break;
	}
	if (cdip == NULL)
		return (NULL);

	if (boot_kb) {
		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "input-device", (caddr_t)&input, &ilen) != DDI_PROP_SUCCESS)
			return (NULL);

		if (strcmp(input, "keyboard") != 0) {
			kmem_free(input, ilen);
			return (NULL);
		}
		kmem_free(input, ilen);

		/* Get path of keyboard.  */
		for (cdip = ddi_get_child(root); cdip != NULL;
		    cdip = ddi_get_next_sibling(cdip)) {
			if (strcmp(ddi_get_name(cdip), "aliases") == 0)
				break;
		}
		if (cdip == NULL)
			return (NULL);

		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "keyboard", (caddr_t)&path, &klen) != DDI_PROP_SUCCESS)
			return (NULL);
	} else if (boot_disk) { /* boot_disk */
		if (ddi_getlongprop(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "boot-device", (caddr_t)&path, &klen) != DDI_PROP_SUCCESS)
			return (NULL);
	} else {
		return (NULL);
	}

	/* Parse the path and find the usb control dip */
	pdip = root;
	value = path;
	while (*value) {
		while (*value != '@')
			value++;
		value++;
		if (pdip == root) {
			/*
			 * Parse the first item and find the dip of
			 * root complex.
			 */
			temp = value;
			plen = 0;
			while (*temp != '/') {
				temp++;
				plen++;
			}
			dev_name = kmem_zalloc(plen + 1, KM_SLEEP);
			(void) strncpy(dev_name, value, plen);
			dev_name[plen] = '\0';
			for (cdip = ddi_get_child(pdip); cdip != NULL;
			    cdip = ddi_get_next_sibling(cdip)) {
				if ((ccdip = ddi_get_child(cdip)) == NULL)
					continue;
				if (PCIE_IS_PCI_DEVICE(ccdip) &&
				    (strcmp(DEVI(cdip)->devi_addr,
				    dev_name) == 0)) {
					break;
				}
			}
			kmem_free(dev_name, plen + 1);
		} else {
			/*
			 * Parse the other items and find the dip of
			 * every device layer by layer.
			 */
			temp = value;
			while (*temp != '@' && *temp != ',')
				temp++;
			if (*temp == ',') {
				(void) sscanf(value, "%d", &device);
				(void) sscanf(++temp, "%d", &func);
			} else {
				(void) sscanf(value, "%d", &device);
				func = 0;
			}

			for (cdip = ddi_get_child(pdip); cdip != NULL;
			    cdip = ddi_get_next_sibling(cdip)) {
				if ((ccdip = ddi_get_child(cdip)) == NULL)
					continue;
				if (pcie_match_device_func(cdip,
				    device, func)) {
					break;
				}
			}
		}

		/* If no match dip found, return. */
		if (cdip == NULL) {
			kmem_free(path, klen);
			return (NULL);
		}

		if ((pdip != root) && !PCIE_IS_PCI_DEVICE(ccdip)) {
			kmem_free(path, klen);
			return (cdip);
		}
		/*
		 * If the device isn't the usb host control, continue to parse
		 * the path.
		 */
		pdip = cdip;
	}
	kmem_free(path, klen);
	return (NULL);
}

dev_info_t *
pcie_get_boot_usb_hc()
{
	return (pcie_get_boot_device("input-device"));
}

dev_info_t *
pcie_get_boot_disk()
{
	return (pcie_get_boot_device("boot-device"));
}

#ifdef	DEBUG

static void
pcie_print_bus(pcie_bus_t *bus_p)
{
	pcie_dbg("\tbus_dip = 0x%p\n", bus_p->bus_dip);
	pcie_dbg("\tbus_fm_flags = 0x%x\n", bus_p->bus_fm_flags);
	pcie_dbg("\tbus_bdf = 0x%x\n", bus_p->bus_bdf);
	pcie_dbg("\tbus_dev_ven_id = 0x%x\n", bus_p->bus_dev_ven_id);
	pcie_dbg("\tbus_rev_id = 0x%x\n", bus_p->bus_rev_id);
	pcie_dbg("\tbus_hdr_type = 0x%x\n", bus_p->bus_hdr_type);
	pcie_dbg("\tbus_dev_type = 0x%x\n", bus_p->bus_dev_type);
	pcie_dbg("\tbus_bdg_secbus = 0x%x\n", bus_p->bus_bdg_secbus);
	pcie_dbg("\tbus_pcie_off = 0x%x\n", bus_p->bus_pcie_off);
	pcie_dbg("\tbus_aer_off = 0x%x\n", bus_p->bus_aer_off);
	pcie_dbg("\tbus_ari_off = 0x%x\n", bus_p->bus_ari_off);
	pcie_dbg("\tbus_acs_off = 0x%x\n", bus_p->bus_acs_off);
	pcie_dbg("\tbus_ext2_off = 0x%x\n", bus_p->bus_ext2_off);
	pcie_dbg("\tbus_pcix_off = 0x%x\n", bus_p->bus_pcix_off);
	pcie_dbg("\tbus_ecc_ver = 0x%x\n", bus_p->bus_ecc_ver);
	pcie_dbg("\tbus_pcie_ver = 0x%x\n", bus_p->bus_pcie_ver);
}

/*
 * For debugging purposes set pcie_dbg_print != 0 to see printf messages
 * during interrupt.
 *
 * When a proper solution is in place this code will disappear.
 * Potential solutions are:
 * o circular buffers
 * o taskq to print at lower pil
 */
int pcie_dbg_print = 0;
void
pcie_dbg(char *fmt, ...)
{
	va_list ap;

	if (!pcie_debug_flags) {
		return;
	}
	va_start(ap, fmt);
	if (servicing_interrupt()) {
		if (pcie_dbg_print) {
			prom_vprintf(fmt, ap);
		}
	} else {
		prom_vprintf(fmt, ap);
	}
	va_end(ap);
}
#endif	/* DEBUG */

#ifdef	__amd64
static void
pcie_check_io_mem_range(ddi_acc_handle_t cfg_hdl, boolean_t *empty_io_range,
    boolean_t *empty_mem_range)
{
	uint8_t	class, subclass;
	uint_t	val;

	class = pci_config_get8(cfg_hdl, PCI_CONF_BASCLASS);
	subclass = pci_config_get8(cfg_hdl, PCI_CONF_SUBCLASS);

	if ((class == PCI_CLASS_BRIDGE) && (subclass == PCI_BRIDGE_PCI)) {
		val = (((uint_t)pci_config_get8(cfg_hdl, PCI_BCNF_IO_BASE_LOW) &
		    PCI_BCNF_IO_MASK) << 8);
		/*
		 * Assuming that a zero based io_range[0] implies an
		 * invalid I/O range.  Likewise for mem_range[0].
		 */
		if (val == 0)
			*empty_io_range = B_TRUE;
		val = (((uint_t)pci_config_get16(cfg_hdl, PCI_BCNF_MEM_BASE) &
		    PCI_BCNF_MEM_MASK) << 16);
		if (val == 0)
			*empty_mem_range = B_TRUE;
	}
}
#endif /* __amd64 */
