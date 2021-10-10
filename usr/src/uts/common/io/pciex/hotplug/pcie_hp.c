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

/*
 * This file contains the common hotplug code that is used by Standard
 * PCIe and PCI HotPlug Controller code.
 *
 * NOTE: This file is compiled and delivered through misc/pcie module.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/autoconf.h>
#include <sys/varargs.h>
#include <sys/ddi_impldefs.h>
#include <sys/time.h>
#include <sys/note.h>
#include <sys/callb.h>
#include <sys/ddi.h>
#include <sys/ddi_hp.h>
#include <sys/ddi_hp_impl.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dr.h>
#include <sys/pci_impl.h>
#include <sys/pci_cap.h>
#include <sys/hotplug/pci/pcicfg.h>
#include <sys/hotplug/pci/pcie_hp.h>
#include <sys/hotplug/pci/pciehpc.h>
#include <sys/hotplug/pci/pcishpc.h>
#include <io/pciex/pcieb.h>

#define	PCIE_HP_LSR_ACTIVITIES	"activities"
#define	PCIE_HP_LSR_IMPACTS	"impacts"
#define	PCIE_HP_LSR_REASON	"reason"

typedef struct lsr_string_value
{
	char *string;
	uint64_t value;
} lsr_string_value_t;

static lsr_string_value_t act_array[] = {
	{ "dma", DDI_CB_LSR_ACT_DMA },
	{ "pio", DDI_CB_LSR_ACT_PIO },
	{ "intr", DDI_CB_LSR_ACT_INTR },
	{ NULL, 0 }
};

static lsr_string_value_t imp_array[] = {
	{ "dma-addr-change", DDI_CB_LSR_IMP_DMA_ADDR_CHANGE },
	{ "dma-prop-change", DDI_CB_LSR_IMP_DMA_PROP_CHANGE },
	{ "device-reset", DDI_CB_LSR_IMP_DEVICE_RESET },
	{ "device-replace", DDI_CB_LSR_IMP_DEVICE_REPLACE },
	{ "lose-power", DDI_CB_LSR_IMP_LOSE_POWER },
	{ NULL, 0 }
};

/* Local functions prototype */
static int pcie_hp_list_occupants(dev_info_t *dip, void *arg);
static int pcie_hp_register_ports_for_dev(dev_info_t *dip, int device_num);
static int pcie_hp_unregister_ports_cb(ddi_hp_cn_info_t *info, void *arg);
static int pcie_hp_get_port_info(ddi_hp_cn_info_t *info, void *arg);
static int pcie_hp_match_dev_func(dev_info_t *dip, void *hdl);
static boolean_t pcie_hp_match_dev(dev_info_t *dip, int dev_num);
static int pcie_hp_get_df_from_port_name(char *cn_name, int *dev_num,
    int *func_num);
static int pcie_hp_create_port_name_num(dev_info_t *dip, dev_info_t *pdip,
    ddi_hp_cn_info_t *cn_info);
static int pcie_hp_check_hardware_existence(dev_info_t *dip, int dev_num,
    int func_num);
static int lsr_parse_string(lsr_string_value_t *sv, char *string,
    uint64_t *ret_value);
static int lsr_compose_string(lsr_string_value_t *array, uint64_t value,
    char **rstr);
static int get_user_lsr_req(ddi_hp_cn_change_state_arg_t *kargp,
    ddi_cb_lsr_t *lsr);
static int compose_pack_lsr_nvlist(ddi_cb_lsr_t *lsr, char **buf,
    size_t *packed_sz);
static void free_async_lsr_state_priv(ddi_hp_cn_change_state_arg_t *kargp);
static int pcie_hp_state_priv_match(dev_info_t *dip, ddi_cb_lsr_t *plsr);
static void pcie_hp_init_state_priv(dev_info_t *dip, ddi_cb_lsr_t *plsr);
static int pcie_hp_check_lsr_capability(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_suspend_children(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_suspend_dependents(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_suspend_device(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_suspend_branch(pcie_hp_port_info_t *port_info,
    ddi_hp_cn_change_state_arg_t *kargp, void *result);
static int pcie_hp_resume_children(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_resume_dependents(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_resume_device(dev_info_t *dip, ddi_cb_lsr_t *lsr);
static int pcie_hp_resume_branch(pcie_hp_port_info_t *port_info,
    ddi_hp_cn_change_state_arg_t *kargp, void *result);
static int pcie_hp_port_get_state_priv(pcie_hp_port_info_t *port_info,
    void *arg, ddi_hp_state_priv_t *rval);
static int pcie_hp_port_ops(dev_info_t *dip, char *cn_name, ddi_hp_op_t op,
    pcie_hp_port_info_t *port_info, void *arg, void *result);
static int pcie_hp_connector_ops(dev_info_t *dip, char *cn_name, ddi_hp_op_t op,
    void *arg, void *result);
static int pcie_hp_debug = 0;

/*
 * Global functions (called by other drivers/modules)
 */

/*
 * return description text for led state
 */
char *
pcie_led_state_text(pcie_hp_led_state_t state)
{
	switch (state) {
	case PCIE_HP_LED_ON:
		return (PCIEHPC_PROP_VALUE_ON);
	case PCIE_HP_LED_OFF:
		return (PCIEHPC_PROP_VALUE_OFF);
	case PCIE_HP_LED_BLINK:
	default:
		return (PCIEHPC_PROP_VALUE_BLINK);
	}
}

/*
 * return description text for slot condition
 */
char *
pcie_slot_condition_text(ap_condition_t condition)
{
	switch (condition) {
	case AP_COND_UNKNOWN:
		return (PCIEHPC_PROP_VALUE_UNKNOWN);
	case AP_COND_OK:
		return (PCIEHPC_PROP_VALUE_OK);
	case AP_COND_FAILING:
		return (PCIEHPC_PROP_VALUE_FAILING);
	case AP_COND_FAILED:
		return (PCIEHPC_PROP_VALUE_FAILED);
	case AP_COND_UNUSABLE:
		return (PCIEHPC_PROP_VALUE_UNUSABLE);
	default:
		return (PCIEHPC_PROP_VALUE_UNKNOWN);
	}
}

/*
 * routine to copy in a nvlist from userland
 */
int
pcie_copyin_nvlist(char *packed_buf, size_t packed_sz, nvlist_t **nvlp)
{
	int		ret = DDI_SUCCESS;
	char		*packed;
	nvlist_t	*dest = NULL;

	if (packed_buf == NULL || packed_sz == 0)
		return (DDI_EINVAL);

	/* copyin packed nvlist */
	if ((packed = kmem_alloc(packed_sz, KM_SLEEP)) == NULL)
		return (DDI_ENOMEM);

	if (copyin(packed_buf, packed, packed_sz) != 0) {
		cmn_err(CE_WARN, "pcie_copyin_nvlist: copyin failed.\n");
		ret = DDI_FAILURE;
		goto copyin_cleanup;
	}

	/* unpack packed nvlist */
	if ((ret = nvlist_unpack(packed, packed_sz, &dest, KM_SLEEP)) != 0) {
		cmn_err(CE_WARN, "pcie_copyin_nvlist: nvlist_unpack "
		    "failed with err %d\n", ret);
		switch (ret) {
		case EINVAL:
		case ENOTSUP:
			ret = DDI_EINVAL;
			goto copyin_cleanup;
		case ENOMEM:
			ret = DDI_ENOMEM;
			goto copyin_cleanup;
		default:
			ret = DDI_FAILURE;
			goto copyin_cleanup;
		}
	}
	*nvlp = dest;
copyin_cleanup:
	kmem_free(packed, packed_sz);
	return (ret);
}

/*
 * routine to copy out a nvlist to userland
 */
int
pcie_copyout_nvlist(nvlist_t *nvl, char *packed_buf, size_t *buf_sz)
{
	int	err = 0;
	char	*buf = NULL;
	size_t	packed_sz;

	if (nvl == NULL || packed_buf == NULL || buf_sz == NULL)
		return (DDI_EINVAL);

	/* pack nvlist, the library will allocate memory */
	if ((err = nvlist_pack(nvl, &buf, &packed_sz, NV_ENCODE_NATIVE, 0))
	    != 0) {
		cmn_err(CE_WARN, "pcie_copyout_nvlist: nvlist_pack "
		    "failed with err %d\n", err);
		switch (err) {
		case EINVAL:
		case ENOTSUP:
			return (DDI_EINVAL);
		case ENOMEM:
			return (DDI_ENOMEM);
		default:
			return (DDI_FAILURE);
		}
	}
	if (packed_sz > *buf_sz) {
		return (DDI_EINVAL);
	}

	/* copyout packed nvlist */
	if (copyout(buf, packed_buf, packed_sz) != 0) {
		cmn_err(CE_WARN, "pcie_copyout_nvlist: copyout " "failed.\n");
		kmem_free(buf, packed_sz);
		return (DDI_FAILURE);
	}

	*buf_sz = packed_sz;
	kmem_free(buf, packed_sz);
	return (DDI_SUCCESS);
}

/*
 * init bus_hp_op entry and init hotpluggable slots & virtual ports
 */
int
pcie_hp_init(dev_info_t *dip, caddr_t arg)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	int		ret = DDI_SUCCESS, count;
	dev_info_t	*cdip;

	if (PCIE_IS_PCIE_HOTPLUG_CAPABLE(bus_p)) {
		/* Init hotplug controller */
		ret = pciehpc_init(dip, arg);
	} else if (PCIE_IS_PCI_HOTPLUG_CAPABLE(bus_p)) {
		ret = pcishpc_init(dip);
	} else {
		/*
		 * VFs(Virtual functions) are configured later during boot
		 * and need to be allocated memory for VF BARS using
		 * ndi_ra_alloc.
		 * Hence call pci_resource_setup() even if the device is not
		 * hot plug capable.
		 */
		(void) pci_resource_setup(dip);
	}

	if (ret != DDI_SUCCESS) {
		PCIE_DBG("pcie_hp_init: initialize hotplug "
		    "controller failed with %d\n", ret);
		return (ret);
	}

	ndi_devi_enter(dip, &count);

	/* Create port for the first level children */
	cdip = ddi_get_child(dip);
	while (cdip != NULL) {
		if ((ret = pcie_hp_register_port(cdip, dip, NULL))
		    != DDI_SUCCESS) {
			/* stop and cleanup */
			break;
		}
		cdip = ddi_get_next_sibling(cdip);
	}
	ndi_devi_exit(dip, count);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_init: initialize virtual "
		    "hotplug port failed with %d\n", ret);
		(void) pcie_hp_uninit(dip);

		return (ret);
	}

	return (DDI_SUCCESS);
}

/*
 * uninit the hotpluggable slots and virtual ports
 */
int
pcie_hp_uninit(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	pcie_hp_unreg_port_t arg;

	/*
	 * Must set arg.rv to NDI_SUCCESS so that if there's no port
	 * under this dip, we still return success thus the bridge
	 * driver can be successfully detached.
	 *
	 * Note that during the probe PCI configurator calls
	 * ndi_devi_offline() to detach driver for a new probed bridge,
	 * so that it can reprogram the resources for the bridge,
	 * ndi_devi_offline() calls into pcieb_detach() which in turn
	 * calls into this function. In this case there are no ports
	 * created under a new probe bridge dip, as ports are only
	 * created after the configurator finishing probing, thus the
	 * ndi_hp_walk_cn() will see no ports when this is called
	 * from the PCI configurtor.
	 */
	arg.nexus_dip = dip;
	arg.connector_num = DDI_HP_CN_NUM_NONE;
	arg.rv = NDI_SUCCESS;

	/* tear down all virtual hotplug handles */
	ndi_hp_walk_cn(dip, pcie_hp_unregister_ports_cb, &arg);

	if (arg.rv != NDI_SUCCESS)
		return (DDI_FAILURE);

	if (PCIE_IS_PCIE_HOTPLUG_ENABLED(bus_p))
		(void) pciehpc_uninit(dip);
	else if (PCIE_IS_PCI_HOTPLUG_ENABLED(bus_p))
		(void) pcishpc_uninit(dip);

	return (DDI_SUCCESS);
}

/*
 * interrupt handler
 */
int
pcie_hp_intr(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	int		ret = DDI_INTR_UNCLAIMED;

	if (PCIE_IS_PCIE_HOTPLUG_ENABLED(bus_p))
		ret = pciehpc_intr(dip);
	else if (PCIE_IS_PCI_HOTPLUG_ENABLED(bus_p))
		ret = pcishpc_intr(dip);

	return (ret);
}

/*
 * Probe the given PCIe/PCI Hotplug Connection (CN).
 */
/*ARGSUSED*/
int
pcie_hp_probe(pcie_hp_slot_t *slot_p)
{
	pcie_hp_ctrl_t	*ctrl_p = slot_p->hs_ctrl;
	dev_info_t	*dip = ctrl_p->hc_dip;

	/*
	 * Call the configurator to probe a given PCI hotplug
	 * Hotplug Connection (CN).
	 */
	if (pcicfg_configure(dip, slot_p->hs_device_num, PCICFG_ALL_FUNC, 0)
	    != PCICFG_SUCCESS) {
		PCIE_DBG("pcie_hp_probe() failed\n");
		return (DDI_FAILURE);
	}
	slot_p->hs_condition = AP_COND_OK;
	pcie_hp_create_occupant_props(dip, makedevice(ddi_driver_major(dip),
	    slot_p->hs_minor), slot_p->hs_device_num);

	/*
	 * Create ports for the newly probed devices.
	 * Note, this is only for the first level children because the
	 * descendants' ports will be created during bridge driver attach.
	 */
	return (pcie_hp_register_ports_for_dev(dip, slot_p->hs_device_num));
}

/*
 * Unprobe the given PCIe/PCI Hotplug Connection (CN):
 *	1. remove all child device nodes
 *	2. unregister all dependent ports
 */
/*ARGSUSED*/
int
pcie_hp_unprobe(pcie_hp_slot_t *slot_p)
{
	pcie_hp_ctrl_t	*ctrl_p = slot_p->hs_ctrl;
	dev_info_t	*dip = ctrl_p->hc_dip;
	pcie_hp_unreg_port_t arg;

	/*
	 * Call the configurator to unprobe a given PCI hotplug
	 * Hotplug Connection (CN).
	 */
	if (pcicfg_unconfigure(dip, slot_p->hs_device_num, PCICFG_ALL_FUNC, 0)
	    != PCICFG_SUCCESS) {
		PCIE_DBG("pcie_hp_unprobe() failed\n");
		return (DDI_FAILURE);
	}
	slot_p->hs_condition = AP_COND_UNKNOWN;
	pcie_hp_delete_occupant_props(dip, makedevice(ddi_driver_major(dip),
	    slot_p->hs_minor));

	/*
	 * Remove ports for the unprobed devices.
	 * Note, this is only for the first level children because the
	 * descendants' ports were already removed during bridge driver dettach.
	 */
	arg.nexus_dip = dip;
	arg.connector_num = slot_p->hs_info.cn_num;
	arg.rv = NDI_SUCCESS;
	ndi_hp_walk_cn(dip, pcie_hp_unregister_ports_cb, &arg);

	return (arg.rv == NDI_SUCCESS) ? (DDI_SUCCESS) : (DDI_FAILURE);
}

/* Read-only probe: no hardware register programming. */
int
pcie_read_only_probe(dev_info_t *dip, char *cn_name, dev_info_t **pcdip)
{
	long dev, func;
	int ret;
	char *sp;
	dev_info_t *cdip;

	*pcdip = NULL;
	/*
	 * Parse the string of a pci Port name and get the device number
	 * and function number.
	 */
	if (ddi_strtol(cn_name + 4, &sp, 10, &dev) != 0)
		return (DDI_EINVAL);
	if (ddi_strtol(sp + 1, NULL, 10, &func) != 0)
		return (DDI_EINVAL);

	ret = pcicfg_configure(dip, (int)dev, (int)func,
	    PCICFG_FLAG_READ_ONLY);
	if (ret == PCICFG_SUCCESS) {
		cdip = pcie_hp_devi_find(dip, (int)dev, (int)func);
		*pcdip = cdip;
	}
	return (ret);
}

/* Read-only unprobe: no hardware register programming. */
int
pcie_read_only_unprobe(dev_info_t *dip, char *cn_name)
{
	long dev, func;
	int ret;
	char *sp;

	/*
	 * Parse the string of a pci Port name and get the device number
	 * and function number.
	 */
	if (ddi_strtol(cn_name + 4, &sp, 10, &dev) != 0)
		return (DDI_EINVAL);
	if (ddi_strtol(sp + 1, NULL, 10, &func) != 0)
		return (DDI_EINVAL);

	ret = pcicfg_unconfigure(dip, (int)dev, (int)func,
	    PCICFG_FLAG_READ_ONLY);

	return (ret);
}

/* Control structure used to find a device in the devinfo tree */
struct pcie_hp_find_ctrl {
	uint_t		device;
	uint_t		function;
	dev_info_t	*dip;
};

/*
 * find a devinfo node with specified device and function number
 * in the device tree under 'dip'
 */
dev_info_t *
pcie_hp_devi_find(dev_info_t *dip, uint_t device, uint_t function)
{
	struct pcie_hp_find_ctrl	ctrl;
	int				count;

	ctrl.device = device;
	ctrl.function = function;
	ctrl.dip = NULL;

	ndi_devi_enter(dip, &count);
	ddi_walk_devs(ddi_get_child(dip), pcie_hp_match_dev_func,
	    (void *)&ctrl);
	ndi_devi_exit(dip, count);

	return (ctrl.dip);
}

/*
 * routine to create 'pci-occupant' property for a hotplug slot
 */
void
pcie_hp_create_occupant_props(dev_info_t *dip, dev_t dev, int pci_dev)
{
	pcie_bus_t		*bus_p = PCIE_DIP2BUS(dip);
	pcie_hp_ctrl_t		*ctrl_p = (pcie_hp_ctrl_t *)bus_p->bus_hp_ctrl;
	pcie_hp_slot_t		*slotp;
	pcie_hp_cn_cfg_t	cn_cfg;
	pcie_hp_occupant_info_t	*occupant;
	int			circular, i;

	ndi_devi_enter(dip, &circular);

	if (PCIE_IS_PCIE_HOTPLUG_ENABLED(bus_p)) {
		slotp = (ctrl_p && (pci_dev == 0)) ?
		    ctrl_p->hc_slots[pci_dev] : NULL;
	} else if (PCIE_IS_PCI_HOTPLUG_ENABLED(bus_p)) {
		if (ctrl_p) {
			int	slot_num;

			slot_num = (ctrl_p->hc_device_increases) ?
			    (pci_dev - ctrl_p->hc_device_start) :
			    (pci_dev + ctrl_p->hc_device_start);

			slotp = ctrl_p->hc_slots[slot_num];
		} else {
			slotp = NULL;
		}
	}

	if (slotp == NULL)
		return;

	occupant = kmem_alloc(sizeof (pcie_hp_occupant_info_t), KM_SLEEP);
	occupant->i = 0;

	cn_cfg.flag = B_FALSE;
	cn_cfg.rv = NDI_SUCCESS;
	cn_cfg.dip = NULL;
	cn_cfg.slotp = (void *)slotp;
	cn_cfg.cn_private = (void *)occupant;

	ddi_walk_devs(ddi_get_child(dip), pcie_hp_list_occupants,
	    (void *)&cn_cfg);

	if (occupant->i == 0) {
		/* no occupants right now, need to create stub property */
		char *c[] = { "" };
		(void) ddi_prop_update_string_array(dev, dip, "pci-occupant",
		    c, 1);
	} else {
		(void) ddi_prop_update_string_array(dev, dip, "pci-occupant",
		    occupant->id, occupant->i);
	}

	for (i = 0; i < occupant->i; i++)
		kmem_free(occupant->id[i], sizeof (char[MAXPATHLEN]));

	kmem_free(occupant, sizeof (pcie_hp_occupant_info_t));

	ndi_devi_exit(dip, circular);
}

/*
 * routine to remove 'pci-occupant' property for a hotplug slot
 */
void
pcie_hp_delete_occupant_props(dev_info_t *dip, dev_t dev)
{
	(void) ddi_prop_remove(dev, dip, "pci-occupant");
}

/*
 * general code to create a minor node, called from hotplug controller
 * drivers.
 */
int
pcie_create_minor_node(pcie_hp_ctrl_t *ctrl_p, int slot)
{
	dev_info_t		*dip = ctrl_p->hc_dip;
	pcie_hp_slot_t		*slot_p = ctrl_p->hc_slots[slot];
	ddi_hp_cn_info_t	*info_p = &slot_p->hs_info;

	if (ddi_create_minor_node(dip, info_p->cn_name,
	    S_IFCHR, slot_p->hs_minor,
	    DDI_NT_PCI_ATTACHMENT_POINT, 0) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	(void) ddi_prop_update_int(DDI_DEV_T_NONE,
	    dip, "ap-names", 1 << slot_p->hs_device_num);

	return (DDI_SUCCESS);
}

/*
 * general code to remove a minor node, called from hotplug controller
 * drivers.
 */
void
pcie_remove_minor_node(pcie_hp_ctrl_t *ctrl_p, int slot)
{
	ddi_remove_minor_node(ctrl_p->hc_dip,
	    ctrl_p->hc_slots[slot]->hs_info.cn_name);
}

/*
 * Local functions (called within this file)
 */

/*
 * Register ports for all the children with device number device_num
 */
static int
pcie_hp_register_ports_for_dev(dev_info_t *dip, int device_num)
{
	dev_info_t	*cdip;
	int		rv;

	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		if (pcie_hp_match_dev(cdip, device_num)) {
			/*
			 * Found the newly probed device under the
			 * current slot. Register a port for it.
			 */
			if ((rv = pcie_hp_register_port(cdip, dip, NULL))
			    != DDI_SUCCESS)
				return (rv);
		} else {
			continue;
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Unregister ports of a pci bridge dip, get called from ndi_hp_walk_cn()
 *
 * If connector_num is specified, then unregister the slot's dependent ports
 * only; Otherwise, unregister all ports of a pci bridge dip.
 */
static int
pcie_hp_unregister_ports_cb(ddi_hp_cn_info_t *info, void *arg)
{
	pcie_hp_unreg_port_t *unreg_arg = (pcie_hp_unreg_port_t *)arg;
	dev_info_t *dip = unreg_arg->nexus_dip;
	int rv = NDI_SUCCESS;

	if (info->cn_type & DDI_HP_CN_TYPE_CONNECTOR_MASK) {
		unreg_arg->rv = rv;
		return (DDI_WALK_CONTINUE);
	}

	if (unreg_arg->connector_num != DDI_HP_CN_NUM_NONE) {
		/* Unregister ports for all unprobed devices under a slot. */
		if (unreg_arg->connector_num == info->cn_num_dpd_on) {

			rv = ndi_hp_unregister(dip, info->cn_name);
		}
	} else {

		/* Unregister all ports of a pci bridge dip. */
		rv = ndi_hp_unregister(dip, info->cn_name);
	}

	unreg_arg->rv = rv;
	if (rv == NDI_SUCCESS)
		return (DDI_WALK_CONTINUE);
	else
		return (DDI_WALK_TERMINATE);
}

/*
 * Find a port according to cn_name and get the port's info.
 */
static int
pcie_hp_get_port_info(ddi_hp_cn_info_t *info, void *arg)
{
	pcie_hp_port_info_t *port_info = (pcie_hp_port_info_t *)arg;

	if (strcmp(info->cn_name, port_info->cn_name) == 0) {
		/* Matched. */
		port_info->cn_child = info->cn_child;
		port_info->cn_type = info->cn_type;
		port_info->cn_state = info->cn_state;
		port_info->rv = DDI_SUCCESS;

		return (DDI_WALK_TERMINATE);
	}

	return (DDI_WALK_CONTINUE);
}

/*
 * Find the connection number that the specified device depends on.
 */
static int
pcie_find_cn_num_dpd_on(dev_info_t *dip, dev_info_t *pdip, int dev_num)
{
	pcie_bus_t	*bus_p;
	pcie_hp_ctrl_t	*ctrl;
	pcie_hp_slot_t	*slot = NULL;

	/*
	 * If the device is a VF, then the pdip is its PF.
	 * Compute the PF's connection number in this case.
	 */
	if (dip != NULL) {
		bus_p =  PCIE_DIP2UPBUS(dip);
		if ((bus_p) && (bus_p->bus_func_type == FUNC_TYPE_VF)) {
			pcie_req_id_t	bdf;
			int		pf_dev_num, pf_func_num;

			if (pcie_get_bdf_from_dip(pdip, &bdf) != DDI_SUCCESS)
				return (DDI_HP_CN_NUM_NONE);

			pf_dev_num = (bdf & (PCI_REG_DEV_M >> 8)) >> 3;
			pf_func_num = bdf & (PCI_REG_FUNC_M >> 8);

			return ((pf_dev_num << 8) | pf_func_num);
		}
	}

	/*
	 * If the device is not a VF, then the pdip is its parent bus.
	 * Search for a physical slot, to use its connection number.
	 */
	bus_p = PCIE_DIP2BUS(pdip);
	ctrl = PCIE_GET_HP_CTRL(pdip);
	if ((bus_p == NULL) || (ctrl == NULL))
		return (DDI_HP_CN_NUM_NONE);
	if (PCIE_IS_PCIE_HOTPLUG_CAPABLE(bus_p)) {
		/* PCIe has only one slot */
		if (dev_num == 0)
			slot = ctrl->hc_slots[0];
	} else {
		for (int i = 0; i < ctrl->hc_num_slots_impl; i++)
			if (ctrl->hc_slots[i]->hs_device_num == dev_num) {
				slot = ctrl->hc_slots[i];
				break;
			}
	}
	if (slot)
		return (slot->hs_info.cn_num);

	return (DDI_HP_CN_NUM_NONE);
}

/*
 * setup slot name/slot-number info for the port which is being registered.
 */
static int
pcie_hp_create_port_name_num(dev_info_t *dip, dev_info_t *pdip,
    ddi_hp_cn_info_t *cn_info)
{
	int		dev_num, func_num, name_len;
	char		tmp[PCIE_HP_DEV_FUNC_NUM_STRING_LEN];

	if (pcie_get_dev_func_num(dip, &dev_num, &func_num) != DDI_SUCCESS)
		return (DDI_FAILURE);

	/*
	 * The string length of dev_num and func_num must be no longer than 8
	 * including the string end mark. (With ARI case considered, e.g.,
	 * dev_num=0x0, func_num=0xff.)
	 */
	(void) snprintf(tmp, PCIE_HP_DEV_FUNC_NUM_STRING_LEN, "%x%x",
	    dev_num, func_num);
	/*
	 * Calculate the length of cn_name.
	 * The format of pci port name is: pci.d,f
	 * d stands for dev_num, f stands for func_num. So the length of the
	 * name string can be calculated as following.
	 */
	name_len = strlen(tmp) + PCIE_HP_PORT_NAME_STRING_LEN + 1;

	cn_info->cn_name = (char *)kmem_zalloc(name_len, KM_SLEEP);
	(void) snprintf(cn_info->cn_name, name_len, "pci.%x,%x",
	    dev_num, func_num);
	cn_info->cn_num = (dev_num << 8) | func_num;
	cn_info->cn_num_dpd_on = pcie_find_cn_num_dpd_on(dip, pdip, dev_num);

	return (DDI_SUCCESS);
}

/*
 * Extract device and function number from port name, whose format is
 * something like 'pci.1,0'
 */
static int
pcie_hp_get_df_from_port_name(char *cn_name, int *dev_num, int *func_num)
{
	int name_len, ret;
	long d, f;
	char *sp;

	/* some checks for the input name */
	name_len = strlen(cn_name);
	if ((name_len <= PCIE_HP_PORT_NAME_STRING_LEN) ||
	    (name_len > (PCIE_HP_PORT_NAME_STRING_LEN +
	    PCIE_HP_DEV_FUNC_NUM_STRING_LEN - 1)) ||
	    (strncmp("pci.", cn_name, 4) != 0)) {
		return (DDI_EINVAL);
	}
	ret = ddi_strtol(cn_name + 4, &sp, 16, &d);
	if (ret != DDI_SUCCESS)
		return (ret);

	if (strncmp(",", sp, 1) != 0)
		return (DDI_EINVAL);

	ret = ddi_strtol(sp + 1, NULL, 16, &f);
	if (ret != DDI_SUCCESS)
		return (ret);
	*dev_num = (int)d;
	*func_num = (int)f;

	return (ret);
}

/*
 * Check/copy cn_name and set connection numbers.
 * If it is a valid name, then setup cn_info for the newly created port.
 */
static int
pcie_hp_setup_port_name_num(dev_info_t *pdip, char *cn_name,
    ddi_hp_cn_info_t *cn_info)
{
	int dev_num, func_num, ret;

	if ((ret = pcie_hp_get_df_from_port_name(cn_name, &dev_num, &func_num))
	    != DDI_SUCCESS)
		return (ret);

	if (pcie_hp_check_hardware_existence(pdip, dev_num, func_num) ==
	    DDI_SUCCESS) {
		cn_info->cn_state = DDI_HP_CN_STATE_PRESENT;
	} else {
		cn_info->cn_state = DDI_HP_CN_STATE_EMPTY;
	}

	cn_info->cn_name = ddi_strdup(cn_name, KM_SLEEP);
	cn_info->cn_num = (dev_num << 8) | func_num;
	cn_info->cn_num_dpd_on = pcie_find_cn_num_dpd_on(NULL, pdip, dev_num);

	return (DDI_SUCCESS);
}

static int
ndi2ddi(int n)
{
	int ret;

	switch (n) {
	case NDI_SUCCESS:
		ret = DDI_SUCCESS;
		break;
	case NDI_NOMEM:
		ret = DDI_ENOMEM;
		break;
	case NDI_BUSY:
		ret = DDI_EBUSY;
		break;
	case NDI_EINVAL:
		ret = DDI_EINVAL;
		break;
	case NDI_ENOTSUP:
		ret = DDI_ENOTSUP;
		break;
	case NDI_FAILURE:
	default:
		ret = DDI_FAILURE;
		break;
	}
	return (ret);
}

/*
 * Common routine to create and register a new port
 *
 * Create an empty port if dip is NULL, and cn_name needs to be specified in
 * this case. Otherwise, create a port mapping to the specified dip, and cn_name
 * is not needed in this case.
 */
int
pcie_hp_register_port(dev_info_t *dip, dev_info_t *pdip, char *cn_name)
{
	ddi_hp_cn_info_t	cn_info;
	int			ret;
	pcie_bus_t		*bus_p = NULL;

	ASSERT((dip == NULL) != (cn_name == NULL));

	if (dip != NULL)
		ret = pcie_hp_create_port_name_num(dip, pdip, &cn_info);
	else
		ret = pcie_hp_setup_port_name_num(pdip, cn_name, &cn_info);

	if (ret != DDI_SUCCESS)
		return (ret);

	cn_info.cn_child = dip;
	cn_info.cn_type = DDI_HP_CN_TYPE_PORT_PCI;
	cn_info.cn_type_str = DDI_HP_CN_TYPE_STR_PORT_PCI;
	if (dip != NULL)
		bus_p =  PCIE_DIP2UPBUS(dip);
	if ((bus_p) && (bus_p->bus_func_type == FUNC_TYPE_VF)) {
		cn_info.cn_type = DDI_HP_CN_TYPE_PORT_IOV_VF;
		cn_info.cn_type_str = DDI_HP_CN_TYPE_STR_PORT_IOV_VF;
	}
	if ((bus_p) && (bus_p->sriov_cap_ptr != 0)) {
		cn_info.cn_type = DDI_HP_CN_TYPE_PORT_IOV_PF;
		cn_info.cn_type_str = DDI_HP_CN_TYPE_STR_PORT_IOV_PF;
	}
#ifdef	DEBUG
		if (pcie_hp_debug) {
			char *path;
			path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) ddi_pathname(dip, path);
			cmn_err(CE_NOTE, "pcie_hp_register_port: path %s\n",
			    path);
			kmem_free(path, MAXPATHLEN);
		}
#endif

	/*
	 * If the device is a VF, then pdip referred to its PF.
	 * Update pdip to refer to the parent bus in this case.
	 */
	if (cn_info.cn_type == DDI_HP_CN_TYPE_PORT_IOV_VF)
		pdip = ddi_get_parent(dip);

	ret = ndi_hp_register(pdip, &cn_info);

	kmem_free(cn_info.cn_name, strlen(cn_info.cn_name) + 1);

	return (ndi2ddi(ret));
}

int
pcie_hp_unregister_port(dev_info_t *dip, dev_info_t *pdip, char *cn_name)
{
	ddi_hp_cn_info_t	*cn_info;
	int			ret;
	pcie_bus_t		*bus_p = NULL;

	ASSERT((dip == NULL) != (cn_name == NULL));
	cn_info = kmem_zalloc(sizeof (ddi_hp_cn_info_t), KM_SLEEP);
	if (dip != NULL)
		ret = pcie_hp_create_port_name_num(dip, pdip, cn_info);
	else
		ret = pcie_hp_setup_port_name_num(pdip, cn_name, cn_info);

	if (ret != DDI_SUCCESS) {
		kmem_free(cn_info, sizeof (ddi_hp_cn_info_t));
		return (ret);
	}

	cn_info->cn_child = dip;
	cn_info->cn_type = DDI_HP_CN_TYPE_PORT_PCI;
	cn_info->cn_type_str = DDI_HP_CN_TYPE_STR_PORT_PCI;
	if (dip != NULL)
		bus_p =  PCIE_DIP2UPBUS(dip);
	if ((bus_p) && (bus_p->bus_func_type == FUNC_TYPE_VF)) {
		cn_info->cn_type = DDI_HP_CN_TYPE_PORT_IOV_VF;
		cn_info->cn_type_str = DDI_HP_CN_TYPE_STR_PORT_IOV_VF;
	}
	if ((bus_p) && (bus_p->sriov_cap_ptr != 0)) {
		cn_info->cn_type = DDI_HP_CN_TYPE_PORT_IOV_PF;
		cn_info->cn_type_str = DDI_HP_CN_TYPE_STR_PORT_IOV_PF;
	}
#ifdef	DEBUG
		if (pcie_hp_debug) {
			char *path;
			path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			(void) ddi_pathname(dip, path);
			cmn_err(CE_NOTE, "pcie_hp_unregister_port: path %s\n",
			    path);
			kmem_free(path, MAXPATHLEN);
		}
#endif
	ret = ndi_hp_unregister(pdip, cn_info->cn_name);

	kmem_free(cn_info->cn_name, strlen(cn_info->cn_name) + 1);
	kmem_free(cn_info, sizeof (ddi_hp_cn_info_t));

	return (ndi2ddi(ret));
}

/* Check if there is a piece of hardware exist corresponding to the cn_name */
static int
pcie_hp_check_hardware_existence(dev_info_t *dip, int dev_num, int func_num)
{

	/*
	 * VHPTODO:
	 * According to device and function number, check if there is a hardware
	 * device exists. Currently, this function can not be reached before
	 * we enable state transition to or from "Port-Empty" or "Port-Present"
	 * states. When the pci device type project is integrated, we are going
	 * to call the pci config space access interfaces introduced by it.
	 */
	_NOTE(ARGUNUSED(dip, dev_num, func_num));

	return (DDI_SUCCESS);
}

/*
 * parse string into a value according to a string-value array.
 *
 * "dma+pio+intr" -->
 *     (DDI_CB_LSR_ACT_DMA | DDI_CB_LSR_ACT_PIO | DDI_CB_LSR_ACT_INTR)
 */
static int
lsr_parse_string(lsr_string_value_t *sv, char *str, uint64_t *ret_value)
{
	uint64_t val = 0;
	char *p;

	if (ret_value == NULL || str == NULL)
		return (DDI_EINVAL);

	*ret_value = 0;

	for (; *str; str = p + 1) {
		lsr_string_value_t *psv;

		p = strchr(str, '+');
		if (p != NULL)
			*p = '\0';

		for (psv = sv; psv->string != NULL; psv++) {
			if (strcasecmp(str, psv->string) == 0) {
				val |= psv->value;
				break;
			}
		}
		if (psv->string == NULL) {
			cmn_err(CE_WARN, "invalid lsr string: %s\n", str);
			return (DDI_EINVAL);
		}

		if (p == NULL)
			break;
	}

	*ret_value = val;

	return (0);
}

/*
 * Compose a string from a value according to a string-value array
 *
 * (DDI_CB_LSR_ACT_DMA | DDI_CB_LSR_ACT_PIO | DDI_CB_LSR_ACT_INTR)
 *   -->
 *  "dma+pio+intr"
 */
static int
lsr_compose_string(lsr_string_value_t *array, uint64_t value, char **rstr)
{
	lsr_string_value_t *psv;
	char *rbuf, *p;
	int len = 0;

	*rstr = NULL;

	for (psv = array; psv->value != 0; psv++) {
		if (value & psv->value)
			len += strlen(psv->string) + 1;
	}
	if (len == 0) /* value is 0 */
		return (0);

	rbuf = kmem_alloc(len, KM_SLEEP);

	p = rbuf;
	for (psv = array; psv->value != 0; psv++) {
		if (value & psv->value) {
			len = strlen(psv->string);
			bcopy(psv->string, p, len);
			p += len;
			*p++ = '+';
		}
	}
	p--;
	*p = '\0';

	*rstr = rbuf;

	return (0);
}

/*
 * Copyin the pcie bus specific state private data from user land
 *
 * state private info from user land is an string nvlist.
 */
static int
get_user_lsr_req(ddi_hp_cn_change_state_arg_t *kargp, ddi_cb_lsr_t *lsr)
{
	ddi_hp_state_priv_t state_priv;
#ifdef _SYSCALL32_IMPL
	ddi_hp_state_priv32_t state_priv32;
#endif
	nvlist_t *lsr_list;
	nvpair_t *lsr_pair;
	char *activities_str = NULL;
	char *impacts_str = NULL;
	char *reason_str = NULL;
	int ret;

	(void) memset(lsr, 0, sizeof (ddi_cb_lsr_t));

	if (get_udatamodel() == DATAMODEL_NATIVE) {
		if (copyin(kargp->target_state.state_priv, &state_priv,
		    sizeof (ddi_hp_state_priv_t)) < 0) {
			cmn_err(CE_WARN, "get_user_lsr_req: copyin failed.\n");
			return (DDI_EINVAL);
		}
	}
#ifdef _SYSCALL32_IMPL
	else {
		bzero(&state_priv, sizeof (state_priv));
		if (copyin(kargp->target_state.state_priv, &state_priv32,
		    sizeof (ddi_hp_state_priv32_t))) {
			cmn_err(CE_WARN, "get_user_lsr_req: "
			    "32-bit copyin failed.\n");
			return (DDI_EINVAL);
		}
		state_priv.nvlist_buf
		    = (char *)(uintptr_t)state_priv32.nvlist_buf;
		state_priv.buf_size = state_priv32.buf_size;
	}
#endif

	ret = pcie_copyin_nvlist(state_priv.nvlist_buf, state_priv.buf_size,
	    &lsr_list);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "get_user_lsr_req: "
		    "pcie_copyin_nvlist failed.\n");
		return (ret);
	}

	lsr_pair = NULL;
	while (lsr_pair = nvlist_next_nvpair(lsr_list, lsr_pair)) {
		char *name = nvpair_name(lsr_pair);

		if (strcasecmp(name, PCIE_HP_LSR_ACTIVITIES) == 0) {
			ret = nvpair_value_string(lsr_pair, &activities_str);
			if (ret != 0) {
				cmn_err(CE_WARN, "get_user_lsr_req: "
				    "can't get LSR 'activities' string\n");
				goto error_out_free;
			}
			ret = lsr_parse_string(act_array, activities_str,
			    &lsr->activities);
			if (ret != 0) {
				cmn_err(CE_WARN, "get_user_lsr_req: "
				    "invalid LSR 'activities' string\n");
				goto error_out_free;
			}
		} else if (strcmp(name, PCIE_HP_LSR_IMPACTS) == 0) {
			ret = nvpair_value_string(lsr_pair, &impacts_str);
			if (ret != 0) {
				cmn_err(CE_WARN, "get_user_lsr_req: "
				    "can't get LSR 'impacts' string\n");
				goto error_out_free;
			}
			ret = lsr_parse_string(imp_array, impacts_str,
			    &lsr->impacts);
			if (ret != 0) {
				cmn_err(CE_WARN, "get_user_lsr_req: "
				    "invalid LSR 'impacts' string\n");
				goto error_out_free;
			}
		} else if (strcmp(name, PCIE_HP_LSR_REASON) == 0) {
			ret = nvpair_value_string(lsr_pair, &reason_str);
			if (ret != 0) {
				cmn_err(CE_WARN, "get_user_lsr_req: "
				    "can't get LSR 'reason' string\n");
				goto error_out_free;
			}
			lsr->reason = ddi_strdup(reason_str, KM_SLEEP);
		} else {
			cmn_err(CE_WARN, "get_user_lsr_req: "
			    "invalid LSR string\n");
			ret = DDI_EINVAL;
			goto error_out_free;
		}
	}

	if (lsr->activities == 0) {
		cmn_err(CE_WARN, "get_user_lsr_req: "
		    "LSR 'activities' must be specified\n");
		ret = DDI_EINVAL;
		goto error_out_free;
	}

	ret = 0;
error_out_free:
	nvlist_free(lsr_list);
	return (ret);
}

/*
 * Make and pack nvlist from lsr, nvlist buf will be allocated and returned
 * in *buf, nvlist buf size returned in *packed_sz
 */
static int
compose_pack_lsr_nvlist(ddi_cb_lsr_t *lsr, char **buf, size_t *packed_sz)
{
	nvlist_t *lsr_rlist;
	char *str;
	int err;
	int ret;

	if (lsr->activities == 0) {
		cmn_err(CE_WARN, "compose_pack_lsr_nvlist: no activities\n");
		return (DDI_EINVAL);
	}

	if (nvlist_alloc(&lsr_rlist, NV_UNIQUE_NAME, KM_SLEEP))
		return (DDI_ENOMEM);

	ret = lsr_compose_string(act_array, lsr->activities, &str);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "compose_pack_lsr_nvlist: "
		    "lsr_compose_string(act) failed\n");
		goto clean_nvlist;
	}
	if (str == NULL) {
		ret = DDI_EINVAL;
		goto clean_nvlist;
	}
	if (nvlist_add_string(lsr_rlist, PCIE_HP_LSR_ACTIVITIES, str) != 0) {
		ret = DDI_FAILURE;
		goto clean_str;
	}
	kmem_free(str, strlen(str) + 1);

	if (lsr->impacts != 0) {
		ret = lsr_compose_string(imp_array, lsr->impacts, &str);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "compose_pack_lsr_nvlist: "
			    "lsr_compose_string(imp) failed\n");
			goto clean_nvlist;
		}
		if (str != NULL) {
			if (nvlist_add_string(lsr_rlist, PCIE_HP_LSR_IMPACTS,
			    str) != 0) {
				ret = DDI_FAILURE;
				goto clean_str;
			}
			kmem_free(str, strlen(str) + 1);
		}
	}

	if (lsr->reason) {
		if (nvlist_add_string(lsr_rlist, PCIE_HP_LSR_REASON,
		    lsr->reason) != 0) {
			ret = DDI_FAILURE;
			goto clean_nvlist;
		}
	}

	/* pack nvlist, the library will allocate memory */
	*buf = NULL;
	*packed_sz = 0;
	if ((err = nvlist_pack(lsr_rlist, buf, packed_sz, NV_ENCODE_NATIVE,
	    KM_SLEEP)) != 0) {
		cmn_err(CE_WARN, "compose_pack_lsr_nvlist: nvlist_pack "
		    "failed with err %d\n", err);
		switch (err) {
		case EINVAL:
		case ENOTSUP:
			ret = DDI_EINVAL;
			break;
		case ENOMEM:
			ret = DDI_ENOMEM;
			break;
		default:
			ret = DDI_FAILURE;
			break;
		}
		goto clean_nvlist;
	}

	nvlist_free(lsr_rlist);

	return (DDI_SUCCESS);
clean_str:
	kmem_free(str, strlen(str) + 1);
clean_nvlist:
	nvlist_free(lsr_rlist);
	return (ret);
}

/*
 * For async Suspend/Resume callers, the state private info must be free-ed
 * by the bus nexus driver, since they may no longer be alive when the
 * Suspend/Resume command is processed.
 *
 * See ndi_hp_state_change_req()
 */
static void
free_async_lsr_state_priv(ddi_hp_cn_change_state_arg_t *kargp)
{
	ddi_cb_lsr_t *lsr;

	if (kargp->type != ARG_PRIV_KERNEL_ASYNC)
		return;

	lsr = (ddi_cb_lsr_t *)kargp->target_state.state_priv;
	if (lsr) {
		if (lsr->reason)
			kmem_free(lsr->reason, strlen(lsr->reason) + 1);
		kmem_free(lsr, sizeof (ddi_cb_lsr_t));
	}
}

/*
 * Check *plsr with the LSR state private info stored in dip
 *
 * Live Resume command should have the same LSR state private info with
 * previous Live Suspend command
 */
static int
pcie_hp_state_priv_match(dev_info_t *dip, ddi_cb_lsr_t *plsr)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	ddi_cb_lsr_t	*cur_plsr = &bus_p->bus_hp_state_priv;

	return (cur_plsr->activities == plsr->activities &&
	    cur_plsr->impacts == plsr->impacts);
}

/*
 * set state priv info, remove old one if exists
 */
static void
pcie_hp_init_state_priv(dev_info_t *dip, ddi_cb_lsr_t *plsr)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	ddi_cb_lsr_t	*cur_plsr = &bus_p->bus_hp_state_priv;

	if (cur_plsr->reason) {
		kmem_free(cur_plsr->reason, strlen(cur_plsr->reason) + 1);
		cur_plsr->reason = NULL;
	}

	cur_plsr->activities = plsr->activities;
	cur_plsr->impacts = plsr->impacts;
	if (plsr->reason)
		cur_plsr->reason = ddi_strdup(plsr->reason, KM_SLEEP);
}

/*
 * Remove state priv info, called when resuming, also called in pcie_fini_bus(),
 * since the device may go to offline from suspended state directly.
 */
void
pcie_hp_fini_state_priv(dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	ddi_cb_lsr_t	*cur_plsr = (ddi_cb_lsr_t *)&bus_p->bus_hp_state_priv;

	if (cur_plsr->reason) {
		kmem_free(cur_plsr->reason, strlen(cur_plsr->reason) + 1);
		cur_plsr->reason = NULL;
	}
}

/*
 * Check if the device support LSR activities/impacts specified in *lsr
 */
static int
pcie_hp_check_lsr_capability(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	ddi_cb_t *cb_p;
	ddi_cb_lsr_t cap;
	int ret;

	cb_p = DEVI(dip)->devi_cb_p;
	ASSERT(cb_p);

	ret = cb_p->cb_func(dip, DDI_CB_LSR_QUERY_CAPABILITY, (void *)&cap,
	    cb_p->cb_arg1, cb_p->cb_arg2);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_check_lsr_capability: "
		    "%p:%s%d callback failed\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (ret);
	}

	if (((cap.activities & lsr->activities) == lsr->activities) &&
	    ((cap.impacts & lsr->impacts) == lsr->impacts))
		return (DDI_SUCCESS);

	cmn_err(CE_WARN, "%p:%s%d doesn't support "
	    "specified LSR activities/impacts\n", (void *)dip,
	    ddi_driver_name(dip), ddi_get_instance(dip));

	return (DDI_ENOTSUP);
}

/*
 * Recursively suspend child devices under *dip.
 *
 * We will skip all the VFs when iterating the children, PF will take
 * care of them.
 *
 * If any descendants failed to suspend, we will try to resume all the
 * previously suspended descendants.
 */
static int
pcie_hp_suspend_children(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	dev_info_t *cdip;
	dev_info_t *failed_cdip;
	int circ;
	int ret;

	if (!pcie_is_pci_nexus(dip))
		return (DDI_SUCCESS);

	ndi_devi_enter(dip, &circ);

	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		pcie_bus_t *bus_p;

		if ((i_ddi_node_state(cdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(cdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(cdip);
		/* VF will be suspended by PF */
		if (PCIE_IS_VF(bus_p))
			continue;

		ret = pcie_hp_suspend_device(cdip, lsr);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pcie_hp_suspend_children: "
			    "pcie_hp_suspend_device() failed\n");
			failed_cdip = cdip;
			goto error_resume;
		}
	}

	ndi_devi_exit(dip, circ);

	return (DDI_SUCCESS);
error_resume:
	/* only need to resume cdips before failed_cdip */
	for (cdip = ddi_get_child(dip); cdip != failed_cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		pcie_bus_t *bus_p;

		if ((i_ddi_node_state(cdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(cdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(cdip);
		if (PCIE_IS_VF(bus_p))
			continue;

		(void) pcie_hp_resume_device(cdip, lsr);
	}
	ndi_devi_exit(dip, circ);

	return (ret);
}

/*
 * If dip is a PF, suspend dependent sibling VFs
 */
static int
pcie_hp_suspend_dependents(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	dev_info_t *pdip, *vdip, *failed_vdip;
	pcie_bus_t *bus_p;
	int ret;

	bus_p = PCIE_DIP2BUS(dip);
	/* No need to go, if we are not a PF */
	if (!PCIE_IS_PF(bus_p))
		return (DDI_SUCCESS);

	pdip = ddi_get_parent(dip);
	for (vdip = ddi_get_child(pdip); vdip;
	    vdip = ddi_get_next_sibling(vdip)) {

		if ((i_ddi_node_state(vdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(vdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(vdip);
		/* Only look at VFs */
		if (!PCIE_IS_VF(bus_p))
			continue;

		/* Skip VFs that are not managed by us */
		if (bus_p->bus_pf_dip != dip)
			continue;

		ret = pcie_hp_suspend_device(vdip, lsr);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pcie_hp_suspend_dependents: "
			    "pcie_hp_suspend_device() failed\n");
			failed_vdip = vdip;
			goto error_resume_vdip;
		}
	}

	return (DDI_SUCCESS);
error_resume_vdip:
	/* only need to resume vdips before failed_vdip */
	for (vdip = ddi_get_child(pdip); vdip != failed_vdip;
	    vdip = ddi_get_next_sibling(vdip)) {

		if ((i_ddi_node_state(vdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(vdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(vdip);
		if (!PCIE_IS_VF(bus_p))
			continue;

		if (bus_p->bus_pf_dip != dip)
			continue;

		(void) pcie_hp_resume_device(vdip, lsr);
	}

	return (ret);
}

/*
 * Live Suspend the device
 */
static int
pcie_hp_suspend_device(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	ddi_cb_t *cb_p;
	int ret;

	/* No callback registered */
	if ((cb_p = DEVI(dip)->devi_cb_p) == NULL) {
		cmn_err(CE_NOTE, "pcie_hp_suspend_device: "
		    "%p:%s#%d: no ddi callback registered\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_ENOTSUP);
	}

	/* Already suspended */
	if (ndi_devi_device_is_live_suspended(dip)) {
		cmn_err(CE_WARN, "pcie_hp_suspend_device: "
		    "%p:%s#%d already suspended\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_EALREADY);
	}

	/* See if device support corresponding LSR activities/impacts */
	ret = pcie_hp_check_lsr_capability(dip, lsr);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_suspend_device: "
		    "%s#%d: failed to check device lsr capability",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (ret);
	}

	/* Suspend dependent sibling VFs if dip is a PF */
	ret = pcie_hp_suspend_dependents(dip, lsr);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_suspend_device: "
		    "%s#%d: failed to suspend dependents\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (ret);
	}

	/* Suspend descendants first */
	ret = pcie_hp_suspend_children(dip, lsr);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_suspend_device: "
		    "%s#%d: failed to suspend descendants\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		goto resume_dependents;
	}

	/* Call driver to do the real work */
	ret = cb_p->cb_func(dip, DDI_CB_LSR_SUSPEND, (void *)lsr,
	    cb_p->cb_arg1, cb_p->cb_arg2);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_suspend_device: "
		    "%p:%s#%d: callback return failed\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		goto resume_children;
	}

	/* save pci config space, if impacts require that */
	if (lsr->impacts &
	    (DDI_CB_LSR_IMP_DEVICE_RESET | DDI_CB_LSR_IMP_LOSE_POWER)) {
		if (pci_save_config_regs(dip) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s#%d: "
			    "pcie_save_config_regs() failed\n",
			    ddi_driver_name(dip), ddi_get_instance(dip));
			(void) cb_p->cb_func(dip, DDI_CB_LSR_RESUME,
			    (void *)lsr, cb_p->cb_arg1, cb_p->cb_arg2);
			ret = DDI_FAILURE;
			goto resume_children;
		}
	}

	/* suspend OK, remember the lsr activities/impacts */
	pcie_hp_init_state_priv(dip, lsr);

	/* set DEVI_DEVICE_SUSPENDED */
	(void) ndi_devi_device_live_suspend(dip);

	return (DDI_SUCCESS);
resume_children:
	(void) pcie_hp_resume_children(dip, lsr);
resume_dependents:
	(void) pcie_hp_resume_dependents(dip, lsr);
	return (ret);
}

/*
 * Suspend port and its descendants
 */
static int
pcie_hp_suspend_branch(pcie_hp_port_info_t *port_info,
    ddi_hp_cn_change_state_arg_t *kargp, void *result)
{
	dev_info_t *dip;
	ddi_cb_lsr_t lsr;
	int ret;

	dip = port_info->cn_child;
	if (dip == NULL) {
		cmn_err(CE_WARN, "pcie_hp_suspend_branch: "
		    "port %s has NULL devinfo\n", port_info->cn_name);
		ret = DDI_EINVAL;
		goto suspend_out;
	}

	if ((i_ddi_node_state(dip) < DS_ATTACHED) ||
	    ndi_dev_is_hidden_node(dip)) {
		cmn_err(CE_WARN, "pcie_hp_suspend_branch: "
		    "port %s has invalid state \n", port_info->cn_name);
		ret = DDI_EINVAL;
		goto suspend_out;
	}

	/*
	 * No matter where the call is from, user land or kernel,
	 * state_priv must not be NULL with LSR Suspend/Resume
	 */
	if (kargp->target_state.state_priv == NULL) {
		cmn_err(CE_WARN, "pcie_hp_suspend_branch: can't suspend %s, "
		    "no LSR state priv info specified\n", port_info->cn_name);
		ret = DDI_EINVAL;
		goto suspend_out;
	}

	if (kargp->type == ARG_PRIV_NVLIST) {
		ret = get_user_lsr_req(kargp, &lsr);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pcie_hp_suspend_branch: "
			    "get_user_lsr_req() failed\n");
			goto suspend_out;
		}
	} else { /* called from kernel */
		ddi_cb_lsr_t *p;

		p = (ddi_cb_lsr_t *)kargp->target_state.state_priv;
		if (p->activities == 0) {
			cmn_err(CE_WARN, "pcie_hp_suspend_branch: "
			    "no activities specified "
			    "from kernel invoking\n");
			ret = DDI_EINVAL;
			goto suspend_out;
		}
		(void) memset(&lsr, 0, sizeof (ddi_cb_lsr_t));
		lsr.activities = p->activities;
		lsr.impacts = p->impacts;
		if (p->reason)
			lsr.reason = ddi_strdup(p->reason, KM_SLEEP);
	}

	cmn_err(CE_NOTE, "Live Suspend: port %s: child dev %s#%d(%p) "
	    "and descendants\n", port_info->cn_name,
	    ddi_driver_name(dip), ddi_get_instance(dip), (void *)dip);

	ret = pcie_hp_suspend_device(dip, &lsr);

	if (lsr.reason)
		kmem_free(lsr.reason, strlen(lsr.reason) + 1);

	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_suspend_branch: "
		    "%s#%d: failed to suspend\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		goto suspend_out;
	}

	if (!ndi_devi_device_is_live_suspended(dip)) {
		cmn_err(CE_WARN, "pcie_hp_suspend_branch: "
		    "%s#%d: DEVI_DEVICE_LIVE_SUSPENDED not set\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		ret = DDI_FAILURE;
		goto suspend_out;
	}

	*(ddi_hp_cn_state_code_t *)result
	    = DDI_HP_CN_STATE_MAINTENANCE_SUSPENDED;

	ret = DDI_SUCCESS;
suspend_out:
	/*
	 * For async kernel LSR Suspend/Resume, the state_priv must
	 * be kmem_alloc()-ed memory.
	 *
	 * See ndi_hp_state_change_req() for more info.
	 */
	if (kargp->type == ARG_PRIV_KERNEL_ASYNC)
		free_async_lsr_state_priv(kargp);

	return (ret);
}

/*
 * Recursively resume child devices under *dip.
 *
 * We will skip all the VFs when iterating the children, PF will take
 * care of them.
 *
 * If any descendants failed to resume, we will try to suspend all the
 * previously resumed descendants.
 */
static int
pcie_hp_resume_children(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	dev_info_t *cdip;
	dev_info_t *failed_cdip;
	int circ;
	int ret;

	if (!pcie_is_pci_nexus(dip))
		return (DDI_SUCCESS);

	ndi_devi_enter(dip, &circ);

	for (cdip = ddi_get_child(dip); cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		pcie_bus_t *bus_p;

		if ((i_ddi_node_state(cdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(cdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(cdip);
		/* VF will be resumed by PF */
		if (PCIE_IS_VF(bus_p))
			continue;

		ret = pcie_hp_resume_device(cdip, lsr);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pcie_hp_resume_children: "
			    "%s#%d: failed to resume\n",
			    ddi_driver_name(dip), ddi_get_instance(dip));
			failed_cdip = cdip;
			goto error_suspend;
		}
	}

	ndi_devi_exit(dip, circ);

	return (DDI_SUCCESS);
error_suspend:
	/* only need to suspend cdips before failed_cdip */
	for (cdip = ddi_get_child(dip); cdip != failed_cdip;
	    cdip = ddi_get_next_sibling(cdip)) {
		pcie_bus_t *bus_p;

		if ((i_ddi_node_state(cdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(cdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(cdip);
		if (PCIE_IS_VF(bus_p))
			continue;

		(void) pcie_hp_suspend_device(cdip, lsr);
	}
	ndi_devi_exit(dip, circ);

	return (ret);
}

/*
 * If dip is a PF, resume dependent sibling VFs
 */
static int
pcie_hp_resume_dependents(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	dev_info_t *pdip, *vdip, *failed_vdip;
	pcie_bus_t *bus_p;
	int ret;

	bus_p = PCIE_DIP2BUS(dip);
	/* No need to go, if we are not a PF */
	if (!PCIE_IS_PF(bus_p))
		return (DDI_SUCCESS);

	pdip = ddi_get_parent(dip);
	for (vdip = ddi_get_child(pdip); vdip;
	    vdip = ddi_get_next_sibling(vdip)) {

		if ((i_ddi_node_state(vdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(vdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(vdip);
		/* Only look at VFs */
		if (!PCIE_IS_VF(bus_p))
			continue;

		/* Skip VFs that are not managed by us */
		if (bus_p->bus_pf_dip != dip)
			continue;

		ret = pcie_hp_resume_device(vdip, lsr);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pcie_hp_resume_dependents: "
			    "pcie_hp_resume_device() failed\n");
			failed_vdip = vdip;
			goto error_suspend_vdip;
		}
	}

	return (DDI_SUCCESS);
error_suspend_vdip:
	/* Only need to re-suspend vdips before failed_vdip */
	for (vdip = ddi_get_child(pdip); vdip != failed_vdip;
	    vdip = ddi_get_next_sibling(vdip)) {

		if ((i_ddi_node_state(vdip) < DS_ATTACHED) ||
		    ndi_dev_is_hidden_node(vdip)) {
			continue;
		}

		bus_p = PCIE_DIP2BUS(vdip);
		if (!PCIE_IS_VF(bus_p))
			continue;

		if (bus_p->bus_pf_dip != dip)
			continue;

		(void) pcie_hp_suspend_device(vdip, lsr);
	}

	return (ret);
}

/*
 * Live Resume the device
 */
static int
pcie_hp_resume_device(dev_info_t *dip, ddi_cb_lsr_t *lsr)
{
	ddi_cb_t *cb_p;
	int ret;

	/* No callback registered */
	if ((cb_p = DEVI(dip)->devi_cb_p) == NULL) {
		cmn_err(CE_NOTE, "pcie_hp_resume_device: "
		    "%p:%s#%d: no ddi callback registered\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_ENOTSUP);
	}

	/* Not suspended */
	if (!ndi_devi_device_is_live_suspended(dip)) {
		cmn_err(CE_WARN, "pcie_hp_resume_device: "
		    "%p:%s#%d: device not suspended\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_SUCCESS);
	}

	/* See if the LSR command match the previous suspending */
	if (!pcie_hp_state_priv_match(dip, lsr)) {
		cmn_err(CE_WARN, "pcie_hp_resume_device: "
		    "%p:%s#%d: live suspension info mismatch\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_EINVAL);
	}

	/* Restore pci config space */
	if (lsr->impacts &
	    (DDI_CB_LSR_IMP_DEVICE_RESET | DDI_CB_LSR_IMP_LOSE_POWER)) {
		if (pci_restore_config_regs(dip) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s#%d: "
			    "pcie_restore_config_regs() failed\n",
			    ddi_driver_name(dip), ddi_get_instance(dip));
			return (DDI_FAILURE);
		}
	}

	/* Need to resume us before descendants */
	ret = cb_p->cb_func(dip, DDI_CB_LSR_RESUME, (void *)lsr,
	    cb_p->cb_arg1, cb_p->cb_arg2);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_resume_device: "
		    "%p:%s#%d: callback failed\n", (void *)dip,
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (ret);
	}

	/* Resume children first */
	ret = pcie_hp_resume_children(dip, lsr);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_resume_device: "
		    "%s#%d: failed to resume children\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		goto suspend_device;
	}

	/* Resume dependent sibling VFs if dip is a PF */
	ret = pcie_hp_resume_dependents(dip, lsr);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_resume_device: "
		    "%s#%d: failed to resume dependents\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		goto suspend_children;
	}

	/* clear state priv info */
	pcie_hp_fini_state_priv(dip);

	/* clear DEVI_DEVICE_LIVE_SUSPENDED */
	(void) ndi_devi_device_live_resume(dip);

	return (DDI_SUCCESS);
suspend_children:
	(void) pcie_hp_suspend_children(dip, lsr);
suspend_device:
	(void) cb_p->cb_func(dip, DDI_CB_LSR_SUSPEND, (void *)lsr,
	    cb_p->cb_arg1, cb_p->cb_arg2);

	if (lsr->impacts &
	    (DDI_CB_LSR_IMP_DEVICE_RESET | DDI_CB_LSR_IMP_LOSE_POWER))
		(void) pci_save_config_regs(dip);

	return (ret);
}

/*
 * Resume port and its descendants
 */
static int
pcie_hp_resume_branch(pcie_hp_port_info_t *port_info,
    ddi_hp_cn_change_state_arg_t *kargp, void *result)
{
	dev_info_t *dip;
	ddi_cb_lsr_t lsr;
	int ret;

	dip = port_info->cn_child;
	if (dip == NULL) {
		cmn_err(CE_WARN, "pcie_hp_resume_branch: "
		    "port %s has NULL devinfo\n", port_info->cn_name);
		ret = DDI_EINVAL;
		goto resume_out;
	}

	if ((i_ddi_node_state(dip) < DS_ATTACHED) ||
	    ndi_dev_is_hidden_node(dip)) {
		cmn_err(CE_WARN, "pcie_hp_resume_branch: "
		    "port %s has invalid state \n", port_info->cn_name);
		ret = DDI_EINVAL;
		goto resume_out;
	}

	/*
	 * No matter where the call is from, user land or kernel,
	 * state_priv must not be NULL with LSR Suspend/Resume
	 */
	if (kargp->target_state.state_priv == NULL) {
		cmn_err(CE_WARN, "pcie_hp_resume_branch: can't resume %s, "
		    "no LSR state priv info specified\n", port_info->cn_name);
		ret = DDI_EINVAL;
		goto resume_out;
	}

	if (kargp->type == ARG_PRIV_NVLIST) {
		ret = get_user_lsr_req(kargp, &lsr);
		if (ret != DDI_SUCCESS) {
			cmn_err(CE_WARN, "pcie_hp_resume_branch: "
			    "get_user_lsr_req() failed\n");
			goto resume_out;
		}
	} else {
		ddi_cb_lsr_t *p;

		p = (ddi_cb_lsr_t *)kargp->target_state.state_priv;
		if (p->activities == 0) {
			cmn_err(CE_WARN, "pcie_hp_resume_branch: "
			    "no activities specified "
			    "from kernel invoking\n");
			ret = DDI_EINVAL;
			goto resume_out;
		}
		(void) memset(&lsr, 0, sizeof (ddi_cb_lsr_t));
		lsr.activities = p->activities;
		lsr.impacts = p->impacts;
		if (p->reason)
			lsr.reason = ddi_strdup(p->reason, KM_SLEEP);
	}

	cmn_err(CE_NOTE, "Live Resume: port %s: child dev %s#%d(%p) "
	    "and descendants\n", port_info->cn_name,
	    ddi_driver_name(dip), ddi_get_instance(dip), (void *)dip);

	ret = pcie_hp_resume_device(dip, &lsr);

	if (lsr.reason)
		kmem_free(lsr.reason, strlen(lsr.reason) + 1);

	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_resume_branch: "
		    "%s#%d: failed to resume\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		goto resume_out;
	}

	if (ndi_devi_device_is_live_suspended(dip)) {
		cmn_err(CE_WARN, "pcie_hp_resume_branch: "
		    "%s#%dDEVI_DEVICE_LIVE_SUSPENDED not cleared\n",
		    ddi_driver_name(dip), ddi_get_instance(dip));
		ret = DDI_FAILURE;
		goto resume_out;
	}

	*(ddi_hp_cn_state_code_t *)result = DDI_HP_CN_STATE_ONLINE;

	ret = DDI_SUCCESS;
resume_out:
	/*
	 * For async kernel LSR Suspend/Resume, the state_priv must be
	 * kmem_alloc()-ed memory.
	 *
	 * See ndi_hp_state_change_req() for more info.
	 */
	if (kargp->type == ARG_PRIV_KERNEL_ASYNC)
		free_async_lsr_state_priv(kargp);

	return (ret);
}

/*
 * Get the state priv info
 */
/* ARGSUSED */
static int
pcie_hp_port_get_state_priv(pcie_hp_port_info_t *port_info, void *arg,
    ddi_hp_state_priv_t *rval)
{
	dev_info_t *dip;
	pcie_bus_t *bus_p;
	ddi_cb_lsr_t *plsr;
	char *packed_buf = NULL;
	size_t packed_buf_sz = 0;
	int ret;

	if (rval == NULL)
		return (DDI_EINVAL);

	rval->nvlist_buf = NULL;
	rval->buf_size = 0;

	dip = port_info->cn_child;
	if (dip == NULL) {
		return (DDI_EINVAL);
	}

	if (!ndi_devi_device_is_live_suspended(dip)) {
		return (DDI_SUCCESS);
	}

	bus_p = PCIE_DIP2BUS(dip);
	plsr = (ddi_cb_lsr_t *)&bus_p->bus_hp_state_priv;

	ret = compose_pack_lsr_nvlist(plsr, &packed_buf, &packed_buf_sz);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "pcie_hp_get_state_priv: "
		    "compose_pack_lsr_nvlist() failed\n");
		return (ret);
	}

	rval->nvlist_buf = packed_buf;
	rval->buf_size = packed_buf_sz;

	return (DDI_SUCCESS);
}

/* ARGSUSED */
int
pcie_hp_port_ops(dev_info_t *dip, char *cn_name, ddi_hp_op_t op,
    pcie_hp_port_info_t *port_info, void *arg, void *result)
{
	pcie_bus_t	*bus_p = NULL;
	ddi_cb_t	*cbp;
	int		ret = DDI_SUCCESS;

	switch (op) {
	case DDI_HPOP_CN_GET_STATE_PRIV:
		return (pcie_hp_port_get_state_priv(port_info, arg,
		    (ddi_hp_state_priv_t *)result));

	case DDI_HPOP_CN_CHANGE_STATE:
	{
		ddi_hp_cn_change_state_arg_t *kargp;
		ddi_hp_cn_state_code_t target_state;
		ddi_hp_cn_state_code_t curr_state;

		kargp = (ddi_hp_cn_change_state_arg_t *)arg;
		target_state = kargp->target_state.state_code;
		curr_state = port_info->cn_state;

		if (target_state < DDI_HP_CN_STATE_PORT_EMPTY ||
		    target_state > DDI_HP_CN_STATE_ONLINE) {
			/* Invalid target state for ports */
			return (DDI_EINVAL);
		}
		PCIE_DBG("pcie_hp_port_ops: change port state"
		    " dip=%p cn_name=%s"
		    " op=%x arg=%p\n", (void *)dip, cn_name, op, arg);

		/*
		 * Check if this is for changing port's state: change to/from
		 * PORT_EMPTY/PRESENT states.
		 */
		if (curr_state < target_state) {
			/* Upgrade state */
			switch (curr_state) {
			case DDI_HP_CN_STATE_PORT_EMPTY:
				if (target_state ==
				    DDI_HP_CN_STATE_PORT_PRESENT) {
					int dev_num, func_num;

					ret = pcie_hp_get_df_from_port_name(
					    cn_name, &dev_num, &func_num);
					if (ret != DDI_SUCCESS)
						goto port_state_done;

					ret = pcie_hp_check_hardware_existence(
					    dip, dev_num, func_num);
				} else if (target_state ==
				    DDI_HP_CN_STATE_OFFLINE) {
					ret = pcie_read_only_probe(dip,
					    cn_name, (dev_info_t **)result);
					return (ret);
				} else
					ret = DDI_EINVAL;

				goto port_state_done;
			case DDI_HP_CN_STATE_PORT_PRESENT:
				if (target_state ==
				    DDI_HP_CN_STATE_OFFLINE) {
					ret = pcie_read_only_probe(dip,
					    cn_name, (dev_info_t **)result);
					return (ret);
				} else
					ret = DDI_EINVAL;

				goto port_state_done;
			case DDI_HP_CN_STATE_MAINTENANCE_SUSPENDED:
				if (target_state == DDI_HP_CN_STATE_ONLINE) {
					ret = pcie_hp_resume_branch(port_info,
					    kargp, result);
					if (ret != DDI_SUCCESS)
						goto port_state_done;
					return (ret);
				} else
					ret = DDI_EINVAL;

				goto port_state_done;
			default:
				ASSERT("unexpected state");
			}
		} else {
			/* Downgrade state */
			switch (curr_state) {
			case DDI_HP_CN_STATE_PORT_PRESENT:
			{
				int dev_num, func_num;

				ret = pcie_hp_get_df_from_port_name(cn_name,
				    &dev_num, &func_num);
				if (ret != DDI_SUCCESS)
					goto port_state_done;

				ret = pcie_hp_check_hardware_existence(dip,
				    dev_num, func_num);

				goto port_state_done;
			}
			case DDI_HP_CN_STATE_OFFLINE:
				ret = pcie_read_only_unprobe(dip, cn_name);

				goto port_state_done;
			case DDI_HP_CN_STATE_ONLINE:
				if (target_state
				    == DDI_HP_CN_STATE_MAINTENANCE_SUSPENDED) {
					ret = pcie_hp_suspend_branch(port_info,
					    kargp, result);
					if (ret != DDI_SUCCESS)
						goto port_state_done;
					return (ret);
				} else
					/* never reach here if not suspend */
					ret = DDI_EINVAL;

				goto port_state_done;
			default:
				ASSERT("unexpected state");
			}
		}
port_state_done:
		*(ddi_hp_cn_state_code_t *)result = curr_state;
		return (ret);
	}
	case DDI_HPOP_CN_PROBE:
	{
		/*
		 * Discover and create the dependent device nodes;
		 * Register new ports to hotplug framework.
		 */
		if (port_info->cn_child == NULL)
			return (DDI_EINVAL);
		bus_p = PCIE_DIP2UPBUS(port_info->cn_child);

		if (port_info->cn_type == DDI_HP_CN_TYPE_PORT_IOV_PF) {
			cbp = DEVI(port_info->cn_child)->devi_cb_p;
			if (bus_p && cbp &&
			    (cbp->cb_flags & DDI_CB_FLAG_SRIOV) &&
			    (bus_p->bus_vf_bar_ptr == NULL)) {
				if (pcie_hp_debug) {
					cmn_err(CE_NOTE,
				"Configuring VFs num_vf = %d\n",
					    bus_p->num_vf);
				}
				ret = pcicfg_config_vf(port_info->cn_child);
			} else {
				/*
				 * We could reach here if VFs are already
				 * configured. In which case simply return
				 * success.
				 */
				if (bus_p->bus_vf_bar_ptr)
					ret = DDI_SUCCESS;
				else {
					if (pcie_hp_debug)
						cmn_err(CE_NOTE,
						    "num_vf configured is %d\n",
						    bus_p->num_vf);
					ret = DDI_FAILURE;
				}
			}

		} else {
			/*
			 * Otherwise (for non-PF ports), just return DDI_ENOTSUP
			 * because there is no requirements to implement it.
			 */
			ret = DDI_ENOTSUP;
		}

		break;
	}
	case DDI_HPOP_CN_UNPROBE:
	{
		/*
		 * Try to remove the dependent device nodes;
		 * Unregister ports from hotplug framework.
		 */
		if (port_info->cn_child == NULL)
			return (DDI_EINVAL);
		bus_p = PCIE_DIP2UPBUS(port_info->cn_child);

		if (port_info->cn_type == DDI_HP_CN_TYPE_PORT_IOV_PF) {
			/*
			 * Add SR-IOV code here:
			 *
			 * If unprobing dependent devices of a PF port, then
			 * try to remove VF nodes and disable VFs first.
			 * And also unregister VF ports from hotplug framework
			 * by calling ndi_hp_unregister().
			 */
			cbp = DEVI(port_info->cn_child)->devi_cb_p;
			if (bus_p && cbp &&
			    (cbp->cb_flags & DDI_CB_FLAG_SRIOV) &&
			    bus_p->bus_vf_bar_ptr) {
				ret = pcicfg_unconfig_vf(port_info->cn_child);
			} else {
				if (pcie_hp_debug)
					cmn_err(CE_NOTE,
					    "num_vf configured is %d\n",
					    bus_p->num_vf);
				ret = DDI_FAILURE;
			}

		} else {
			/*
			 * Otherwise (for non-PF ports), just return DDI_ENOTSUP
			 * because there is no requirements to implement it.
			 */
			ret = DDI_ENOTSUP;
		}

		break;
	}
	default:
		ret = DDI_ENOTSUP;
		cmn_err(CE_WARN, "pcie_hp_port_ops: op is not supported."
		    " dip=%p cn_name=%s"
		    " op=%x arg=%p\n", (void *)dip, cn_name, op, arg);
		break;
	}

	return (ret);
}

static int
pcie_hp_connector_ops(dev_info_t *dip, char *cn_name, ddi_hp_op_t op,
    void *arg, void *result)
{
	pcie_bus_t *bus_p = PCIE_DIP2BUS(dip);
	int ret = DDI_SUCCESS;

	if (op == DDI_HPOP_CN_GET_STATE_PRIV) {
		/* currently, connectors have no state priv info */
		ddi_hp_state_priv_t *rval = (ddi_hp_state_priv_t *)result;

		rval->nvlist_buf = NULL;
		rval->buf_size = 0;

		return (DDI_SUCCESS);
	}

	if (PCIE_IS_PCIE_HOTPLUG_CAPABLE(bus_p)) {
		/* PCIe hotplug */
		ret = pciehpc_hp_ops(dip, cn_name, op, arg, result);
	} else if (PCIE_IS_PCI_HOTPLUG_CAPABLE(bus_p)) {
		/* PCI SHPC hotplug */
		ret = pcishpc_hp_ops(dip, cn_name, op, arg, result);
	} else {
		cmn_err(CE_WARN, "pcie_hp_connector_ops: op is not supported."
		    " dip=%p cn_name=%s"
		    " op=%x arg=%p\n", (void *)dip, cn_name, op, arg);
		ret = DDI_ENOTSUP;
	}

#ifdef	__amd64
	/*
	 * like in attach, since hotplugging can change error registers,
	 * we need to ensure that the proper bits are set on this port
	 * after a configure operation
	 */
	{
		ddi_hp_cn_change_state_arg_t *kargp;
		ddi_hp_cn_state_code_t target_state_code;

		kargp = (ddi_hp_cn_change_state_arg_t *)arg;
		target_state_code = kargp->target_state.state_code;
		if ((ret == DDI_SUCCESS) && (op == DDI_HPOP_CN_CHANGE_STATE) &&
		    (target_state_code == DDI_HP_CN_STATE_ENABLED))
			pcieb_intel_error_workaround(dip);
	}
#endif	/* __amd64 */

	return (ret);
}

/*
 * Dispatch hotplug commands to different hotplug controller drivers, including
 * physical and virtual hotplug operations.
 */
/* ARGSUSED */
int
pcie_hp_common_ops(dev_info_t *dip, char *cn_name, ddi_hp_op_t op,
    void *arg, void *result)
{
	pcie_hp_port_info_t port_info;
	int		ret = DDI_SUCCESS;

	PCIE_DBG("pcie_hp_common_ops: dip=%p cn_name=%s op=%x arg=%p\n",
	    dip, cn_name, op, arg);

	switch (op) {
	case DDI_HPOP_CN_CREATE_PORT:
		/* create an empty port */
		return (pcie_hp_register_port(NULL, dip, cn_name));
	case DDI_HPOP_CN_GET_STATE:
		return (pcie_hp_connector_ops(dip, cn_name, op, arg, result));
	default:
		break;
	}

	port_info.rv = DDI_FAILURE;
	port_info.cn_name = cn_name;
	ndi_hp_walk_cn(dip, pcie_hp_get_port_info, &port_info);
	if (port_info.rv != DDI_SUCCESS) {
		/* can not find the port */
		return (DDI_EINVAL);
	}

	if (port_info.cn_type & DDI_HP_CN_TYPE_CONNECTOR_MASK) {
		ret = pcie_hp_connector_ops(dip, cn_name, op, arg, result);
	} else {
		ret = pcie_hp_port_ops(dip, cn_name, op, &port_info, arg,
		    result);
	}

	return (ret);
}

/*
 * pcie_hp_match_dev_func:
 * Match dip's PCI device number and function number with input ones.
 */
static int
pcie_hp_match_dev_func(dev_info_t *dip, void *hdl)
{
	struct pcie_hp_find_ctrl	*ctrl = (struct pcie_hp_find_ctrl *)hdl;
	pci_regspec_t			*pci_rp;
	int				length;
	int				pci_dev, pci_func;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (int **)&pci_rp, (uint_t *)&length) != DDI_PROP_SUCCESS) {
		ctrl->dip = NULL;
		return (DDI_WALK_TERMINATE);
	}

	/* get the PCI device address info */
	pci_dev = PCI_REG_DEV_G(pci_rp->pci_phys_hi);
	pci_func = PCI_REG_FUNC_G(pci_rp->pci_phys_hi);

	/*
	 * free the memory allocated by ddi_prop_lookup_int_array
	 */
	ddi_prop_free(pci_rp);

	if ((pci_dev == ctrl->device) && (pci_func == ctrl->function)) {
		/* found the match for the specified device address */
		ctrl->dip = dip;
		return (DDI_WALK_TERMINATE);
	}

	/*
	 * continue the walk to the next sibling to look for a match.
	 */
	return (DDI_WALK_PRUNECHILD);
}

/*
 * pcie_hp_match_dev:
 * Match the dip's pci device number with the input dev_num
 */
static boolean_t
pcie_hp_match_dev(dev_info_t *dip, int dev_num)
{
	pci_regspec_t			*pci_rp;
	int				length;
	int				pci_dev;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (int **)&pci_rp, (uint_t *)&length) != DDI_PROP_SUCCESS) {
		return (B_FALSE);
	}

	/* get the PCI device address info */
	pci_dev = PCI_REG_DEV_G(pci_rp->pci_phys_hi);

	/*
	 * free the memory allocated by ddi_prop_lookup_int_array
	 */
	ddi_prop_free(pci_rp);

	if (pci_dev == dev_num) {
		/* found the match for the specified device address */
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Callback function to match with device number in order to list
 * occupants under a specific slot
 */
static int
pcie_hp_list_occupants(dev_info_t *dip, void *arg)
{
	pcie_hp_cn_cfg_t	*cn_cfg_p = (pcie_hp_cn_cfg_t *)arg;
	pcie_hp_occupant_info_t	*occupant =
	    (pcie_hp_occupant_info_t *)cn_cfg_p->cn_private;
	pcie_hp_slot_t		*slot_p =
	    (pcie_hp_slot_t *)cn_cfg_p->slotp;
	int			pci_dev;
	pci_regspec_t		*pci_rp;
	int			length;
	major_t			major;

	/*
	 * Get the PCI device number information from the devinfo
	 * node. Since the node may not have the address field
	 * setup (this is done in the DDI_INITCHILD of the parent)
	 * we look up the 'reg' property to decode that information.
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (int **)&pci_rp,
	    (uint_t *)&length) != DDI_PROP_SUCCESS) {
		cn_cfg_p->rv = DDI_FAILURE;
		cn_cfg_p->dip = dip;
		return (DDI_WALK_TERMINATE);
	}

	/* get the pci device id information */
	pci_dev = PCI_REG_DEV_G(pci_rp->pci_phys_hi);

	/*
	 * free the memory allocated by ddi_prop_lookup_int_array
	 */
	ddi_prop_free(pci_rp);

	/*
	 * Match the node for the device number of the slot.
	 */
	if (pci_dev == slot_p->hs_device_num) {

		major = ddi_driver_major(dip);

		/*
		 * If the node is not yet attached, then don't list it
		 * as an occupant. This is valid, since nothing can be
		 * consuming it until it is attached, and cfgadm will
		 * ask for the property explicitly which will cause it
		 * to be re-freshed right before checking with rcm.
		 */
		if ((major == DDI_MAJOR_T_NONE) || !i_ddi_devi_attached(dip))
			return (DDI_WALK_PRUNECHILD);

		/*
		 * If we have used all our occupants then print mesage
		 * and terminate walk.
		 */
		if (occupant->i >= PCIE_HP_MAX_OCCUPANTS) {
			cmn_err(CE_WARN,
			    "pcie (%s%d): unable to list all occupants",
			    ddi_driver_name(ddi_get_parent(dip)),
			    ddi_get_instance(ddi_get_parent(dip)));
			return (DDI_WALK_TERMINATE);
		}

		/*
		 * No need to hold the dip as ddi_walk_devs
		 * has already arranged that for us.
		 */
		occupant->id[occupant->i] =
		    kmem_alloc(sizeof (char[MAXPATHLEN]), KM_SLEEP);
		(void) ddi_pathname(dip, (char *)occupant->id[occupant->i]);
		occupant->i++;
	}

	/*
	 * continue the walk to the next sibling to look for a match
	 * or to find other nodes if this card is a multi-function card.
	 */
	return (DDI_WALK_PRUNECHILD);
}

/*
 * Generate the System Event for ESC_DR_REQ.
 * One of the consumers is pcidr, it calls to libcfgadm to perform a
 * configure or unconfigure operation to the AP.
 */
void
pcie_hp_gen_sysevent_req(char *slot_name, int hint,
    dev_info_t *self, int kmflag)
{
	sysevent_id_t	eid;
	nvlist_t	*ev_attr_list = NULL;
	char		cn_path[MAXPATHLEN];
	char		*ap_id;
	int		err, ap_id_len;

	/*
	 * Minor device name (AP) will be bus path
	 * concatenated with slot name
	 */
	(void) strcpy(cn_path, "/devices");
	(void) ddi_pathname(self, cn_path + strlen("/devices"));

	ap_id_len = strlen(cn_path) + strlen(":") +
	    strlen(slot_name) + 1;
	ap_id = kmem_zalloc(ap_id_len, kmflag);
	if (ap_id == NULL) {
		cmn_err(CE_WARN,
		    "%s%d: Failed to allocate memory for AP ID: %s:%s",
		    ddi_driver_name(self), ddi_get_instance(self),
		    cn_path, slot_name);

		return;
	}

	(void) strcpy(ap_id, cn_path);
	(void) strcat(ap_id, ":");
	(void) strcat(ap_id, slot_name);

	err = nvlist_alloc(&ev_attr_list, NV_UNIQUE_NAME_TYPE, kmflag);
	if (err != 0) {
		cmn_err(CE_WARN,
		    "%s%d: Failed to allocate memory "
		    "for event attributes%s", ddi_driver_name(self),
		    ddi_get_instance(self), ESC_DR_REQ);

		kmem_free(ap_id, ap_id_len);
		return;
	}

	switch (hint) {

	case SE_INVESTIGATE_RES:	/* fall through */
	case SE_INCOMING_RES:		/* fall through */
	case SE_OUTGOING_RES:		/* fall through */

		err = nvlist_add_string(ev_attr_list, DR_REQ_TYPE,
		    SE_REQ2STR(hint));

		if (err != 0) {
			cmn_err(CE_WARN,
			    "%s%d: Failed to add attr [%s] "
			    "for %s event", ddi_driver_name(self),
			    ddi_get_instance(self),
			    DR_REQ_TYPE, ESC_DR_REQ);

			goto done;
		}
		break;

	default:
		cmn_err(CE_WARN, "%s%d:  Unknown hint on sysevent",
		    ddi_driver_name(self), ddi_get_instance(self));

		goto done;
	}

	/*
	 * Add attachment point as attribute (common attribute)
	 */

	err = nvlist_add_string(ev_attr_list, DR_AP_ID, ap_id);

	if (err != 0) {
		cmn_err(CE_WARN, "%s%d: Failed to add attr [%s] for %s event",
		    ddi_driver_name(self), ddi_get_instance(self),
		    DR_AP_ID, EC_DR);

		goto done;
	}


	/*
	 * Log this event with sysevent framework.
	 */

	err = ddi_log_sysevent(self, DDI_VENDOR_SUNW, EC_DR,
	    ESC_DR_REQ, ev_attr_list, &eid,
	    ((kmflag == KM_SLEEP) ? DDI_SLEEP : DDI_NOSLEEP));
	if (err != 0) {
		cmn_err(CE_WARN, "%s%d: Failed to log %s event",
		    ddi_driver_name(self), ddi_get_instance(self), EC_DR);
	}

done:
	nvlist_free(ev_attr_list);
	kmem_free(ap_id, ap_id_len);
}
