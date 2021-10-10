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

/*
 *     PCI configurator (pcicfg)
 */

#include <sys/isa_defs.h>

#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/modctl.h>
#include <sys/autoconf.h>
#include <sys/hwconf.h>
#include <sys/ddi_impldefs.h>
#include <sys/fcode.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pci_impl.h>
#include <sys/iovcfg.h>
#include <sys/pcie_impl.h>
#include <sys/memlist.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/taskq.h>
#include <sys/pci_cap.h>
#include <sys/hotplug/pci/pcicfg.h>
#include <sys/hotplug/pci/pcie_hp.h>
#include <sys/ndi_impldefs.h>
#include <sys/pci_cfgacc.h>
#include <sys/pciconf.h>

#define	PCICFG_DEVICE_TYPE_PCI	1
#define	PCICFG_DEVICE_TYPE_PCIE	2

#define	EFCODE21554	/* changes for supporting 21554 */

static int pcicfg_alloc_resource(dev_info_t *, pci_regspec_t *, char *, int);
static int pcicfg_free_pci_regspec(dev_info_t *, pci_regspec_t *);
static int pcicfg_free_resource(dev_info_t *, pci_regspec_t, pcicfg_flags_t,
	char *);
static int pcicfg_remove_assigned_prop(dev_info_t *, pci_regspec_t *, char *);
static void pcicfg_free_vf_bar(dev_info_t *);

#ifdef	PCICFG_INTERPRET_FCODE
static int pcicfg_fcode_assign_bars(ddi_acc_handle_t, dev_info_t *,
    uint_t, uint_t, uint_t, int32_t, pci_regspec_t *);
#endif	/* PCICFG_INTERPRET_FCODE */

/*
 * ************************************************************************
 * *** Implementation specific local data structures/definitions.	***
 * ************************************************************************
 */

static	int	pcicfg_start_devno = 0;	/* for Debug only */

#define	PCICFG_MAX_DEVICE 32
#define	PCICFG_MAX_FUNCTION 8
#define	PCICFG_MAX_ARI_FUNCTION 256
#define	PCICFG_MAX_REGISTER 64
#define	PCICFG_MAX_BUS_DEPTH 255

#define	PCICFG_NODEVICE 42
#define	PCICFG_NOMEMORY 43
#define	PCICFG_NOMULTI	44

#define	PCICFG_HIADDR(n) ((uint32_t)(((uint64_t)(n) & 0xFFFFFFFF00000000)>> 32))
#define	PCICFG_LOADDR(n) ((uint32_t)((uint64_t)(n) & 0x00000000FFFFFFFF))
#define	PCICFG_LADDR(lo, hi)	(((uint64_t)(hi) << 32) | (uint32_t)(lo))

#define	PCICFG_HIWORD(n) ((uint16_t)(((uint32_t)(n) & 0xFFFF0000)>> 16))
#define	PCICFG_LOWORD(n) ((uint16_t)((uint32_t)(n) & 0x0000FFFF))
#define	PCICFG_HIBYTE(n) ((uint8_t)(((uint16_t)(n) & 0xFF00)>> 8))
#define	PCICFG_LOBYTE(n) ((uint8_t)((uint16_t)(n) & 0x00FF))

#define	PCICFG_ROUND_UP(addr, gran) ((uintptr_t)((gran+addr-1)&(~(gran-1))))
#define	PCICFG_ROUND_DOWN(addr, gran) ((uintptr_t)((addr) & ~(gran-1)))

#define	PCICFG_MEMGRAN 0x100000
#define	PCICFG_IOGRAN 0x1000
#define	PCICFG_4GIG_LIMIT 0xFFFFFFFFUL

#define	PCICFG_MEM_MULT 4
#define	PCICFG_IO_MULT 4
#define	PCICFG_RANGE_LEN 2 /* Number of range entries */

static int pcicfg_slot_busnums = 8;
static int pcicfg_slot_memsize = 32 * PCICFG_MEMGRAN; /* 32MB per slot */
static int pcicfg_slot_iosize = 16 * PCICFG_IOGRAN; /* 64K per slot */
static int pcicfg_chassis_per_tree = 1;
static int pcicfg_sec_reset_delay = 1000000;

/*
 * The following typedef is used to represent a
 * 1275 "bus-range" property of a PCI Bus node.
 * DAF - should be in generic include file...
 */

typedef struct pcicfg_bus_range {
	uint32_t lo;
	uint32_t hi;
} pcicfg_bus_range_t;

typedef struct pcicfg_range {

	uint32_t child_hi;
	uint32_t child_mid;
	uint32_t child_lo;
	uint32_t parent_hi;
	uint32_t parent_mid;
	uint32_t parent_lo;
	uint32_t size_hi;
	uint32_t size_lo;

} pcicfg_range_t;

typedef struct hole hole_t;

struct hole {
	uint64_t	start;
	uint64_t	len;
	hole_t		*next;
};

typedef struct pcicfg_phdl pcicfg_phdl_t;

struct pcicfg_phdl {

	dev_info_t	*dip;		/* Associated with the attach point */
	pcicfg_phdl_t	*next;

	uint64_t	memory_base;	/* Memory base for this attach point */
	uint64_t	memory_last;
	uint64_t	memory_len;
	uint32_t	io_base;	/* I/O base for this attach point */
	uint32_t	io_last;
	uint32_t	io_len;

	int		error;
	uint_t		highest_bus;	/* Highest bus seen on the probe */

	hole_t		mem_hole;	/* Memory hole linked list. */
	hole_t		io_hole;	/* IO hole linked list */

	ndi_ra_request_t mem_req;	/* allocator request for memory */
	ndi_ra_request_t io_req;	/* allocator request for I/O */
};

struct pcicfg_standard_prop_entry {
    uchar_t *name;
    uint_t  config_offset;
    uint_t  size;
};


struct pcicfg_name_entry {
    uint32_t class_code;
    char  *name;
};

struct pcicfg_find_ctrl {
	uint_t		device;
	uint_t		function;
	dev_info_t	*dip;
};

typedef struct pcicfg_err_regs {
	uint16_t cmd;
	uint16_t bcntl;
	uint16_t pcie_dev;
	uint16_t devctl;
	uint16_t pcie_cap_off;
} pcicfg_err_regs_t;

/*
 * List of Indirect Config Map Devices. At least the intent of the
 * design is to look for a device in this list during the configure
 * operation, and if the device is listed here, then it is a nontransparent
 * bridge, hence load the driver and avail the config map services from
 * the driver. Class and Subclass should be as defined in the PCI specs
 * ie. class is 0x6, and subclass is 0x9.
 */
static struct {
	uint8_t		mem_range_bar_offset;
	uint8_t		io_range_bar_offset;
	uint8_t		prefetch_mem_range_bar_offset;
} pcicfg_indirect_map_devs[] = {
	PCI_CONF_BASE3, PCI_CONF_BASE2, PCI_CONF_BASE3,
	0,	0,	0,
};

#define	PCICFG_MAKE_REG_HIGH(busnum, devnum, funcnum, register)\
	(\
	((ulong_t)(busnum & 0xff) << 16)    |\
	((ulong_t)(devnum & 0x1f) << 11)    |\
	((ulong_t)(funcnum & 0x7) <<  8)    |\
	((ulong_t)(register & 0x3f)))

/*
 * debug macros:
 */
#if	defined(DEBUG)
extern int busra_debug;
extern void prom_printf(const char *, ...);

/*
 * Following values are defined for this debug flag.
 *
 * 1 = dump configuration header only.
 * 2 = dump generic debug data only (no config header dumped)
 * 3 = dump everything (both 1 and 2)
 */
int pcicfg_debug = 0;
int pcicfg_dump_fcode = 0;

static void debug(char *, uintptr_t, uintptr_t,
	uintptr_t, uintptr_t, uintptr_t);

#define	DEBUG0(fmt)\
	debug(fmt, 0, 0, 0, 0, 0);
#define	DEBUG1(fmt, a1)\
	debug(fmt, (uintptr_t)(a1), 0, 0, 0, 0);
#define	DEBUG2(fmt, a1, a2)\
	debug(fmt, (uintptr_t)(a1), (uintptr_t)(a2), 0, 0, 0);
#define	DEBUG3(fmt, a1, a2, a3)\
	debug(fmt, (uintptr_t)(a1), (uintptr_t)(a2),\
		(uintptr_t)(a3), 0, 0);
#define	DEBUG4(fmt, a1, a2, a3, a4)\
	debug(fmt, (uintptr_t)(a1), (uintptr_t)(a2),\
		(uintptr_t)(a3), (uintptr_t)(a4), 0);
#define	DEBUG5(fmt, a1, a2, a3, a4, a5)\
	debug(fmt, (uintptr_t)(a1), (uintptr_t)(a2),\
		(uintptr_t)(a3), (uintptr_t)(a4), (uintptr_t)(a5));
#define	ERR_MSG(x)\
		(err_msg = x);
#else
#define	DEBUG0(fmt)
#define	DEBUG1(fmt, a1)
#define	DEBUG2(fmt, a1, a2)
#define	DEBUG3(fmt, a1, a2, a3)
#define	DEBUG4(fmt, a1, a2, a3, a4)
#define	DEBUG5(fmt, a1, a2, a3, a4, a5)
#define	ERR_MSG(x)
#endif

#ifdef PCICFG_INTERPRET_FCODE
int pcicfg_dont_interpret = 0;
#else
int pcicfg_dont_interpret = 1;
#endif

/*
 * forward declarations for routines defined in this module (called here)
 */

static int pcicfg_add_config_reg(dev_info_t *, uint_t);
static int pcicfg_probe_children(dev_info_t *, uint_t, uint_t *,
    pcicfg_flags_t, boolean_t);
static int pcicfg_probe_vf(dev_info_t *, uint_t, uint16_t);

#ifdef PCICFG_INTERPRET_FCODE
static int pcicfg_load_fcode(dev_info_t *, uint_t, uint_t, uint_t,
	uint16_t, uint16_t, uchar_t **, int *, int, int);
#endif

static int pcicfg_fcode_probe(dev_info_t *, uint_t, uint_t, uint_t,
    uint_t *, pcicfg_flags_t, boolean_t);
static int pcicfg_probe_bridge(dev_info_t *, ddi_acc_handle_t, uint_t,
    uint_t *, boolean_t);
static int pcicfg_free_all_resources(dev_info_t *);
static int pcicfg_alloc_new_resources(dev_info_t *);
static int pcicfg_match_dev(dev_info_t *, void *);
static dev_info_t *pcicfg_devi_find(dev_info_t *, uint_t, uint_t);
static pcicfg_phdl_t *pcicfg_find_phdl(dev_info_t *);
static pcicfg_phdl_t *pcicfg_create_phdl(dev_info_t *);
static int pcicfg_destroy_phdl(dev_info_t *);
static int pcicfg_sum_resources(dev_info_t *, void *);
static int pcicfg_find_resource_end(dev_info_t *, void *);
static int pcicfg_allocate_chunk(dev_info_t *);
static int pcicfg_program_ap(dev_info_t *);
static int pcicfg_device_assign(dev_info_t *);
static int pcicfg_bridge_assign(dev_info_t *, void *);
static int pcicfg_device_assign_readonly(dev_info_t *);
static int pcicfg_free_resources(dev_info_t *, pcicfg_flags_t);
static void pcicfg_setup_bridge(pcicfg_phdl_t *, ddi_acc_handle_t,
    dev_info_t *);
static void pcicfg_update_bridge(pcicfg_phdl_t *, ddi_acc_handle_t);
static void pcicfg_enable_bridge_probe_err(dev_info_t *dip,
				ddi_acc_handle_t h, pcicfg_err_regs_t *regs);
static void pcicfg_disable_bridge_probe_err(dev_info_t *dip,
				ddi_acc_handle_t h, pcicfg_err_regs_t *regs);
static int pcicfg_update_assigned_prop(dev_info_t *, pci_regspec_t *, char *);
static void pcicfg_device_on(ddi_acc_handle_t);
static void pcicfg_device_off(ddi_acc_handle_t);
static int pcicfg_set_busnode_props(dev_info_t *, uint8_t, int, int);
static int pcicfg_free_bridge_resources(dev_info_t *);
static int pcicfg_free_device_resources(dev_info_t *, pcicfg_flags_t, char *);
static int pcicfg_teardown_device(dev_info_t *, pcicfg_flags_t, boolean_t);
static int pcicfg_config_setup(dev_info_t *, ddi_acc_handle_t *);
static void pcicfg_config_teardown(ddi_acc_handle_t *);
static void pcicfg_get_mem(pcicfg_phdl_t *, uint32_t, uint64_t *);
static void pcicfg_get_io(pcicfg_phdl_t *, uint32_t, uint32_t *);
static int pcicfg_update_ranges_prop(dev_info_t *, pcicfg_range_t *);
static int pcicfg_map_phys(dev_info_t *, pci_regspec_t *, caddr_t *,
    ddi_device_acc_attr_t *, ddi_acc_handle_t *);
static void pcicfg_unmap_phys(ddi_acc_handle_t *, pci_regspec_t *);
static int pcicfg_dump_assigned(dev_info_t *);
static uint_t pcicfg_configure_ntbridge(dev_info_t *, uint_t, uint_t);
static int pcicfg_indirect_map(dev_info_t *dip);
static uint_t pcicfg_get_ntbridge_child_range(dev_info_t *, uint64_t *,
				uint64_t *, uint_t);
static int pcicfg_is_ntbridge(dev_info_t *);
static int pcicfg_ntbridge_allocate_resources(dev_info_t *);
static int pcicfg_ntbridge_configure_done(dev_info_t *);
static int pcicfg_ntbridge_unconfigure(dev_info_t *);
static int pcicfg_ntbridge_unconfigure_child(dev_info_t *, uint_t);
static void pcicfg_free_hole(hole_t *);
static uint64_t pcicfg_alloc_hole(hole_t *, uint64_t *, uint32_t);
static int pcicfg_update_available_prop(dev_info_t *, pci_regspec_t *);
static int pcicfg_ari_configure(dev_info_t *);
static int pcicfg_populate_reg_props(dev_info_t *, ddi_acc_handle_t);
static int pcicfg_populate_props_from_bar(dev_info_t *, ddi_acc_handle_t);
static int pcicfg_update_assigned_prop_value(dev_info_t *, uint32_t,
    uint32_t, uint32_t, uint_t, char *);
static boolean_t is_pcie_fabric(dev_info_t *dip);
static int pcicfg_dump_assigned_prop(dev_info_t *, char *);
static int pcicfg_probe_VFs(dev_info_t *, ddi_acc_handle_t);

int pcicfg_assign_vf_bars(dev_info_t *, ddi_acc_handle_t);

#ifdef DEBUG
static void pci_dump(ddi_acc_handle_t config_handle);
static void pcicfg_dump_common_config(ddi_acc_handle_t config_handle);
static void pcicfg_dump_sriov_config(ddi_acc_handle_t, uint16_t);
static void pcicfg_dump_device_config(ddi_acc_handle_t);

static void pcicfg_dump_bridge_config(ddi_acc_handle_t config_handle);
static uint64_t pcicfg_unused_space(hole_t *, uint32_t *);

#define	PCICFG_DUMP_COMMON_CONFIG(hdl) (void)pcicfg_dump_common_config(hdl)
#define	PCICFG_DUMP_DEVICE_CONFIG(hdl) (void)pcicfg_dump_device_config(hdl)
#define	PCICFG_DUMP_BRIDGE_CONFIG(hdl) (void)pcicfg_dump_bridge_config(hdl)
#else
#define	PCICFG_DUMP_COMMON_CONFIG(handle)
#define	PCICFG_DUMP_DEVICE_CONFIG(handle)
#define	PCICFG_DUMP_BRIDGE_CONFIG(handle)
#endif

static kmutex_t pcicfg_list_mutex; /* Protects the probe handle list */
static pcicfg_phdl_t *pcicfg_phdl_list = NULL;

#ifndef _DONT_USE_1275_GENERIC_NAMES
/*
 * Class code table
 */
static struct pcicfg_name_entry pcicfg_class_lookup [] = {

	{ 0x001, "display" },
	{ 0x100, "scsi" },
	{ 0x101, "ide" },
	{ 0x102, "fdc" },
	{ 0x103, "ipi" },
	{ 0x104, "raid" },
	{ 0x200, "ethernet" },
	{ 0x201, "token-ring" },
	{ 0x202, "fddi" },
	{ 0x203, "atm" },
	{ 0x300, "display" },
	{ 0x400, "video" },
	{ 0x401, "sound" },
	{ 0x500, "memory" },
	{ 0x501, "flash" },
	{ 0x600, "host" },
	{ 0x601, "isa" },
	{ 0x602, "eisa" },
	{ 0x603, "mca" },
	{ 0x604, "pci" },
	{ 0x605, "pcmcia" },
	{ 0x606, "nubus" },
	{ 0x607, "cardbus" },
	{ 0x609, "pci" },
	{ 0x700, "serial" },
	{ 0x701, "parallel" },
	{ 0x800, "interrupt-controller" },
	{ 0x801, "dma-controller" },
	{ 0x802, "timer" },
	{ 0x803, "rtc" },
	{ 0x900, "keyboard" },
	{ 0x901, "pen" },
	{ 0x902, "mouse" },
	{ 0xa00, "dock" },
	{ 0xb00, "cpu" },
	{ 0xc00, "firewire" },
	{ 0xc01, "access-bus" },
	{ 0xc02, "ssa" },
	{ 0xc03, "usb" },
	{ 0xc04, "fibre-channel" },
	{ 0, 0 }
};
#endif /* _DONT_USE_1275_GENERIC_NAMES */

/*
 * Module control operations
 */

extern struct mod_ops mod_miscops;

static struct modlmisc modlmisc = {
	&mod_miscops, /* Type of module */
	"PCIe/PCI Config (EFCode Enabled)"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

#ifdef DEBUG

static void
pcicfg_dump_common_config(ddi_acc_handle_t config_handle)
{
	if ((pcicfg_debug & 1) == 0)
		return;
	cmn_err(CE_CONT, " Vendor ID   = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_VENID));
	cmn_err(CE_CONT, " Device ID   = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_DEVID));
	cmn_err(CE_CONT, " Command REG = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_COMM));
	cmn_err(CE_CONT, " Status  REG = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_STAT));
	cmn_err(CE_CONT, " Revision ID = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_REVID));
	cmn_err(CE_CONT, " Prog Class  = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_PROGCLASS));
	cmn_err(CE_CONT, " Dev Class   = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_SUBCLASS));
	cmn_err(CE_CONT, " Base Class  = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_BASCLASS));
	cmn_err(CE_CONT, " Device ID   = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_CACHE_LINESZ));
	cmn_err(CE_CONT, " Header Type = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_HEADER));
	cmn_err(CE_CONT, " BIST        = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_BIST));
	cmn_err(CE_CONT, " BASE 0      = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_BASE0));
	cmn_err(CE_CONT, " BASE 1      = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_BASE1));

}

static void
pcicfg_dump_sriov_config(ddi_acc_handle_t config_handle, uint16_t sriov_cap_ptr)
{
	uint32_t		value;
	int			bar_index;

	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
	cmn_err(CE_CONT, " SRIOV_CONTROL = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET);
	cmn_err(CE_CONT, " SRIOV_INITIAL_VFS = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_TOTAL_VFS_OFFSET);
	cmn_err(CE_CONT, " SRIOV_TOTAL_VFS = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET);
	cmn_err(CE_CONT, " SRIOV_NUM_VFS = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	cmn_err(CE_CONT, " SRIOV_First_VF_OFFSET = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	cmn_err(CE_CONT, " SRIOV_VF_STRIDE = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_DEV_ID_OFFSET);
	cmn_err(CE_CONT, " SRIOV_VF_DEVICE_ID = [0x%x]\n", value);
	value = PCI_XCAP_GET16(config_handle, NULL, sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_SYSTEM_PAGE_SIZE_OFFSET);
	cmn_err(CE_CONT, " SRIOV_SYSTEM_PAGESIZE = [0x%x]\n", value);
	for (bar_index = PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET;
	    bar_index <= PCIE_EXT_CAP_SRIOV_VF_BAR5_OFFSET;
	    bar_index += 4) {
		value = pci_config_get32(config_handle,
		    (bar_index + sriov_cap_ptr));
		cmn_err(CE_CONT, " SRIOV_VF_BAR = [0x%x]\n", value);
	}
}

static void
pcicfg_dump_device_config(ddi_acc_handle_t config_handle)
{
	if ((pcicfg_debug & 1) == 0)
		return;
	pcicfg_dump_common_config(config_handle);

	cmn_err(CE_CONT, " BASE 2      = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_BASE2));
	cmn_err(CE_CONT, " BASE 3      = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_BASE3));
	cmn_err(CE_CONT, " BASE 4      = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_BASE4));
	cmn_err(CE_CONT, " BASE 5      = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_BASE5));
	cmn_err(CE_CONT, " Cardbus CIS = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_CIS));
	cmn_err(CE_CONT, " Sub VID     = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_SUBVENID));
	cmn_err(CE_CONT, " Sub SID     = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_SUBSYSID));
	cmn_err(CE_CONT, " ROM         = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_CONF_ROM));
	cmn_err(CE_CONT, " I Line      = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_ILINE));
	cmn_err(CE_CONT, " I Pin       = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_IPIN));
	cmn_err(CE_CONT, " Max Grant   = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_MIN_G));
	cmn_err(CE_CONT, " Max Latent  = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_CONF_MAX_L));
}

static void
pcicfg_dump_bridge_config(ddi_acc_handle_t config_handle)
{
	if ((pcicfg_debug & 1) == 0)
		return;

	pcicfg_dump_common_config(config_handle);

	cmn_err(CE_CONT, "........................................\n");

	cmn_err(CE_CONT, " Pri Bus     = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_PRIBUS));
	cmn_err(CE_CONT, " Sec Bus     = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_SECBUS));
	cmn_err(CE_CONT, " Sub Bus     = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_SUBBUS));
	cmn_err(CE_CONT, " Latency     = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_LATENCY_TIMER));
	cmn_err(CE_CONT, " I/O Base LO = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_IO_BASE_LOW));
	cmn_err(CE_CONT, " I/O Lim LO  = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_IO_LIMIT_LOW));
	cmn_err(CE_CONT, " Sec. Status = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_SEC_STATUS));
	cmn_err(CE_CONT, " Mem Base    = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_MEM_BASE));
	cmn_err(CE_CONT, " Mem Limit   = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_MEM_LIMIT));
	cmn_err(CE_CONT, " PF Mem Base = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_PF_BASE_LOW));
	cmn_err(CE_CONT, " PF Mem Lim  = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_PF_LIMIT_LOW));
	cmn_err(CE_CONT, " PF Base HI  = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_BCNF_PF_BASE_HIGH));
	cmn_err(CE_CONT, " PF Lim  HI  = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_BCNF_PF_LIMIT_HIGH));
	cmn_err(CE_CONT, " I/O Base HI = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_IO_BASE_HI));
	cmn_err(CE_CONT, " I/O Lim HI  = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_IO_LIMIT_HI));
	cmn_err(CE_CONT, " ROM addr    = [0x%x]\n",
	    pci_config_get32(config_handle, PCI_BCNF_ROM));
	cmn_err(CE_CONT, " Intr Line   = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_ILINE));
	cmn_err(CE_CONT, " Intr Pin    = [0x%x]\n",
	    pci_config_get8(config_handle, PCI_BCNF_IPIN));
	cmn_err(CE_CONT, " Bridge Ctrl = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_BCNF_BCNTRL));
}

#endif


int
_init()
{
	DEBUG0("PCI configurator installed - Fcode Interpretation/21554\n");

	mutex_init(&pcicfg_list_mutex, NULL, MUTEX_DRIVER, NULL);
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	error = mod_remove(&modlinkage);
	if (error != 0) {
		return (error);
	}
	mutex_destroy(&pcicfg_list_mutex);
	return (0);
}

int
_info(modinfop)
struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static uint8_t
pcicfg_get_nslots(dev_info_t *dip, ddi_acc_handle_t handle)
{
	uint8_t num_slots = 0;
	uint16_t cap_ptr;

	if ((PCI_CAP_LOCATE(handle, PCI_CAP_ID_PCI_HOTPLUG,
	    &cap_ptr)) == DDI_SUCCESS) {
		uint32_t config;

		PCI_CAP_PUT8(handle, NULL, cap_ptr, PCI_HP_DWORD_SELECT_OFF,
		    PCI_HP_SLOT_CONFIGURATION_REG);
		config = PCI_CAP_GET32(handle, NULL, cap_ptr,
		    PCI_HP_DWORD_DATA_OFF);
		num_slots = config & 0x1F;
	} else if ((PCI_CAP_LOCATE(handle, PCI_CAP_ID_SLOT_ID, &cap_ptr))
	    == DDI_SUCCESS) {
		uint8_t esr_reg = PCI_CAP_GET8(handle, NULL,
		    cap_ptr, PCI_CAP_ID_REGS_OFF);

		num_slots = PCI_CAPSLOT_NSLOTS(esr_reg);
	} else if ((PCI_CAP_LOCATE(handle, PCI_CAP_ID_PCI_E, &cap_ptr))
	    == DDI_SUCCESS) {
		int port_type = PCI_CAP_GET16(handle, NULL, cap_ptr,
		    PCIE_PCIECAP) & PCIE_PCIECAP_DEV_TYPE_MASK;

		if ((port_type == PCIE_PCIECAP_DEV_TYPE_DOWN) &&
		    (PCI_CAP_GET16(handle, NULL, cap_ptr, PCIE_PCIECAP)
		    & PCIE_PCIECAP_SLOT_IMPL))
				num_slots = 1;
	}

	DEBUG3("%s#%d has %d slots",
	    ddi_get_name(dip), ddi_get_instance(dip), num_slots);

	return (num_slots);
}

/*ARGSUSED*/
static uint8_t
pcicfg_is_chassis(dev_info_t *dip, ddi_acc_handle_t handle)
{
	uint16_t cap_ptr;

	if ((PCI_CAP_LOCATE(handle, PCI_CAP_ID_SLOT_ID, &cap_ptr)) !=
	    DDI_FAILURE) {

		uint8_t esr_reg = PCI_CAP_GET8(handle, NULL, cap_ptr, 2);
		if (PCI_CAPSLOT_FIC(esr_reg))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*ARGSUSED*/
static int
pcicfg_pcie_dev(dev_info_t *dip, int bus_type, pcicfg_err_regs_t *regs)
{
	/* get parent device's device_type property */
	char *device_type;
	int rc = DDI_FAILURE;
	dev_info_t *pdip = ddi_get_parent(dip);

	regs->pcie_dev = 0;
	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, pdip,
	    DDI_PROP_DONTPASS, "device_type", &device_type)
	    != DDI_PROP_SUCCESS) {
		DEBUG2("device_type property missing for %s#%d",
		    ddi_get_name(pdip), ddi_get_instance(pdip));
		return (DDI_FAILURE);
	}
	switch (bus_type) {
		case PCICFG_DEVICE_TYPE_PCIE:
			if (strcmp(device_type, "pciex") == 0) {
				rc = DDI_SUCCESS;
				regs->pcie_dev = 1;
			}
			break;
		case PCICFG_DEVICE_TYPE_PCI:
			if (strcmp(device_type, "pci") == 0)
				rc = DDI_SUCCESS;
			break;
		default:
			break;
	}
	ddi_prop_free(device_type);
	return (rc);
}

/*ARGSUSED*/
static int
pcicfg_pcie_port_type(dev_info_t *dip, ddi_acc_handle_t handle)
{
	int port_type = -1;
	uint16_t cap_ptr;

	if ((PCI_CAP_LOCATE(handle, PCI_CAP_ID_PCI_E, &cap_ptr)) !=
	    DDI_FAILURE)
		port_type = PCI_CAP_GET16(handle, NULL,
		    cap_ptr, PCIE_PCIECAP) & PCIE_PCIECAP_DEV_TYPE_MASK;

	return (port_type);
}

static int
pcicfg_pcie_device_type(dev_info_t *dip, ddi_acc_handle_t handle)
{
	int port_type = pcicfg_pcie_port_type(dip, handle);

	DEBUG1("device port_type = %x\n", port_type);
	/* No PCIe CAP regs, we are not PCIe device_type */
	if (port_type < 0)
		return (DDI_FAILURE);

	/* check for all PCIe device_types */
	if ((port_type == PCIE_PCIECAP_DEV_TYPE_UP) ||
	    (port_type == PCIE_PCIECAP_DEV_TYPE_DOWN) ||
	    (port_type == PCIE_PCIECAP_DEV_TYPE_ROOT) ||
	    (port_type == PCIE_PCIECAP_DEV_TYPE_PCI2PCIE))
		return (DDI_SUCCESS);

	return (DDI_FAILURE);

}

static int
pcicfg_dump_assigned_prop(dev_info_t *dip, char *assigned_prop_name)
{
	pci_regspec_t		*reg;
	int			length;
	int			rcount;
	int			i;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, assigned_prop_name, (caddr_t)&reg,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG1("Failed to read %s property\n", assigned_prop_name);
		return (PCICFG_FAILURE);
	}

	DEBUG1("property name is %s\n", assigned_prop_name);
	rcount = length / sizeof (pci_regspec_t);
	for (i = 0; i < rcount; i++) {
		DEBUG4("pcicfg_dump_assigned - size=%x low=%x mid=%x high=%x\n",
		    reg[i].pci_size_low, reg[i].pci_phys_low,
		    reg[i].pci_phys_mid, reg[i].pci_phys_hi);
	}
	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)reg, length);

	return (PCICFG_SUCCESS);
}

static void
pcicfg_free_vf_bar(dev_info_t *pf_dip)
{
	pcie_bus_t		*bus_p = PCIE_DIP2UPBUS(pf_dip);

	if (bus_p) {
		kmem_free(bus_p->bus_vf_bar_ptr,
		    sizeof (pci_regspec_t) * (PCI_BASE_NUM + 1));
		bus_p->bus_vf_bar_ptr = NULL;
	}
}

int
pcicfg_assign_vf_bars(dev_info_t *pf_dip, ddi_acc_handle_t config_handle)
{
	/*
	 * Assign values to all VF BARs so that it is safe to turn on the
	 * device for accessing the fcode on the PROM. On successful
	 * exit from this function, "vf-assigned-addresses" are created
	 * for all BARs.
	 */
	pci_regspec_t		*vf_assigned_addresses;
	pci_regspec_t		*assigned_addresses;
	int			i, hash_vfs, vf_assigned_len, assigned_len;
	uint16_t		num_VFs;
	pcie_bus_t		*bus_p = PCIE_DIP2UPBUS(pf_dip);
	int			num_vf_bars, num_bars;

	DEBUG1("pcicfg_assign_vf_bars:%s\n", DEVI(pf_dip)->devi_name);

	if (ddi_getlongprop(DDI_DEV_T_ANY, pf_dip, DDI_PROP_DONTPASS,
	    "vf-assigned-addresses", (caddr_t)&vf_assigned_addresses,
	    &vf_assigned_len) != DDI_PROP_SUCCESS) {
		DEBUG0(
		"pcicg_assign_vf_bars: Could not find vf-assigned-addresses\n");
		return (PCICFG_FAILURE);
	}
	num_vf_bars = vf_assigned_len / sizeof (pci_regspec_t);
	if (num_vf_bars >= PCI_BASE_NUM) {
		DEBUG1(
		"pcicfg_assign_vf_bars:num of BARS %d exceeeds PCI_BASE_NUM\n",
		    num_vf_bars);
		return (PCICFG_FAILURE);
	}
	hash_vfs = ddi_getprop(DDI_DEV_T_ANY, pf_dip, DDI_PROP_DONTPASS,
	    "#vfs", -1);
	if (hash_vfs <= 0) {
		DEBUG0("pcicg_assign_vf_bars: #vfs is <= 0\n");
		goto failure;
	}
	num_VFs = PCI_XCAP_GET16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET);
	if (hash_vfs < num_VFs) {
		DEBUG2(
		    "pcicg_assign_vf_bars: #vfs %d is <= num_VFs %d\n",
		    hash_vfs, num_VFs);
		goto failure;
	}
	DEBUG1("pcicfg_assign_vf_bars:num_VFs is %d.\n", num_VFs);
	if (num_VFs == 0) {
		DEBUG0("pcicfg_assign_vf_bars:num_VFs is 0.\n");
		goto failure;
	}
	/*
	 * Allocate memory to store VF BARs plus one extra for PCI_CONF_ROM
	 */
	bus_p->bus_vf_bar_ptr = (pci_regspec_t *)kmem_zalloc(
	    sizeof (pci_regspec_t) * (PCI_BASE_NUM + 1), KM_SLEEP);
	if (bus_p->bus_vf_bar_ptr == NULL) {
		DEBUG0("Failed to allocate memory for VF BAR pointer\n");
		goto failure;
	}
	/*
	 * store the VF BAR info in bus structure of pf dip
	 */
	bcopy(vf_assigned_addresses, bus_p->bus_vf_bar_ptr,
	    vf_assigned_len);
	kmem_free(vf_assigned_addresses, vf_assigned_len);
	if (ddi_getlongprop(DDI_DEV_T_ANY, pf_dip, DDI_PROP_DONTPASS,
	    "assigned-addresses", (caddr_t)&assigned_addresses,
	    &assigned_len) != DDI_PROP_SUCCESS) {
		DEBUG0(
		"pcicg_assign_vf_bars: Could not find assigned-addresses\n");
		return (PCICFG_FAILURE);
	}
	num_bars = assigned_len / sizeof (pci_regspec_t);
	for (i = 0; i < num_bars; i++) {
		if (PCI_REG_REG_G((assigned_addresses + i)->pci_phys_hi) ==
		    PCI_CONF_ROM)
			bcopy((assigned_addresses + i),
			    bus_p->bus_vf_bar_ptr + num_vf_bars,
			    sizeof (pci_regspec_t));
	}
	kmem_free(assigned_addresses, assigned_len);
	return (PCICFG_SUCCESS);
failure:
	kmem_free(vf_assigned_addresses, vf_assigned_len);
	return (PCICFG_FAILURE);

}

static int
pcicfg_probe_VFs(dev_info_t *pf_dip, ddi_acc_handle_t handle)
{
	pcie_req_id_t	pf_bdf;
	pcie_req_id_t	vf_bdf;
	uint16_t	num_VFs, vf_offset, vf_stride;
	uint16_t	vf_num, sriov_control;
	uint32_t	value;
	int		rv, circ;
	dev_info_t	*pdip, *new_device;
	pcie_bus_t	*bus_p = PCIE_DIP2UPBUS(pf_dip);
	char *path = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	(void) ddi_pathname(pf_dip, path);
	if (pcie_get_bdf_from_dip(pf_dip, &pf_bdf) != DDI_SUCCESS) {
		DEBUG1("pcicfg_probe_VFs: could not get PF routing id info."
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	if (bus_p->sriov_cap_ptr == 0) {
		DEBUG1("pcfcfg_probe_VFs:device is not SRIOV capable"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	value = PCI_XCAP_GET16(handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		DEBUG1("pcicfg_probe_VFs: could not get num_VFs\n"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	num_VFs = (uint16_t)value;
	value = PCI_XCAP_GET16(handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		DEBUG1("pcicfg_probe_VFs: could not get VF offset"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	vf_offset = (uint16_t)value;
	value = PCI_XCAP_GET16(handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		DEBUG1("pcicfg_probe_VFs: could not get vf stride"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	vf_stride = (uint16_t)value;
	value = PCI_XCAP_GET16(handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		DEBUG1("pcicfg_probe_VFs: could not get sriov control"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	sriov_control = (uint16_t)value;
	if (!(sriov_control & PCIE_SRIOV_CONTROL_VF_ENABLE)) {
		DEBUG1(
	"pcicfg_probe_VFs: VF ENABLE bit not set in sriov control register"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	if (!(sriov_control & PCIE_SRIOV_CONTROL_MS_ENABLE)) {
		DEBUG1(
	"pcicfg_probe_VFs: MS ENABLE bit not set in sriov control register"
		    " PF device is %s\n", path);
		rv = PCICFG_FAILURE;
		goto exit;
	}
	pdip = ddi_get_parent(pf_dip);
	ndi_devi_enter(pdip, &circ);
	DEBUG1("pcicfg_probe_VFs:probing Vfs for PF device %s\n", path);
	for (vf_bdf = pf_bdf + vf_offset, vf_num = 0; vf_num < num_VFs;
	    vf_num++, vf_bdf += vf_stride) {
		/*
		 * probe the parent of PF device at the vf_bdf.
		 */
		rv = pcicfg_probe_vf(pf_dip, (uint_t)vf_bdf, vf_num);
		switch (rv) {
			case PCICFG_FAILURE:
				DEBUG2("pcicfg_probe_VFs:configure failed: "
				    "vf bdf [0x%x] PF device is %s\n",
				    (uint_t)vf_bdf, path);
				goto cleanup;
			case PCICFG_NODEVICE:
				DEBUG2("pcicfg_probe_VFs:did not find VF "
				    "vf bdf [0x%x] PF device is %s\n",
				    (uint_t)vf_bdf, path);
				goto cleanup;
			default:
				DEBUG2("pcicfg_probe_VFs:configure: "
				    "vf bdf [0x%x] PF device is %s\n",
				    (uint_t)vf_bdf, path);
			break;
		}
		if ((new_device = pcicfg_devi_find(pdip,
		    (vf_bdf & PCIE_REQ_ID_DEV_MASK) >> PCIE_REQ_ID_DEV_SHIFT,
		    (vf_bdf & PCIE_REQ_ID_FUNC_MASK))) == NULL) {
			DEBUG0("pcicfg_probe_VFs:Did'nt find device node "
			    "just created\n");
			goto cleanup;
		}
		bus_p = PCIE_DIP2UPBUS(new_device);
		if (bus_p == NULL) {
			DEBUG2("pcicfg_probe_VFs:bus_p of VF device "
			" bdf [0x%x] is NULL PF device is %s\n",
			    (uint_t)vf_bdf, path);
			goto cleanup;
		}
		if (bus_p->bus_func_type != FUNC_TYPE_VF) {
			DEBUG2("pcicfg_probe_VFs: bus func type is not VF "
			    " bdf = 0x%x PF device is %s\n",
			    vf_bdf, path);
			goto cleanup;
		}
		/* register the VF nodes created */
		rv = pcie_hp_register_port(new_device, pf_dip, NULL);
		if (rv != DDI_SUCCESS) {
			cmn_err(CE_WARN,
		"registering VF device failed. bdf = 0x%x PF device is %s\n",
			    vf_bdf, path);
		}
		bus_p = PCIE_DIP2UPBUS(pf_dip);
	}
	rv = PCICFG_SUCCESS;
	ndi_hold_devi(pf_dip);
	goto exit;
cleanup:
	rv = PCICFG_FAILURE;
	/*
	 * clean up  partially created VF devinfo nodes.
	 */

	for (vf_num = 0; vf_num < num_VFs; vf_num++) {
		vf_bdf = pf_bdf + vf_offset + (vf_stride * vf_num);
		if ((new_device = pcicfg_devi_find(pdip,
		    (vf_bdf & PCIE_REQ_ID_DEV_MASK) >> PCIE_REQ_ID_DEV_SHIFT,
		    (vf_bdf & PCIE_REQ_ID_FUNC_MASK))) == NULL) {
			continue;
		}
		DEBUG1("pcicfg_probe_VFs:Cleaning up bdf [0x%x]", vf_bdf);
		/* unregister the VF nodes created */
		(void) pcie_hp_unregister_port(new_device, pdip, NULL);
		/*
		 * Mark the VF nodes as UNASSIGNED and remove them.
		 */
		DEVI(new_device)->devi_flags &= ~DEVI_ASSIGNED;
		(void) ndi_devi_offline(new_device, NDI_DEVI_REMOVE);
	}
	pcicfg_free_vf_bar(pf_dip);
exit:
	ndi_devi_exit(pdip, circ);
	if (path)
		kmem_free(path, MAXPATHLEN);
	return (rv);
}

static void
pcicfg_reattach_pf(void *arg)
{
	dev_info_t *pf_dip = (dev_info_t *)arg;
	char		*path;
	int		ret = DDI_SUCCESS;


	DEBUG0("pcicfg_reattach_pf starting...\n");
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) ddi_pathname(pf_dip, path);
	ret = ndi_devi_offline(pf_dip, NDI_UNCONFIG);
	if (ret != NDI_SUCCESS) {
		DEBUG1("Could not offline pf_device %s \n", path);
		goto exit;
	}
	ret = ndi_devi_online(pf_dip, NDI_CONFIG);
	if (ret != NDI_SUCCESS) {
		DEBUG1("Could not online pf_device %s \n", path);
		goto exit;
	}
	DEBUG1("pf_device %s reattached successfully \n", path);
exit:
	if (path)
		kmem_free(path, MAXPATHLEN);
}

int
pcicfg_unconfig_vf(dev_info_t *pf_dip)
{
	pcie_bus_t			*bus_p = PCIE_DIP2UPBUS(pf_dip);
	pcie_bus_t			*cdip_busp;
	ddi_acc_handle_t		config_handle = bus_p->bus_cfg_hdl;
	uint16_t			sriov_control;
	uint16_t			num_vf;
	uint32_t			value;
	pciv_config_vf_t		vf_config;
	uint16_t			offset, stride;
	pcie_req_id_t			pf_bdf;
	int				ret;
#ifdef DEBUG
	char				*err_msg;
#endif
	char				*path = NULL;
	dev_info_t			*pdip, *cdip, *next_cdip;

	/*
	 * Make sure that pf_dip is SRIOV capable
	 */

	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	pdip = ddi_get_parent(pf_dip);
	if (bus_p->sriov_cap_ptr == 0) {
		ERR_MSG("device is not SRIOV capable");
		ret = PCIE_CONFIG_VF_ENOT_SRIOV;
		goto pcicfg_unconfig_vf_fail;
	}
	value = PCI_XCAP_GET16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get NUMVFS");
		ret = PCIE_CONFIG_VF_EINITIAL_VF;
		goto pcicfg_unconfig_vf_fail;
	}
	num_vf = (uint16_t)value;
	if (num_vf == 0) {
		ERR_MSG("NUMVFS is 0");
		ret = PCICFG_FAILURE;
		goto pcicfg_unconfig_vf_fail;
	}
	if (pcie_get_bdf_from_dip(pf_dip, &pf_bdf) != DDI_SUCCESS) {
		ERR_MSG(
		    "pcicfg_unconfig_vf: could not get PF routing id info.");
		ret = PCICFG_FAILURE;
		goto pcicfg_unconfig_vf_fail;
	}
	vf_config.num_vf = (uint16_t)num_vf;
	vf_config.first_vf_offset = 0;
	vf_config.vf_stride = 0;
	vf_config.page_size = PCICFG_SRIOV_SYSTEM_PAGE_SIZE;
	if (bus_p->bus_ari == B_TRUE) {
		vf_config.ari_cap = B_TRUE;
	} else {
		vf_config.ari_cap = B_FALSE;
	}
	value = PCI_XCAP_GET16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get VF offset");
		ret = PCIE_CONFIG_VF_EVF_OFFSET;
		goto pcicfg_unconfig_vf_fail;
	}
	offset =  (uint16_t)value;
	if (offset == 0) {
		ERR_MSG("VF offset is 0 !!");
		ret = PCIE_CONFIG_VF_EOFFSET_INVAL;
		goto pcicfg_unconfig_vf_fail;
	}
	vf_config.first_vf_offset = offset;
	value = PCI_XCAP_GET16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get VF stride");
		ret = PCIE_CONFIG_VF_EVF_STRIDE;
		goto pcicfg_unconfig_vf_fail;
	}
	stride = (uint16_t)value;
	if ((num_vf > 1) && (stride == 0)) {
		ERR_MSG("stride is 0 when num_VFs is > 1");
		ret = PCIE_CONFIG_VF_ESTRIDE_INVAL;
		goto pcicfg_unconfig_vf_fail;
	}
	vf_config.vf_stride = stride;
	DEBUG2("First VF OFFSET is 0x%x stride is %d\n",
	    vf_config.first_vf_offset, vf_config.vf_stride);
	vf_config.cmd = PCIV_EVT_VFDISABLE_PRE;
	/* do callback */
	ret = pcie_cb_execute(pf_dip, DDI_CB_PCIV_CONFIG_VF,
	    (void *)(&vf_config));
	ret = NDI_SUCCESS;
	for (cdip = ddi_get_child(pdip);
	    cdip; cdip = ddi_get_next_sibling(cdip)) {
		cdip_busp = PCIE_DIP2UPBUS(cdip);
		if (!cdip_busp || (cdip_busp->bus_pf_dip != pf_dip))
			continue;
		ret = ndi_devi_offline(cdip, NDI_UNCONFIG);
		if (ret != NDI_SUCCESS) {
			(void) ddi_pathname(cdip, path);
			DEBUG1("pcicfg_unconfig_vf: device %s is busy.\n",
			    path);
			break;
		}
	}
	if (ret != NDI_SUCCESS) {
		/*
		 * Make the offlined devices online again.
		 */
		for (cdip = ddi_get_child(pdip);
		    cdip; cdip = ddi_get_next_sibling(cdip)) {
			cdip_busp = PCIE_DIP2UPBUS(cdip);
			if (!cdip_busp || (cdip_busp->bus_pf_dip != pf_dip))
				continue;
			ret = ndi_devi_online(cdip, NDI_CONFIG);
			if (ret != NDI_SUCCESS) {
				(void) ddi_pathname(cdip, path);
				DEBUG1(
				"failed to restore device %s to online state\n",
				    path);
			}
		}
		ERR_MSG("VF device(s) busy. could not be made offline.");
		ret = PCICFG_FAILURE;
		goto pcicfg_unconfig_vf_fail;
	}
	/*
	 * All Vf devices have been made offline.
	 */
	for (cdip = ddi_get_child(pdip); cdip; cdip = next_cdip) {
		cdip_busp = PCIE_DIP2UPBUS(cdip);
		next_cdip = ddi_get_next_sibling(cdip);
		if (!cdip_busp || (cdip_busp->bus_pf_dip != pf_dip))
			continue;
		/* unregister the VF nodes created */
		(void) pcie_hp_unregister_port(cdip, pdip, NULL);
		pcie_fini_bus(cdip, PCIE_BUS_ALL);
		if (ndi_devi_offline(cdip, NDI_DEVI_REMOVE)
		    != NDI_SUCCESS) {
			(void) ddi_pathname(cdip, path);
			DEBUG1("failed to offline and remove device %s\n",
			    path);
			ERR_MSG("Failed to remove VF node.");
			ret = PCICFG_FAILURE;
			goto pcicfg_unconfig_vf_fail;
		}
	}
	value = PCI_XCAP_GET16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("pcicfg_unconfig_vf: could not read sriov control");
		ret = PCICFG_FAILURE;
		goto pcicfg_unconfig_vf_fail;
	}
	sriov_control = (uint16_t)value;
	if (!(sriov_control & PCIE_SRIOV_CONTROL_VF_ENABLE)) {
		ERR_MSG("VF ENABLE bit not set in sriov control register");
		ret = PCICFG_FAILURE;
		goto pcicfg_unconfig_vf_fail;
	}
	sriov_control ^= PCIE_SRIOV_CONTROL_VF_ENABLE;
	ret = PCI_XCAP_PUT16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET, sriov_control);
	if (ret != DDI_SUCCESS) {
		ERR_MSG("unable to set SRIOV control register");
		ret = PCIE_CONFIG_VF_ESET_CONTROL;
		goto pcicfg_unconfig_vf_fail;
	}
	/*
	 * set num_VFs to 0
	 */
	ret = PCI_XCAP_PUT16(config_handle, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET, (uint16_t)0);
	if (ret != PCICFG_SUCCESS) {
		ERR_MSG("unable to set num_VFs");
		ret = PCIE_CONFIG_VF_ESET_NUMVF;
		goto pcicfg_unconfig_vf_fail;
	}
	DEBUG0("Freeing up VF BARS...\n");
	pcicfg_free_vf_bar(pf_dip);
	vf_config.cmd = PCIV_EVT_VFDISABLE_POST;
	/* do callback */
	ret = pcie_cb_execute(pf_dip, DDI_CB_PCIV_CONFIG_VF,
	    (void *)(&vf_config));
	if (path)
		kmem_free(path, MAXPATHLEN);
	ndi_rele_devi(pf_dip);
	return (PCICFG_SUCCESS);
pcicfg_unconfig_vf_fail:
#ifdef DEBUG
	(void) ddi_pathname(pf_dip, path);
	DEBUG2("pcicfg_unconfig_vf:%s:%s\n", path, err_msg);
#endif
	if (path)
		kmem_free(path, MAXPATHLEN);
	return (ret);
}

int
pcicfg_config_vf(dev_info_t *pf_dip)
{
	pcie_bus_t			*bus_p = PCIE_DIP2UPBUS(pf_dip);
	int				num_VFs;
	ddi_acc_handle_t		hdl = bus_p->bus_cfg_hdl;
	pciv_config_vf_t		vf_config;
	uint16_t			sriov_control;
	uint16_t			initial_VFs;
	uint16_t			offset, stride;
	uint_t				bus_num;
	uint32_t			supported_page_sizes;
	int				ret;
#ifdef DEBUG
	char				*err_msg = NULL;
#endif
	char				*path = NULL;
	uint32_t			value;
	dev_info_t			*rcdip, *pf_parent;
	pcie_req_id_t			pf_bdf;
	int				secbus, subbus;

	num_VFs = bus_p->num_vf;

	ndi_hold_devi(pf_dip);
	if (i_ddi_node_state(pf_dip) < DS_PROBED) {
		ERR_MSG("PF device possibly detached");
		ret = PCIE_CONFIG_VF_PF_NOT_ATTACHED;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * Make sure that pf_dip is SRIOV capable
	 */

	if (bus_p->sriov_cap_ptr == 0) {
		ERR_MSG("device is not SRIOV capable");
		ret = PCIE_CONFIG_VF_ENOT_SRIOV;
		goto pcicfg_config_vf_fail;
	}
	if (bus_p->bus_vf_bar_ptr) {
		/* already configured */
		ndi_rele_devi(pf_dip);
		return (PCICFG_SUCCESS);
	}
	(void) pciconf_parse_now(PCICONF_FILE);
	apply_pciconf_configs(pf_dip);
	num_VFs = bus_p->num_vf;
	if (num_VFs == 0) {
		ERR_MSG("num-vf is 0");
		ret = PCIE_CONFIG_VF_EINVAL;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * Validate num_VFs
	 */
	value = PCI_XCAP_GET16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get initial VFs");
		ret = PCIE_CONFIG_VF_EINITIAL_VF;
		goto pcicfg_config_vf_fail;
	}
	initial_VFs = (uint16_t)value;
	if (num_VFs == 0xffff)
		num_VFs = initial_VFs;
	if ((uint16_t)num_VFs > initial_VFs) {
		ERR_MSG("num_VFs is greater than initial_VFs");
		ret = PCIE_CONFIG_VF_EINVAL;
		goto pcicfg_config_vf_fail;
	}
	if (pcie_get_bdf_from_dip(pf_dip, &pf_bdf) != DDI_SUCCESS) {
		ERR_MSG(
		    "pcicfg_config_vf: could not get PF routing id info.\n");
		ret = PCICFG_FAILURE;
		goto pcicfg_config_vf_fail;
	}
	value = PCI_XCAP_GET16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_INITIAL_VFS_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get initial VFs");
		ret = PCIE_CONFIG_VF_ETOTAL_VF;
		goto pcicfg_config_vf_fail;
	}
	initial_VFs = (uint16_t)value;
	value = PCI_XCAP_GET16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("pcicfg_config_vf: could not read sriov control\n");
		ret = PCICFG_FAILURE;
		goto pcicfg_config_vf_fail;
	}
	sriov_control = (uint16_t)value;
	if ((sriov_control & PCIE_SRIOV_CONTROL_VF_ENABLE)) {
		DEBUG0(
	"pcicfg_config_vf: VF ENABLE bit set in sriov control register \n");
		ERR_MSG("VF Enable bit already set in  SRIOV control register");
		ret = PCIE_CONFIG_VF_ESET_CONTROL;
		goto pcicfg_config_vf_fail;
	}
	vf_config.num_vf = (uint16_t)num_VFs;
	vf_config.first_vf_offset = 0;
	vf_config.vf_stride = 0;
	/*
	 * set num_VFs
	 */
	ret = PCI_XCAP_PUT16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_NUMVFS_OFFSET, (uint16_t)num_VFs);
	if (ret != PCICFG_SUCCESS) {
		ERR_MSG("unable to set num_VFs");
		ret = PCIE_CONFIG_VF_ESET_NUMVF;
		goto pcicfg_config_vf_fail;
	}
	/* check if system page size is supported */

	value = PCI_XCAP_GET32(hdl, NULL,
	    bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_SUPPORTED_PAGE_SIZE_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get supported page sizes");
		ret = PCIE_CONFIG_VF_ESUPPORTED_PAGE;
		goto pcicfg_config_vf_fail;
	}
	supported_page_sizes = value;
	vf_config.page_size = PCICFG_SRIOV_SYSTEM_PAGE_SIZE;
	if ((vf_config.page_size & supported_page_sizes) == 0) {
		ERR_MSG("system page size is not not supported by device");
		ret = PCIE_CONFIG_VF_EPAGE_NOTSUP;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * set system page size
	 */
	ret = PCI_XCAP_PUT16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_SYSTEM_PAGE_SIZE_OFFSET,
	    vf_config.page_size);
	if (ret != DDI_SUCCESS) {
		ERR_MSG("unable to set system page size");
		ret = PCIE_CONFIG_VF_ESET_PAGE_SIZE;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * Check if bus_ari is set in the pf_dip. If set
	 * it means that the parent of pf_dip is capable of ARI forwarding.
	 */
	if (bus_p->bus_ari == B_TRUE) {
		vf_config.ari_cap = B_TRUE;
		DEBUG0(" ARI forwarding ENABLED\n");
	} else {
		vf_config.ari_cap = B_FALSE;
		DEBUG0(" ARI forwarding DISABLED\n");
	}
	/*
	 * Check if we have adequate bus resources.
	 */
	value = PCI_XCAP_GET16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_OFFSET_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get VF offset");
		ret = PCIE_CONFIG_VF_EVF_OFFSET;
		goto pcicfg_config_vf_fail;
	}
	offset =  (uint16_t)value;
	if (offset == 0) {
		ERR_MSG("VF offset is 0 !!\n");
		ret = PCIE_CONFIG_VF_EOFFSET_INVAL;
		goto pcicfg_config_vf_fail;
	}
	vf_config.first_vf_offset = offset;
	value = PCI_XCAP_GET16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_STRIDE_OFFSET);
	if (value == PCI_CAP_EINVAL32) {
		ERR_MSG("unable to get VF stride");
		ret = PCIE_CONFIG_VF_EVF_STRIDE;
		goto pcicfg_config_vf_fail;
	}
	stride = (uint16_t)value;
	if ((num_VFs > 1) && (stride == 0)) {
		ERR_MSG("stride is 0 when num_VFs is > 1");
		ret = PCIE_CONFIG_VF_ESTRIDE_INVAL;
		goto pcicfg_config_vf_fail;
	}
	vf_config.vf_stride = stride;
	DEBUG2("First VF OFFSET is 0x%x stride is %d\n",
	    vf_config.first_vf_offset, vf_config.vf_stride);

	/*
	 * compute the maximum bus num required to support VFs.
	 */
	pf_parent = ddi_get_parent(pf_dip);
	/* get secondary bus number */
	rcdip = PCIE_GET_RC_DIP(PCIE_DIP2BUS(pf_parent));
	ASSERT(rcdip != NULL);
	secbus = pci_cfgacc_get8(rcdip, PCIE_DIP2UPBUS(pf_parent)->bus_bdf,
	    PCI_BCNF_SECBUS);
	subbus = pci_cfgacc_get8(rcdip, PCIE_DIP2UPBUS(pf_parent)->bus_bdf,
	    PCI_BCNF_SUBBUS);
	DEBUG2(" parent secbus = 0x%x  parent subbus = 0x%x\n", secbus, subbus);
	DEBUG3("pf bdf = 0x%x offset = 0x%x stride = 0x%x\n",
	    bus_p->bus_bdf, offset, stride);
	bus_num = ((bus_p->bus_bdf + offset + (stride * (num_VFs -1)))
	    >> PCIE_REQ_ID_BUS_SHIFT);
	if ((bus_num < PCIE_DIP2UPBUS(pf_parent)->bus_bus_range.lo) ||
	    (bus_num >  PCIE_DIP2UPBUS(pf_parent)->bus_bus_range.hi)) {
		DEBUG1("pcicfg_config_vf: bus_bdf = 0x%x\n",
		    bus_p->bus_bdf);
		DEBUG5("pcicfg_config_vf: offset = %d stride = %d"
		" required bus_num = %d bus_range_hi = %d"
		" bus_range_lo = %d \n",
		    offset, stride, bus_num,
		    PCIE_DIP2UPBUS(pf_parent)->bus_bus_range.hi,
		    PCIE_DIP2UPBUS(pf_parent)->bus_bus_range.lo);
		ERR_MSG("cannot satisfy VF bus resources\n");
		ret = PCIE_CONFIG_VF_EBUS_NOTSUP;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * Call the PF driver with upcoming config parameters.
	 */
	vf_config.cmd = PCIV_EVT_VFENABLE_PRE;
	/* do callback */
	ret = pcie_cb_execute(pf_dip, DDI_CB_PCIV_CONFIG_VF,
	    (void *)(&vf_config));
	switch (ret) {
		case PCIV_REQRESET:
		case PCIV_REQREATTACH:
			if (i_ddi_node_state(pf_dip) < DS_READY) {
				ERR_MSG(
		"RESET/REATTACH requested when node is not fully attached");
				ret = PCICFG_FAILURE;
				goto pcicfg_config_vf_fail;
			}
			if (ret == PCIV_REQRESET)
				ret = pcie_flr(pf_dip);
			ndi_rele_devi(pf_dip);
			pcicfg_reattach_pf(pf_dip);
			if (bus_p->bus_vf_bar_ptr)
				return (PCICFG_SUCCESS);
			ERR_MSG("VFs not configured after reattach");
			return (PCICFG_FAILURE);

		case DDI_SUCCESS:
			break;
		default:
			ERR_MSG(
			    "call to PF driver during PRE of vf config failed");
			ret = PCIE_CONFIG_VF_ECALLBACK_PRE;
			goto pcicfg_config_vf_fail;
	}
	/*
	 * Alloc resources for VFs by calling hotplug interface.
	 */
	ret = pcicfg_assign_vf_bars(pf_dip, hdl);
	if (ret != PCICFG_SUCCESS) {
		ERR_MSG("failed to allocate vf bars\n");
		ret = PCIE_CONFIG_VF_EVF_BAR_ASSIGN;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * We can now enable VFs
	 */
	sriov_control |= (PCIE_SRIOV_CONTROL_VF_ENABLE |
	    PCIE_SRIOV_CONTROL_MS_ENABLE);
	ret = PCI_XCAP_PUT16(hdl, NULL, bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET, sriov_control);
	if (ret != DDI_SUCCESS) {
		ERR_MSG("unable to set SRIOV control register");
		ret = PCIE_CONFIG_VF_ESET_CONTROL;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * Need to wait atleast 1 second before issuing configuration
	 * requests to VFs enabled. VFs enabled are permitted to return
	 * a CRS status to the configuration requests upto the 1.0s limit,
	 * if they are not ready.
	 */
	delay(MSEC_TO_TICK(1500));
#ifdef DEBUG
	if (pcicfg_debug) {
		pcicfg_dump_common_config(hdl);
		pcicfg_dump_sriov_config(hdl, bus_p->sriov_cap_ptr);
	}
#endif
	/*
	 * probe each of the vf, create devinfo node and
	 * add to the Solaris device tree.
	 */
	ret = pcicfg_probe_VFs(pf_dip, hdl);
	if (ret != PCICFG_SUCCESS) {
		/*
		 * Reset VF ENABLE bit. Preserve other bits.
		 */
		value = PCI_XCAP_GET16(hdl, NULL, bus_p->sriov_cap_ptr,
		    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET);
		if (value == PCI_CAP_EINVAL32) {
			ERR_MSG(
			    "pcicfg_config_vf: could not read sriov control");
			ret = PCICFG_FAILURE;
			goto pcicfg_config_vf_fail;
		}
		sriov_control = (uint16_t)value;
		if (!(sriov_control & PCIE_SRIOV_CONTROL_VF_ENABLE)) {
			ret = PCIE_CONFIG_VF_EPROBE_VF;
			goto pcicfg_config_vf_fail;
		}
		sriov_control ^= PCIE_SRIOV_CONTROL_VF_ENABLE;
		ret = PCI_XCAP_PUT16(hdl, NULL, bus_p->sriov_cap_ptr,
		    PCIE_EXT_CAP_SRIOV_CONTROL_OFFSET, sriov_control);
		if (ret != DDI_SUCCESS) {
			ERR_MSG("unable to reset VF ENABLE bitr");
			ret = PCIE_CONFIG_VF_ESET_CONTROL;
			goto pcicfg_config_vf_fail;
		}
		DEBUG0("pcicfg_config_vf: VF ENABLE bit RESET\n");
		ERR_MSG("failed to create devinfo nodes for VFs");
		ret = PCIE_CONFIG_VF_EPROBE_VF;
		goto pcicfg_config_vf_fail;
	}
	/*
	 * call pf driver to inform that VF is enabled.
	 */
	vf_config.cmd = PCIV_EVT_VFENABLE_POST;
	/* do callback */
	ret = pcie_cb_execute(pf_dip, DDI_CB_PCIV_CONFIG_VF,
	    (void *)(&vf_config));

	if (ret != DDI_SUCCESS) {
		ERR_MSG("call to PF driver during POST of vf config failed");
		ret = PCIE_CONFIG_VF_ECALLBACK_POST;
		goto pcicfg_config_vf_fail;
	}
	ndi_rele_devi(pf_dip);
	return (PCIE_CONFIG_VF_SUCCESS);
pcicfg_config_vf_fail:
#ifdef DEBUG
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) ddi_pathname(pf_dip, path);
	if (err_msg)
		DEBUG2("pcicfg_config_vf:%s:%s\n", path, err_msg);
	kmem_free(path, MAXPATHLEN);
#endif
	ndi_rele_devi(pf_dip);
	return (ret);
}


/*
 * In the following functions ndi_devi_enter() without holding the
 * parent dip is sufficient. This is because  pci dr is driven through
 * opens on the nexus which is in the device tree path above the node
 * being operated on, and implicitly held due to the open.
 */

/*
 * This entry point is called to configure a device (and
 * all its children) on the given bus. It is called when
 * a new device is added to the PCI domain.  This routine
 * will create the device tree and program the devices
 * registers.
 */

int
pcicfg_configure(dev_info_t *devi, uint_t device, uint_t function,
    pcicfg_flags_t flags)
{
	uint_t bus;
	int len;
	int func;
	int trans_device;
	dev_info_t *new_device;
	pcicfg_bus_range_t pci_bus_range;
	int rv;
	int circ;
	uint_t highest_bus = 0;
	int ari_mode = B_FALSE;
	int max_function = PCICFG_MAX_FUNCTION;
	boolean_t is_pcie;

	if (flags == PCICFG_FLAG_ENABLE_ARI)
		return (pcicfg_ari_configure(devi));

	/*
	 * Start probing at the device specified in "device" on the
	 * "bus" specified.
	 */
	len = sizeof (pcicfg_bus_range_t);
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, devi, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&pci_bus_range, &len) != DDI_SUCCESS) {
		DEBUG0("no bus-range property\n");
		return (PCICFG_FAILURE);
	}

	bus = pci_bus_range.lo; /* primary bus number of this bus node */

	is_pcie = is_pcie_fabric(devi);

	ndi_devi_enter(devi, &circ);
	for (func = 0; func < max_function; ) {
		if ((function != PCICFG_ALL_FUNC) && (function != func))
			goto next;

		if (ari_mode)
			trans_device = func >> 3;
		else
			trans_device = device;

		DEBUG3("Configuring [0x%x][0x%x][0x%x]\n",
		    bus, trans_device, func & 7);

		/*
		 * Try executing fcode if available.
		 */
		switch (rv = pcicfg_fcode_probe(devi, bus, trans_device,
		    func & 7, &highest_bus, flags, is_pcie)) {
			case PCICFG_FAILURE:
				DEBUG2("configure failed: "
				    "bus [0x%x] device [0x%x]\n",
				    bus, trans_device);
				break;
			case PCICFG_NODEVICE:
				DEBUG3("no device : bus "
				    "[0x%x] slot [0x%x] func [0x%x]\n",
				    bus, trans_device, func & 7);

				/*
				 * When walking the list of ARI functions
				 * we don't expect to see a non-present
				 * function, so we will stop walking
				 * the function list.
				 */
				if (ari_mode == B_TRUE)
					break;

				if (func)
					goto next;
				break;
			default:
				DEBUG3("configure: bus => [%d] "
				    "slot => [%d] func => [%d]\n",
				    bus, trans_device, func & 7);
				break;
		}

		if (rv != PCICFG_SUCCESS)
			break;

		if ((new_device = pcicfg_devi_find(devi,
		    trans_device, (func & 7))) == NULL) {
			DEBUG0("Did'nt find device node just created\n");
			goto cleanup;
		}

next:
		/*
		 * Determine if ARI Forwarding should be enabled.
		 */
		if (func == 0) {
			if ((pcie_ari_supported(devi)
			    == PCIE_ARI_FORW_SUPPORTED) &&
			    (pcie_ari_device(new_device) == PCIE_ARI_DEVICE)) {
				if (pcie_ari_enable(devi) == DDI_SUCCESS) {
					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    devi,  DDI_PROP_CANSLEEP,
					    "ari-enabled", NULL, 0);

					ari_mode = B_TRUE;
					max_function = PCICFG_MAX_ARI_FUNCTION;
				}
			}
		}

		if (ari_mode == B_TRUE) {
			int next_function;

			DEBUG0("Next Function - ARI Device\n");
			if (pcie_ari_get_next_function(new_device,
			    &next_function) != DDI_SUCCESS)
				goto cleanup;

			/*
			 * Check if there are more fucntions to probe.
			 */
			if (next_function == 0) {
				DEBUG0("Next Function - "
				    "No more ARI Functions\n");
				break;
			}
			func = next_function;
		} else {
			func++;
		}

		DEBUG1("Next Function - %x\n", func);
	}

	ndi_devi_exit(devi, circ);

	if (func == 0)
		return (PCICFG_FAILURE);	/* probe failed */
	else
		return (PCICFG_SUCCESS);

cleanup:
	/*
	 * Clean up a partially created "probe state" tree.
	 * There are no resources allocated to the in the
	 * probe state.
	 */
	if (pcie_ari_is_enabled(devi) == PCIE_ARI_FORW_ENABLED)
		max_function = PCICFG_MAX_ARI_FUNCTION;
	else
		max_function = PCICFG_MAX_FUNCTION;

	for (func = 0; func < max_function; func++) {

		if (max_function == PCICFG_MAX_ARI_FUNCTION)
			trans_device = func >> 3; /* ARI Device */
		else
			trans_device = device;

		if ((new_device = pcicfg_devi_find(devi,
		    trans_device, (func & 0x7))) == NULL) {
			DEBUG0("No more devices to clean up\n");
			continue;
		}

		DEBUG2("Cleaning up device [0x%x] function [0x%x]\n",
		    trans_device, func & 7);
		/*
		 * If this was a bridge device it will have a
		 * probe handle - if not, no harm in calling this.
		 */
		(void) pcicfg_destroy_phdl(new_device);

		if (is_pcie) {
			/*
			 * Free bus_t structure
			 */
			if (ddi_get_child(new_device) != NULL)
				pcie_fab_fini_bus(new_device, PCIE_BUS_ALL);

			pcie_fini_bus(new_device, PCIE_BUS_ALL);
		}
		/*
		 * This will free up the node
		 */
		(void) ndi_devi_offline(new_device, NDI_DEVI_REMOVE);
	}
	ndi_devi_exit(devi, circ);

	return (PCICFG_FAILURE);
}

/*
 * configure the child nodes of ntbridge. new_device points to ntbridge itself
 */
/*ARGSUSED*/
static uint_t
pcicfg_configure_ntbridge(dev_info_t *new_device, uint_t bus, uint_t device)
{
	int bus_range[2], rc = PCICFG_FAILURE, rc1, max_devs = 0;
	int			devno;
	dev_info_t		*new_ntbridgechild;
	ddi_acc_handle_t	config_handle;
	uint16_t		vid;
	uint64_t		next_bus;
	uint64_t		blen;
	ndi_ra_request_t	req;
	uint8_t			pcie_device_type = 0;

	/*
	 * If we need to do indirect config, lets create a property here
	 * to let the child conf map routine know that it has to
	 * go through the DDI calls, and not assume the devices are
	 * mapped directly under the host.
	 */
	if ((rc = ndi_prop_update_int(DDI_DEV_T_NONE, new_device,
	    PCI_DEV_CONF_MAP_PROP, (int)DDI_SUCCESS))
	    != DDI_SUCCESS) {

		DEBUG0("Cannot create indirect conf map property.\n");
		return ((uint_t)PCICFG_FAILURE);
	}
	if (pci_config_setup(new_device, &config_handle) != DDI_SUCCESS)
		return ((uint_t)PCICFG_FAILURE);
	/* check if we are PCIe device */
	if (pcicfg_pcie_device_type(new_device, config_handle) == DDI_SUCCESS)
		pcie_device_type = 1;
	pci_config_teardown(&config_handle);

	/* create Bus node properties for ntbridge. */
	if (pcicfg_set_busnode_props(new_device, pcie_device_type, -1, -1) !=
	    PCICFG_SUCCESS) {
		DEBUG0("Failed to set busnode props\n");
		return (rc);
	}

	/* For now: Lets only support one layer of child */
	bzero((caddr_t)&req, sizeof (ndi_ra_request_t));
	req.ra_len = 1;
	if (ndi_ra_alloc(ddi_get_parent(new_device), &req,
	    &next_bus, &blen, NDI_RA_TYPE_PCI_BUSNUM,
	    NDI_RA_PASS) != NDI_SUCCESS) {
		DEBUG0("ntbridge: Failed to get a bus number\n");
		return (rc);
	}

	DEBUG1("ntbridge bus range start  ->[%d]\n", next_bus);

	/*
	 * Following will change, as we detect more bridges
	 * on the way.
	 */
	bus_range[0] = (int)next_bus;
	bus_range[1] = (int)next_bus;

	if (ndi_prop_update_int_array(DDI_DEV_T_NONE, new_device,
	    "bus-range", bus_range, 2) != DDI_SUCCESS) {
		DEBUG0("Cannot set ntbridge bus-range property");
		return (rc);
	}

	/*
	 * The other interface (away from the host) will be
	 * initialized by the nexus driver when it loads.
	 * We just have to set the registers and the nexus driver
	 * figures out the rest.
	 */

	/*
	 * finally, lets load and attach the driver
	 * before configuring children of ntbridge.
	 */
	rc = ndi_devi_online(new_device, NDI_NO_EVENT|NDI_CONFIG);
	if (rc != NDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "pcicfg: Fail: can\'t load non-transparent bridge \
		    driver.\n");
		rc = PCICFG_FAILURE;
		return (rc);
	}
	DEBUG0("pcicfg: Success loading nontransparent bridge nexus driver..");

	/* Now set aside pci resources for our children. */
	if (pcicfg_ntbridge_allocate_resources(new_device) !=
	    PCICFG_SUCCESS) {
		max_devs = 0;
		rc = PCICFG_FAILURE;
	} else
		max_devs = PCICFG_MAX_DEVICE;

	/* Probe devices on 2nd bus */
	for (devno = pcicfg_start_devno; devno < max_devs; devno++) {

		if (ndi_devi_alloc(new_device, DEVI_PSEUDO_NEXNAME,
		    (pnode_t)DEVI_SID_NODEID, &new_ntbridgechild)
		    != NDI_SUCCESS) {

			DEBUG0("pcicfg: Failed to alloc test node\n");
			rc = PCICFG_FAILURE;
			break;
		}

		if (pcicfg_add_config_reg(new_ntbridgechild,
		    PCI_GETBDF(next_bus, devno, 0)) != DDI_PROP_SUCCESS) {
			cmn_err(CE_WARN,
			    "Failed to add conf reg for ntbridge child.\n");
			(void) ndi_devi_free(new_ntbridgechild);
			rc = PCICFG_FAILURE;
			break;
		}

		if ((rc = pci_config_setup(new_ntbridgechild,
		    &config_handle)) != PCICFG_SUCCESS) {
			cmn_err(CE_WARN,
			    "Cannot map ntbridge child %x\n", devno);
			(void) ndi_devi_free(new_ntbridgechild);
			rc = PCICFG_FAILURE;
			break;
		}

		/*
		 * See if there is any PCI HW at this location
		 * by reading the Vendor ID.  If it returns with 0xffff
		 * then there is no hardware at this location.
		 */
		vid = pci_config_get16(config_handle, PCI_CONF_VENID);

		pci_config_teardown(&config_handle);
		(void) ndi_devi_free(new_ntbridgechild);
		if (vid	== 0xffff)
			continue;

		/* Lets fake attachments points for each child, */
		if (pcicfg_configure(new_device, devno, PCICFG_ALL_FUNC, 0)
		    != PCICFG_SUCCESS) {
			int old_dev = pcicfg_start_devno;

			cmn_err(CE_WARN,
			"Error configuring ntbridge child dev=%d\n", devno);

			rc = PCICFG_FAILURE;
			while (old_dev != devno) {
				if (pcicfg_ntbridge_unconfigure_child(
				    new_device, old_dev) == PCICFG_FAILURE)

					cmn_err(CE_WARN,
					    "Unconfig Error ntbridge child "
					    "dev=%d\n", old_dev);
				old_dev++;
			}
			break;
		}
	} /* devno loop */
	DEBUG1("ntbridge: finish probing 2nd bus, rc=%d\n", rc);

	if (rc != PCICFG_FAILURE)
		rc = pcicfg_ntbridge_configure_done(new_device);
	else {
		pcicfg_phdl_t *entry = pcicfg_find_phdl(new_device);
		uint_t			*bus;
		int			k;

		if (ddi_getlongprop(DDI_DEV_T_ANY, new_device,
		    DDI_PROP_DONTPASS, "bus-range", (caddr_t)&bus,
		    &k) != DDI_PROP_SUCCESS) {
			DEBUG0("Failed to read bus-range property\n");
			rc = PCICFG_FAILURE;
			return (rc);
		}

		DEBUG2("Need to free bus [%d] range [%d]\n",
		    bus[0], bus[1] - bus[0] + 1);

		if (ndi_ra_free(ddi_get_parent(new_device),
		    (uint64_t)bus[0], (uint64_t)(bus[1] - bus[0] + 1),
		    NDI_RA_TYPE_PCI_BUSNUM, NDI_RA_PASS) != NDI_SUCCESS) {
			DEBUG0("Failed to free a bus number\n");
			rc = PCICFG_FAILURE;
			/*
			 * Don't forget to free up memory from ddi_getlongprop
			 */
			kmem_free((caddr_t)bus, k);

			return (rc);
		}

		/*
		 * Since no memory allocations are done for non transparent
		 * bridges (but instead we just set the handle with the
		 * already allocated memory, we just need to reset the
		 * following values before calling the destroy_phdl()
		 * function next, otherwise the it will try to free
		 * memory allocated as in case of a transparent bridge.
		 */
		entry->memory_len = 0;
		entry->io_len = 0;
		/* the following will free hole data. */
		(void) pcicfg_destroy_phdl(new_device);
		/*
		 * Don't forget to free up memory from ddi_getlongprop
		 */
		kmem_free((caddr_t)bus, k);
	}

	/*
	 * Unload driver just in case child configure failed!
	 */
	rc1 = ndi_devi_offline(new_device, NDI_NO_EVENT);
	DEBUG1("pcicfg: now unloading the ntbridge driver. rc1=%d\n", rc1);
	if (rc1 != NDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "pcicfg: can\'t unload ntbridge driver children.\n");
		rc = PCICFG_FAILURE;
	}

	return (rc);
}

static int
pcicfg_ntbridge_allocate_resources(dev_info_t *dip)
{
	pcicfg_phdl_t		*phdl;
	ndi_ra_request_t	*mem_request;
	ndi_ra_request_t	*io_request;
	uint64_t		boundbase, boundlen;

	phdl = pcicfg_find_phdl(dip);
	ASSERT(phdl);

	mem_request = &phdl->mem_req;
	io_request  = &phdl->io_req;

	phdl->error = PCICFG_SUCCESS;

	/* Set Memory space handle for ntbridge */
	if (pcicfg_get_ntbridge_child_range(dip, &boundbase, &boundlen,
	    PCI_BASE_SPACE_MEM) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "ntbridge: Mem resource information failure\n");
		phdl->memory_len  = 0;
		return (PCICFG_FAILURE);
	}
	mem_request->ra_boundbase = boundbase;
	mem_request->ra_boundlen = boundbase + boundlen;
	mem_request->ra_len = boundlen;
	mem_request->ra_align_mask =
	    PCICFG_MEMGRAN - 1; /* 1M alignment on memory space */
	mem_request->ra_flags |= NDI_RA_ALLOC_BOUNDED;

	/*
	 * mem_request->ra_len =
	 * PCICFG_ROUND_UP(mem_request->ra_len, PCICFG_MEMGRAN);
	 */

	phdl->memory_base = phdl->memory_last = boundbase;
	phdl->memory_len  = boundlen;
	phdl->mem_hole.start = phdl->memory_base;
	phdl->mem_hole.len = mem_request->ra_len;
	phdl->mem_hole.next = (hole_t *)NULL;

	DEBUG2("Connector requested [0x%llx], needs [0x%llx] bytes of memory\n",
	    boundlen, mem_request->ra_len);

	/* set up a memory resource map for NT bridge */
	if (ndi_ra_map_setup(dip, NDI_RA_TYPE_MEM) == NDI_FAILURE) {
		DEBUG0("Can not setup ntbridge memory resource map\n");
		return (PCICFG_FAILURE);
	}
	/* initialize the memory map */
	if (ndi_ra_free(dip, boundbase, boundlen, NDI_RA_TYPE_MEM,
	    NDI_RA_PASS) != NDI_SUCCESS) {
		DEBUG0("Can not initalize ntbridge memory resource map\n");
		return (PCICFG_FAILURE);
	}
	/* Set IO space handle for ntbridge */
	if (pcicfg_get_ntbridge_child_range(dip, &boundbase, &boundlen,
	    PCI_BASE_SPACE_IO) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "ntbridge: IO resource information failure\n");
		phdl->io_len  = 0;
		return (PCICFG_FAILURE);
	}
	io_request->ra_len = boundlen;
	io_request->ra_align_mask =
	    PCICFG_IOGRAN - 1;   /* 4K alignment on I/O space */
	io_request->ra_boundbase = boundbase;
	io_request->ra_boundlen = boundbase + boundlen;
	io_request->ra_flags |= NDI_RA_ALLOC_BOUNDED;

	/*
	 * io_request->ra_len =
	 * PCICFG_ROUND_UP(io_request->ra_len, PCICFG_IOGRAN);
	 */

	phdl->io_base = phdl->io_last = (uint32_t)boundbase;
	phdl->io_len  = (uint32_t)boundlen;
	phdl->io_hole.start = phdl->io_base;
	phdl->io_hole.len = io_request->ra_len;
	phdl->io_hole.next = (hole_t *)NULL;

	DEBUG2("Connector requested [0x%llx], needs [0x%llx] bytes of IO\n",
	    boundlen, io_request->ra_len);

	DEBUG2("MEMORY BASE = [0x%x] length [0x%x]\n",
	    phdl->memory_base, phdl->memory_len);
	DEBUG2("IO     BASE = [0x%x] length [0x%x]\n",
	    phdl->io_base, phdl->io_len);

	/* set up a IO resource map for NT bridge */
	if (ndi_ra_map_setup(dip, NDI_RA_TYPE_IO) == NDI_FAILURE) {
		DEBUG0("Can not setup ntbridge memory resource map\n");
		return (PCICFG_FAILURE);
	}
	/* initialize the IO map */
	if (ndi_ra_free(dip, boundbase, boundlen, NDI_RA_TYPE_IO,
	    NDI_RA_PASS) != NDI_SUCCESS) {
		DEBUG0("Can not initalize ntbridge memory resource map\n");
		return (PCICFG_FAILURE);
	}

	return (PCICFG_SUCCESS);
}

static int
pcicfg_ntbridge_configure_done(dev_info_t *dip)
{
	pcicfg_range_t range[PCICFG_RANGE_LEN];
	pcicfg_phdl_t		*entry;
	uint_t			len;
	pcicfg_bus_range_t	bus_range;
	int			new_bus_range[2];

	DEBUG1("Configuring children for %llx\n", dip);

	entry = pcicfg_find_phdl(dip);
	ASSERT(entry);

	bzero((caddr_t)range,
	    sizeof (pcicfg_range_t) * PCICFG_RANGE_LEN);
	range[1].child_hi = range[1].parent_hi |=
	    (PCI_REG_REL_M | PCI_ADDR_MEM32);
	range[1].child_lo = range[1].parent_lo = (uint32_t)entry->memory_base;

	range[0].child_hi = range[0].parent_hi |=
	    (PCI_REG_REL_M | PCI_ADDR_IO);
	range[0].child_lo = range[0].parent_lo = (uint32_t)entry->io_base;

	len = sizeof (pcicfg_bus_range_t);
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&bus_range, (int *)&len) != DDI_SUCCESS) {
		DEBUG0("no bus-range property\n");
		return (PCICFG_FAILURE);
	}

	new_bus_range[0] = bus_range.lo;	/* primary bus number */
	if (entry->highest_bus) {	/* secondary bus number */
		if (entry->highest_bus < bus_range.lo) {
			cmn_err(CE_WARN,
			    "ntbridge bus range invalid !(%d,%d)\n",
			    bus_range.lo, entry->highest_bus);
			new_bus_range[1] = bus_range.lo + entry->highest_bus;
		}
		else
			new_bus_range[1] = entry->highest_bus;
	}
	else
		new_bus_range[1] = bus_range.hi;

	DEBUG2("ntbridge: bus range lo=%x, hi=%x\n",
	    new_bus_range[0], new_bus_range[1]);

	if (ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "bus-range", new_bus_range, 2) != DDI_SUCCESS) {
		DEBUG0("Failed to set bus-range property");
		entry->error = PCICFG_FAILURE;
		return (PCICFG_FAILURE);
	}

#ifdef DEBUG
	{
		uint64_t	unused;
		unused = pcicfg_unused_space(&entry->io_hole, &len);
		DEBUG2("ntbridge: Unused IO space %llx bytes over %d holes\n",
		    unused, len);
	}
#endif

	range[0].size_lo = entry->io_len;
	if (pcicfg_update_ranges_prop(dip, &range[0])) {
		DEBUG0("Failed to update ranges (i/o)\n");
		entry->error = PCICFG_FAILURE;
		return (PCICFG_FAILURE);
	}

#ifdef DEBUG
	{
		uint64_t	unused;
		unused = pcicfg_unused_space(&entry->mem_hole, &len);
		DEBUG2("ntbridge: Unused Mem space %llx bytes over %d holes\n",
		    unused, len);
	}
#endif

	range[1].size_lo = entry->memory_len;
	if (pcicfg_update_ranges_prop(dip, &range[1])) {
		DEBUG0("Failed to update ranges (memory)\n");
		entry->error = PCICFG_FAILURE;
		return (PCICFG_FAILURE);
	}

	return (PCICFG_SUCCESS);
}

static int
pcicfg_ntbridge_unconfigure_child(dev_info_t *new_device, uint_t devno)
{

	dev_info_t	*new_ntbridgechild;
	int 		len, bus;
	uint16_t	vid;
	ddi_acc_handle_t	config_handle;
	pcicfg_bus_range_t pci_bus_range;

	len = sizeof (pcicfg_bus_range_t);
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, new_device, DDI_PROP_DONTPASS,
	    "bus-range", (caddr_t)&pci_bus_range, &len) != DDI_SUCCESS) {
		DEBUG0("no bus-range property\n");
		return (PCICFG_FAILURE);
	}

	bus = pci_bus_range.lo; /* primary bus number of this bus node */

	if (ndi_devi_alloc(new_device, DEVI_PSEUDO_NEXNAME,
	    (pnode_t)DEVI_SID_NODEID, &new_ntbridgechild) != NDI_SUCCESS) {

		DEBUG0("pcicfg: Failed to alloc test node\n");
		return (PCICFG_FAILURE);
	}

	if (pcicfg_add_config_reg(new_ntbridgechild,
	    PCI_GETBDF(bus, devno, 0)) != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN,
		"Unconfigure: Failed to add conf reg prop for ntbridge "
		    "child.\n");
		(void) ndi_devi_free(new_ntbridgechild);
		return (PCICFG_FAILURE);
	}

	if (pcicfg_config_setup(new_ntbridgechild, &config_handle)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "pcicfg: Cannot map ntbridge child %x\n", devno);
		(void) ndi_devi_free(new_ntbridgechild);
		return (PCICFG_FAILURE);
	}

	/*
	 * See if there is any PCI HW at this location
	 * by reading the Vendor ID.  If it returns with 0xffff
	 * then there is no hardware at this location.
	 */
	vid = pci_config_get16(config_handle, PCI_CONF_VENID);

	pci_config_teardown(&config_handle);
	(void) ndi_devi_free(new_ntbridgechild);
	if (vid	== 0xffff)
		return (PCICFG_NODEVICE);

	return (pcicfg_unconfigure(new_device, devno, PCICFG_ALL_FUNC, 0));
}

static int
pcicfg_ntbridge_unconfigure(dev_info_t *dip)
{
	pcicfg_phdl_t *entry = pcicfg_find_phdl(dip);
	uint_t			*bus;
	int			k, rc = PCICFG_FAILURE;

	if (entry->memory_len)
		if (ndi_ra_map_destroy(dip, NDI_RA_TYPE_MEM) == NDI_FAILURE) {
			DEBUG1("cannot destroy ntbridge memory map size=%x\n",
			    entry->memory_len);
			return (PCICFG_FAILURE);
		}
	if (entry->io_len)
		if (ndi_ra_map_destroy(dip, NDI_RA_TYPE_IO) == NDI_FAILURE) {
			DEBUG1("cannot destroy ntbridge io map size=%x\n",
			    entry->io_len);
			return (PCICFG_FAILURE);
		}
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "bus-range", (caddr_t)&bus,
	    &k) != DDI_PROP_SUCCESS) {
		DEBUG0("ntbridge: Failed to read bus-range property\n");
		return (rc);
	}

	DEBUG2("ntbridge: Need to free bus [%d] range [%d]\n",
	    bus[0], bus[1] - bus[0] + 1);

	if (ndi_ra_free(ddi_get_parent(dip),
	    (uint64_t)bus[0], (uint64_t)(bus[1] - bus[0] + 1),
	    NDI_RA_TYPE_PCI_BUSNUM, NDI_RA_PASS) != NDI_SUCCESS) {
		DEBUG0("ntbridge: Failed to free a bus number\n");
		/*
		 * Don't forget to free up memory from ddi_getlongprop
		 */
		kmem_free((caddr_t)bus, k);

		return (rc);
	}

	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)bus, k);

	/*
	 * Since our resources will be freed at the parent level,
	 * just reset these values.
	 */
	entry->memory_len = 0;
	entry->io_len = 0;
	/* the following will also free hole data. */
	return (pcicfg_destroy_phdl(dip));

}

static int
pcicfg_is_ntbridge(dev_info_t *dip)
{
	ddi_acc_handle_t	config_handle;
	uint8_t		class, subclass;
	int		rc = DDI_SUCCESS;

	if (pcicfg_config_setup(dip, &config_handle) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "pcicfg: cannot map config space, to get map type\n");
		return (DDI_FAILURE);
	}
	class = pci_config_get8(config_handle, PCI_CONF_BASCLASS);
	subclass = pci_config_get8(config_handle, PCI_CONF_SUBCLASS);

	/* check for class=6, subclass=9, for non transparent bridges.  */
	if ((class != PCI_CLASS_BRIDGE) || (subclass != PCI_BRIDGE_STBRIDGE))
		rc = DDI_FAILURE;

	DEBUG3("pcicfg: checking device %x,%x for indirect map. rc=%d\n",
	    pci_config_get16(config_handle, PCI_CONF_VENID),
	    pci_config_get16(config_handle, PCI_CONF_DEVID),
	    rc);
	pci_config_teardown(&config_handle);
	return (rc);
}

/*
 * this function is called only for SPARC platforms, where we may have
 * a mix n' match of direct vs indirectly mapped configuration space.
 * On x86, this function does not get called. We always return TRUE
 * via a macro for x86.
 */
/*ARGSUSED*/
static int
pcicfg_indirect_map(dev_info_t *dip)
{
#if defined(__sparc)
	int rc = DDI_FAILURE;

	if (ddi_prop_get_int(DDI_DEV_T_ANY, ddi_get_parent(dip), 0,
	    PCI_DEV_CONF_MAP_PROP, DDI_FAILURE) != DDI_FAILURE)
		rc = DDI_SUCCESS;
	else
		if (ddi_prop_get_int(DDI_DEV_T_ANY, ddi_get_parent(dip),
		    0, PCI_BUS_CONF_MAP_PROP,
		    DDI_FAILURE) != DDI_FAILURE)
			rc = DDI_SUCCESS;
	DEBUG1("pci conf map = %d", rc);
	return (rc);
#else
	return (DDI_SUCCESS);
#endif
}

static uint_t
pcicfg_get_ntbridge_child_range(dev_info_t *dip, uint64_t *boundbase,
				uint64_t *boundlen, uint_t space_type)
{
	int		length, found = DDI_FAILURE, acount, i, ibridge;
	pci_regspec_t	*assigned;

	if ((ibridge = pcicfg_is_ntbridge(dip)) == DDI_FAILURE)
		return (found);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses", (caddr_t)&assigned,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG1("Failed to get assigned-addresses property %llx\n", dip);
		return (found);
	}
	DEBUG1("pcicfg: ntbridge child range: dip = %s\n",
	    ddi_driver_name(dip));

	acount = length / sizeof (pci_regspec_t);

	for (i = 0; i < acount; i++) {
		if ((PCI_REG_REG_G(assigned[i].pci_phys_hi) ==
		    pcicfg_indirect_map_devs[ibridge].mem_range_bar_offset) &&
		    (space_type == PCI_BASE_SPACE_MEM)) {
			found = DDI_SUCCESS;
			break;
		} else {
			if ((PCI_REG_REG_G(assigned[i].pci_phys_hi) ==
			    pcicfg_indirect_map_devs[ibridge].\
			    io_range_bar_offset) &&
			    (space_type == PCI_BASE_SPACE_IO)) {
				found = DDI_SUCCESS;
				break;
			}
		}
	}
	DEBUG3("pcicfg: ntbridge child range: space=%x, base=%lx, len=%lx\n",
	    space_type, assigned[i].pci_phys_low, assigned[i].pci_size_low);

	if (found == DDI_SUCCESS)  {
		*boundbase = assigned[i].pci_phys_low;
		*boundlen = assigned[i].pci_size_low;
	}

	kmem_free(assigned, length);
	return (found);
}

/*
 * This will turn  resources allocated by pcicfg_configure()
 * and remove the device tree from the Hotplug Connection (CN)
 * and below.  The routine assumes the devices have their
 * drivers detached.
 */
int
pcicfg_unconfigure(dev_info_t *devi, uint_t device, uint_t function,
    pcicfg_flags_t flags)
{
	dev_info_t *child_dip;
	int func;
	int i;
	int max_function;
	int trans_device;
	int circ;
	boolean_t is_pcie;

	if (pcie_ari_is_enabled(devi) == PCIE_ARI_FORW_ENABLED)
		max_function = PCICFG_MAX_ARI_FUNCTION;
	else
		max_function = PCICFG_MAX_FUNCTION;

	/*
	 * Cycle through devices to make sure none are busy.
	 * If a single device is busy fail the whole unconfigure.
	 */
	is_pcie = is_pcie_fabric(devi);

	ndi_devi_enter(devi, &circ);
	for (func = 0; func < max_function; func++) {

		if (max_function == PCICFG_MAX_ARI_FUNCTION)
			trans_device = func >> 3; /* ARI Device */
		else
			trans_device = device;

		if ((child_dip = pcicfg_devi_find(devi, trans_device,
		    (func & 0x7))) == NULL)
			continue;

		if (ndi_devi_offline(child_dip, NDI_UNCONFIG) == NDI_SUCCESS)
			continue;
		/*
		 * Device function is busy. Before returning we have to
		 * put all functions back online which were taken
		 * offline during the process.
		 */
		DEBUG2("Device [0x%x] function [%x] is busy\n", device, func);
		/*
		 * If we are only asked to offline one specific function,
		 * and that fails, we just simply return.
		 */
		if (function != PCICFG_ALL_FUNC)
			return (PCICFG_FAILURE);

		for (i = 0; i < func; i++) {

			if (max_function == PCICFG_MAX_ARI_FUNCTION)
				trans_device = i >> 3;

			if ((child_dip =
			    pcicfg_devi_find(devi, trans_device, (i & 7)))
			    == NULL) {
				DEBUG0(
				    "No more devices to put back on line!!\n");
				/*
				 * Made it through all functions
				 */
				continue;
			}
			if (ndi_devi_online(child_dip, NDI_CONFIG)
			    != NDI_SUCCESS) {
				DEBUG0("Failed to put back devices state\n");
				goto fail;
			}
		}
		goto fail;
	}

	/*
	 * Now, tear down all devinfo nodes for this Connector.
	 */
	for (func = 0; func < max_function; func++) {

		if (max_function == PCICFG_MAX_ARI_FUNCTION)
			trans_device = func >> 3; /* ARI Device */
		else
			trans_device = device;

		if ((child_dip = pcicfg_devi_find(devi,
		    trans_device, (func & 7))) == NULL) {
			DEBUG0("No more devices to tear down!\n");
			continue;
		}

		DEBUG2("Tearing down device [0x%x] function [0x%x]\n",
		    trans_device, (func & 7));

		if (pcicfg_is_ntbridge(child_dip) != DDI_FAILURE)
			if (pcicfg_ntbridge_unconfigure(child_dip) !=
			    PCICFG_SUCCESS) {
				cmn_err(CE_WARN,
				    "ntbridge: unconfigure failed\n");
				goto fail;
			}

		if (pcicfg_teardown_device(child_dip, flags, is_pcie)
		    != PCICFG_SUCCESS) {
			DEBUG2("Failed to tear down device [0x%x]"
			    "function [0x%x]\n",
			    trans_device, func & 7);
			goto fail;
		}
	}

	if (pcie_ari_is_enabled(devi) == PCIE_ARI_FORW_ENABLED) {
		(void) ddi_prop_remove(DDI_DEV_T_NONE, devi, "ari-enabled");
		(void) pcie_ari_disable(devi);
	}

	ndi_devi_exit(devi, circ);
	return (PCICFG_SUCCESS);

fail:
	ndi_devi_exit(devi, circ);
	return (PCICFG_FAILURE);
}

/*ARGSUSED*/
static int
pcicfg_destroy_ra_map(dev_info_t *dip, void *arg)
{
	pci_resource_destroy(dip);
	return (DDI_WALK_CONTINUE);
}

static int
pcicfg_teardown_device(dev_info_t *dip, pcicfg_flags_t flags, boolean_t is_pcie)
{
	ddi_acc_handle_t	config_handle;
	int			circular_count;

	/*
	 * Free up resources associated with 'dip'
	 */
	if (pcicfg_free_resources(dip, flags) != PCICFG_SUCCESS) {
		DEBUG0("Failed to free resources\n");
		return (PCICFG_FAILURE);
	}

	/*
	 * This will disable the device
	 */
	if (pci_config_setup(dip, &config_handle) != PCICFG_SUCCESS) {
		return (PCICFG_FAILURE);
	}

	pcicfg_device_off(config_handle);
	pci_config_teardown(&config_handle);

	/*
	 * free pcie_bus_t for the sub-tree
	 */
	if (is_pcie) {
		if (ddi_get_child(dip) != NULL)
			pcie_fab_fini_bus(dip, PCIE_BUS_ALL);

		pcie_fini_bus(dip, PCIE_BUS_ALL);
	}
	(void) pcicfg_destroy_ra_map(dip, NULL);
	ndi_devi_enter(dip, &circular_count);
	ddi_walk_devs(ddi_get_child(dip), pcicfg_destroy_ra_map, NULL);
	ndi_devi_exit(dip, circular_count);

	/*
	 * The framework provides this routine which can
	 * tear down a sub-tree.
	 */
	if (ndi_devi_offline(dip, NDI_DEVI_REMOVE) != NDI_SUCCESS) {
		DEBUG0("Failed to offline and remove node\n");
		return (PCICFG_FAILURE);
	}

	return (PCICFG_SUCCESS);
}

/*
 * BEGIN GENERIC SUPPORT ROUTINES
 */
static pcicfg_phdl_t *
pcicfg_find_phdl(dev_info_t *dip)
{
	pcicfg_phdl_t *entry;
	mutex_enter(&pcicfg_list_mutex);
	for (entry = pcicfg_phdl_list; entry != NULL; entry = entry->next) {
		if (entry->dip == dip) {
			mutex_exit(&pcicfg_list_mutex);
			return (entry);
		}
	}
	mutex_exit(&pcicfg_list_mutex);

	/*
	 * Did'nt find entry - create one
	 */
	return (pcicfg_create_phdl(dip));
}

static pcicfg_phdl_t *
pcicfg_create_phdl(dev_info_t *dip)
{
	pcicfg_phdl_t *new;

	new = (pcicfg_phdl_t *)kmem_zalloc(sizeof (pcicfg_phdl_t),
	    KM_SLEEP);

	new->dip = dip;
	mutex_enter(&pcicfg_list_mutex);
	new->next = pcicfg_phdl_list;
	pcicfg_phdl_list = new;
	mutex_exit(&pcicfg_list_mutex);

	return (new);
}

static int
pcicfg_destroy_phdl(dev_info_t *dip)
{
	pcicfg_phdl_t *entry;
	pcicfg_phdl_t *follow = NULL;

	mutex_enter(&pcicfg_list_mutex);
	for (entry = pcicfg_phdl_list; entry != NULL; follow = entry,
	    entry = entry->next) {
		if (entry->dip == dip) {
			if (entry == pcicfg_phdl_list) {
				pcicfg_phdl_list = entry->next;
			} else {
				follow->next = entry->next;
			}
			/*
			 * If this entry has any allocated memory
			 * or IO space associated with it, that
			 * must be freed up.
			 */
			if (entry->memory_len > 0) {
				(void) ndi_ra_free(ddi_get_parent(dip),
				    entry->memory_base,
				    entry->memory_len,
				    NDI_RA_TYPE_MEM, NDI_RA_PASS);
			}
			pcicfg_free_hole(&entry->mem_hole);

			if (entry->io_len > 0) {
				(void) ndi_ra_free(ddi_get_parent(dip),
				    entry->io_base,
				    entry->io_len,
				    NDI_RA_TYPE_IO, NDI_RA_PASS);
			}
			pcicfg_free_hole(&entry->io_hole);

			/*
			 * Destroy this entry
			 */
			kmem_free((caddr_t)entry, sizeof (pcicfg_phdl_t));
			mutex_exit(&pcicfg_list_mutex);
			return (PCICFG_SUCCESS);
		}
	}
	mutex_exit(&pcicfg_list_mutex);
	/*
	 * Did'nt find the entry
	 */
	return (PCICFG_FAILURE);
}

static int
pcicfg_program_ap(dev_info_t *dip)
{
	pcicfg_phdl_t *phdl;
	uint8_t header_type;
	ddi_acc_handle_t handle;
	pcicfg_phdl_t *entry;

	if (pcicfg_config_setup(dip, &handle) != DDI_SUCCESS) {
		DEBUG0("Failed to map config space!\n");
		return (PCICFG_FAILURE);

	}

	header_type = pci_config_get8(handle, PCI_CONF_HEADER);

	(void) pcicfg_config_teardown(&handle);

	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {

		if (pcicfg_allocate_chunk(dip) != PCICFG_SUCCESS) {
			DEBUG0("Not enough memory to hotplug\n");
			(void) pcicfg_destroy_phdl(dip);
			return (PCICFG_FAILURE);
		}

		phdl = pcicfg_find_phdl(dip);
		ASSERT(phdl);

		(void) pcicfg_bridge_assign(dip, (void *)phdl);

		if (phdl->error != PCICFG_SUCCESS) {
			DEBUG0("Problem assigning bridge\n");
			(void) pcicfg_destroy_phdl(dip);
			return (phdl->error);
		}

		/*
		 * Successfully allocated and assigned
		 * memory.  Set the memory and IO length
		 * to zero so when the handle is freed up
		 * it will not de-allocate assigned resources.
		 */
		entry = (pcicfg_phdl_t *)phdl;

		entry->memory_len = entry->io_len = 0;

		/*
		 * Free up the "entry" structure.
		 */
		(void) pcicfg_destroy_phdl(dip);
	} else {
		if (pcicfg_device_assign(dip) != PCICFG_SUCCESS) {
			return (PCICFG_FAILURE);
		}
	}
	return (PCICFG_SUCCESS);
}

static int
pcicfg_bridge_assign(dev_info_t *dip, void *hdl)
{
	ddi_acc_handle_t handle;
	pci_regspec_t *reg;
	int length;
	int rcount;
	int i;
	int offset;
	uint64_t mem_answer;
	uint32_t io_answer;
	int count;
	uint8_t header_type;
	pcicfg_range_t range[PCICFG_RANGE_LEN];
	int bus_range[2];

	pcicfg_phdl_t *entry = (pcicfg_phdl_t *)hdl;

	DEBUG1("bridge assign: assigning addresses to %s\n", ddi_get_name(dip));

	if (entry == NULL) {
		DEBUG0("Failed to get entry\n");
		return (DDI_WALK_TERMINATE);
	}

	entry->error = PCICFG_SUCCESS;

	if (pcicfg_config_setup(dip, &handle) != DDI_SUCCESS) {
		DEBUG0("Failed to map config space!\n");
		entry->error = PCICFG_FAILURE;
		return (DDI_WALK_TERMINATE);
	}

	header_type = pci_config_get8(handle, PCI_CONF_HEADER);

	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {

		bzero((caddr_t)range,
		    sizeof (pcicfg_range_t) * PCICFG_RANGE_LEN);

		(void) pcicfg_setup_bridge(entry, handle, dip);

		range[0].child_hi = range[0].parent_hi |=
		    (PCI_REG_REL_M | PCI_ADDR_IO);
		range[0].child_lo = range[0].parent_lo =
		    entry->io_last;
		range[1].child_hi = range[1].parent_hi |=
		    (PCI_REG_REL_M | PCI_ADDR_MEM32);
		range[1].child_lo = range[1].parent_lo =
		    entry->memory_last;

		ndi_devi_enter(dip, &count);
		ddi_walk_devs(ddi_get_child(dip),
		    pcicfg_bridge_assign, (void *)entry);
		ndi_devi_exit(dip, count);

		(void) pcicfg_update_bridge(entry, handle);

		bus_range[0] = pci_config_get8(handle, PCI_BCNF_SECBUS);
		bus_range[1] = pci_config_get8(handle, PCI_BCNF_SUBBUS);

		if (ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
		    "bus-range", bus_range, 2) != DDI_SUCCESS) {
			DEBUG0("Failed to set bus-range property");
			entry->error = PCICFG_FAILURE;
			return (DDI_WALK_TERMINATE);
		}

		if (entry->io_len > 0) {
			range[0].size_lo = entry->io_last - entry->io_base;
			if (pcicfg_update_ranges_prop(dip, &range[0])) {
				DEBUG0("Failed to update ranges (i/o)\n");
				entry->error = PCICFG_FAILURE;
				return (DDI_WALK_TERMINATE);
			}
		}
		if (entry->memory_len > 0) {
			range[1].size_lo =
			    entry->memory_last - entry->memory_base;
			if (pcicfg_update_ranges_prop(dip, &range[1])) {
				DEBUG0("Failed to update ranges (memory)\n");
				entry->error = PCICFG_FAILURE;
				return (DDI_WALK_TERMINATE);
			}
		}

		(void) pcicfg_device_on(handle);

		PCICFG_DUMP_BRIDGE_CONFIG(handle);

		return (DDI_WALK_PRUNECHILD);
	}

	/*
	 * If there is an interrupt pin set program
	 * interrupt line with default values.
	 */
	if (pci_config_get8(handle, PCI_CONF_IPIN)) {
		pci_config_put8(handle, PCI_CONF_ILINE, 0xf);
	}

	/*
	 * A single device (under a bridge).
	 * For each "reg" property with a length, allocate memory
	 * and program the base registers.
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (caddr_t)&reg,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read reg property\n");
		entry->error = PCICFG_FAILURE;
		return (DDI_WALK_TERMINATE);
	}

	rcount = length / sizeof (pci_regspec_t);
	offset = PCI_CONF_BASE0;
	for (i = 0; i < rcount; i++) {
		if ((reg[i].pci_size_low != 0)||
		    (reg[i].pci_size_hi != 0)) {

			offset = PCI_REG_REG_G(reg[i].pci_phys_hi);

			switch (PCI_REG_ADDR_G(reg[i].pci_phys_hi)) {
			case PCI_REG_ADDR_G(PCI_ADDR_MEM64):

				(void) pcicfg_get_mem(entry,
				    reg[i].pci_size_low, &mem_answer);
				pci_config_put64(handle, offset, mem_answer);
				DEBUG2("REGISTER off %x (64)LO ----> [0x%x]\n",
				    offset,
				    pci_config_get32(handle, offset));
				DEBUG2("REGISTER off %x (64)HI ----> [0x%x]\n",
				    offset + 4,
				    pci_config_get32(handle, offset + 4));

				reg[i].pci_phys_low = PCICFG_HIADDR(mem_answer);
				reg[i].pci_phys_mid  =
				    PCICFG_LOADDR(mem_answer);

				break;

			case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
				/* allocate memory space from the allocator */

				(void) pcicfg_get_mem(entry,
				    reg[i].pci_size_low, &mem_answer);
				pci_config_put32(handle,
				    offset, (uint32_t)mem_answer);

				DEBUG2("REGISTER off %x(32)LO ----> [0x%x]\n",
				    offset,
				    pci_config_get32(handle, offset));

				reg[i].pci_phys_low = (uint32_t)mem_answer;

				break;
			case PCI_REG_ADDR_G(PCI_ADDR_IO):
				/* allocate I/O space from the allocator */

				(void) pcicfg_get_io(entry,
				    reg[i].pci_size_low, &io_answer);
				pci_config_put32(handle, offset, io_answer);

				DEBUG2("REGISTER off %x (I/O)LO ----> [0x%x]\n",
				    offset,
				    pci_config_get32(handle, offset));

				reg[i].pci_phys_low = io_answer;

				break;
			default:
				DEBUG0("Unknown register type\n");
				kmem_free(reg, length);
				(void) pcicfg_config_teardown(&handle);
				entry->error = PCICFG_FAILURE;
				return (DDI_WALK_TERMINATE);
			} /* switch */

			/*
			 * Now that memory locations are assigned,
			 * update the assigned address property.
			 */
			if (pcicfg_update_assigned_prop(dip,
			    &reg[i], "assigned-addresses") != PCICFG_SUCCESS) {
				kmem_free(reg, length);
				(void) pcicfg_config_teardown(&handle);
				entry->error = PCICFG_FAILURE;
				return (DDI_WALK_TERMINATE);
			}
		}
	}
	(void) pcicfg_device_on(handle);

	PCICFG_DUMP_DEVICE_CONFIG(handle);

	(void) pcicfg_config_teardown(&handle);
	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)reg, length);

	return (DDI_WALK_CONTINUE);
}

static int
pcicfg_device_assign(dev_info_t *dip)
{
	ddi_acc_handle_t	handle;
	pci_regspec_t		*reg;
	int			length;
	int			rcount;
	int			i;
	int			offset;
	ndi_ra_request_t	request;
	uint64_t		answer;
	uint64_t		alen;

	DEBUG1("%llx now under configuration\n", dip);

	/*
	 * XXX Failure here should be noted
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (caddr_t)&reg,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read reg property\n");
		return (PCICFG_FAILURE);
	}

	if (pcicfg_config_setup(dip, &handle) != DDI_SUCCESS) {
		DEBUG0("Failed to map config space!\n");
		/*
		 * Don't forget to free up memory from ddi_getlongprop
		 */
		kmem_free((caddr_t)reg, length);

		return (PCICFG_FAILURE);
	}

	/*
	 * A single device
	 *
	 * For each "reg" property with a length, allocate memory
	 * and program the base registers.
	 */

	/*
	 * If there is an interrupt pin set program
	 * interrupt line with default values.
	 */
	if (pci_config_get8(handle, PCI_CONF_IPIN)) {
		pci_config_put8(handle, PCI_CONF_ILINE, 0xf);
	}

	bzero((caddr_t)&request, sizeof (ndi_ra_request_t));

	request.ra_flags = NDI_RA_ALIGN_SIZE;
	request.ra_boundbase = 0;
	request.ra_boundlen = PCICFG_4GIG_LIMIT;

	rcount = length / sizeof (pci_regspec_t);
	for (i = 0; i < rcount; i++) {
		if ((reg[i].pci_size_low != 0)||
		    (reg[i].pci_size_hi != 0)) {

			offset = PCI_REG_REG_G(reg[i].pci_phys_hi);
			request.ra_len = reg[i].pci_size_low;

			switch (PCI_REG_ADDR_G(reg[i].pci_phys_hi)) {
			case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
				request.ra_flags &= ~NDI_RA_ALLOC_BOUNDED;
				/* allocate memory space from the allocator */
				if (ndi_ra_alloc(ddi_get_parent(dip),
				    &request, &answer, &alen,
				    NDI_RA_TYPE_MEM, NDI_RA_PASS)
				    != NDI_SUCCESS) {
					DEBUG0("Failed to allocate 64b mem\n");
					kmem_free(reg, length);
					(void) pcicfg_config_teardown(&handle);
					return (PCICFG_FAILURE);
				}
				DEBUG3("64 addr = [0x%x.%x] len [0x%x]\n",
				    PCICFG_HIADDR(answer),
				    PCICFG_LOADDR(answer),
				    alen);
				/* program the low word */
				pci_config_put32(handle,
				    offset, PCICFG_LOADDR(answer));

				/* program the high word */
				pci_config_put32(handle, offset + 4,
				    PCICFG_HIADDR(answer));

				reg[i].pci_phys_low = PCICFG_LOADDR(answer);
				reg[i].pci_phys_mid = PCICFG_HIADDR(answer);

				/* adjust to 32b address space when possible */
				if ((answer + alen) <= PCICFG_4GIG_LIMIT)
					reg[i].pci_phys_hi ^=
					    PCI_ADDR_MEM64 ^ PCI_ADDR_MEM32;
				break;

			case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
				request.ra_flags |= NDI_RA_ALLOC_BOUNDED;
				/* allocate memory space from the allocator */
				if (ndi_ra_alloc(ddi_get_parent(dip),
				    &request, &answer, &alen,
				    NDI_RA_TYPE_MEM, NDI_RA_PASS)
				    != NDI_SUCCESS) {
					DEBUG0("Failed to allocate 32b mem\n");
					kmem_free(reg, length);
					(void) pcicfg_config_teardown(&handle);
					return (PCICFG_FAILURE);
				}
				DEBUG3("32 addr = [0x%x.%x] len [0x%x]\n",
				    PCICFG_HIADDR(answer),
				    PCICFG_LOADDR(answer),
				    alen);
				/* program the low word */
				pci_config_put32(handle,
				    offset, PCICFG_LOADDR(answer));

				reg[i].pci_phys_low = PCICFG_LOADDR(answer);
				break;

			case PCI_REG_ADDR_G(PCI_ADDR_IO):
				/* allocate I/O space from the allocator */
				request.ra_flags |= NDI_RA_ALLOC_BOUNDED;
				if (ndi_ra_alloc(ddi_get_parent(dip),
				    &request, &answer, &alen,
				    NDI_RA_TYPE_IO, NDI_RA_PASS)
				    != NDI_SUCCESS) {
					DEBUG0("Failed to allocate I/O\n");
					kmem_free(reg, length);
					(void) pcicfg_config_teardown(&handle);
					return (PCICFG_FAILURE);
				}
				DEBUG3("I/O addr = [0x%x.%x] len [0x%x]\n",
				    PCICFG_HIADDR(answer),
				    PCICFG_LOADDR(answer),
				    alen);
				pci_config_put32(handle,
				    offset, PCICFG_LOADDR(answer));

				reg[i].pci_phys_low = PCICFG_LOADDR(answer);
				break;

			default:
				DEBUG0("Unknown register type\n");
				kmem_free(reg, length);
				(void) pcicfg_config_teardown(&handle);
				return (PCICFG_FAILURE);
			} /* switch */

			/*
			 * Now that memory locations are assigned,
			 * update the assigned address property.
			 */

			if (pcicfg_update_assigned_prop(dip,
			    &reg[i], "assigned-addresses") != PCICFG_SUCCESS) {
				kmem_free(reg, length);
				(void) pcicfg_config_teardown(&handle);
				return (PCICFG_FAILURE);
			}
		}
	}

	(void) pcicfg_device_on(handle);
	kmem_free(reg, length);

	PCICFG_DUMP_DEVICE_CONFIG(handle);

	(void) pcicfg_config_teardown(&handle);
	return (PCICFG_SUCCESS);
}

static int
pcicfg_device_assign_readonly(dev_info_t *dip)
{
	ddi_acc_handle_t	handle;
	pci_regspec_t		*assigned;
	int			length;
	int			acount;
	int			i;
	ndi_ra_request_t	request;
	uint64_t		answer;
	uint64_t		alen;


	DEBUG1("%llx now under configuration\n", dip);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses", (caddr_t)&assigned,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read assigned-addresses property\n");
		return (PCICFG_FAILURE);
	}

	if (pcicfg_config_setup(dip, &handle) != DDI_SUCCESS) {
		DEBUG0("Failed to map config space!\n");
		/*
		 * Don't forget to free up memory from ddi_getlongprop
		 */
		kmem_free((caddr_t)assigned, length);

		return (PCICFG_FAILURE);
	}

	/*
	 * For each "assigned-addresses" property entry with a length,
	 * call the memory allocation routines to return the
	 * resource.
	 */
	/*
	 * If there is an interrupt pin set program
	 * interrupt line with default values.
	 */
	if (pci_config_get8(handle, PCI_CONF_IPIN)) {
		pci_config_put8(handle, PCI_CONF_ILINE, 0xf);
	}

	bzero((caddr_t)&request, sizeof (ndi_ra_request_t));

	request.ra_flags = NDI_RA_ALLOC_SPECIFIED; /* specified addr */
	request.ra_boundbase = 0;
	request.ra_boundlen = PCICFG_4GIG_LIMIT;

	acount = length / sizeof (pci_regspec_t);
	for (i = 0; i < acount; i++) {
		if ((assigned[i].pci_size_low != 0)||
		    (assigned[i].pci_size_hi != 0)) {

			request.ra_len = assigned[i].pci_size_low;

			switch (PCI_REG_ADDR_G(assigned[i].pci_phys_hi)) {
			case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
				request.ra_addr = (uint64_t)PCICFG_LADDR(
				    assigned[i].pci_phys_low,
				    assigned[i].pci_phys_mid);

				/* allocate memory space from the allocator */
				if (ndi_ra_alloc(ddi_get_parent(dip),
				    &request, &answer, &alen,
				    NDI_RA_TYPE_MEM, NDI_RA_PASS)
				    != NDI_SUCCESS) {
					DEBUG0("Failed to allocate 64b mem\n");
					kmem_free(assigned, length);
					return (PCICFG_FAILURE);
				}

				break;
			case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
				request.ra_addr = (uint64_t)
				    assigned[i].pci_phys_low;

				/* allocate memory space from the allocator */
				if (ndi_ra_alloc(ddi_get_parent(dip),
				    &request, &answer, &alen,
				    NDI_RA_TYPE_MEM, NDI_RA_PASS)
				    != NDI_SUCCESS) {
					DEBUG0("Failed to allocate 32b mem\n");
					kmem_free(assigned, length);
					return (PCICFG_FAILURE);
				}

				break;
			case PCI_REG_ADDR_G(PCI_ADDR_IO):
				request.ra_addr = (uint64_t)
				    assigned[i].pci_phys_low;

				/* allocate I/O space from the allocator */
				if (ndi_ra_alloc(ddi_get_parent(dip),
				    &request, &answer, &alen,
				    NDI_RA_TYPE_IO, NDI_RA_PASS)
				    != NDI_SUCCESS) {
					DEBUG0("Failed to allocate I/O\n");
					kmem_free(assigned, length);
					return (PCICFG_FAILURE);
				}

				break;
			default:
				DEBUG0("Unknown register type\n");
				kmem_free(assigned, length);
				return (PCICFG_FAILURE);
			} /* switch */
		}
	}

	(void) pcicfg_device_on(handle);
	kmem_free(assigned, length);

	PCICFG_DUMP_DEVICE_CONFIG(handle);

	(void) pcicfg_config_teardown(&handle);
	return (PCICFG_SUCCESS);
}

/*
 * The "dip" passed to this routine is assumed to be
 * the device at the Hotplug Connection (CN). Currently it is
 * assumed to be a bridge.
 */
static int
pcicfg_allocate_chunk(dev_info_t *dip)
{
	pcicfg_phdl_t		*phdl;
	ndi_ra_request_t	*mem_request;
	ndi_ra_request_t	*io_request;
	uint64_t		mem_answer;
	uint64_t		io_answer;
	int			count;
	uint64_t		alen;

	/*
	 * This should not find an existing entry - so
	 * it will create a new one.
	 */
	phdl = pcicfg_find_phdl(dip);
	ASSERT(phdl);

	mem_request = &phdl->mem_req;
	io_request  = &phdl->io_req;

	/*
	 * From this point in the tree - walk the devices,
	 * The function passed in will read and "sum" up
	 * the memory and I/O requirements and put them in
	 * structure "phdl".
	 */
	ndi_devi_enter(ddi_get_parent(dip), &count);
	ddi_walk_devs(dip, pcicfg_sum_resources, (void *)phdl);
	ndi_devi_exit(ddi_get_parent(dip), count);

	if (phdl->error != PCICFG_SUCCESS) {
		DEBUG0("Failure summing resources\n");
		return (phdl->error);
	}

	/*
	 * Call into the memory allocator with the request.
	 * Record the addresses returned in the phdl
	 */
	DEBUG1("Connector requires [0x%x] bytes of memory space\n",
	    mem_request->ra_len);
	DEBUG1("Connector requires [0x%x] bytes of I/O space\n",
	    io_request->ra_len);

	mem_request->ra_align_mask =
	    PCICFG_MEMGRAN - 1; /* 1M alignment on memory space */
	io_request->ra_align_mask =
	    PCICFG_IOGRAN - 1;   /* 4K alignment on I/O space */
	io_request->ra_boundbase = 0;
	io_request->ra_boundlen = PCICFG_4GIG_LIMIT;
	io_request->ra_flags |= NDI_RA_ALLOC_BOUNDED;

	mem_request->ra_len =
	    PCICFG_ROUND_UP(mem_request->ra_len, PCICFG_MEMGRAN);

	io_request->ra_len =
	    PCICFG_ROUND_UP(io_request->ra_len, PCICFG_IOGRAN);

	if (ndi_ra_alloc(ddi_get_parent(dip),
	    mem_request, &mem_answer, &alen,
	    NDI_RA_TYPE_MEM, NDI_RA_PASS) != NDI_SUCCESS) {
		DEBUG0("Failed to allocate memory\n");
		return (PCICFG_FAILURE);
	}

	phdl->memory_base = phdl->memory_last = mem_answer;
	phdl->memory_len  = alen;

	phdl->mem_hole.start = phdl->memory_base;
	phdl->mem_hole.len = phdl->memory_len;
	phdl->mem_hole.next = (hole_t *)NULL;

	if (ndi_ra_alloc(ddi_get_parent(dip), io_request, &io_answer,
	    &alen, NDI_RA_TYPE_IO, NDI_RA_PASS) != NDI_SUCCESS) {

		DEBUG0("Failed to allocate I/O space\n");
		(void) ndi_ra_free(ddi_get_parent(dip), mem_answer,
		    alen, NDI_RA_TYPE_MEM, NDI_RA_PASS);
		phdl->memory_len = phdl->io_len = 0;
		return (PCICFG_FAILURE);
	}

	phdl->io_base = phdl->io_last = (uint32_t)io_answer;
	phdl->io_len  = (uint32_t)alen;

	phdl->io_hole.start = phdl->io_base;
	phdl->io_hole.len = phdl->io_len;
	phdl->io_hole.next = (hole_t *)NULL;

	DEBUG2("MEMORY BASE = [0x%x] length [0x%x]\n",
	    phdl->memory_base, phdl->memory_len);
	DEBUG2("IO     BASE = [0x%x] length [0x%x]\n",
	    phdl->io_base, phdl->io_len);

	return (PCICFG_SUCCESS);
}

#ifdef	DEBUG
/*
 * This function is useful in debug mode, where we can measure how
 * much memory was wasted/unallocated in bridge device's domain.
 */
static uint64_t
pcicfg_unused_space(hole_t *hole, uint32_t *hole_count)
{
	uint64_t len = 0;
	uint32_t count = 0;

	do {
		len += hole->len;
		hole = hole->next;
		count++;
	} while (hole);
	*hole_count = count;
	return (len);
}
#endif

/*
 * This function frees data structures that hold the hole information
 * which are allocated in pcicfg_alloc_hole(). This is not freeing
 * any memory allocated through NDI calls.
 */
static void
pcicfg_free_hole(hole_t *addr_hole)
{
	hole_t *nhole, *hole = addr_hole->next;

	while (hole) {
		nhole = hole->next;
		kmem_free(hole, sizeof (hole_t));
		hole = nhole;
	}
}

static uint64_t
pcicfg_alloc_hole(hole_t *addr_hole, uint64_t *alast, uint32_t length)
{
	uint64_t actual_hole_start, ostart, olen;
	hole_t	*hole = addr_hole, *thole, *nhole;

	do {
		actual_hole_start = PCICFG_ROUND_UP(hole->start, length);
		if (((actual_hole_start - hole->start) + length) <= hole->len) {
			DEBUG3("hole found. start %llx, len %llx, req=%x\n",
			    hole->start, hole->len, length);
			ostart = hole->start;
			olen = hole->len;
			/* current hole parameters adjust */
			if ((actual_hole_start - hole->start) == 0) {
				hole->start += length;
				hole->len -= length;
				if (hole->start > *alast)
					*alast = hole->start;
			} else {
				hole->len = actual_hole_start - hole->start;
				nhole = (hole_t *)kmem_zalloc(sizeof (hole_t),
				    KM_SLEEP);
				nhole->start = actual_hole_start + length;
				nhole->len = (ostart + olen) - nhole->start;
				nhole->next = NULL;
				thole = hole->next;
				hole->next = nhole;
				nhole->next = thole;
				if (nhole->start > *alast)
					*alast = nhole->start;
				DEBUG2("put new hole to %llx, %llx\n",
				    nhole->start, nhole->len);
			}
			DEBUG2("adjust current hole to %llx, %llx\n",
			    hole->start, hole->len);
			break;
		}
		actual_hole_start = 0;
		hole = hole->next;
	} while (hole);

	DEBUG1("return hole at %llx\n", actual_hole_start);
	return (actual_hole_start);
}

static void
pcicfg_get_mem(pcicfg_phdl_t *entry,
	uint32_t length, uint64_t *ans)
{
	uint64_t new_mem;

	/* See if there is a hole, that can hold this request. */
	new_mem = pcicfg_alloc_hole(&entry->mem_hole, &entry->memory_last,
	    length);
	if (new_mem) {	/* if non-zero, found a hole. */
		if (ans != NULL)
			*ans = new_mem;
	} else
		cmn_err(CE_WARN, "No %u bytes memory window for %s\n",
		    length, ddi_get_name(entry->dip));
}

static void
pcicfg_get_io(pcicfg_phdl_t *entry,
	uint32_t length, uint32_t *ans)
{
	uint32_t new_io;
	uint64_t io_last;

	/*
	 * See if there is a hole, that can hold this request.
	 * Pass 64 bit parameters and then truncate to 32 bit.
	 */
	io_last = entry->io_last;
	new_io = (uint32_t)pcicfg_alloc_hole(&entry->io_hole, &io_last, length);
	if (new_io) {	/* if non-zero, found a hole. */
		entry->io_last = (uint32_t)io_last;
		if (ans != NULL)
			*ans = new_io;
	} else
		cmn_err(CE_WARN, "No %u bytes IO space window for %s\n",
		    length, ddi_get_name(entry->dip));
}

static int
pcicfg_sum_resources(dev_info_t *dip, void *hdl)
{
	pcicfg_phdl_t *entry = (pcicfg_phdl_t *)hdl;
	pci_regspec_t *pci_rp;
	int length;
	int rcount;
	int i;
	ndi_ra_request_t *mem_request;
	ndi_ra_request_t *io_request;
	uint8_t header_type;
	ddi_acc_handle_t handle;

	entry->error = PCICFG_SUCCESS;

	mem_request = &entry->mem_req;
	io_request =  &entry->io_req;

	if (pcicfg_config_setup(dip, &handle) != DDI_SUCCESS) {
		DEBUG0("Failed to map config space!\n");
		entry->error = PCICFG_FAILURE;
		return (DDI_WALK_TERMINATE);
	}

	header_type = pci_config_get8(handle, PCI_CONF_HEADER);

	/*
	 * If its a bridge - just record the highest bus seen
	 */
	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {

		if (entry->highest_bus < pci_config_get8(handle,
		    PCI_BCNF_SECBUS)) {
			entry->highest_bus =
			    pci_config_get8(handle, PCI_BCNF_SECBUS);
		}

		(void) pcicfg_config_teardown(&handle);
		entry->error = PCICFG_FAILURE;
		return (DDI_WALK_CONTINUE);
	} else {
		if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "reg", (caddr_t)&pci_rp,
		    &length) != DDI_PROP_SUCCESS) {
			/*
			 * If one node in (the subtree of nodes)
			 * does'nt have a "reg" property fail the
			 * allocation.
			 */
			entry->memory_len = 0;
			entry->io_len = 0;
			entry->error = PCICFG_FAILURE;
			return (DDI_WALK_TERMINATE);
		}
		/*
		 * For each "reg" property with a length, add that to the
		 * total memory (or I/O) to allocate.
		 */
		rcount = length / sizeof (pci_regspec_t);

		for (i = 0; i < rcount; i++) {

			switch (PCI_REG_ADDR_G(pci_rp[i].pci_phys_hi)) {

			case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
				mem_request->ra_len =
				    pci_rp[i].pci_size_low +
				    PCICFG_ROUND_UP(mem_request->ra_len,
				    pci_rp[i].pci_size_low);
				DEBUG1("ADDING 32 --->0x%x\n",
				    pci_rp[i].pci_size_low);

			break;
			case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
				mem_request->ra_len =
				    pci_rp[i].pci_size_low +
				    PCICFG_ROUND_UP(mem_request->ra_len,
				    pci_rp[i].pci_size_low);
				DEBUG1("ADDING 64 --->0x%x\n",
				    pci_rp[i].pci_size_low);

			break;
			case PCI_REG_ADDR_G(PCI_ADDR_IO):
				io_request->ra_len =
				    pci_rp[i].pci_size_low +
				    PCICFG_ROUND_UP(io_request->ra_len,
				    pci_rp[i].pci_size_low);
				DEBUG1("ADDING I/O --->0x%x\n",
				    pci_rp[i].pci_size_low);
			break;
			default:
			    /* Config space register - not included */
			break;
			}
		}

		/*
		 * free the memory allocated by ddi_getlongprop
		 */
		kmem_free(pci_rp, length);

		/*
		 * continue the walk to the next sibling to sum memory
		 */

		(void) pcicfg_config_teardown(&handle);

		return (DDI_WALK_CONTINUE);
	}
}

static int
pcicfg_find_resource_end(dev_info_t *dip, void *hdl)
{
	pcicfg_phdl_t *entry_p = (pcicfg_phdl_t *)hdl;
	pci_regspec_t *pci_ap;
	pcicfg_range_t *ranges;
	int length;
	int rcount;
	int i;

	entry_p->error = PCICFG_SUCCESS;

	if (dip == entry_p->dip) {
		DEBUG0("Don't include parent bridge node\n");
		return (DDI_WALK_CONTINUE);
	}

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "ranges",
	    (caddr_t)&ranges, &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Node doesn't have ranges\n");
		goto ap;
	}

	rcount = length / sizeof (pcicfg_range_t);

	for (i = 0; i < rcount; i++) {
		uint64_t base;
		uint64_t mid = ranges[i].child_mid;
		uint64_t lo = ranges[i].child_lo;
		uint64_t size = ranges[i].size_lo;

		switch (PCI_REG_ADDR_G(ranges[i].child_hi)) {

		case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
			base = entry_p->memory_base;
			entry_p->memory_base = MAX(base, lo + size);
			break;
		case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
			base = entry_p->memory_base;
			entry_p->memory_base = MAX(base,
			    PCICFG_LADDR(lo, mid) + size);
			break;
		case PCI_REG_ADDR_G(PCI_ADDR_IO):
			base = entry_p->io_base;
			entry_p->io_base = MAX(base, lo + size);
			break;
		}
	}

	kmem_free(ranges, length);
	return (DDI_WALK_CONTINUE);

ap:	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses",
	    (caddr_t)&pci_ap,  &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Node doesn't have assigned-addresses\n");
		return (DDI_WALK_CONTINUE);
	}

	rcount = length / sizeof (pci_regspec_t);

	for (i = 0; i < rcount; i++) {

		switch (PCI_REG_ADDR_G(pci_ap[i].pci_phys_hi)) {

		case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
			if ((pci_ap[i].pci_phys_low +
			    pci_ap[i].pci_size_low) >
			    entry_p->memory_base) {
				entry_p->memory_base =
				    pci_ap[i].pci_phys_low +
				    pci_ap[i].pci_size_low;
			}
		break;
		case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
			if ((PCICFG_LADDR(pci_ap[i].pci_phys_low,
			    pci_ap[i].pci_phys_mid) +
			    pci_ap[i].pci_size_low) >
			    entry_p->memory_base) {
				entry_p->memory_base = PCICFG_LADDR(
				    pci_ap[i].pci_phys_low,
				    pci_ap[i].pci_phys_mid) +
				    pci_ap[i].pci_size_low;
			}
		break;
		case PCI_REG_ADDR_G(PCI_ADDR_IO):
			if ((pci_ap[i].pci_phys_low +
			    pci_ap[i].pci_size_low) >
			    entry_p->io_base) {
				entry_p->io_base =
				    pci_ap[i].pci_phys_low +
				    pci_ap[i].pci_size_low;
			}
		break;
		}
	}

	/*
	 * free the memory allocated by ddi_getlongprop
	 */
	kmem_free(pci_ap, length);

	/*
	 * continue the walk to the next sibling to sum memory
	 */
	return (DDI_WALK_CONTINUE);
}

static int
pcicfg_free_bridge_resources(dev_info_t *dip)
{
	pcicfg_range_t		*ranges;
	uint_t			*bus;
	int			k;
	int			length;
	int			i;


	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "ranges", (caddr_t)&ranges,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read ranges property\n");
		if (ddi_get_child(dip)) {
			cmn_err(CE_WARN, "No ranges property found for %s",
			    ddi_get_name(dip));
			/*
			 * strictly speaking, we can check for children with
			 * assigned-addresses but for now it is better to
			 * be conservative and assume that if there are child
			 * nodes, then they do consume PCI memory or IO
			 * resources, Hence return failure.
			 */
			return (PCICFG_FAILURE);
		}
		length = 0;

	}

	for (i = 0; i < length / sizeof (pcicfg_range_t); i++) {
		if (ranges[i].size_lo != 0 ||
		    ranges[i].size_hi != 0) {
			switch (ranges[i].parent_hi & PCI_REG_ADDR_M) {
				case PCI_ADDR_IO:
					DEBUG2("Free I/O    "
					    "base/length = [0x%x]/[0x%x]\n",
					    ranges[i].child_lo,
					    ranges[i].size_lo);
					if (ndi_ra_free(ddi_get_parent(dip),
					    (uint64_t)ranges[i].child_lo,
					    (uint64_t)ranges[i].size_lo,
					    NDI_RA_TYPE_IO, NDI_RA_PASS)
					    != NDI_SUCCESS) {
						DEBUG0("Trouble freeing "
						    "PCI i/o space\n");
						kmem_free(ranges, length);
						return (PCICFG_FAILURE);
					}
				break;
				case PCI_ADDR_MEM32:
				case PCI_ADDR_MEM64:
					DEBUG3("Free Memory base/length = "
					    "[0x%x.%x]/[0x%x]\n",
					    ranges[i].child_mid,
					    ranges[i].child_lo,
					    ranges[i].size_lo)
					if (ndi_ra_free(ddi_get_parent(dip),
					    PCICFG_LADDR(ranges[i].child_lo,
					    ranges[i].child_mid),
					    (uint64_t)ranges[i].size_lo,
					    NDI_RA_TYPE_MEM, NDI_RA_PASS)
					    != NDI_SUCCESS) {
						DEBUG0("Trouble freeing "
						    "PCI memory space\n");
						kmem_free(ranges, length);
						return (PCICFG_FAILURE);
					}
				break;
				default:
					DEBUG0("Unknown memory space\n");
				break;
			}
		}
	}

	if (length)
		kmem_free(ranges, length);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "bus-range", (caddr_t)&bus,
	    &k) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read bus-range property\n");
		return (PCICFG_FAILURE);
	}

	DEBUG2("Need to free bus [%d] range [%d]\n",
	    bus[0], bus[1] - bus[0] + 1);

	if (ndi_ra_free(ddi_get_parent(dip),
	    (uint64_t)bus[0], (uint64_t)(bus[1] - bus[0] + 1),
	    NDI_RA_TYPE_PCI_BUSNUM, NDI_RA_PASS) != NDI_SUCCESS) {
		/*EMPTY*/
		DEBUG0("Failed to free a bus number\n");
	}
	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)bus, k);

	return (PCICFG_SUCCESS);
}

static int
pcicfg_free_device_resources(dev_info_t *dip, pcicfg_flags_t flags,
	char *prop_name)
{
	pci_regspec_t *assigned;

	int length;
	int acount;
	int i;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, prop_name, (caddr_t)&assigned,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read assigned-addresses property\n");
		return (PCICFG_FAILURE);
	}

	/*
	 * For each prop_name property entry with a length,
	 * call the memory allocation routines to return the
	 * resource.
	 */
	acount = length / sizeof (pci_regspec_t);
	for (i = 0; i < acount; i++) {
		/*
		 * Workaround for Devconf (x86) bug to skip extra entries
		 * beyond the PCI_CONF_BASE5 offset. But we want to free up
		 * any memory for expansion roms if allocated.
		 */
		if ((PCI_REG_REG_G(assigned[i].pci_phys_hi) > PCI_CONF_BASE5) &&
		    (PCI_REG_REG_G(assigned[i].pci_phys_hi) != PCI_CONF_ROM))
			break;

		if (pcicfg_free_resource(dip, assigned[i], flags,
		    "assigned-addresses")) {
			DEBUG1("pcicfg_free_device_resources - Trouble freeing "
			    "%x\n", assigned[i].pci_phys_hi);
			/*
			 * Don't forget to free up memory from ddi_getlongprop
			 */
			kmem_free((caddr_t)assigned, length);

			return (PCICFG_FAILURE);
		}
	}
	kmem_free(assigned, length);
	return (PCICFG_SUCCESS);
}

static int
pcicfg_free_resources(dev_info_t *dip, pcicfg_flags_t flags)
{
	ddi_acc_handle_t handle;
	uint8_t header_type;

	if (pcicfg_config_setup(dip, &handle) != DDI_SUCCESS) {
		DEBUG0("Failed to map config space!\n");
		return (PCICFG_FAILURE);
	}

	header_type = pci_config_get8(handle, PCI_CONF_HEADER);

	(void) pci_config_teardown(&handle);

	/*
	 * A different algorithm is used for bridges and leaf devices.
	 */
	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {
		/*
		 * We only support readonly probing for leaf devices.
		 */
		if (flags & PCICFG_FLAG_READ_ONLY)
			return (PCICFG_FAILURE);

		if (pcicfg_free_bridge_resources(dip) != PCICFG_SUCCESS) {
			DEBUG0("Failed freeing up bridge resources\n");
			return (PCICFG_FAILURE);
		}
	} else {
		if (pcicfg_free_device_resources(dip, flags,
		    "assigned-addresses")
		    != PCICFG_SUCCESS) {
			DEBUG0("Failed freeing up device resources\n");
			return (PCICFG_FAILURE);
		}
	}
	return (PCICFG_SUCCESS);
}

#ifndef _DONT_USE_1275_GENERIC_NAMES
static char *
pcicfg_get_class_name(uint32_t classcode)
{
	struct pcicfg_name_entry *ptr;

	for (ptr = &pcicfg_class_lookup[0]; ptr->name != NULL; ptr++) {
		if (ptr->class_code == classcode) {
			return (ptr->name);
		}
	}
	return (NULL);
}
#endif /* _DONT_USE_1275_GENERIC_NAMES */

static dev_info_t *
pcicfg_devi_find(dev_info_t *dip, uint_t device, uint_t function)
{
	struct pcicfg_find_ctrl ctrl;
	int count;

	ctrl.device = device;
	ctrl.function = function;
	ctrl.dip = NULL;

	ndi_devi_enter(dip, &count);
	ddi_walk_devs(ddi_get_child(dip), pcicfg_match_dev, (void *)&ctrl);
	ndi_devi_exit(dip, count);

	return (ctrl.dip);
}

static int
pcicfg_match_dev(dev_info_t *dip, void *hdl)
{
	struct pcicfg_find_ctrl *ctrl = (struct pcicfg_find_ctrl *)hdl;
	pci_regspec_t *pci_rp;
	int length;
	int pci_dev;
	int pci_func;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (int **)&pci_rp,
	    (uint_t *)&length) != DDI_PROP_SUCCESS) {
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

static int
pcicfg_update_assigned_prop(dev_info_t *dip, pci_regspec_t *newone,
	char *prop_name)
{
	int		alen;
	pci_regspec_t	*assigned;
	caddr_t		newreg;
	uint_t		status;
	int		newreg_len, num_entries, i;

	DEBUG2("pcicfg_update_assigned_prop: prop name is %s phys_hi = 0x%x\n",
	    prop_name, newone->pci_phys_hi);
	status = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    prop_name, (caddr_t)&assigned, &alen);
	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG1("no memory for %s property\n", prop_name);
			return (PCICFG_FAILURE);
		default:
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    prop_name, (int *)newone,
			    sizeof (*newone)/sizeof (int));

			(void) pcicfg_dump_assigned_prop(dip, prop_name);

			return (PCICFG_SUCCESS);
	}

	/*
	 * Allocate memory for the existing
	 * assigned-addresses(s) plus one and then
	 * build it.
	 */

	newreg = kmem_zalloc(alen+sizeof (*newone), KM_SLEEP);

	/*
	 * If newone exists, replace the old with newone.
	 */
	num_entries = alen / sizeof (pci_regspec_t);

	/*
	 * Rebuild the assigned-addresses property.
	 */
	for (i = 0; i < num_entries; i++) {
		if (assigned[i].pci_phys_hi == newone->pci_phys_hi) {
			bcopy(newone, (assigned + i), sizeof (*newone));
			break;
		}
	}
	bcopy(assigned, newreg, alen);
	newreg_len = alen/sizeof (int);
	if (i >= num_entries) {
		/* did not find a match */
		bcopy(newone, newreg + alen, sizeof (*newone));
		newreg_len =  ((alen + sizeof (*newone))/sizeof (int));
	}

	/*
	 * Write out the new prop_name spec
	 */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    prop_name, (int *)newreg, newreg_len);

	kmem_free((caddr_t)newreg, alen+sizeof (*newone));

	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)assigned, alen);

	(void) pcicfg_dump_assigned_prop(dip, prop_name);

	return (PCICFG_SUCCESS);
}
static int
pcicfg_update_ranges_prop(dev_info_t *dip, pcicfg_range_t *addition)
{
	int		rlen;
	pcicfg_range_t	*ranges;
	caddr_t		newreg;
	uint_t		status;

	status = ddi_getlongprop(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, "ranges", (caddr_t)&ranges, &rlen);


	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG0("ranges present, but unable to get memory\n");
			return (PCICFG_FAILURE);
		default:
			DEBUG0("no ranges property - creating one\n");
			if (ndi_prop_update_int_array(DDI_DEV_T_NONE,
			    dip, "ranges", (int *)addition,
			    sizeof (pcicfg_range_t)/sizeof (int))
			    != DDI_SUCCESS) {
				DEBUG0("Did'nt create ranges property\n");
				return (PCICFG_FAILURE);
			}
			return (PCICFG_SUCCESS);
	}

	/*
	 * Allocate memory for the existing reg(s) plus one and then
	 * build it.
	 */
	newreg = kmem_zalloc(rlen + sizeof (pcicfg_range_t), KM_SLEEP);

	bcopy(ranges, newreg, rlen);
	bcopy(addition, newreg + rlen, sizeof (pcicfg_range_t));

	/*
	 * Write out the new "ranges" property
	 */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE,
	    dip, "ranges", (int *)newreg,
	    (rlen + sizeof (pcicfg_range_t))/sizeof (int));

	kmem_free((caddr_t)newreg, rlen+sizeof (pcicfg_range_t));

	kmem_free((caddr_t)ranges, rlen);

	return (PCICFG_SUCCESS);
}

static int
pcicfg_update_reg_prop(dev_info_t *dip, uint32_t regvalue, uint_t reg_offset)
{
	int		rlen;
	pci_regspec_t	*reg;
	caddr_t		newreg;
	uint32_t	hiword;
	pci_regspec_t	addition;
	uint32_t	size;
	uint_t		status;

	status = ddi_getlongprop(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, "reg", (caddr_t)&reg, &rlen);

	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG0("reg present, but unable to get memory\n");
			return (PCICFG_FAILURE);
		default:
			DEBUG0("no reg property\n");
			return (PCICFG_FAILURE);
	}

	/*
	 * Allocate memory for the existing reg(s) plus one and then
	 * build it.
	 */
	newreg = kmem_zalloc(rlen+sizeof (pci_regspec_t), KM_SLEEP);

	/*
	 * Build the regspec, then add it to the existing one(s)
	 */

	hiword = PCICFG_MAKE_REG_HIGH(PCI_REG_BUS_G(reg->pci_phys_hi),
	    PCI_REG_DEV_G(reg->pci_phys_hi),
	    PCI_REG_FUNC_G(reg->pci_phys_hi), reg_offset);

	if (reg_offset == PCI_CONF_ROM) {
		size = (~(PCI_BASE_ROM_ADDR_M & regvalue))+1;
		hiword |= PCI_ADDR_MEM32;
	} else {
		size = (~(PCI_BASE_M_ADDR_M & regvalue))+1;

		if ((PCI_BASE_SPACE_M & regvalue) == PCI_BASE_SPACE_MEM) {
			if ((PCI_BASE_TYPE_M & regvalue) == PCI_BASE_TYPE_MEM) {
				hiword |= PCI_ADDR_MEM32;
			} else if ((PCI_BASE_TYPE_M & regvalue)
			    == PCI_BASE_TYPE_ALL) {
				hiword |= PCI_ADDR_MEM64;
			}
		} else {
			hiword |= PCI_ADDR_IO;
		}
	}

	addition.pci_phys_hi = hiword;
	addition.pci_phys_mid = 0;
	addition.pci_phys_low = 0;
	addition.pci_size_hi = 0;
	addition.pci_size_low = size;

	bcopy(reg, newreg, rlen);
	bcopy(&addition, newreg + rlen, sizeof (pci_regspec_t));

	/*
	 * Write out the new "reg" property
	 */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE,
	    dip, "reg", (int *)newreg,
	    (rlen + sizeof (pci_regspec_t))/sizeof (int));

	kmem_free((caddr_t)newreg, rlen+sizeof (pci_regspec_t));
	kmem_free((caddr_t)reg, rlen);

	return (PCICFG_SUCCESS);
}
static int
pcicfg_update_available_prop(dev_info_t *dip, pci_regspec_t *newone)
{
	int		alen;
	pci_regspec_t	*avail_p;
	caddr_t		new_avail;
	uint_t		status;

	DEBUG2("pcicfg_update_available_prop() - Address %lx Size %x\n",
	    newone->pci_phys_low, newone->pci_size_low);
	status = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "available", (caddr_t)&avail_p, &alen);
	switch (status) {
		case DDI_PROP_SUCCESS:
			break;
		case DDI_PROP_NO_MEMORY:
			DEBUG0("no memory for available property\n");
			return (PCICFG_FAILURE);
		default:
			(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
			    "available", (int *)newone,
			    sizeof (*newone)/sizeof (int));

			return (PCICFG_SUCCESS);
	}

	/*
	 * Allocate memory for the existing available plus one and then
	 * build it.
	 */
	new_avail = kmem_zalloc(alen+sizeof (*newone), KM_SLEEP);

	bcopy(avail_p, new_avail, alen);
	bcopy(newone, new_avail + alen, sizeof (*newone));

	/* Write out the new "available" spec */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "available", (int *)new_avail,
	    (alen + sizeof (*newone))/sizeof (int));

	kmem_free((caddr_t)new_avail, alen+sizeof (*newone));

	/* Don't forget to free up memory from ddi_getlongprop */
	kmem_free((caddr_t)avail_p, alen);

	return (PCICFG_SUCCESS);
}

static int
pcicfg_update_vf_reg_prop(dev_info_t *dip, uint32_t size, uint_t hiword)
{
	int		rlen;
	pci_regspec_t	*reg, *newreg;
	pci_regspec_t	addition;
	uint_t		status;
#ifdef DEBUG
	int		rcount, i;
#endif

	status = ddi_getlongprop(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, "reg", (caddr_t)&reg, &rlen);

	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG0("reg present, but unable to get memory\n");
			return (PCICFG_FAILURE);
		default:
			DEBUG0("no reg property\n");
			return (PCICFG_FAILURE);
	}

	/*
	 * Allocate memory for the existing reg(s) plus one and then
	 * build it.
	 */
	newreg = (pci_regspec_t *)kmem_zalloc(rlen+sizeof (pci_regspec_t),
	    KM_SLEEP);

	/*
	 * Build the regspec, then add it to the existing one(s)
	 */
	addition.pci_phys_hi = hiword;
	addition.pci_phys_mid = 0;
	addition.pci_phys_low = 0;
	addition.pci_size_hi = 0;
	addition.pci_size_low = size;

	bcopy(reg, newreg, rlen);
	bcopy(&addition, newreg + (rlen / sizeof (pci_regspec_t)),
	    sizeof (pci_regspec_t));

	/*
	 * Write out the new "reg" property
	 */
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip, "reg",
	    (int *)newreg, (rlen + sizeof (pci_regspec_t))/sizeof (int));

#ifdef DEBUG
	rcount = (rlen / sizeof (pci_regspec_t)) + 1;
	for (i = 0; i < rcount; i++) {
		DEBUG4(
		"pcicfg_dump_vf_reg_prop- size=%x low=%x mid=%x high=%x\n",
		    (newreg + i)->pci_size_low, (newreg + i)->pci_phys_low,
		    (newreg + i)->pci_phys_mid, (newreg + i)->pci_phys_hi);
	}
#endif
	kmem_free((caddr_t)newreg, rlen+sizeof (pci_regspec_t));
	kmem_free((caddr_t)reg, rlen);

	return (PCICFG_SUCCESS);
}

static int
pcicfg_update_assigned_prop_value(dev_info_t *dip, uint32_t size,
    uint32_t base, uint32_t base_hi, uint_t reg_offset, char *prop_name)
{
	int		rlen;
	pci_regspec_t	*reg;
	uint32_t	hiword;
	pci_regspec_t	addition;
	uint_t		status;

	status = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&reg, &rlen);

	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG0("reg present, but unable to get memory\n");
			return (PCICFG_FAILURE);
		default:
			/*
			 * Since the config space "reg" entry should have been
			 * created, we expect a "reg" property already
			 * present here.
			 */
			DEBUG0("no reg property\n");
			return (PCICFG_FAILURE);
	}

	/*
	 * Build the regspec, then add it to the existing one(s)
	 */

	hiword = PCICFG_MAKE_REG_HIGH(PCI_REG_BUS_G(reg->pci_phys_hi),
	    PCI_REG_DEV_G(reg->pci_phys_hi),
	    PCI_REG_FUNC_G(reg->pci_phys_hi), reg_offset);

	hiword |= PCI_REG_REL_M;

	if (reg_offset == PCI_CONF_ROM) {
		hiword |= PCI_ADDR_MEM32;

		base = PCI_BASE_ROM_ADDR_M & base;
	} else {
		if ((PCI_BASE_SPACE_M & base) == PCI_BASE_SPACE_MEM) {
			if ((PCI_BASE_TYPE_M & base) == PCI_BASE_TYPE_MEM) {
				hiword |= PCI_ADDR_MEM32;
			} else if ((PCI_BASE_TYPE_M & base)
			    == PCI_BASE_TYPE_ALL) {
				hiword |= PCI_ADDR_MEM64;
			}

			if (base & PCI_BASE_PREF_M)
				hiword |= PCI_REG_PF_M;

			base = PCI_BASE_M_ADDR_M & base;
		} else {
			hiword |= PCI_ADDR_IO;

			base = PCI_BASE_IO_ADDR_M & base;
			base_hi = 0;
		}
	}

	addition.pci_phys_hi = hiword;
	addition.pci_phys_mid = base_hi;
	addition.pci_phys_low = base;
	addition.pci_size_hi = 0;
	addition.pci_size_low = size;

	DEBUG3("updating BAR@off %x with %x,%x\n", reg_offset, hiword, size);

	kmem_free((caddr_t)reg, rlen);

	return (pcicfg_update_assigned_prop(dip, &addition,
	    prop_name));
}

static void
pcicfg_device_on(ddi_acc_handle_t config_handle)
{
	/*
	 * Enable memory, IO, and bus mastership
	 * XXX should we enable parity, SERR#,
	 * fast back-to-back, and addr. stepping?
	 */
	pci_config_put16(config_handle, PCI_CONF_COMM,
	    pci_config_get16(config_handle, PCI_CONF_COMM) | 0x7);
}

static void
pcicfg_device_off(ddi_acc_handle_t config_handle)
{
	/*
	 * Disable I/O and memory traffic through the bridge
	 */
	pci_config_put16(config_handle, PCI_CONF_COMM, 0x0);
}

/*
 * Setup the basic 1275 properties based on information found in the config
 * header of the PCI device
 */
static int
pcicfg_set_standard_props(dev_info_t *dip, ddi_acc_handle_t config_handle,
	uint8_t pcie_dev)
{
	int ret;
	uint16_t val, cap_ptr;
	uint32_t wordval;
	uint8_t byteval;
	pcie_bus_t	*bus_p = PCIE_DIP2UPBUS(dip);

	/* These two exists only for non-bridges */
	if (((pci_config_get8(config_handle, PCI_CONF_HEADER)
	    & PCI_HEADER_TYPE_M) == PCI_HEADER_ZERO) && !pcie_dev) {
		byteval = pci_config_get8(config_handle, PCI_CONF_MIN_G);
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "min-grant", byteval)) != DDI_SUCCESS) {
			return (ret);
		}

		byteval = pci_config_get8(config_handle, PCI_CONF_MAX_L);
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "max-latency", byteval)) != DDI_SUCCESS) {
			return (ret);
		}
	}

	/*
	 * These should always exist and have the value of the
	 * corresponding register value
	 */
	val = pci_config_get16(config_handle, PCI_CONF_VENID);
	if ((bus_p) && (bus_p->bus_func_type == FUNC_TYPE_VF))
		val = (bus_p->bus_dev_ven_id & 0xffff);
	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip, "vendor-id", val))
	    != DDI_SUCCESS) {
		return (ret);
	}
	val = pci_config_get16(config_handle, PCI_CONF_DEVID);
	if ((bus_p) && (bus_p->bus_func_type == FUNC_TYPE_VF))
		val = (bus_p->bus_dev_ven_id >> 16);
	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip, "device-id", val))
	    != DDI_SUCCESS) {
		return (ret);
	}
	byteval = pci_config_get8(config_handle, PCI_CONF_REVID);
	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "revision-id", byteval)) != DDI_SUCCESS) {
		return (ret);
	}

	wordval = (pci_config_get16(config_handle, PCI_CONF_SUBCLASS)<< 8) |
	    (pci_config_get8(config_handle, PCI_CONF_PROGCLASS));

	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "class-code", wordval)) != DDI_SUCCESS) {
		return (ret);
	}
	/* devsel-speed starts at the 9th bit */
	val = (pci_config_get16(config_handle,
	    PCI_CONF_STAT) & PCI_STAT_DEVSELT) >> 9;
	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "devsel-speed", val)) != DDI_SUCCESS) {
		return (ret);
	}

	/*
	 * The next three are bits set in the status register.  The property is
	 * present (but with no value other than its own existence) if the bit
	 * is set, non-existent otherwise
	 */
	if ((!pcie_dev) &&
	    (pci_config_get16(config_handle, PCI_CONF_STAT) &
	    PCI_STAT_FBBC)) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "fast-back-to-back", 0)) != DDI_SUCCESS) {
			return (ret);
		}
	}
	if ((!pcie_dev) &&
	    (pci_config_get16(config_handle, PCI_CONF_STAT) &
	    PCI_STAT_66MHZ)) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "66mhz-capable", 0)) != DDI_SUCCESS) {
			return (ret);
		}
	}
	if (pci_config_get16(config_handle, PCI_CONF_STAT) & PCI_STAT_UDF) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "udf-supported", 0)) != DDI_SUCCESS) {
			return (ret);
		}
	}

	/*
	 * These next three are optional and are not present
	 * if the corresponding register is zero.  If the value
	 * is non-zero then the property exists with the value
	 * of the register.
	 */
	if ((val = pci_config_get16(config_handle,
	    PCI_CONF_SUBVENID)) != 0) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "subsystem-vendor-id", val)) != DDI_SUCCESS) {
			return (ret);
		}
	}
	if ((val = pci_config_get16(config_handle,
	    PCI_CONF_SUBSYSID)) != 0) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "subsystem-id", val)) != DDI_SUCCESS) {
			return (ret);
		}
	}
	if ((val = pci_config_get16(config_handle,
	    PCI_CONF_CACHE_LINESZ)) != 0) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "cache-line-size", val)) != DDI_SUCCESS) {
			return (ret);
		}
	}

	/*
	 * If the Interrupt Pin register is non-zero then the
	 * interrupts property exists
	 */
	if ((byteval = pci_config_get8(config_handle, PCI_CONF_IPIN)) != 0) {
		/*
		 * If interrupt pin is non-zero,
		 * record the interrupt line used
		 */
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "interrupts", byteval)) != DDI_SUCCESS) {
			return (ret);
		}
	}

	ret = PCI_CAP_LOCATE(config_handle, PCI_CAP_ID_PCI_E, &cap_ptr);

	if (pcie_dev && (ret == DDI_SUCCESS)) {
		val = PCI_CAP_GET16(config_handle, NULL, cap_ptr,
		    PCIE_PCIECAP) & PCIE_PCIECAP_SLOT_IMPL;
		/* if slot implemented, get physical slot number */
		if (val) {
			wordval = (PCI_CAP_GET32(config_handle, NULL,
			    cap_ptr, PCIE_SLOTCAP) >>
			    PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT) &
			    PCIE_SLOTCAP_PHY_SLOT_NUM_MASK;
			if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE,
			    dip, "physical-slot#", wordval))
			    != DDI_SUCCESS) {
				return (ret);
			}
		}
	}
	return (PCICFG_SUCCESS);
}
static int
pcicfg_set_busnode_props(dev_info_t *dip, uint8_t pcie_device_type,
    int pbus, int sbus)
{
	int ret;
	char device_type[8];

	if (pcie_device_type)
		(void) strcpy(device_type, "pciex");
	else
		(void) strcpy(device_type, "pci");

	if ((ret = ndi_prop_update_string(DDI_DEV_T_NONE, dip,
	    "device_type", device_type)) != DDI_SUCCESS) {
		return (ret);
	}
	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "#address-cells", 3)) != DDI_SUCCESS) {
		return (ret);
	}
	if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "#size-cells", 2)) != DDI_SUCCESS) {
		return (ret);
	}

	/*
	 * Create primary-bus and secondary-bus properties to be used
	 * to restore bus numbers in the pcicfg_setup_bridge() routine.
	 */
	if (pbus != -1 && sbus != -1) {
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "primary-bus", pbus)) != DDI_SUCCESS) {
				return (ret);
		}
		if ((ret = ndi_prop_update_int(DDI_DEV_T_NONE, dip,
		    "secondary-bus", sbus)) != DDI_SUCCESS) {
				return (ret);
		}
	}
	return (PCICFG_SUCCESS);
}

static int
pcicfg_set_childnode_props(dev_info_t *dip, ddi_acc_handle_t config_handle,
	uint8_t pcie_dev)
{

	int		ret;
	char		*name;
	char		buffer[64], pprefix[8];
	uint16_t	classcode;
	uint8_t		revid, pif, pclass, psubclass;
	char		*compat[24];
	int		i;
	int		n;
	uint16_t		sub_vid, sub_sid, vid, did;
	pcie_bus_t	*bus_p = NULL;

	/* set the property prefix based on the device type */
	if (pcie_dev) {
		(void) sprintf(pprefix, "pciex");
		bus_p = PCIE_DIP2UPBUS(dip);
	} else {
		(void) sprintf(pprefix, "pci");
	}
	/*
	 * For VF devices use the pf_dip config_handle
	 */
	if (bus_p && bus_p->bus_func_type == FUNC_TYPE_VF) {
		config_handle =  PCIE_DIP2UPBUS(bus_p->bus_pf_dip)->bus_cfg_hdl;
	}
	sub_vid = pci_config_get16(config_handle, PCI_CONF_SUBVENID);
	sub_sid = pci_config_get16(config_handle, PCI_CONF_SUBSYSID);
	vid = pci_config_get16(config_handle, PCI_CONF_VENID);
	did = pci_config_get16(config_handle, PCI_CONF_DEVID);
	revid = pci_config_get8(config_handle, PCI_CONF_REVID);
	pif = pci_config_get8(config_handle, PCI_CONF_PROGCLASS);
	classcode = pci_config_get16(config_handle, PCI_CONF_SUBCLASS);
	pclass = pci_config_get8(config_handle, PCI_CONF_BASCLASS);
	psubclass = pci_config_get8(config_handle, PCI_CONF_SUBCLASS);
	/*
	 * For VF devices extract device and vendor id from bus_dev_ven_id
	 */
	if (bus_p && bus_p->bus_func_type == FUNC_TYPE_VF) {
		vid =  bus_p->bus_dev_ven_id & 0xffff;
		did = ((bus_p->bus_dev_ven_id >> 16) & 0xffff);
	}
	DEBUG4("vid = 0x%x did = 0x%x sub_vid = 0x%x sub_sid = 0x%x\n",
	    vid, did, sub_vid, sub_sid);

	/*
	 * NOTE: These are for both a child and PCI-PCI bridge node
	 */

	/*
	 *	"name" property rule
	 *	--------------------
	 *
	 *
	 * |	  \svid |
	 * |	   \    |
	 * |	    \   |
	 * |	ssid \  |	=0		|	!= 0		|
	 * |------------|-----------------------|-----------------------|
	 * |		|			|			|
	 * |	=0	|	vid,did		|	svid,ssid	|
	 * |		|			|			|
	 * |------------|-----------------------|-----------------------|
	 * |		|			|			|
	 * |	!=0	|	svid,ssid	|	svid,ssid	|
	 * |		|			|			|
	 * |------------|-----------------------|-----------------------|
	 *
	 * where:
	 *    vid = vendor id
	 *    did = device id
	 *   svid = subsystem vendor id
	 *   ssid = subsystem id
	 */

	if ((sub_sid != 0) || (sub_vid != 0)) {
		(void) sprintf(buffer, "%s%x,%x", pprefix, sub_vid, sub_sid);
	} else {
		(void) sprintf(buffer, "%s%x,%x", pprefix, vid, did);
	}
	if (bus_p && bus_p->bus_func_type == FUNC_TYPE_VF)
		(void) sprintf(buffer, "%s%x,%x", pprefix, vid, did);

	/*
	 * In some environments, trying to use "generic" 1275 names is
	 * not the convention.  In those cases use the name as created
	 * above.  In all the rest of the cases, check to see if there
	 * is a generic name first.
	 */
#ifdef _DONT_USE_1275_GENERIC_NAMES
	name = buffer;
#else
	if ((name = pcicfg_get_class_name(classcode)) == NULL) {
		/*
		 * Set name to the above fabricated name
		 */
		name = buffer;
	}
#endif

	/*
	 * The node name field needs to be filled in with the name
	 */
	if (ndi_devi_set_nodename(dip, name, 0) != NDI_SUCCESS) {
		DEBUG0("Failed to set nodename for node\n");
		return (PCICFG_FAILURE);
	}
	DEBUG1("nodename is set as %s\n", name);

	/*
	 * Create the compatible property as an array of pointers
	 * to strings.  Start with the buffer created above.
	 */
	n = 0;
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	/*
	 * Setup 'compatible' as per the PCI2.1 bindings document.
	 *	pci[ex]VVVV,DDDD.SSSS.ssss.RR
	 *	pci[ex]VVVV,DDDD.SSSS.ssss
	 *	pciSSSS.ssss  -> not created for PCIe as per PCIe bindings
	 *	pci[ex]VVVV,DDDD.RR
	 *	pci[ex]VVVV,DDDD
	 *	pci[ex]class,CCSSPP
	 *	pci[ex]class,CCSS
	 */

	/* pci[ex]VVVV,DDDD.SSSS.ssss.RR */
	(void) sprintf(buffer, "%s%x,%x.%x.%x.%x", pprefix,  vid, did,
	    sub_vid, sub_sid, revid);
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	/* pci[ex]VVVV,DDDD.SSSS.ssss */
	(void) sprintf(buffer, "%s%x,%x.%x.%x", pprefix,  vid, did,
	    sub_vid, sub_sid);
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	/* pciSSSS.ssss  -> not created for PCIe as per PCIe bindings */
	if (!pcie_dev) {
		(void) sprintf(buffer, "pci%x,%x", sub_vid, sub_sid);
		compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
		(void) strcpy(compat[n++], buffer);
	}

	/* pci[ex]VVVV,DDDD.RR */
	(void) sprintf(buffer, "%s%x,%x.%x", pprefix,  vid, did, revid);
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	/* pci[ex]VVVV,DDDD */
	(void) sprintf(buffer, "%s%x,%x", pprefix, vid, did);
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	/* pci[ex]class,CCSSPP */
	(void) sprintf(buffer, "%sclass,%02x%02x%02x", pprefix,
	    pclass, psubclass, pif);
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	/* pci[ex]class,CCSS */
	(void) sprintf(buffer, "%sclass,%04x", pprefix, classcode);
	compat[n] = kmem_alloc(strlen(buffer) + 1, KM_SLEEP);
	(void) strcpy(compat[n++], buffer);

	if ((ret = ndi_prop_update_string_array(DDI_DEV_T_NONE, dip,
	    "compatible", (char **)compat, n)) != DDI_SUCCESS) {
		return (ret);
	}

	for (i = 0; i < n; i++) {
		kmem_free(compat[i], strlen(compat[i]) + 1);
	}

	DEBUG1("pcicfg_set_childnode_props - creating name=%s\n", name);
	if ((ret = ndi_prop_update_string(DDI_DEV_T_NONE, dip,
	    "name", name)) != DDI_SUCCESS) {

		DEBUG0("pcicfg_set_childnode_props - Unable to create name "
		    "property\n");

		return (ret);
	}

	return (PCICFG_SUCCESS);
}

/*
 * Program the bus numbers into the bridge
 */

static void
pcicfg_set_bus_numbers(ddi_acc_handle_t config_handle,
uint_t primary, uint_t secondary, uint_t subordinate)
{
	DEBUG3("Setting bridge bus-range %d,%d,%d\n", primary, secondary,
	    subordinate);
	/*
	 * Primary bus#
	 */
	pci_config_put8(config_handle, PCI_BCNF_PRIBUS, primary);

	/*
	 * Secondary bus#
	 */
	pci_config_put8(config_handle, PCI_BCNF_SECBUS, secondary);

	/*
	 * Subordinate bus#
	 */
	pci_config_put8(config_handle, PCI_BCNF_SUBBUS, subordinate);
}

/*
 * Put bridge registers into initial state
 */
static void
pcicfg_setup_bridge(pcicfg_phdl_t *entry,
    ddi_acc_handle_t handle, dev_info_t *dip)
{
	int pbus, sbus;

	/*
	 * The highest bus seen during probing is the max-subordinate bus
	 */
	pci_config_put8(handle, PCI_BCNF_SUBBUS, entry->highest_bus);


	/*
	 * If there exists more than 1 downstream bridge, it
	 * will be reset by the below secondary bus reset which
	 * will clear the bus numbers assumed to be programmed in
	 * the pcicfg_probe_children() routine.  We therefore restore
	 * them here.
	 */
	if (pci_config_get8(handle, PCI_BCNF_SECBUS) == 0) {
		pbus = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "primary-bus", -1);
		sbus = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "secondary-bus", -1);
		if (pbus != -1 && sbus != -1) {
			pci_config_put8(handle, PCI_BCNF_PRIBUS, (uint_t)pbus);
			pci_config_put8(handle, PCI_BCNF_SECBUS, (uint_t)sbus);
		} else {
			cmn_err(CE_WARN, "Invalid Bridge number detected: \
			    %s%d: pbus = 0x%x, sbus = 0x%x",
			    ddi_get_name(dip), ddi_get_instance(dip), pbus,
			    sbus);
		}
	}

	/*
	 * Reset the secondary bus
	 */
	pci_config_put16(handle, PCI_BCNF_BCNTRL,
	    pci_config_get16(handle, PCI_BCNF_BCNTRL) | 0x40);

	drv_usecwait(100);

	pci_config_put16(handle, PCI_BCNF_BCNTRL,
	    pci_config_get16(handle, PCI_BCNF_BCNTRL) & ~0x40);

	/*
	 * Program the memory base register with the
	 * start of the memory range
	 */
	pci_config_put16(handle, PCI_BCNF_MEM_BASE,
	    PCICFG_HIWORD(PCICFG_LOADDR(entry->memory_last)));

	/*
	 * Program the I/O base register with the start of the I/O range
	 */
	pci_config_put8(handle, PCI_BCNF_IO_BASE_LOW,
	    PCICFG_HIBYTE(PCICFG_LOWORD(PCICFG_LOADDR(entry->io_last))));
	pci_config_put16(handle, PCI_BCNF_IO_BASE_HI,
	    PCICFG_HIWORD(PCICFG_LOADDR(entry->io_last)));

	/*
	 * Clear status bits
	 */
	pci_config_put16(handle, PCI_BCNF_SEC_STATUS, 0xffff);

	/*
	 * Turn off prefetchable range
	 */
	pci_config_put32(handle, PCI_BCNF_PF_BASE_LOW, 0x0000ffff);
	pci_config_put32(handle, PCI_BCNF_PF_BASE_HIGH, 0xffffffff);
	pci_config_put32(handle, PCI_BCNF_PF_LIMIT_HIGH, 0x0);

	/*
	 * Needs to be set to this value
	 */
	pci_config_put8(handle, PCI_CONF_ILINE, 0xf);

	/*
	 * After a Reset, we need to wait 2^25 clock cycles before the
	 * first Configuration access.  The worst case is 33MHz, which
	 * is a 1 second wait.
	 */
	drv_usecwait(pcicfg_sec_reset_delay);

}

static void
pcicfg_update_bridge(pcicfg_phdl_t *entry,
	ddi_acc_handle_t handle)
{
	uint_t length;

	/*
	 * Program the memory limit register with the end of the memory range
	 */

	DEBUG1("DOWN ROUNDED ===>[0x%x]\n",
	    PCICFG_ROUND_DOWN(entry->memory_last,
	    PCICFG_MEMGRAN));

	pci_config_put16(handle, PCI_BCNF_MEM_LIMIT,
	    PCICFG_HIWORD(PCICFG_LOADDR(
	    PCICFG_ROUND_DOWN(entry->memory_last,
	    PCICFG_MEMGRAN))));
	/*
	 * Since this is a bridge, the rest of this range will
	 * be responded to by the bridge.  We have to round up
	 * so no other device claims it.
	 */
	if ((length = (PCICFG_ROUND_UP(entry->memory_last,
	    PCICFG_MEMGRAN) - entry->memory_last)) > 0) {
		(void) pcicfg_get_mem(entry, length, NULL);
		DEBUG1("Added [0x%x]at the top of "
		    "the bridge (mem)\n", length);
	}

	/*
	 * Program the I/O limit register with the end of the I/O range
	 */
	pci_config_put8(handle, PCI_BCNF_IO_LIMIT_LOW,
	    PCICFG_HIBYTE(PCICFG_LOWORD(
	    PCICFG_LOADDR(PCICFG_ROUND_DOWN(entry->io_last,
	    PCICFG_IOGRAN)))));

	pci_config_put16(handle, PCI_BCNF_IO_LIMIT_HI,
	    PCICFG_HIWORD(PCICFG_LOADDR(PCICFG_ROUND_DOWN(entry->io_last,
	    PCICFG_IOGRAN))));

	/*
	 * Same as above for I/O space. Since this is a
	 * bridge, the rest of this range will be responded
	 * to by the bridge.  We have to round up so no
	 * other device claims it.
	 */
	if ((length = (PCICFG_ROUND_UP(entry->io_last,
	    PCICFG_IOGRAN) - entry->io_last)) > 0) {
		(void) pcicfg_get_io(entry, length, NULL);
		DEBUG1("Added [0x%x]at the top of "
		    "the bridge (I/O)\n",  length);
	}
}

/*ARGSUSED*/
static void
pcicfg_disable_bridge_probe_err(dev_info_t *dip, ddi_acc_handle_t h,
	pcicfg_err_regs_t *regs)
{
	uint16_t val;

	/* disable SERR generated in the context of Master Aborts. */
	regs->cmd = val = pci_config_get16(h, PCI_CONF_COMM);
	val &= ~PCI_COMM_SERR_ENABLE;
	pci_config_put16(h, PCI_CONF_COMM, val);
	regs->bcntl = val = pci_config_get16(h, PCI_BCNF_BCNTRL);
	val &= ~PCI_BCNF_BCNTRL_SERR_ENABLE;
	pci_config_put16(h, PCI_BCNF_BCNTRL, val);
	/* clear any current pending errors */
	pci_config_put16(h, PCI_CONF_STAT, PCI_STAT_S_TARG_AB|
	    PCI_STAT_R_TARG_AB | PCI_STAT_R_MAST_AB | PCI_STAT_S_SYSERR);
	pci_config_put16(h, PCI_BCNF_SEC_STATUS, PCI_STAT_S_TARG_AB|
	    PCI_STAT_R_TARG_AB | PCI_STAT_R_MAST_AB | PCI_STAT_S_SYSERR);
	/* if we are a PCIe device, disable the generation of UR, CE and NFE */
	if (regs->pcie_dev) {
		uint16_t devctl;
		uint16_t cap_ptr;

		if ((PCI_CAP_LOCATE(h, PCI_CAP_ID_PCI_E, &cap_ptr)) ==
		    DDI_FAILURE)
			return;

		regs->pcie_cap_off = cap_ptr;
		regs->devctl = devctl = PCI_CAP_GET16(h, NULL, cap_ptr,
		    PCIE_DEVCTL);
		devctl &= ~(PCIE_DEVCTL_UR_REPORTING_EN |
		    PCIE_DEVCTL_CE_REPORTING_EN |
		    PCIE_DEVCTL_NFE_REPORTING_EN |
		    PCIE_DEVCTL_FE_REPORTING_EN);
		PCI_CAP_PUT16(h, NULL, cap_ptr, PCIE_DEVCTL, devctl);
	}
}

/*ARGSUSED*/
static void
pcicfg_enable_bridge_probe_err(dev_info_t *dip, ddi_acc_handle_t h,
	pcicfg_err_regs_t *regs)
{
	/* clear any pending errors */
	pci_config_put16(h, PCI_CONF_STAT, PCI_STAT_S_TARG_AB|
	    PCI_STAT_R_TARG_AB | PCI_STAT_R_MAST_AB | PCI_STAT_S_SYSERR);
	pci_config_put16(h, PCI_BCNF_SEC_STATUS, PCI_STAT_S_TARG_AB|
	    PCI_STAT_R_TARG_AB | PCI_STAT_R_MAST_AB | PCI_STAT_S_SYSERR);

	/* restore original settings */
	if (regs->pcie_dev) {
		pcie_clear_errors(dip);
		pci_config_put16(h, regs->pcie_cap_off + PCIE_DEVCTL,
		    regs->devctl);
	}

	pci_config_put16(h, PCI_BCNF_BCNTRL, regs->bcntl);
	pci_config_put16(h, PCI_CONF_COMM, regs->cmd);

}

static int
pcicfg_probe_children(dev_info_t *parent, uint_t bdf,
    uint_t *highest_bus, pcicfg_flags_t flags, boolean_t is_pcie)
{
	dev_info_t		*new_child;
	ddi_acc_handle_t	config_handle;
	uint8_t			header_type, pcie_dev = 0;
	int			ret;
	pcicfg_err_regs_t	regs;

	/*
	 * This node will be put immediately below
	 * "parent". Allocate a blank device node.  It will either
	 * be filled in or freed up based on further probing.
	 */
	/*
	 * Note: in usr/src/uts/common/io/hotplug/pcicfg/pcicfg.c
	 * ndi_devi_alloc() is called as ndi_devi_alloc_sleep()
	 */
	if (ndi_devi_alloc(parent, DEVI_PSEUDO_NEXNAME,
	    (pnode_t)DEVI_SID_NODEID, &new_child)
	    != NDI_SUCCESS) {
		DEBUG0("pcicfg_probe_children(): Failed to alloc child node\n");
		return (PCICFG_FAILURE);
	}

	if (pcicfg_add_config_reg(new_child, bdf) != DDI_SUCCESS) {
		DEBUG0("pcicfg_probe_children():"
		    "Failed to add candidate REG\n");
		goto failedconfig;
	}

	if ((ret = pcicfg_config_setup(new_child, &config_handle))
	    != PCICFG_SUCCESS) {
		if (ret == PCICFG_NODEVICE) {
			(void) ndi_devi_free(new_child);
			return (ret);
		}
		DEBUG0("pcicfg_probe_children():"
		    "Failed to setup config space\n");
		goto failedconfig;
	}

	if (is_pcie)
		(void) pcie_init_bus(new_child, bdf, PCIE_BUS_INITIAL);

	/*
	 * As soon as we have access to config space,
	 * turn off device. It will get turned on
	 * later (after memory is assigned).
	 */
	(void) pcicfg_device_off(config_handle);

	/* check if we are PCIe device */
	if (pcicfg_pcie_dev(new_child, PCICFG_DEVICE_TYPE_PCIE, &regs)
	    == DDI_SUCCESS) {
		DEBUG0("PCIe device detected\n");
		pcie_dev = 1;
	}

	/*
	 * Set 1275 properties common to all devices
	 */
	if (pcicfg_set_standard_props(new_child, config_handle,
	    pcie_dev) != PCICFG_SUCCESS) {
		DEBUG0("Failed to set standard properties\n");
		goto failedchild;
	}

	/*
	 * Child node properties  NOTE: Both for PCI-PCI bridge and child node
	 */
	if (pcicfg_set_childnode_props(new_child, config_handle,
	    pcie_dev) != PCICFG_SUCCESS) {
		goto failedchild;
	}

	header_type = pci_config_get8(config_handle, PCI_CONF_HEADER);

	/*
	 * If this is not a multi-function card only probe function zero.
	 */
	if ((!(header_type & PCI_HEADER_MULTI)) &&
	    ((bdf & PCIE_REQ_ID_FUNC_MASK) != 0)) {

		(void) pcicfg_config_teardown(&config_handle);
		(void) ndi_devi_free(new_child);
		return (PCICFG_NODEVICE);
	}

	DEBUG1("---Vendor ID = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_VENID));
	DEBUG1("---Device ID = [0x%x]\n",
	    pci_config_get16(config_handle, PCI_CONF_DEVID));

	/*
	 * Attach the child to its parent
	 */
	(void) i_ndi_config_node(new_child, DS_LINKED, 0);

	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {

		DEBUG1("--Bridge found bdf [0x%x]", bdf);

		/* Only support read-only probe for leaf device */
		if (flags & PCICFG_FLAG_READ_ONLY)
			goto failedchild;

		if (pcicfg_probe_bridge(new_child, config_handle,
		    (bdf & PCIE_REQ_ID_BUS_MASK) >> PCIE_REQ_ID_BUS_SHIFT,
		    highest_bus, is_pcie) != PCICFG_SUCCESS) {
			(void) pcicfg_free_bridge_resources(new_child);
			goto failedchild;
		}

	} else {

		DEBUG1("--Leaf device found bdf [0x%x]", bdf);

		if (flags & PCICFG_FLAG_READ_ONLY) {
			/*
			 * with read-only probe, don't do any resource
			 * allocation, just read the BARs and update props.
			 */
			ret = pcicfg_populate_props_from_bar(new_child,
			    config_handle);
			if (ret != PCICFG_SUCCESS)
				goto failedchild;

			/*
			 * for readonly probe "assigned-addresses" property
			 * has already been setup by reading the BAR, here
			 * just substract the resource from its parent here.
			 */
			ret = pcicfg_device_assign_readonly(new_child);
			if (ret != PCICFG_SUCCESS) {
				(void) pcicfg_free_device_resources(new_child,
				    flags, "assigned-addresses");
				goto failedchild;
			}
		} else {
			/*
			 * update "reg" property by sizing the BARs.
			 */
			ret = pcicfg_populate_reg_props(new_child,
			    config_handle);
			if (ret != PCICFG_SUCCESS)
				goto failedchild;

			/* now allocate & program the resources */
			ret = pcicfg_device_assign(new_child);
			if (ret != PCICFG_SUCCESS) {
				(void) pcicfg_free_device_resources(new_child,
				    flags, "assigned-addresses");
				goto failedchild;
			}
		}

		(void) ndi_devi_bind_driver(new_child, 0);
	}

	(void) pcicfg_config_teardown(&config_handle);

	/*
	 * Properties have been setted up, so initilize the rest fields
	 * in bus_t.
	 */
	if (is_pcie)
		(void) pcie_init_bus(new_child, 0, PCIE_BUS_FINAL);

	return (PCICFG_SUCCESS);

failedchild:

	(void) pcicfg_config_teardown(&config_handle);
	if (is_pcie)
		pcie_fini_bus(new_child, PCIE_BUS_FINAL);

failedconfig:

	(void) ndi_devi_free(new_child);
	return (PCICFG_FAILURE);
}

static int
pcicfg_probe_vf(dev_info_t *pf_dip, uint_t bdf, uint16_t vf_num)
{
	dev_info_t		*new_child, *parent;
	ddi_acc_handle_t	config_handle;
	uint8_t			header_type, pcie_dev = 0;
	int			ret = PCICFG_FAILURE;
	pci_regspec_t 		phys_spec;
	int			i, vf_bar_size;
	pcie_bus_t		*bus_p = PCIE_DIP2UPBUS(pf_dip);
	uint64_t		vf_bar_addr;
	uint16_t		pf_ven_id, vf_dev_id;
	uint_t			hiword;
	boolean_t		vf_assigned = B_FALSE;
	char			*path;
	int			reg_offset;
	pcie_req_id_t		pf_bdf;


	if ((bus_p == NULL) || (bus_p->sriov_cap_ptr == 0))
		return (PCICFG_FAILURE);
	vf_dev_id = PCI_XCAP_GET16(bus_p->bus_cfg_hdl, NULL,
	    bus_p->sriov_cap_ptr,
	    PCIE_EXT_CAP_SRIOV_VF_DEV_ID_OFFSET);
	pf_ven_id = bus_p->bus_dev_ven_id & 0xffff;
	ret = pcie_get_bdf_from_dip(pf_dip, &pf_bdf);
	parent = ddi_get_parent(pf_dip);
	/*
	 * This node will be put immediately below
	 * "parent" of PF. Allocate a blank device node.  It will either
	 * be filled in or freed up based on further probing.
	 */

	ndi_devi_alloc_sleep(parent, DEVI_PSEUDO_NEXNAME,
	    (pnode_t)DEVI_SID_NODEID, &new_child);

	if (pcicfg_add_config_reg(new_child, bdf) != DDI_SUCCESS) {
		DEBUG0("pcicfg_probe_vf():"
		"Failed to add candidate REG\n");
		goto failedconfig;
	}

	if ((ret = pcicfg_config_setup(new_child, &config_handle))
	    != PCICFG_SUCCESS) {
		if (ret == PCICFG_NODEVICE) {
			(void) ndi_devi_free(new_child);
			return (ret);
		}
		DEBUG0("pcicfg_probe_vf():"
		"Failed to setup config space\n");
		goto failedconfig;
	}
	(void) pcie_init_bus(new_child, bdf, PCIE_BUS_INITIAL);
	bus_p = PCIE_DIP2UPBUS(new_child);
	bus_p->bus_dev_ven_id = ((vf_dev_id << 16) | pf_ven_id);
	bus_p->bus_func_type = FUNC_TYPE_VF;
	bus_p->bus_pf_dip = pf_dip;
	DEBUG1("bus dev ven id = 0x%x\n", bus_p->bus_dev_ven_id);
	/*
	 * As soon as we have access to config space,
	 * turn off device. It will get turned on
	 * later (after memory is assigned).
	 */
	(void) pcicfg_device_off(config_handle);

	DEBUG1("PCIe device detected bdf = 0x%x\n", bdf);
	pcie_dev = 1;

	/*
	 * Set 1275 properties common to all devices
	 */
	if (pcicfg_set_standard_props(new_child, config_handle, pcie_dev)
	    != PCICFG_SUCCESS) {
		DEBUG1("Failed to set standard properties bdf = 0x%x\n", bdf);
		goto failedchild;
	}

	/*
	 * Child node properties  NOTE: Both for PCI-PCI bridge and child node
	 */
	if (pcicfg_set_childnode_props(new_child, config_handle, pcie_dev)
	    != PCICFG_SUCCESS) {
		goto failedchild;
	}

	header_type = pci_config_get8(config_handle, PCI_CONF_HEADER);
	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {

		DEBUG1(
		"-- Expecting a Leaf devcie but a Bridge found bdf [0x%x] ",
		    bdf);

		goto failedchild;

	}

	DEBUG1("--Leaf device found bdf [0x%x] ", bdf);

	/*
	 * Don't do any resource allocation, just read the
	 * bus_vf_bar from the pf_dip and add it
	 * as assigned-addresses property to child node(VF).
	 */
	bus_p = PCIE_DIP2UPBUS(pf_dip);
	for (i = 0; i < PCI_BASE_NUM; i++) {
		if ((bus_p->bus_vf_bar_ptr + i)->pci_phys_hi == 0)
			continue;
		vf_bar_size =
		    ((bus_p->bus_vf_bar_ptr + i)->pci_size_low);
		phys_spec.pci_size_low = vf_bar_size;
		switch (PCI_REG_ADDR_G(
		    (bus_p->bus_vf_bar_ptr + i)->pci_phys_hi)) {
			case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
				vf_bar_addr = PCICFG_LADDR(
				    (bus_p->bus_vf_bar_ptr + i)->pci_phys_low,
				    (bus_p->bus_vf_bar_ptr + i)->pci_phys_mid);
				vf_bar_addr +=
				    (vf_bar_size * vf_num);
				phys_spec.pci_phys_mid =
				    PCICFG_HIADDR(vf_bar_addr);
				phys_spec.pci_phys_low =
				    PCICFG_LOADDR(vf_bar_addr);
				DEBUG5("64 bit bar addr = [0x%x.%x] len [0x%x] "
				    "bdf = 0x%x vf_num = %d\n",
				    phys_spec.pci_phys_mid,
				    phys_spec.pci_phys_low,
				    phys_spec.pci_size_low,
				    bdf, vf_num);
				break;
			case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
				phys_spec.pci_phys_mid = 0;
				phys_spec.pci_phys_low =
				    (bus_p->bus_vf_bar_ptr + i)->pci_phys_low +
				    (vf_bar_size * vf_num);
				DEBUG4(
		"32 bit bar addr = [0x%x] len [0x%x] bdf = 0x%x vf_num = %d\n",
				    phys_spec.pci_phys_low,
				    phys_spec.pci_size_low,
				    bdf, vf_num);
				break;
		}
		hiword = ((bus_p->bus_vf_bar_ptr + i)->pci_phys_hi &
		    (~(PCI_REG_BUS_M | PCI_REG_DEV_M |  PCI_REG_FUNC_M |
		    PCI_REG_REG_M)));
		reg_offset = PCI_REG_REG_G(
		    (bus_p->bus_vf_bar_ptr + i)->pci_phys_hi);
		if (reg_offset  == PCI_CONF_ROM) {
			hiword |= reg_offset;
			phys_spec.pci_phys_mid =
			    (bus_p->bus_vf_bar_ptr + i)->pci_phys_mid;
			phys_spec.pci_phys_low =
			    (bus_p->bus_vf_bar_ptr + i)->pci_phys_low;
		} else {
			hiword |= (PCI_CONF_BASE0 + (reg_offset * 4));
		}
		hiword |= (bdf << PCI_REG_FUNC_SHIFT);
		phys_spec.pci_phys_hi = hiword;
		if (pcicfg_update_assigned_prop(new_child, &phys_spec,
		    "assigned-addresses")) {
			DEBUG1(
		"failed to add assigned-addresses property for bdf  0x%x\n",
			    bdf);
			goto failedchild;
		}
		if (pcicfg_update_vf_reg_prop(new_child, phys_spec.pci_size_low,
		    hiword & (~PCI_REG_REL_M))) {
			DEBUG1(
			    "failed to add reg property for bdf  0x%x\n", bdf);
			goto failedchild;
		}
	}

	DEBUG1("ddi driver name = %s\n", ddi_driver_name(new_child));
	path = kmem_alloc(MAXPATHLEN + 1, KM_SLEEP);
	(void) pcie_pathname(pf_dip, path);
	DEBUG2("pf_path = %s vf_num = %d\n", path, vf_num);
	ret = iovcfg_is_vf_assigned(path, vf_num, &vf_assigned);
	(void) pcie_pathname(new_child, path);
	DEBUG1("vf_path = %s\n", path);
	if (vf_assigned) {
		DEBUG1("vf %s is assigned out.\n", path);
	}
	kmem_free(path, MAXPATHLEN + 1);
	DEBUG1("dumping config  for VF device 0x%x\n", bdf);
#ifdef DEBUG
	if (pcicfg_debug)
		pci_dump(config_handle);
#endif
	if (vf_assigned) {
		DEVI(new_child)->devi_flags |= DEVI_ASSIGNED;
		bus_p->has_assigned_vf = B_TRUE;
	}
	(void) pcie_init_bus(new_child, bdf, PCIE_BUS_FINAL);
	ret = i_ndi_config_node(new_child, DS_INITIALIZED, 0);
	if (ret) {
#ifdef DEBUG
		path = kmem_alloc(MAXPATHLEN + 1, KM_SLEEP);
		(void) pcie_pathname(new_child, path);
		DEBUG1("Failed to set the VF device %s to INITIALIZED state\n",
		    path);
		kmem_free(path, MAXPATHLEN + 1);
#endif
		goto failedchild;
	}
	(void) pcicfg_config_teardown(&config_handle);
	return (PCICFG_SUCCESS);

failedchild:
	/*
	 * XXX check if it should be taken offline (if online)
	 */
	(void) pcicfg_config_teardown(&config_handle);

failedconfig:

	(void) ndi_devi_free(new_child);
	return (PCICFG_FAILURE);
}

/*
 * Sizing the BARs and update "reg" property
 */
static int
pcicfg_populate_reg_props(dev_info_t *new_child,
    ddi_acc_handle_t config_handle)
{
	int		i;
	uint32_t	request;

	i = PCI_CONF_BASE0;

	while (i <= PCI_CONF_BASE5) {

		pci_config_put32(config_handle, i, 0xffffffff);

		request = pci_config_get32(config_handle, i);
		/*
		 * If its a zero length, don't do
		 * any programming.
		 */
		if (request != 0) {
			/*
			 * Add to the "reg" property
			 */
			if (pcicfg_update_reg_prop(new_child,
			    request, i) != PCICFG_SUCCESS) {
				goto failedchild;
			}
		} else {
			DEBUG1("BASE register [0x%x] asks for "
			    "[0x0]=[0x0](32)\n", i);
			i += 4;
			continue;
		}

		/*
		 * Increment by eight if it is 64 bit address space
		 */
		if ((PCI_BASE_TYPE_M & request) == PCI_BASE_TYPE_ALL) {
			DEBUG3("BASE register [0x%x] asks for "
			    "[0x%x]=[0x%x] (64)\n",
			    i, request,
			    (~(PCI_BASE_M_ADDR_M & request))+1)
			i += 8;
		} else {
			DEBUG3("BASE register [0x%x] asks for "
			    "[0x%x]=[0x%x](32)\n",
			    i, request,
			    (~(PCI_BASE_M_ADDR_M & request))+1)
			i += 4;
		}
	}

	/*
	 * Get the ROM size and create register for it
	 */
	pci_config_put32(config_handle, PCI_CONF_ROM, 0xfffffffe);

	request = pci_config_get32(config_handle, PCI_CONF_ROM);
	/*
	 * If its a zero length, don't do
	 * any programming.
	 */

	if (request != 0) {
		DEBUG3("BASE register [0x%x] asks for [0x%x]=[0x%x]\n",
		    PCI_CONF_ROM, request,
		    (~(PCI_BASE_ROM_ADDR_M & request))+1);
		/*
		 * Add to the "reg" property
		 */
		if (pcicfg_update_reg_prop(new_child,
		    request, PCI_CONF_ROM) != PCICFG_SUCCESS) {
			goto failedchild;
		}
	}

	return (PCICFG_SUCCESS);

failedchild:
	return (PCICFG_FAILURE);
}

static int
pcicfg_fcode_probe(dev_info_t *parent, uint_t bus, uint_t device,
    uint_t func, uint_t *highest_bus, pcicfg_flags_t flags, boolean_t is_pcie)
{
	dev_info_t		*new_child;
	int8_t			header_type;
	int			ret;
	ddi_acc_handle_t	h, ph;
	int			error = 0;
	extern int		pcicfg_dont_interpret;
	pcicfg_err_regs_t	parent_regs, regs;
	char			*status_prop = NULL;
#ifdef PCICFG_INTERPRET_FCODE
	struct pci_ops_bus_args	po;
	fco_handle_t		c;
	char			unit_address[64];
	int			fcode_size = 0;
	uchar_t			*fcode_addr;
	uint64_t		mem_answer, mem_alen;
	pci_regspec_t		p;
	int32_t			request;
	ndi_ra_request_t	req;
	int16_t			vendor_id, device_id;
#endif

	/*
	 * check if our parent is of type pciex.
	 * if so, program config space to disable error msgs during probe.
	 */
	if (pcicfg_pcie_dev(parent, PCICFG_DEVICE_TYPE_PCIE, &parent_regs)
	    == DDI_SUCCESS) {
		DEBUG0("PCI/PCIe parent detected. Disable errors.\n");
		/*
		 * disable parent generating URs or SERR#s during probing
		 * alone.
		 */
		if (pci_config_setup(parent, &ph) != DDI_SUCCESS)
			return (DDI_FAILURE);

		if ((flags & PCICFG_FLAG_READ_ONLY) == 0) {
			pcicfg_disable_bridge_probe_err(parent,
			    ph, &parent_regs);
		}
	}

	/*
	 * This node will be put immediately below
	 * "parent". Allocate a blank device node.  It will either
	 * be filled in or freed up based on further probing.
	 */

	if (ndi_devi_alloc(parent, DEVI_PSEUDO_NEXNAME,
	    (pnode_t)DEVI_SID_NODEID, &new_child)
	    != NDI_SUCCESS) {
		DEBUG0("pcicfg_fcode_probe(): Failed to alloc child node\n");
		/* return (PCICFG_FAILURE); */
		ret = PCICFG_FAILURE;
		goto failed2;
	}

	/*
	 * Create a dummy reg property.  This will be replaced with
	 * a real reg property when fcode completes or if we need to
	 * produce one by hand.
	 */
	if (pcicfg_add_config_reg(new_child,  PCI_GETBDF(bus, device, func))
	    != DDI_SUCCESS) {
		ret = PCICFG_FAILURE;
		goto failed3;
	}
#ifdef	EFCODE21554
	if ((ret = pcicfg_config_setup(new_child, &h))
	    != PCICFG_SUCCESS) {
		DEBUG0("pcicfg_fcode_probe():"
		    "Failed to setup config space\n");
		ret = PCICFG_NODEVICE;
		goto failed3;
	}

#else
	p.pci_phys_hi = PCICFG_MAKE_REG_HIGH(bus, device, func, 0);
	p.pci_phys_mid = p.pci_phys_low = 0;
	p.pci_size_hi = p.pci_size_low = 0;

	/*
	 * Map in configuration space (temporarily)
	 */
	acc.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	acc.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	acc.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	if (pcicfg_map_phys(new_child, &p, &virt, &acc, &h)) {
		DEBUG0("pcicfg_fcode_probe():"
		    "Failed to setup config space\n");
		ret = PCICFG_NODEVICE;
		goto failed3;
	}

	/*
	 * First use ddi_peek16 so that if there is not a device there,
	 * a bus error will not cause a panic.
	 */
	v = virt + PCI_CONF_VENID;
	if (ddi_peek16(new_child, (int16_t *)v, &vendor_id)) {
		DEBUG0("Can not read Vendor ID");
		pcicfg_unmap_phys(&h, &p);
		ret = PCICFG_NODEVICE;
		goto failed3;
	}
#endif

	if (is_pcie)
		(void) pcie_init_bus(new_child, PCI_GETBDF(bus, device, func),
		    PCIE_BUS_INITIAL);

	DEBUG0("fcode_probe: conf space mapped.\n");
	/*
	 * As soon as we have access to config space,
	 * turn off device. It will get turned on
	 * later (after memory is assigned).
	 */
	(void) pcicfg_device_off(h);

	/* check if we are PCIe device */
	if (pcicfg_pcie_dev(new_child, PCICFG_DEVICE_TYPE_PCIE, &regs)
	    == DDI_SUCCESS) {
		/*EMPTY*/
		DEBUG0("PCI/PCIe device detected\n");
	}

	/*
	 * Set 1275 properties common to all devices
	 */
	if (pcicfg_set_standard_props(new_child,
	    h, regs.pcie_dev) != PCICFG_SUCCESS) {
		DEBUG0("Failed to set standard properties\n");
		goto failed;
	}

	/*
	 * Child node properties  NOTE: Both for PCI-PCI bridge and child node
	 */
	if (pcicfg_set_childnode_props(new_child,
	    h, regs.pcie_dev) != PCICFG_SUCCESS) {
		ret = PCICFG_FAILURE;
		goto failed;
	}

	header_type = pci_config_get8(h, PCI_CONF_HEADER);

	/*
	 * If this is not a multi-function card only probe function zero.
	 */
	if (!(header_type & PCI_HEADER_MULTI) && (func > 0)) {

		ret = PCICFG_NODEVICE;
		goto failed;
	}

	if ((header_type & PCI_HEADER_TYPE_M) == PCI_HEADER_PPB) {

		/*
		 * XXX - Transparent bridges are handled differently
		 * than other devices with regards to fcode.  Since
		 * no transparent bridge currently ships with fcode,
		 * there is no reason to try to extract it from its rom
		 * or call the fcode interpreter to try to load a drop-in.
		 * If fcode is developed to handle transparent bridges,
		 * this code will have to change.
		 */

		DEBUG3("--Bridge found bus [0x%x] device"
		    "[0x%x] func [0x%x]\n", bus, device, func);

		/* Only support read-only probe for leaf device */
		if (flags & PCICFG_FLAG_READ_ONLY)
			goto failed;

		if ((ret = pcicfg_probe_bridge(new_child, h,
		    bus, highest_bus, is_pcie)) != PCICFG_SUCCESS)
			(void) pcicfg_free_bridge_resources(new_child);
		goto done;
	} else {

		DEBUG3("--Leaf device found bus [0x%x] device"
		    "[0x%x] func [0x%x]\n",
		    bus, device, func);

		/*
		 * link in tree, but don't bind driver
		 * We don't have compatible property yet
		 */
		(void) i_ndi_config_node(new_child, DS_LINKED, 0);

		/* XXX for now, don't run Fcode in read-only probe. */
		if (flags & PCICFG_FLAG_READ_ONLY)
			goto no_fcode;

		if (pci_config_get8(h, PCI_CONF_IPIN)) {
			pci_config_put8(h, PCI_CONF_ILINE, 0xf);
		}

#ifdef PCICFG_INTERPRET_FCODE
		/*
		 * Some platforms (x86) don't run fcode, so don't interpret
		 * fcode that might be in the ROM.
		 */
		if (pcicfg_dont_interpret == 0) {

			/* This platform supports fcode */

			vendor_id = pci_config_get16(h, PCI_CONF_VENID);
			device_id = pci_config_get16(h, PCI_CONF_DEVID);

			/*
			 * Get the ROM size and create register for it
			 */
			pci_config_put32(h, PCI_CONF_ROM, 0xfffffffe);

			request = pci_config_get32(h, PCI_CONF_ROM);

			/*
			 * If its a zero length, don't do
			 * any programming.
			 */

			if (request != 0) {
				/*
				 * Add resource to assigned-addresses.
				 */
				if (pcicfg_fcode_assign_bars(h, new_child,
				    bus, device, func, request, &p)
				    != PCICFG_SUCCESS) {
					DEBUG0("Failed to assign addresses to "
					    "implemented BARs");
					ret = PCICFG_FAILURE;
					goto failed;
				}

				/* Turn device on */
				(void) pcicfg_device_on(h);

				/*
				 * Attempt to load fcode.
				 */
				(void) pcicfg_load_fcode(new_child, bus, device,
				    func, vendor_id, device_id, &fcode_addr,
				    &fcode_size, PCICFG_LOADDR(mem_answer),
				    (~(PCI_BASE_ROM_ADDR_M & request)) + 1);

				/* Turn device off */
				(void) pcicfg_device_off(h);

				/*
				 * Free the ROM resources.
				 */
				(void) pcicfg_free_resource(new_child, p, 0,
				    "assigned-addresses");

				DEBUG2("configure: fcode addr %lx size %x\n",
				    fcode_addr, fcode_size);

				/*
				 * Create the fcode-rom-offset property.  The
				 * buffer containing the fcode always starts
				 * with 0xF1, so the fcode offset is zero.
				 */
				if (ndi_prop_update_int(DDI_DEV_T_NONE,
				    new_child, "fcode-rom-offset", 0)
				    != DDI_SUCCESS) {
					DEBUG0("Failed to create "
					    "fcode-rom-offset property\n");
					ret = PCICFG_FAILURE;
					goto failed;
				}
			} else {
				DEBUG0("There is no Expansion ROM\n");
				fcode_addr = NULL;
				fcode_size = 0;
			}

			/*
			 * Fill in the bus specific arguments. For
			 * PCI, it is the config address.
			 */
			po.config_address =
			    PCICFG_MAKE_REG_HIGH(bus, device, func, 0);

			DEBUG1("config_address=%x\n", po.config_address);

			/*
			 * Build unit address.
			 */
			(void) sprintf(unit_address, "%x,%x", device, func);

			DEBUG3("pci_fc_ops_alloc_handle ap=%lx "
			    "new device=%lx unit address=%s\n",
			    parent, new_child, unit_address);

			c = pci_fc_ops_alloc_handle(parent, new_child,
			    fcode_addr, fcode_size, unit_address, &po);

			DEBUG0("calling fcode_interpreter()\n");

			DEBUG3("Before int DIP=%lx binding name %s major %d\n",
			    new_child, ddi_binding_name(new_child),
			    ddi_driver_major(new_child));

			error = fcode_interpreter(parent, &pci_fc_ops, c);

			DEBUG1("returned from fcode_interpreter() - "
			    "returned %x\n", error);

			pci_fc_ops_free_handle(c);

			DEBUG1("fcode size = %x\n", fcode_size);
			/*
			 * We don't need the fcode anymore. While allocating
			 * we had rounded up to a page size.
			 */
			if (fcode_size) {
				kmem_free(fcode_addr, ptob(btopr(fcode_size)));
			}
		} else {
			/* This platform does not support fcode */

			DEBUG0("NOT calling fcode_interpreter()\n");
		}

#endif /* PCICFG_INTERPRET_FCODE */

		if ((error == 0) && (pcicfg_dont_interpret == 0)) {
			/*
			 * The interpreter completed successfully.
			 * We need to redo the resources based on the new reg
			 * property.
			 */
			DEBUG3("DIP=%lx binding name %s major %d\n", new_child,
			    ddi_binding_name(new_child),
			    ddi_driver_major(new_child));

			/*
			 * Readjust resources specified by reg property.
			 */
			if (pcicfg_alloc_new_resources(new_child) ==
			    PCICFG_FAILURE) {
				ret = PCICFG_FAILURE;
				goto failed;
			}

			/*
			 * At this stage, there should be enough info to pull
			 * the status property if it exists.
			 */
			if (ddi_prop_lookup_string(DDI_DEV_T_ANY,
			    new_child, NULL, "status", &status_prop) ==
			    DDI_PROP_SUCCESS) {
				if ((strncmp("disabled", status_prop, 8) ==
				    0) || (strncmp("fail", status_prop, 4) ==
				    0)) {
					ret = PCICFG_FAILURE;
					ddi_prop_free(status_prop);
					goto failed;
				} else {
					ddi_prop_free(status_prop);
				}
			}

			ret = PCICFG_SUCCESS;
			/* no fcode, bind driver now */
			(void) ndi_devi_bind_driver(new_child, 0);

			goto done;
		} else if ((error != FC_NO_FCODE) &&
		    (pcicfg_dont_interpret == 0))  {
			/*
			 * The interpreter located fcode, but had an error in
			 * processing. Cleanup and fail the operation.
			 */
			DEBUG0("Interpreter detected fcode failure\n");
			(void) pcicfg_free_resources(new_child, flags);
			ret = PCICFG_FAILURE;
			goto failed;
		} else {
no_fcode:
			/*
			 * Either the interpreter failed with FC_NO_FCODE or we
			 * chose not to run the interpreter
			 * (pcicfg_dont_interpret).
			 *
			 * If the interpreter failed because there was no
			 * dropin, then we need to probe the device ourself.
			 */

			/*
			 * Free any resources that may have been assigned
			 * during fcode loading/execution since we need
			 * to start over.
			 */
			(void) pcicfg_free_resources(new_child, flags);

#ifdef	EFCODE21554
			pcicfg_config_teardown(&h);
#else
			pcicfg_unmap_phys(&h, &p);
#endif
			/* destroy the bus_t before the dev node is gone */
			if (is_pcie)
				pcie_fini_bus(new_child, PCIE_BUS_FINAL);

			(void) ndi_devi_free(new_child);

			DEBUG0("No Drop-in Probe device ourself\n");

			ret = pcicfg_probe_children(parent,
			    PCI_GETBDF(bus, device, func),
			    highest_bus, flags, is_pcie);

			if (ret != PCICFG_SUCCESS) {
				DEBUG0("Could not self probe child\n");
				goto failed2;
			}

			/*
			 * We successfully self probed the device.
			 */
			if ((new_child = pcicfg_devi_find(
			    parent, device, func)) == NULL) {
				DEBUG0("Did'nt find device node "
				    "just created\n");
				ret = PCICFG_FAILURE;
				goto failed2;
			}
#ifdef	EFCODE21554
			/*
			 * Till now, we have detected a non transparent bridge
			 * (ntbridge) as a part of the generic probe code and
			 * configured only one configuration
			 * header which is the side facing the host bus.
			 * Now, configure the other side and create children.
			 *
			 * To make the process simpler, lets load the device
			 * driver for the non transparent bridge as this is a
			 * Solaris bundled driver, and use its configuration map
			 * services rather than programming it here.
			 * If the driver is not bundled into Solaris, it must be
			 * first loaded and configured before performing any
			 * hotplug operations.
			 *
			 * This not only makes the code simpler but also more
			 * generic.
			 *
			 * So here we go.
			 */
			if (pcicfg_is_ntbridge(new_child) != DDI_FAILURE) {

				DEBUG0("Found nontransparent bridge.\n");

				ret = pcicfg_configure_ntbridge(new_child,
				    bus, device);
			}
			if (ret != PCICFG_SUCCESS) {
				/*
				 * Bridge configure failed. Free up the self
				 * probed entry. The bus resource allocation
				 * maps need to be cleaned up to prevent
				 * warnings on retries of the failed configure.
				 */
				(void) pcicfg_ntbridge_unconfigure(new_child);
				(void) pcicfg_teardown_device(new_child,
				    flags, is_pcie);
			}
#endif
			goto done2;
		}
	}
done:
failed:
	if (is_pcie) {
		if (ret == PCICFG_SUCCESS)
			(void) pcie_init_bus(new_child, 0, PCIE_BUS_FINAL);
		else
			pcie_fini_bus(new_child, PCIE_BUS_FINAL);
	}

#ifdef	EFCODE21554
	pcicfg_config_teardown(&h);
#else
	pcicfg_unmap_phys(&h, &p);
#endif
failed3:
	if (ret != PCICFG_SUCCESS)
		(void) ndi_devi_free(new_child);
done2:
failed2:
	if (parent_regs.pcie_dev) {
		if ((flags & PCICFG_FLAG_READ_ONLY) == 0) {
			pcicfg_enable_bridge_probe_err(parent,
			    ph, &parent_regs);
		}
		pci_config_teardown(&ph);
	}

	return (ret);
}

/*
 * Read the BARs and update properties. Used in virtual hotplug.
 */
static int
pcicfg_populate_props_from_bar(dev_info_t *new_child,
    ddi_acc_handle_t config_handle)
{
	uint32_t request, base, base_hi, size;
	int i;

	i = PCI_CONF_BASE0;

	while (i <= PCI_CONF_BASE5) {
		/*
		 * determine the size of the address space
		 */
		base = pci_config_get32(config_handle, i);
		pci_config_put32(config_handle, i, 0xffffffff);
		request = pci_config_get32(config_handle, i);
		pci_config_put32(config_handle, i, base);

		/*
		 * If its a zero length, don't do any programming.
		 */
		if (request != 0) {
			/*
			 * Add to the "reg" property
			 */
			if (pcicfg_update_reg_prop(new_child,
			    request, i) != PCICFG_SUCCESS) {
				goto failedchild;
			}

			if ((PCI_BASE_SPACE_IO & request) == 0 &&
			    (PCI_BASE_TYPE_M & request) == PCI_BASE_TYPE_ALL) {
				base_hi = pci_config_get32(config_handle, i+4);
			} else {
				base_hi = 0;
			}
			/*
			 * Add to "assigned-addresses" property
			 */
			size = (~(PCI_BASE_M_ADDR_M & request))+1;
			if (pcicfg_update_assigned_prop_value(new_child,
			    size, base, base_hi, i, "assigned-addresses")
			    != PCICFG_SUCCESS) {
				goto failedchild;
			}
		} else {
			DEBUG1("BASE register [0x%x] asks for "
			"[0x0]=[0x0](32)\n", i);
			i += 4;
			continue;
		}

		/*
		 * Increment by eight if it is 64 bit address space
		 */
		if ((PCI_BASE_TYPE_M & request) == PCI_BASE_TYPE_ALL) {
			DEBUG3("BASE register [0x%x] asks for "
			"[0x%x]=[0x%x] (64)\n",
			    i, request,
			    (~(PCI_BASE_M_ADDR_M & request))+1)
			i += 8;
		} else {
			DEBUG3("BASE register [0x%x] asks for "
			"[0x%x]=[0x%x](32)\n",
			    i, request,
			    (~(PCI_BASE_M_ADDR_M & request))+1)
			i += 4;
		}
	}

	/*
	 * Get the ROM size and create register for it
	 */
	base = pci_config_get32(config_handle, PCI_CONF_ROM);
	pci_config_put32(config_handle, PCI_CONF_ROM, 0xfffffffe);
	request = pci_config_get32(config_handle, PCI_CONF_ROM);
	pci_config_put32(config_handle, PCI_CONF_ROM, base);

	/*
	 * If its a zero length, don't do
	 * any programming.
	 */
	if (request != 0) {
		DEBUG3("BASE register [0x%x] asks for [0x%x]=[0x%x]\n",
		    PCI_CONF_ROM, request,
		    (~(PCI_BASE_ROM_ADDR_M & request))+1);
		/*
		 * Add to the "reg" property
		 */
		if (pcicfg_update_reg_prop(new_child,
		    request, PCI_CONF_ROM) != PCICFG_SUCCESS) {
			goto failedchild;
		}
		/*
		 * Add to "assigned-addresses" property
		 */
		size = (~(PCI_BASE_ROM_ADDR_M & request))+1;
		if (pcicfg_update_assigned_prop_value(new_child, size,
		    base, 0, PCI_CONF_ROM, "assigned-addresses") !=
		    PCICFG_SUCCESS) {
			goto failedchild;
		}
	}

	return (PCICFG_SUCCESS);

failedchild:
	return (PCICFG_FAILURE);
}

static int
pcicfg_probe_bridge(dev_info_t *new_child, ddi_acc_handle_t h, uint_t bus,
	uint_t *highest_bus, boolean_t is_pcie)
{
	uint64_t next_bus;
	uint_t new_bus, num_slots;
	ndi_ra_request_t req;
	int rval, i, j;
	uint64_t mem_answer, mem_base, mem_alen, mem_size, mem_end;
	uint64_t io_answer, io_base, io_alen, io_size, io_end;
	uint64_t round_answer, round_len;
	pcicfg_range_t range[PCICFG_RANGE_LEN];
	int bus_range[2];
	pcicfg_phdl_t phdl;
	int count;
	uint64_t pcibus_base, pcibus_alen;
	uint64_t max_bus;
	uint8_t pcie_device_type = 0;
	dev_info_t *new_device;
	int trans_device;
	int ari_mode = B_FALSE;
	int max_function = PCICFG_MAX_FUNCTION;

	/*
	 * Set "device_type" to "pci", the actual type will be set later
	 * by pcicfg_set_busnode_props() below. This is needed as the
	 * pcicfg_ra_free() below would update "available" property based
	 * on "device_type".
	 *
	 * This code can be removed later after PCI configurator is changed
	 * to use PCIRM, which automatically update properties upon allocation
	 * and free, at that time we'll be able to remove the code inside
	 * ndi_ra_alloc/free() which currently updates "available" property
	 * for pci/pcie devices in pcie fabric.
	 */
	if (ndi_prop_update_string(DDI_DEV_T_NONE, new_child,
	    "device_type", "pci") != DDI_SUCCESS) {
		DEBUG0("Failed to set \"device_type\" props\n");
		return (PCICFG_FAILURE);
	}

	bzero((caddr_t)&req, sizeof (ndi_ra_request_t));
	req.ra_flags = (NDI_RA_ALLOC_BOUNDED | NDI_RA_ALLOC_PARTIAL_OK);
	req.ra_boundbase = 0;
	req.ra_boundlen = PCICFG_MAX_BUS_DEPTH;
	req.ra_len = PCICFG_MAX_BUS_DEPTH;
	req.ra_align_mask = 0;  /* no alignment needed */

	rval = ndi_ra_alloc(ddi_get_parent(new_child), &req,
	    &pcibus_base, &pcibus_alen, NDI_RA_TYPE_PCI_BUSNUM, NDI_RA_PASS);

	if (rval != NDI_SUCCESS) {
		if (rval == NDI_RA_PARTIAL_REQ) {
			/*EMPTY*/
			DEBUG0("NDI_RA_PARTIAL_REQ returned for bus range\n");
		} else {
			DEBUG0(
			    "Failed to allocate bus range for bridge\n");
			return (PCICFG_FAILURE);
		}
	}

	DEBUG2("Bus Range Allocated [base=%d] [len=%d]\n",
	    pcibus_base, pcibus_alen);

	if (ndi_ra_map_setup(new_child, NDI_RA_TYPE_PCI_BUSNUM)
	    == NDI_FAILURE) {
		DEBUG0("Can not setup resource map - NDI_RA_TYPE_PCI_BUSNUM\n");
		return (PCICFG_FAILURE);
	}

	/*
	 * Put available bus range into the pool.
	 * Take the first one for this bridge to use and don't give
	 * to child.
	 */
	(void) ndi_ra_free(new_child, pcibus_base+1, pcibus_alen-1,
	    NDI_RA_TYPE_PCI_BUSNUM, NDI_RA_PASS);

	next_bus = pcibus_base;
	max_bus = pcibus_base + pcibus_alen - 1;

	new_bus = next_bus;

	DEBUG1("NEW bus found  ->[%d]\n", new_bus);

	/* Keep track of highest bus for subordinate bus programming */
	*highest_bus = new_bus;

	/*
	 * Allocate Memory Space for Bridge
	 */
	bzero((caddr_t)&req, sizeof (ndi_ra_request_t));
	req.ra_flags = (NDI_RA_ALLOC_BOUNDED | NDI_RA_ALLOC_PARTIAL_OK);
	req.ra_boundbase = 0;
	/*
	 * Note: To support a 32b system, boundlen and len need to be
	 * 32b quantities
	 */
	req.ra_boundlen = PCICFG_4GIG_LIMIT + 1;
	req.ra_len = PCICFG_4GIG_LIMIT + 1; /* Get as big as possible */
	req.ra_align_mask =
	    PCICFG_MEMGRAN - 1; /* 1M alignment on memory space */

	rval = ndi_ra_alloc(ddi_get_parent(new_child), &req,
	    &mem_answer, &mem_alen,  NDI_RA_TYPE_MEM, NDI_RA_PASS);

	if (rval != NDI_SUCCESS) {
		if (rval == NDI_RA_PARTIAL_REQ) {
			/*EMPTY*/
			DEBUG0("NDI_RA_PARTIAL_REQ returned\n");
		} else {
			DEBUG0(
			    "Failed to allocate memory for bridge\n");
			return (PCICFG_FAILURE);
		}
	}

	DEBUG3("Bridge Memory Allocated [0x%x.%x] len [0x%x]\n",
	    PCICFG_HIADDR(mem_answer),
	    PCICFG_LOADDR(mem_answer),
	    mem_alen);

	if (ndi_ra_map_setup(new_child, NDI_RA_TYPE_MEM) == NDI_FAILURE) {
		DEBUG0("Can not setup resource map - NDI_RA_TYPE_MEM\n");
		return (PCICFG_FAILURE);
	}

	/*
	 * Put available memory into the pool.
	 */
	(void) ndi_ra_free(new_child, mem_answer, mem_alen, NDI_RA_TYPE_MEM,
	    NDI_RA_PASS);

	mem_base = mem_answer;

	/*
	 * Allocate I/O Space for Bridge
	 */
	bzero((caddr_t)&req, sizeof (ndi_ra_request_t));
	req.ra_align_mask = PCICFG_IOGRAN - 1; /* 4k alignment */
	req.ra_boundbase = 0;
	req.ra_boundlen = PCICFG_4GIG_LIMIT;
	req.ra_flags = (NDI_RA_ALLOC_BOUNDED | NDI_RA_ALLOC_PARTIAL_OK);
	req.ra_len = PCICFG_4GIG_LIMIT; /* Get as big as possible */

	rval = ndi_ra_alloc(ddi_get_parent(new_child), &req, &io_answer,
	    &io_alen, NDI_RA_TYPE_IO, NDI_RA_PASS);

	if (rval != NDI_SUCCESS) {
		if (rval == NDI_RA_PARTIAL_REQ) {
			/*EMPTY*/
			DEBUG0("NDI_RA_PARTIAL_REQ returned\n");
		} else {
			DEBUG0("Failed to allocate io space for bridge\n");
			io_base = io_answer = io_alen = 0;
			/* return (PCICFG_FAILURE); */
		}
	}

	if (io_alen) {
		DEBUG3("Bridge IO Space Allocated [0x%x.%x] len [0x%x]\n",
		    PCICFG_HIADDR(io_answer), PCICFG_LOADDR(io_answer),
		    io_alen);

		if (ndi_ra_map_setup(new_child, NDI_RA_TYPE_IO) ==
		    NDI_FAILURE) {
			DEBUG0("Can not setup resource map - NDI_RA_TYPE_IO\n");
			return (PCICFG_FAILURE);
		}

		/*
		 * Put available I/O into the pool.
		 */
		(void) ndi_ra_free(new_child, io_answer, io_alen,
		    NDI_RA_TYPE_IO, NDI_RA_PASS);
		io_base = io_answer;
	}

	pcicfg_set_bus_numbers(h, bus, new_bus, max_bus);

	/*
	 * Setup "bus-range" property before onlining the bridge.
	 */
	bus_range[0] = new_bus;
	bus_range[1] = max_bus;

	if (ndi_prop_update_int_array(DDI_DEV_T_NONE, new_child,
	    "bus-range", bus_range, 2) != DDI_SUCCESS) {
		DEBUG0("Failed to set bus-range property");
		return (PCICFG_FAILURE);
	}

	/*
	 * Reset the secondary bus
	 */
	pci_config_put16(h, PCI_BCNF_BCNTRL,
	    pci_config_get16(h, PCI_BCNF_BCNTRL) | 0x40);

	drv_usecwait(100);

	pci_config_put16(h, PCI_BCNF_BCNTRL,
	    pci_config_get16(h, PCI_BCNF_BCNTRL) & ~0x40);

	/*
	 * Program the memory base register with the
	 * start of the memory range
	 */
	pci_config_put16(h, PCI_BCNF_MEM_BASE,
	    PCICFG_HIWORD(PCICFG_LOADDR(mem_answer)));

	/*
	 * Program the memory limit register with the
	 * end of the memory range.
	 */

	pci_config_put16(h, PCI_BCNF_MEM_LIMIT,
	    PCICFG_HIWORD(PCICFG_LOADDR(
	    PCICFG_ROUND_DOWN((mem_answer + mem_alen), PCICFG_MEMGRAN) - 1)));

	/*
	 * Allocate the chunk of memory (if any) not programmed into the
	 * bridge because of the round down.
	 */
	if (PCICFG_ROUND_DOWN((mem_answer + mem_alen), PCICFG_MEMGRAN)
	    != (mem_answer + mem_alen)) {
		DEBUG0("Need to allocate Memory round off chunk\n");
		bzero((caddr_t)&req, sizeof (ndi_ra_request_t));
		req.ra_flags = NDI_RA_ALLOC_SPECIFIED;
		req.ra_addr = PCICFG_ROUND_DOWN((mem_answer + mem_alen),
		    PCICFG_MEMGRAN);
		req.ra_len =  (mem_answer + mem_alen) -
		    (PCICFG_ROUND_DOWN((mem_answer + mem_alen),
		    PCICFG_MEMGRAN));

		(void) ndi_ra_alloc(new_child, &req,
		    &round_answer, &round_len,  NDI_RA_TYPE_MEM, NDI_RA_PASS);
	}

	/*
	 * Program the I/O Space Base
	 */
	pci_config_put8(h, PCI_BCNF_IO_BASE_LOW,
	    PCICFG_HIBYTE(PCICFG_LOWORD(
	    PCICFG_LOADDR(io_answer))));

	pci_config_put16(h, PCI_BCNF_IO_BASE_HI,
	    PCICFG_HIWORD(PCICFG_LOADDR(io_answer)));

	/*
	 * Program the I/O Space Limit
	 */
	pci_config_put8(h, PCI_BCNF_IO_LIMIT_LOW,
	    PCICFG_HIBYTE(PCICFG_LOWORD(
	    PCICFG_LOADDR(PCICFG_ROUND_DOWN(io_answer + io_alen,
	    PCICFG_IOGRAN)))) - 1);

	pci_config_put16(h, PCI_BCNF_IO_LIMIT_HI,
	    PCICFG_HIWORD(PCICFG_LOADDR(
	    PCICFG_ROUND_DOWN(io_answer + io_alen, PCICFG_IOGRAN)))
	    - 1);

	/*
	 * Allocate the chunk of I/O (if any) not programmed into the
	 * bridge because of the round down.
	 */
	if (PCICFG_ROUND_DOWN((io_answer + io_alen), PCICFG_IOGRAN)
	    != (io_answer + io_alen)) {
		DEBUG0("Need to allocate I/O round off chunk\n");
		bzero((caddr_t)&req, sizeof (ndi_ra_request_t));
		req.ra_flags = NDI_RA_ALLOC_SPECIFIED;
		req.ra_addr = PCICFG_ROUND_DOWN((io_answer + io_alen),
		    PCICFG_IOGRAN);
		req.ra_len =  (io_answer + io_alen) -
		    (PCICFG_ROUND_DOWN((io_answer + io_alen),
		    PCICFG_IOGRAN));

		(void) ndi_ra_alloc(new_child, &req,
		    &round_answer, &round_len,  NDI_RA_TYPE_IO, NDI_RA_PASS);
	}

	/*
	 * Setup "ranges" property before onlining the bridge.
	 */
	bzero((caddr_t)range, sizeof (pcicfg_range_t) * PCICFG_RANGE_LEN);

	range[0].child_hi = range[0].parent_hi |= (PCI_REG_REL_M | PCI_ADDR_IO);
	range[0].child_lo = range[0].parent_lo = io_base;
	range[1].child_hi = range[1].parent_hi |=
	    (PCI_REG_REL_M | PCI_ADDR_MEM32);
	range[1].child_lo = range[1].parent_lo = mem_base;

	range[0].size_lo = io_alen;
	if (pcicfg_update_ranges_prop(new_child, &range[0])) {
		DEBUG0("Failed to update ranges (io)\n");
		return (PCICFG_FAILURE);
	}
	range[1].size_lo = mem_alen;
	if (pcicfg_update_ranges_prop(new_child, &range[1])) {
		DEBUG0("Failed to update ranges (memory)\n");
		return (PCICFG_FAILURE);
	}

	/*
	 * Clear status bits
	 */
	pci_config_put16(h, PCI_BCNF_SEC_STATUS, 0xffff);

	/*
	 * Turn off prefetchable range
	 */
	pci_config_put32(h, PCI_BCNF_PF_BASE_LOW, 0x0000ffff);
	pci_config_put32(h, PCI_BCNF_PF_BASE_HIGH, 0xffffffff);
	pci_config_put32(h, PCI_BCNF_PF_LIMIT_HIGH, 0x0);

	/*
	 * Needs to be set to this value
	 */
	pci_config_put8(h, PCI_CONF_ILINE, 0xf);

	/* check our device_type as defined by Open Firmware */
	if (pcicfg_pcie_device_type(new_child, h) == DDI_SUCCESS)
		pcie_device_type = 1;

	/*
	 * Set bus properties
	 */
	if (pcicfg_set_busnode_props(new_child, pcie_device_type,
	    (int)bus, (int)new_bus) != PCICFG_SUCCESS) {
		DEBUG0("Failed to set busnode props\n");
		return (PCICFG_FAILURE);
	}

	(void) pcicfg_device_on(h);

	if (is_pcie)
		(void) pcie_init_bus(new_child, 0, PCIE_BUS_FINAL);
	if (ndi_devi_online(new_child, NDI_NO_EVENT|NDI_CONFIG)
	    != NDI_SUCCESS) {
		DEBUG0("Unable to online bridge\n");
		return (PCICFG_FAILURE);
	}

	DEBUG0("Bridge is ONLINE\n");

	/*
	 * After a Reset, we need to wait 2^25 clock cycles before the
	 * first Configuration access.  The worst case is 33MHz, which
	 * is a 1 second wait.
	 */
	drv_usecwait(pcicfg_sec_reset_delay);

	/*
	 * Probe all children devices
	 */
	DEBUG0("Bridge Programming Complete - probe children\n");
	ndi_devi_enter(new_child, &count);
	for (i = 0; ((i < PCICFG_MAX_DEVICE) && (ari_mode == B_FALSE));
	    i++) {
		for (j = 0; j < max_function; ) {
			if (ari_mode)
				trans_device = j >> 3;
			else
				trans_device = i;

			if ((rval = pcicfg_fcode_probe(new_child,
			    new_bus, trans_device, (j & 7), highest_bus,
			    0, is_pcie))
			    != PCICFG_SUCCESS) {
				if (rval == PCICFG_NODEVICE) {
					DEBUG3("No Device at bus [0x%x]"
					    "device [0x%x] "
					    "func [0x%x]\n", new_bus,
					    trans_device, j & 7);

					if (j)
						goto next;
				} else {
					DEBUG3("Failed to configure bus "
					    "[0x%x] device [0x%x] "
					    "func [0x%x]\n", new_bus,
					    trans_device, j & 7);

					rval = PCICFG_FAILURE;
				}
				break;
			}
next:
			new_device = pcicfg_devi_find(new_child,
			    trans_device, (j & 7));

			/*
			 * Determine if ARI Forwarding should be enabled.
			 */
			if (j == 0) {
				if (new_device == NULL)
					break;

				if ((pcie_ari_supported(new_child) ==
				    PCIE_ARI_FORW_ENABLED) &&
				    (pcie_ari_device(new_device) ==
				    PCIE_ARI_DEVICE)) {
					if (pcie_ari_enable(new_child) ==
					    DDI_SUCCESS) {
						(void) ddi_prop_create(
						    DDI_DEV_T_NONE,
						    new_child,
						    DDI_PROP_CANSLEEP,
						    "ari-enabled", NULL, 0);
						ari_mode = B_TRUE;
						max_function =
						    PCICFG_MAX_ARI_FUNCTION;
					}
				}
			}

			if (ari_mode == B_TRUE) {
				int next_function;

				if (new_device == NULL)
					break;

				if (pcie_ari_get_next_function(new_device,
				    &next_function) != DDI_SUCCESS)
					break;

				j = next_function;

				if (next_function == 0)
					break;
			} else
				j++;
		}
	}

	ndi_devi_exit(new_child, count);

	/* if empty topology underneath, it is still a success. */
	if (rval != PCICFG_FAILURE)
		rval = PCICFG_SUCCESS;

	/*
	 * Offline the bridge to allow reprogramming of resources.
	 *
	 * This should always succeed since nobody else has started to
	 * use it yet, failing to detach the driver would indicate a bug.
	 * Also in that case it's better just panic than allowing the
	 * configurator to proceed with BAR reprogramming without bridge
	 * driver detached.
	 */
	VERIFY(ndi_devi_offline(new_child, NDI_NO_EVENT|NDI_UNCONFIG)
	    == NDI_SUCCESS);
	if (is_pcie)
		pcie_fini_bus(new_child, PCIE_BUS_INITIAL);

	phdl.dip = new_child;
	phdl.memory_base = mem_answer;
	phdl.io_base = (uint32_t)io_answer;
	phdl.error = PCICFG_SUCCESS;    /* in case of empty child tree */

	ndi_devi_enter(ddi_get_parent(new_child), &count);
	ddi_walk_devs(new_child, pcicfg_find_resource_end, (void *)&phdl);
	ndi_devi_exit(ddi_get_parent(new_child), count);

	if (phdl.error != PCICFG_SUCCESS) {
		DEBUG0("Failure summing resources\n");
		return (PCICFG_FAILURE);
	}

	num_slots = pcicfg_get_nslots(new_child, h);
	mem_end = PCICFG_ROUND_UP(phdl.memory_base, PCICFG_MEMGRAN);
	io_end = PCICFG_ROUND_UP(phdl.io_base, PCICFG_IOGRAN);

	DEBUG3("Start of Unallocated Bridge(%d slots) Resources "
	    "Mem=0x%lx I/O=0x%lx\n", num_slots, mem_end, io_end);

	/*
	 * Before probing the children we've allocated maximum MEM/IO
	 * resources from parent, and updated "available" property
	 * accordingly. Later we'll be giving up unused resources to
	 * the parent, thus we need to destroy "available" property
	 * here otherwise it will be out-of-sync with the actual free
	 * resources this bridge has. This property will be rebuilt below
	 * with the actual free resources reserved for hotplug slots
	 * (if any).
	 */
	(void) ndi_prop_remove(DDI_DEV_T_NONE, new_child, "available");
	/*
	 * if the bridge a slots, then preallocate. If not, assume static
	 * configuration. Also check for preallocation limits and spit
	 * warning messages appropriately (perhaps some can be in debug mode).
	 */
	if (num_slots) {
		pci_regspec_t reg;
		uint64_t mem_assigned = mem_end;
		uint64_t io_assigned = io_end;
		uint64_t mem_reqd = mem_answer + (num_slots *
		    pcicfg_slot_memsize);
		uint64_t io_reqd = io_answer + (num_slots *
		    pcicfg_slot_iosize);
		uint8_t highest_bus_reqd = new_bus + (num_slots *
		    pcicfg_slot_busnums);
#ifdef DEBUG
		if (mem_end > mem_reqd)
			DEBUG3("Memory space consumed by bridge"
			    " more than planned for %d slot(s)(%lx, %lx)",
			    num_slots, mem_answer, mem_end);
		if (io_end > io_reqd)
			DEBUG3("IO space consumed by bridge"
			    " more than planned for %d slot(s)(%lx, %lx)",
			    num_slots, io_answer, io_end);
		if (*highest_bus > highest_bus_reqd)
			DEBUG3("Buses consumed by bridge"
			    " more than planned for %d slot(s)(%x, %x)",
			    num_slots, new_bus, *highest_bus);

		if (mem_reqd > (mem_answer + mem_alen))
			DEBUG3("Memory space required by bridge"
			    " more than available for %d slot(s)(%lx, %lx)",
			    num_slots, mem_answer, mem_end);

		if (io_reqd > (io_answer + io_alen))
			DEBUG3("IO space required by bridge"
			    " more than available for %d slot(s)(%lx, %lx)",
			    num_slots, io_answer, io_end);
		if (highest_bus_reqd > max_bus)
			DEBUG3("Bus numbers required by bridge"
			    " more than available for %d slot(s)(%x, %x)",
			    num_slots, new_bus, *highest_bus);
#endif
		mem_end = MAX((MIN(mem_reqd, (mem_answer + mem_alen))),
		    mem_end);
		io_end = MAX((MIN(io_reqd, (io_answer + io_alen))), io_end);
		*highest_bus = MAX((MIN(highest_bus_reqd, max_bus)),
		    *highest_bus);
		DEBUG3("mem_end %lx, io_end %lx, highest_bus %x\n",
		    mem_end, io_end, *highest_bus);

		mem_size = mem_end - mem_assigned;
		io_size = io_end - io_assigned;

		reg.pci_phys_mid = reg.pci_size_hi = 0;
		if (io_size > 0) {
			reg.pci_phys_hi = (PCI_REG_REL_M | PCI_ADDR_IO);
			reg.pci_phys_low = io_assigned;
			reg.pci_size_low = io_size;
			if (pcicfg_update_available_prop(new_child, &reg)) {
				DEBUG0("Failed to update available prop "
				    "(io)\n");
				return (PCICFG_FAILURE);
			}
		}
		if (mem_size > 0) {
			reg.pci_phys_hi = (PCI_REG_REL_M | PCI_ADDR_MEM32);
			reg.pci_phys_low = mem_assigned;
			reg.pci_size_low = mem_size;
			if (pcicfg_update_available_prop(new_child, &reg)) {
				DEBUG0("Failed to update available prop "
				    "(memory)\n");
				return (PCICFG_FAILURE);
			}
		}
	}

	/*
	 * Give back unused memory space to parent.
	 */
	(void) ndi_ra_free(ddi_get_parent(new_child),
	    mem_end, (mem_answer + mem_alen) - mem_end, NDI_RA_TYPE_MEM,
	    NDI_RA_PASS);

	if (mem_end == mem_answer) {
		DEBUG0("No memory resources used\n");
		/*
		 * To prevent the bridge from forwarding any Memory
		 * transactions, the Memory Limit will be programmed
		 * with a smaller value than the Memory Base.
		 */
		pci_config_put16(h, PCI_BCNF_MEM_BASE, 0xffff);
		pci_config_put16(h, PCI_BCNF_MEM_LIMIT, 0);

		mem_size = 0;
	} else {
		/*
		 * Reprogram the end of the memory.
		 */
		pci_config_put16(h, PCI_BCNF_MEM_LIMIT,
		    PCICFG_HIWORD(mem_end) - 1);
		mem_size = mem_end - mem_base;
	}

	/*
	 * Give back unused io space to parent.
	 */
	(void) ndi_ra_free(ddi_get_parent(new_child),
	    io_end, (io_answer + io_alen) - io_end,
	    NDI_RA_TYPE_IO, NDI_RA_PASS);

	if (io_end == io_answer) {
		DEBUG0("No IO Space resources used\n");

		/*
		 * To prevent the bridge from forwarding any I/O
		 * transactions, the I/O Limit will be programmed
		 * with a smaller value than the I/O Base.
		 */
		pci_config_put8(h, PCI_BCNF_IO_LIMIT_LOW, 0);
		pci_config_put16(h, PCI_BCNF_IO_LIMIT_HI, 0);
		pci_config_put8(h, PCI_BCNF_IO_BASE_LOW, 0xff);
		pci_config_put16(h, PCI_BCNF_IO_BASE_HI, 0);

		io_size = 0;
	} else {
		/*
		 * Reprogram the end of the io space.
		 */
		pci_config_put8(h, PCI_BCNF_IO_LIMIT_LOW,
		    PCICFG_HIBYTE(PCICFG_LOWORD(
		    PCICFG_LOADDR(io_end) - 1)));

		pci_config_put16(h, PCI_BCNF_IO_LIMIT_HI,
		    PCICFG_HIWORD(PCICFG_LOADDR(io_end - 1)));

		io_size = io_end - io_base;
	}

	if ((max_bus - *highest_bus) > 0) {
		/*
		 * Give back unused bus numbers
		 */
		(void) ndi_ra_free(ddi_get_parent(new_child),
		    *highest_bus+1, max_bus - *highest_bus,
		    NDI_RA_TYPE_PCI_BUSNUM, NDI_RA_PASS);
	}

	/*
	 * Set bus numbers to ranges encountered during scan
	 */
	pcicfg_set_bus_numbers(h, bus, new_bus, *highest_bus);

	bus_range[0] = pci_config_get8(h, PCI_BCNF_SECBUS);
	bus_range[1] = pci_config_get8(h, PCI_BCNF_SUBBUS);
	DEBUG1("End of bridge probe: bus_range[0] =  %d\n", bus_range[0]);
	DEBUG1("End of bridge probe: bus_range[1] =  %d\n", bus_range[1]);

	if (ndi_prop_update_int_array(DDI_DEV_T_NONE, new_child,
	    "bus-range", bus_range, 2) != DDI_SUCCESS) {
		DEBUG0("Failed to set bus-range property");
		return (PCICFG_FAILURE);
	}

	/*
	 * Remove the ranges property if it exists since we will create
	 * a new one.
	 */
	(void) ndi_prop_remove(DDI_DEV_T_NONE, new_child, "ranges");

	DEBUG2("Creating Ranges property - Mem Address %lx Mem Size %x\n",
	    mem_base, mem_size);
	DEBUG2("                         - I/O Address %lx I/O Size %x\n",
	    io_base, io_size);

	bzero((caddr_t)range, sizeof (pcicfg_range_t) * PCICFG_RANGE_LEN);

	range[0].child_hi = range[0].parent_hi |= (PCI_REG_REL_M | PCI_ADDR_IO);
	range[0].child_lo = range[0].parent_lo = io_base;
	range[1].child_hi = range[1].parent_hi |=
	    (PCI_REG_REL_M | PCI_ADDR_MEM32);
	range[1].child_lo = range[1].parent_lo = mem_base;

	if (io_size > 0) {
		range[0].size_lo = io_size;
		if (pcicfg_update_ranges_prop(new_child, &range[0])) {
			DEBUG0("Failed to update ranges (io)\n");
			return (PCICFG_FAILURE);
		}
	}
	if (mem_size > 0) {
		range[1].size_lo = mem_size;
		if (pcicfg_update_ranges_prop(new_child, &range[1])) {
			DEBUG0("Failed to update ranges (memory)\n");
			return (PCICFG_FAILURE);
		}
	}

	/*
	 * Remove the resource maps for the bridge since we no longer
	 * need them.  Note that the failure is ignored since the
	 * ndi_devi_offline above may have already taken care of it via
	 * driver detach.
	 * It has been checked that there are no other reasons for
	 * failure other than map itself being non-existent. So we are Ok.
	 */
	if (ndi_ra_map_destroy(new_child, NDI_RA_TYPE_MEM) == NDI_FAILURE) {
		/*EMPTY*/
		DEBUG0("Can not destroy resource map - NDI_RA_TYPE_MEM\n");
	}

	if (ndi_ra_map_destroy(new_child, NDI_RA_TYPE_IO) == NDI_FAILURE) {
		/*EMPTY*/
		DEBUG0("Can not destroy resource map - NDI_RA_TYPE_IO\n");
	}

	if (ndi_ra_map_destroy(new_child, NDI_RA_TYPE_PCI_BUSNUM)
	    == NDI_FAILURE) {
		/*EMPTY*/
		DEBUG0("Can't destroy resource map - NDI_RA_TYPE_PCI_BUSNUM\n");
	}

	return (rval);
}

/*
 * Return PCICFG_SUCCESS if device exists at the specified address.
 * Return PCICFG_NODEVICE is no device exists at the specified address.
 *
 */
int
pcicfg_config_setup(dev_info_t *dip, ddi_acc_handle_t *handle)
{
	caddr_t			virt;
	ddi_device_acc_attr_t	attr;
	int			status;
	int			rlen;
	pci_regspec_t		*reg;
	int			ret = DDI_SUCCESS;
	int16_t			tmp;
	uint8_t			header_type;
	/*
	 * flags = PCICFG_CONF_INDIRECT_MAP if configuration space is indirectly
	 * mapped, otherwise it is 0. "flags" is introduced in support of any
	 * non transparent bridges, where configuration space is indirectly
	 * mapped.
	 * Indirect mapping is always true on sun4v systems.
	 */
	int			flags = 0;


	/*
	 * Get the pci register spec from the node
	 */
	status = ddi_getlongprop(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, "reg", (caddr_t)&reg, &rlen);

	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG0("reg present, but unable to get memory\n");
			return (PCICFG_FAILURE);
		default:
			DEBUG0("no reg property\n");
			return (PCICFG_FAILURE);
	}

	if (pcicfg_indirect_map(dip) == DDI_SUCCESS)
		flags |= PCICFG_CONF_INDIRECT_MAP;

	/*
	 * Map in configuration space (temporarily)
	 */
	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	attr.devacc_attr_access = DDI_CAUTIOUS_ACC;

#ifdef	EFCODE21554
	if (ddi_regs_map_setup(dip, 0, &virt,
	    0, 0, &attr, handle) != DDI_SUCCESS)
#else
	if (pcicfg_map_phys(dip, reg, &virt, &attr, handle)
	    != DDI_SUCCESS)
#endif
	{
		DEBUG0("pcicfg_config_setup():"
		"Failed to setup config space\n");

		kmem_free((caddr_t)reg, rlen);
		return (PCICFG_FAILURE);
	}

	if (flags & PCICFG_CONF_INDIRECT_MAP) {
		/*
		 * need to use DDI interfaces as the conf space is
		 * cannot be directly accessed by the host.
		 */
		tmp = (int16_t)ddi_get16(*handle, (uint16_t *)virt);
	} else {
		ret = ddi_peek16(dip, (int16_t *)virt, &tmp);
	}

	if (ret == DDI_SUCCESS) {
		if ((tmp == (int16_t)0xffff) || (tmp == -1)) {
			/*
			 * This could be a VF device. Check if SRIOV capable
			 */
			DEBUG0("Checking if the device is VF...\n");
			header_type = pci_config_get8(*handle, PCI_CONF_HEADER);
			DEBUG1("header type = 0x%x\n", (uint_t)header_type);
			if (header_type == 0xff) {
				DEBUG1("NO DEVICEFOUND, read %x\n", tmp);
				ret = PCICFG_NODEVICE;
			}
		} else {
			/* XXX - Need to check why HV is returning 0 */
			if (tmp == 0) {
				DEBUG0("Device Not Ready yet ?");
				ret = PCICFG_NODEVICE;
			} else {
				DEBUG1("DEVICEFOUND, read %x\n", tmp);
				ret = PCICFG_SUCCESS;
			}
		}
	} else {
		DEBUG0("ddi_peek failed, must be NODEVICE\n");
		ret = PCICFG_NODEVICE;
	}

	/*
	 * A bug in XMITS 3.0 causes us to miss the Master Abort Split
	 * Completion message.  The result is the error message being
	 * sent back as part of the config data.  If the first two words
	 * of the config space happen to be the same as the Master Abort
	 * message, then report back that there is no device there.
	 */
	if ((ret == PCICFG_SUCCESS) && !(flags & PCICFG_CONF_INDIRECT_MAP)) {
		int32_t	pcix_scm;

#define		PCICFG_PCIX_SCM	0x10000004

		pcix_scm = 0;
		(void) ddi_peek32(dip, (int32_t *)virt, &pcix_scm);
		if (pcix_scm == PCICFG_PCIX_SCM) {
			pcix_scm = 0;
			(void) ddi_peek32(dip,
			    (int32_t *)(virt + 4), &pcix_scm);
			if (pcix_scm == PCICFG_PCIX_SCM)
				ret = PCICFG_NODEVICE;
		}
	}

	if (ret == PCICFG_NODEVICE)
#ifdef	EFCODE21554
		ddi_regs_map_free(handle);
#else
		pcicfg_unmap_phys(handle, reg);
#endif

	kmem_free((caddr_t)reg, rlen);

	return (ret);

}

static void
pcicfg_config_teardown(ddi_acc_handle_t *handle)
{
	(void) ddi_regs_map_free(handle);
}

static int
pcicfg_add_config_reg(dev_info_t *dip, uint_t bdf)
{
	int reg[10] = { PCI_ADDR_CONFIG, 0, 0, 0, 0};

	reg[0] = (bdf << 8);

	return (ndi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "reg", reg, 5));
}

static int
pcicfg_dump_assigned(dev_info_t *dip)
{
	pci_regspec_t		*reg;
	int			length;
	int			rcount;
	int			i;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses", (caddr_t)&reg,
	    &length) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read assigned-addresses property\n");
		return (PCICFG_FAILURE);
	}

	rcount = length / sizeof (pci_regspec_t);
	for (i = 0; i < rcount; i++) {
		DEBUG4("pcicfg_dump_assigned - size=%x low=%x mid=%x high=%x\n",
		    reg[i].pci_size_low, reg[i].pci_phys_low,
		    reg[i].pci_phys_mid, reg[i].pci_phys_hi);
	}
	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)reg, length);

	return (PCICFG_SUCCESS);
}

#ifdef PCICFG_INTERPRET_FCODE
static int
pcicfg_load_fcode(dev_info_t *dip, uint_t bus, uint_t device, uint_t func,
    uint16_t vendor_id, uint16_t device_id, uchar_t **fcode_addr,
    int *fcode_size, int rom_paddr, int rom_size)
{
	pci_regspec_t		p;
	int			pci_data;
	int			start_of_fcode;
	int			image_length;
	int			code_type;
	ddi_acc_handle_t	h;
	ddi_device_acc_attr_t	acc;
	uint8_t			*addr;
	int8_t			image_not_found, indicator;
	uint16_t		vendor_id_img, device_id_img;
	int16_t			rom_sig;
#ifdef DEBUG
	int i;
#endif

	DEBUG4("pcicfg_load_fcode() - "
	    "bus %x device =%x func=%x rom_size=%lx\n",
	    bus, device, func, rom_size);
	DEBUG2("pcicfg_load_fcode() - vendor_id=%x device_id=%x\n",
	    vendor_id, device_id);

	*fcode_size = 0;
	*fcode_addr = NULL;

	acc.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	acc.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	acc.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	p.pci_phys_hi = PCI_ADDR_MEM32 | PCICFG_MAKE_REG_HIGH(bus, device,
	    func, PCI_CONF_ROM);

	p.pci_phys_mid = 0;
	p.pci_phys_low = 0;

	p.pci_size_low = rom_size;
	p.pci_size_hi = 0;

	if (pcicfg_map_phys(dip, &p, (caddr_t *)&addr, &acc, &h)) {
		DEBUG1("Can Not map in ROM %x\n", p.pci_phys_low);
		return (PCICFG_FAILURE);
	}

	/*
	 * Walk the ROM to find the proper image for this device.
	 */
	image_not_found = 1;
	while (image_not_found) {
		DEBUG1("Expansion ROM maps to %lx\n", addr);

#ifdef DEBUG
		if (pcicfg_dump_fcode) {
			for (i = 0; i < 100; i++)
				DEBUG2("ROM 0x%x --> 0x%x\n", i,
				    ddi_get8(h, (uint8_t *)(addr + i)));
		}
#endif

		/*
		 * Some device say they have an Expansion ROM, but do not, so
		 * for non-21554 devices use peek so we don't panic due to
		 * accessing non existent memory.
		 */
		if (pcicfg_indirect_map(dip) == DDI_SUCCESS) {
			rom_sig = ddi_get16(h,
			    (uint16_t *)(addr + PCI_ROM_SIGNATURE));
		} else {
			if (ddi_peek16(dip,
			    (int16_t *)(addr + PCI_ROM_SIGNATURE), &rom_sig)) {
				cmn_err(CE_WARN,
				    "PCI Expansion ROM is not accessible");
				pcicfg_unmap_phys(&h, &p);
				return (PCICFG_FAILURE);
			}
		}

		/*
		 * Validate the ROM Signature.
		 */
		if ((uint16_t)rom_sig != 0xaa55) {
			DEBUG1("Invalid ROM Signature %x\n", (uint16_t)rom_sig);
			pcicfg_unmap_phys(&h, &p);
			return (PCICFG_FAILURE);
		}

		DEBUG0("Valid ROM Signature Found\n");

		start_of_fcode = ddi_get16(h, (uint16_t *)(addr + 2));

		pci_data =  ddi_get16(h,
		    (uint16_t *)(addr + PCI_ROM_PCI_DATA_STRUCT_PTR));

		DEBUG2("Pointer To PCI Data Structure %x %x\n", pci_data,
		    addr);

		/*
		 * Validate the PCI Data Structure Signature.
		 * 0x52494350 = "PCIR"
		 */

		if (ddi_get8(h, (uint8_t *)(addr + pci_data)) != 0x50) {
			DEBUG0("Invalid PCI Data Structure Signature\n");
			pcicfg_unmap_phys(&h, &p);
			return (PCICFG_FAILURE);
		}

		if (ddi_get8(h, (uint8_t *)(addr + pci_data + 1)) != 0x43) {
			DEBUG0("Invalid PCI Data Structure Signature\n");
			pcicfg_unmap_phys(&h, &p);
			return (PCICFG_FAILURE);
		}
		if (ddi_get8(h, (uint8_t *)(addr + pci_data + 2)) != 0x49) {
			DEBUG0("Invalid PCI Data Structure Signature\n");
			pcicfg_unmap_phys(&h, &p);
			return (PCICFG_FAILURE);
		}
		if (ddi_get8(h, (uint8_t *)(addr + pci_data + 3)) != 0x52) {
			DEBUG0("Invalid PCI Data Structure Signature\n");
			pcicfg_unmap_phys(&h, &p);
			return (PCICFG_FAILURE);
		}

		/*
		 * Is this image for this device?
		 */
		vendor_id_img = ddi_get16(h,
		    (uint16_t *)(addr + pci_data + PCI_PDS_VENDOR_ID));
		device_id_img = ddi_get16(h,
		    (uint16_t *)(addr + pci_data + PCI_PDS_DEVICE_ID));

		DEBUG2("This image is for vendor_id=%x device_id=%x\n",
		    vendor_id_img, device_id_img);

		code_type = ddi_get8(h, addr + pci_data + PCI_PDS_CODE_TYPE);

		switch (code_type) {
		case PCI_PDS_CODE_TYPE_PCAT:
			DEBUG0("ROM is of x86/PC-AT Type\n");
			break;
		case PCI_PDS_CODE_TYPE_OPEN_FW:
			DEBUG0("ROM is of Open Firmware Type\n");
			break;
		default:
			DEBUG1("ROM is of Unknown Type 0x%x\n", code_type);
			break;
		}

		if ((vendor_id_img != vendor_id) ||
		    (device_id_img != device_id) ||
		    (code_type != PCI_PDS_CODE_TYPE_OPEN_FW)) {
			DEBUG0("Firmware Image is not for this device..."
			    "goto next image\n");
			/*
			 * Read indicator byte to see if there is another
			 * image in the ROM
			 */
			indicator = ddi_get8(h,
			    (uint8_t *)(addr + pci_data + PCI_PDS_INDICATOR));

			if (indicator != 1) {
				/*
				 * There is another image in the ROM.
				 */
				image_length = ddi_get16(h,  (uint16_t *)(addr +
				    pci_data + PCI_PDS_IMAGE_LENGTH)) * 512;

				addr += image_length;
			} else {
				/*
				 * There are no more images.
				 */
				DEBUG0("There are no more images in the ROM\n");
				pcicfg_unmap_phys(&h, &p);

				return (PCICFG_FAILURE);
			}
		} else {
			DEBUG0("Correct image was found\n");
			image_not_found = 0;  /* Image was found */
		}
	}

	*fcode_size =  (ddi_get8(h, addr + start_of_fcode + 4) << 24) |
	    (ddi_get8(h, addr + start_of_fcode + 5) << 16) |
	    (ddi_get8(h, addr + start_of_fcode + 6) << 8) |
	    (ddi_get8(h, addr + start_of_fcode + 7));

	DEBUG1("Fcode Size %x\n", *fcode_size);

	/*
	 * Allocate page aligned buffer space
	 */
	*fcode_addr = kmem_zalloc(ptob(btopr(*fcode_size)), KM_SLEEP);

	if (*fcode_addr == NULL) {
		DEBUG0("kmem_zalloc returned NULL\n");
		pcicfg_unmap_phys(&h, &p);
		return (PCICFG_FAILURE);
	}

	DEBUG1("Fcode Addr %lx\n", *fcode_addr);

	ddi_rep_get8(h, *fcode_addr, addr + start_of_fcode, *fcode_size,
	    DDI_DEV_AUTOINCR);

	pcicfg_unmap_phys(&h, &p);

	return (PCICFG_SUCCESS);
}

static int
pcicfg_fcode_assign_bars(ddi_acc_handle_t h, dev_info_t *dip, uint_t bus,
    uint_t device, uint_t func, int32_t fc_request, pci_regspec_t *rom_regspec)
{
	/*
	 * Assign values to all BARs so that it is safe to turn on the
	 * device for accessing the fcode on the PROM. On successful
	 * exit from this function, "assigned-addresses" are created
	 * for all BARs and ROM BAR is enabled. Also, rom_regspec is
	 * filled with the values that can be used to free up this
	 * resource later.
	 */
	uint32_t request, hiword, size;
	pci_regspec_t phys_spec;
	ndi_ra_request_t req;
	uint64_t mem_answer, mem_alen;
	int i;

	DEBUG1("pcicfg_fcode_assign_bars :%s\n", DEVI(dip)->devi_name);

	/*
	 * Process the BARs.
	 */
	for (i = PCI_CONF_BASE0; i <= PCI_CONF_BASE5; ) {
		pci_config_put32(h, i, 0xffffffff);
		request = pci_config_get32(h, i);
		/*
		 * Check if implemented
		 */
		if (request == 0) {
			DEBUG1("pcicfg_fcode_assign_bars :"
			    "BASE register [0x%x] asks for 0(32)\n", i);
			i += 4;
			continue;
		}
		/*
		 * Build the phys_spec for this BAR
		 */
		hiword = PCICFG_MAKE_REG_HIGH(bus, device, func, i);
		size = (~(PCI_BASE_M_ADDR_M & request)) + 1;

		DEBUG3("pcicfg_fcode_assign_bars :"
		    "BASE register [0x%x] asks for [0x%x]=[0x%x]\n",
		    i, request, size);

		if ((PCI_BASE_SPACE_M & request) == PCI_BASE_SPACE_MEM) {
			if ((PCI_BASE_TYPE_M & request) == PCI_BASE_TYPE_MEM) {
				hiword |= PCI_ADDR_MEM32;
			} else if ((PCI_BASE_TYPE_M & request)
			    == PCI_BASE_TYPE_ALL) {
				hiword |= PCI_ADDR_MEM64;
			}
			if (request & PCI_BASE_PREF_M)
				hiword |= PCI_REG_PF_M;
		} else {
			hiword |= PCI_ADDR_IO;
		}
		phys_spec.pci_phys_hi = hiword;
		phys_spec.pci_phys_mid = 0;
		phys_spec.pci_phys_low = 0;
		phys_spec.pci_size_hi = 0;
		phys_spec.pci_size_low = size;

		/*
		 * The following function
		 * - allocates address space
		 * - programs the BAR
		 * - adds an "assigned-addresses" property
		 */
		if (pcicfg_alloc_resource(dip, &phys_spec,
		    "assigned-addresses", 0)) {
			cmn_err(CE_WARN, "failed to allocate %d bytes"
			    " for dev %s BASE register [0x%x]\n",
			    size, DEVI(dip)->devi_name, i);
			goto failure;
		}
		if ((PCI_BASE_TYPE_M & request) == PCI_BASE_TYPE_ALL) {
			/*
			 * 64 bit, should be in memory space.
			 */
			i += 8;
		} else {
			/*
			 * 32 bit, either memory or I/O space.
			 */
			i += 4;
		}
	}

	/*
	 * Handle ROM BAR. We do not use the common
	 * resource allocator function because we need to
	 * return reg spec to the caller.
	 */
	size = (~(PCI_BASE_ROM_ADDR_M & fc_request)) + 1;

	DEBUG3("BASE register [0x%x] asks for "
	    "[0x%x]=[0x%x]\n", PCI_CONF_ROM, fc_request, size);

	bzero((caddr_t)&req, sizeof (ndi_ra_request_t));

	req.ra_boundbase = 0;
	req.ra_boundlen = PCICFG_4GIG_LIMIT;
	req.ra_len = size;
	req.ra_flags = (NDI_RA_ALIGN_SIZE | NDI_RA_ALLOC_BOUNDED);

	if (ndi_ra_alloc(ddi_get_parent(dip),
	    &req, &mem_answer, &mem_alen,
	    NDI_RA_TYPE_MEM, NDI_RA_PASS)) {
		cmn_err(CE_WARN, "failed to allocate %d bytes"
		    " for dev %s ROM BASE register\n",
		    size, DEVI(dip)->devi_name);
		goto failure;
	}

	DEBUG3("ROM addr = [0x%x.%x] len [0x%x]\n",
	    PCICFG_HIADDR(mem_answer),
	    PCICFG_LOADDR(mem_answer), mem_alen);

	/*
	 * Assign address space and enable ROM.
	 */
	pci_config_put32(h, PCI_CONF_ROM,
	    PCICFG_LOADDR(mem_answer) | PCI_BASE_ROM_ENABLE);

	/*
	 * Add resource to assigned-addresses.
	 */
	phys_spec.pci_phys_hi = PCICFG_MAKE_REG_HIGH(bus, device, func, \
	    PCI_CONF_ROM) | PCI_ADDR_MEM32;
	if (fc_request & PCI_BASE_PREF_M)
		phys_spec.pci_phys_hi |= PCI_REG_PF_M;
	phys_spec.pci_phys_mid = 0;
	phys_spec.pci_phys_low = PCICFG_LOADDR(mem_answer);
	phys_spec.pci_size_hi = 0;
	phys_spec.pci_size_low = size;

	if (pcicfg_update_assigned_prop(dip, &phys_spec, "assigned-addresses")
	    != PCICFG_SUCCESS) {
		cmn_err(CE_WARN, "failed to update"
		    " assigned-address property for dev %s\n",
		    DEVI(dip)->devi_name);
		goto failure;
	}
	/*
	 * Copy out the reg spec.
	 */
	*rom_regspec = phys_spec;

	return (PCICFG_SUCCESS);

failure:
	/*
	 * We came in with no "assigned-addresses".
	 * Free up the resources we may have allocated.
	 */
	(void) pcicfg_free_device_resources(dip, 0, "assigned-addresses");

	return (PCICFG_FAILURE);
}

#endif /* PCICFG_INTERPRET_FCODE */

static int
pcicfg_free_all_resources(dev_info_t *dip)
{
	pci_regspec_t		*assigned;
	int			assigned_len;
	int			acount;
	int			i;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses", (caddr_t)&assigned,
	    &assigned_len) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read assigned-addresses property\n");
		return (PCICFG_FAILURE);
	}

	acount = assigned_len / sizeof (pci_regspec_t);

	for (i = 0; i < acount; i++) {
		if (pcicfg_free_resource(dip, assigned[i], 0,
		    "assigned-addresses")) {
			/*
			 * Dont forget to free mem from ddi_getlongprop
			 */
			kmem_free((caddr_t)assigned, assigned_len);
			return (PCICFG_FAILURE);
		}
	}

	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	if (assigned_len)
		kmem_free((caddr_t)assigned, assigned_len);

	return (PCICFG_SUCCESS);
}
static int
pcicfg_alloc_new_resources(dev_info_t *dip)
{
	pci_regspec_t		*assigned, *reg;
	int			assigned_len, reg_len;
	int			acount, rcount;
	int			i, j, alloc_size;
	boolean_t		alloc;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reg", (caddr_t)&reg,
	    &reg_len) != DDI_PROP_SUCCESS) {
		DEBUG0("Failed to read reg property\n");
		return (PCICFG_FAILURE);
	}
	rcount = reg_len / sizeof (pci_regspec_t);

	DEBUG2("pcicfg_alloc_new_resources() reg size=%x entries=%x\n",
	    reg_len, rcount);

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "assigned-addresses", (caddr_t)&assigned,
	    &assigned_len) != DDI_PROP_SUCCESS) {
		acount = 0;
	} else {
		acount = assigned_len / sizeof (pci_regspec_t);
	}

	DEBUG1("assigned-addresses property len=%x\n", acount);

	/*
	 * For each address described by reg, search for it in the
	 * assigned-addresses property. If it does not exist, allocate
	 * resources for it. If it does exist, check the size in both.
	 * The size needs to be bigger of the two.
	 */
	for (i = 1; i < rcount; i++) {
		alloc = B_TRUE;
		alloc_size = reg[i].pci_size_low;
		for (j = 0; j < acount; j++) {
			if (assigned[j].pci_phys_hi == reg[i].pci_phys_hi) {
				/*
				 * There is an exact match. Check size.
				 */
				DEBUG1("pcicfg_alloc_new_resources "
				    "- %x - MATCH\n",
				    reg[i].pci_phys_hi);

				if (reg[i].pci_size_low >
				    assigned[j].pci_size_low) {
					/*
					 * Fcode wants more.
					 */
					DEBUG3("pcicfg_alloc_new_resources"
					    " - %x - RESIZE"
					    " assigned 0x%x reg 0x%x\n",
					    assigned[j].pci_phys_hi,
					    assigned[j].pci_size_low,
					    reg[i].pci_size_low);

					/*
					 * Free the old resource.
					 */
					(void) pcicfg_free_resource(dip,
					    assigned[j], 0,
					    "assigned-addresses");
				} else {
					DEBUG3("pcicfg_alloc_new_resources"
					    " - %x - ENOUGH"
					    " assigned 0x%x reg 0x%x\n",
					    assigned[j].pci_phys_hi,
					    assigned[j].pci_size_low,
					    reg[i].pci_size_low);

					alloc = B_FALSE;
				}
				break;
			}
			/*
			 * Fcode may have set one or more of the
			 * NPT bits in phys.hi.
			 */
			if (PCI_REG_BDFR_G(assigned[j].pci_phys_hi) ==
			    PCI_REG_BDFR_G(reg[i].pci_phys_hi)) {

				DEBUG2("pcicfg_alloc_new_resources "
				    "- PARTIAL MATCH assigned 0x%x "
				    "reg 0x%x\n", assigned[j].pci_phys_hi,
				    reg[i].pci_phys_hi);
				/*
				 * Changing the SS bits is an error
				 */
				if (PCI_REG_ADDR_G(
				    assigned[j].pci_phys_hi) !=
				    PCI_REG_ADDR_G(reg[i].pci_phys_hi)) {

					DEBUG2("Fcode changing"
					    " SS bits of - 0x%x -"
					    " on %s\n", reg[i].pci_phys_hi,
					    DEVI(dip)->devi_name);

				}


				/*
				 * We are going to allocate new resource.
				 * Free the old resource. Again, adjust
				 * the size to be safe.
				 */
				(void) pcicfg_free_resource(dip,
				    assigned[j], 0, "assigned-addresses");

				alloc_size = MAX(reg[i].pci_size_low,
				    assigned[j].pci_size_low);

				break;
			}
		}
		/*
		 * We are allocating resources for one of three reasons -
		 * - Fcode wants a larger address space
		 * - Fcode has set changed/set n, p, t bits.
		 * - It is a new "reg", it should be only ROM bar, but
		 *   we don't do the checking.
		 */
		if (alloc == B_TRUE) {
			DEBUG1("pcicfg_alloc_new_resources : creating 0x%x\n",
			    reg[i].pci_phys_hi);

			reg[i].pci_size_low = alloc_size;
			if (pcicfg_alloc_resource(dip, reg + i,
			    "assigned-addresses", 0)) {
				/*
				 * Dont forget to free mem from
				 * ddi_getlongprop
				 */
				if (acount != 0)
					kmem_free((caddr_t)assigned,
					    assigned_len);
				kmem_free((caddr_t)reg, reg_len);
				return (PCICFG_FAILURE);
			}
		}
	}

	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	if (acount != 0)
		kmem_free((caddr_t)assigned, assigned_len);
	kmem_free((caddr_t)reg, reg_len);

	return (PCICFG_SUCCESS);
}

static int
pcicfg_alloc_resource(dev_info_t *dip, pci_regspec_t *phys_spec,
	char *prop_name, int num_vf)
{
	uint64_t answer;
	uint64_t alen;
	int offset;
	pci_regspec_t config;
	caddr_t virt, v;
	ddi_device_acc_attr_t acc;
	ddi_acc_handle_t h;
	ndi_ra_request_t request;
	pci_regspec_t *assigned;
	int assigned_len, entries, i;
	pcie_bus_t	*bus_p = PCIE_DIP2UPBUS(dip);
	int		ret = PCICFG_SUCCESS;
	boolean_t	is_pf;

	is_pf = strcmp("vf-assigned-addresses", prop_name) == 0;
	if (!is_pf && (ddi_getlongprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, prop_name, (caddr_t)&assigned,
	    &assigned_len) == DDI_PROP_SUCCESS)) {
		DEBUG1("pcicfg_alloc_resource - "
		    "searching %s\n", prop_name);

		entries = assigned_len / (sizeof (pci_regspec_t));

		/*
		 * Walk through the prop_name entries. If there is
		 * a match, there is no need to allocate the resource.
		 */
		for (i = 0; i < entries; i++) {
			if (assigned[i].pci_phys_hi == phys_spec->pci_phys_hi) {
				DEBUG1("pcicfg_alloc_resource - MATCH %x\n",
				    assigned[i].pci_phys_hi);
				kmem_free(assigned, assigned_len);
				return (0);
			}
		}
		kmem_free(assigned, assigned_len);
	}

	bzero((caddr_t)&request, sizeof (ndi_ra_request_t));

	config.pci_phys_hi = PCI_CONF_ADDR_MASK & phys_spec->pci_phys_hi;
	config.pci_phys_hi &= ~PCI_REG_REG_M;
	config.pci_phys_mid = config.pci_phys_low = 0;
	config.pci_size_hi = config.pci_size_low = 0;

	/*
	 * Map in configuration space (temporarily)
	 */
	acc.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	acc.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	acc.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	if (pcicfg_map_phys(dip, &config, &virt, &acc, &h)) {
		DEBUG0("Can not map in config space\n");
		return (1);
	}

	request.ra_boundbase = 0;
	request.ra_boundlen = PCICFG_4GIG_LIMIT;

	/*
	 * Use size stored in phys_spec parameter.
	 */
	request.ra_len = phys_spec->pci_size_low;
	if (!is_pf)
		request.ra_flags = NDI_RA_ALIGN_SIZE;
	else
		request.ra_align_mask = request.ra_len/num_vf - 1;


	offset = PCI_REG_REG_G(phys_spec->pci_phys_hi);
	if (strcmp("vf-assigned-addresses", prop_name) == 0) {
		if (bus_p->sriov_cap_ptr == 0) {
			ret =  PCICFG_FAILURE;
			goto exit;
		}
		DEBUG2("offset = 0x%x sriov_cap_ptr = 0x%x\n", offset,
		    bus_p->sriov_cap_ptr);
		offset = bus_p->sriov_cap_ptr +
		    (PCIE_EXT_CAP_SRIOV_VF_BAR0_OFFSET +
		    (offset * 4));
		DEBUG1("VF BAR offset is 0x%x\n", offset);
	}
	v = virt + offset;

	if (PCI_REG_REG_G(phys_spec->pci_phys_hi) == PCI_CONF_ROM) {

		request.ra_flags |= NDI_RA_ALLOC_BOUNDED;

		/* allocate memory space from the allocator */

		if (ndi_ra_alloc(ddi_get_parent(dip),
		    &request, &answer, &alen,
		    NDI_RA_TYPE_MEM, NDI_RA_PASS)
		    != NDI_SUCCESS) {
			DEBUG0("(ROM)Failed to allocate 32b mem");
			ret =  PCICFG_FAILURE;
			goto exit;
		}
		DEBUG3("ROM addr = [0x%x.%x] len [0x%x]\n",
		    PCICFG_HIADDR(answer),
		    PCICFG_LOADDR(answer),
		    alen);

		/* program the low word */

		ddi_put32(h, (uint32_t *)v, (uint32_t)PCICFG_LOADDR(answer));

		phys_spec->pci_phys_low = PCICFG_LOADDR(answer);
		phys_spec->pci_phys_mid = PCICFG_HIADDR(answer);
	} else {

		switch (PCI_REG_ADDR_G(phys_spec->pci_phys_hi)) {
		case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
			request.ra_flags &= ~NDI_RA_ALLOC_BOUNDED;
			/* allocate memory space from the allocator */
			if (ndi_ra_alloc(ddi_get_parent(dip),
			    &request, &answer, &alen,
			    NDI_RA_TYPE_MEM, NDI_RA_PASS)
			    != NDI_SUCCESS) {
				DEBUG1(
				"Failed to allocate 64 bit mem of size 0x%x\n",
				    request.ra_len);
				ret =  PCICFG_FAILURE;
				goto exit;
			}
			DEBUG3(
			    "allocated 64 bit addr = [0x%x.%x] len is 0x%x\n",
			    PCICFG_HIADDR(answer),
			    PCICFG_LOADDR(answer),
			    alen);

			DEBUG1("virt address of conf register = %llx\n", v);
			{
				/* program the low word */

				ddi_put32(h, (uint32_t *)v,
				    (uint32_t)PCICFG_LOADDR(answer));

				/* program the high word with value zero */
				v += 4;
				ddi_put32(h, (uint32_t *)v,
				    (uint32_t)PCICFG_HIADDR(answer));
			}
			DEBUG2("64 bit address value read from the VF BAR"
			    " register: 0x%x.%x\n",
			    ddi_get32(h, (uint32_t *)v),
			    ddi_get32(h, (uint32_t *)(v - 4)));
			phys_spec->pci_phys_low = PCICFG_LOADDR(answer);
			phys_spec->pci_phys_mid = PCICFG_HIADDR(answer);
			/*
			 * currently support 32b address space
			 * assignments only.
			 */
			phys_spec->pci_phys_hi ^= PCI_ADDR_MEM64 ^
			    PCI_ADDR_MEM32;

			break;

		case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
			request.ra_flags |= NDI_RA_ALLOC_BOUNDED;
			/* allocate memory space from the allocator */
			if (ndi_ra_alloc(ddi_get_parent(dip),
			    &request, &answer, &alen,
			    NDI_RA_TYPE_MEM, NDI_RA_PASS)
			    != NDI_SUCCESS) {
				DEBUG1(
				"Failed to allocate 32 bit mem of size 0x%x\n",
				    request.ra_len);
				ret =  PCICFG_FAILURE;
				goto exit;
			}

			DEBUG3("32 bit addr = [0x%x.%x] len is 0x%x\n",
			    PCICFG_HIADDR(answer),
			    PCICFG_LOADDR(answer),
			    alen);

			/* program the low word */

			ddi_put32(h, (uint32_t *)v,
			    (uint32_t)PCICFG_LOADDR(answer));
			DEBUG1("32 bit address value read from the VF BAR"
			    " register: 0x%x\n",
			    ddi_get32(h, (uint32_t *)v));
			phys_spec->pci_phys_low = PCICFG_LOADDR(answer);

			break;
		case PCI_REG_ADDR_G(PCI_ADDR_IO):
			/* allocate I/O space from the allocator */
			request.ra_flags |= NDI_RA_ALLOC_BOUNDED;
			if (ndi_ra_alloc(ddi_get_parent(dip),
			    &request, &answer, &alen,
			    NDI_RA_TYPE_IO, NDI_RA_PASS)
			    != NDI_SUCCESS) {
				DEBUG0("Failed to allocate I/O\n");
				ret =  PCICFG_FAILURE;
				goto exit;
			}
			DEBUG3("I/O addr = [0x%x.%x] len [0x%x]\n",
			    PCICFG_HIADDR(answer),
			    PCICFG_LOADDR(answer),
			    alen);

			ddi_put32(h, (uint32_t *)v,
			    (uint32_t)PCICFG_LOADDR(answer));

			phys_spec->pci_phys_low = PCICFG_LOADDR(answer);

			break;
		default:
			DEBUG0("Unknown register type\n");
			ret =  PCICFG_FAILURE;
			goto exit;
		} /* switch */
	}

	/*
	 * Now that memory locations are assigned,
	 * update the assigned address property.
	 */
	if (strcmp("vf-assigned-addresses", prop_name) != 0) {
		DEBUG1("updating assigned-addresss for %x\n",
		    phys_spec->pci_phys_hi);
		if (pcicfg_update_assigned_prop(dip, phys_spec, prop_name)) {
			ret =  PCICFG_FAILURE;
			goto exit;
		}
	}
exit:
	pcicfg_unmap_phys(&h, &config);
	return (ret);
}

static int
pcicfg_free_pci_regspec(dev_info_t *dip, pci_regspec_t *phys_spec)
{
	if (phys_spec->pci_size_low == 0)
		return (PCICFG_SUCCESS);
	if (PCI_REG_REG_G(phys_spec->pci_phys_hi) == PCI_CONF_ROM) {

		/* free memory back to the allocator */
		if (ndi_ra_free(ddi_get_parent(dip), phys_spec->pci_phys_low,
		    phys_spec->pci_size_low,
		    NDI_RA_TYPE_MEM, NDI_RA_PASS) != NDI_SUCCESS) {
			DEBUG0("(ROM)Can not free 32b mem");
			return (1);
		}
	} else {

		switch (PCI_REG_ADDR_G(phys_spec->pci_phys_hi)) {
		case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
			/* free memory back to the allocator */
			if (ndi_ra_free(ddi_get_parent(dip),
			    PCICFG_LADDR(phys_spec->pci_phys_low,
			    phys_spec->pci_phys_mid),
			    phys_spec->pci_size_low, NDI_RA_TYPE_MEM,
			    NDI_RA_PASS) != NDI_SUCCESS) {
				DEBUG3("couldn't free 0x%x.%x size 0x%x\n",
				    phys_spec->pci_phys_low,
				    phys_spec->pci_phys_mid,
				    phys_spec->pci_size_low);
				return (1);
			}
			DEBUG3("Freed memory 0x%x.%x size 0x%x\n",
			    phys_spec->pci_phys_low,
			    phys_spec->pci_phys_mid,
			    phys_spec->pci_size_low);
			break;

		case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
			/* free memory back to the allocator */
			if (ndi_ra_free(ddi_get_parent(dip),
			    phys_spec->pci_phys_low,
			    phys_spec->pci_size_low, NDI_RA_TYPE_MEM,
			    NDI_RA_PASS) != NDI_SUCCESS) {
				DEBUG2("couldn't free 0x%x size 0x%x\n",
				    phys_spec->pci_phys_low,
				    phys_spec->pci_size_low);
				return (1);
			}
			DEBUG2("Freed memory 0x%x size 0x%x\n",
			    phys_spec->pci_phys_low,
			    phys_spec->pci_size_low);
			break;
		case PCI_REG_ADDR_G(PCI_ADDR_IO):
			/* free I/O space back to the allocator */
			if (ndi_ra_free(ddi_get_parent(dip),
			    phys_spec->pci_phys_low,
			    phys_spec->pci_size_low, NDI_RA_TYPE_IO,
			    NDI_RA_PASS) != NDI_SUCCESS) {
				DEBUG0("Can not free I/O space");
				return (1);
			}
			break;
		default:
			DEBUG1("Unknown register type %x\n",
			    PCI_REG_ADDR_G(phys_spec->pci_phys_hi));
			return (1);
		} /* switch */
	}
	return (0);
}

static int
pcicfg_free_resource(dev_info_t *dip, pci_regspec_t phys_spec,
    pcicfg_flags_t flags, char *prop_name)
{
	int offset;
	pci_regspec_t config;
	caddr_t virt, v;
	ddi_device_acc_attr_t acc;
	ddi_acc_handle_t h;
	ndi_ra_request_t request;
	int l;

	bzero((caddr_t)&request, sizeof (ndi_ra_request_t));

	config.pci_phys_hi = PCI_CONF_ADDR_MASK & phys_spec.pci_phys_hi;
	config.pci_phys_hi &= ~PCI_REG_REG_M;
	config.pci_phys_mid = config.pci_phys_low = 0;
	config.pci_size_hi = config.pci_size_low = 0;

	/*
	 * Map in configuration space (temporarily)
	 */
	acc.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	acc.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	acc.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	if (pcicfg_map_phys(dip, &config, &virt, &acc, &h)) {
		DEBUG0("Can not map in config space\n");
		return (1);
	}

	offset = PCI_REG_REG_G(phys_spec.pci_phys_hi);

	v = virt + offset;

	/*
	 * Use size stored in phys_spec parameter.
	 */
	l = phys_spec.pci_size_low;

	if (PCI_REG_REG_G(phys_spec.pci_phys_hi) == PCI_CONF_ROM) {

		/* free memory back to the allocator */
		if (ndi_ra_free(ddi_get_parent(dip), phys_spec.pci_phys_low,
		    l, NDI_RA_TYPE_MEM, NDI_RA_PASS) != NDI_SUCCESS) {
			DEBUG0("(ROM)Can not free 32b mem");
			pcicfg_unmap_phys(&h, &config);
			return (1);
		}

		/* Unmap the BAR by writing a zero */

		if ((flags & PCICFG_FLAG_READ_ONLY) == 0)
			ddi_put32(h, (uint32_t *)v, (uint32_t)0);
	} else {

		switch (PCI_REG_ADDR_G(phys_spec.pci_phys_hi)) {

		case PCI_REG_ADDR_G(PCI_ADDR_MEM64):
		case PCI_REG_ADDR_G(PCI_ADDR_MEM32):
			/* free memory back to the allocator */
			if (ndi_ra_free(ddi_get_parent(dip),
			    PCICFG_LADDR(phys_spec.pci_phys_low,
			    phys_spec.pci_phys_mid),
			    l, NDI_RA_TYPE_MEM,
			    NDI_RA_PASS) != NDI_SUCCESS) {
				DEBUG0("Cannot free mem");
				pcicfg_unmap_phys(&h, &config);
				return (1);
			}
			break;

		case PCI_REG_ADDR_G(PCI_ADDR_IO):
			/* free I/O space back to the allocator */
			if (ndi_ra_free(ddi_get_parent(dip),
			    phys_spec.pci_phys_low,
			    l, NDI_RA_TYPE_IO,
			    NDI_RA_PASS) != NDI_SUCCESS) {
				DEBUG0("Can not free I/O space");
				pcicfg_unmap_phys(&h, &config);
				return (1);
			}
			break;

		default:
			DEBUG0("Unknown register type\n");
			pcicfg_unmap_phys(&h, &config);
			return (1);
		} /* switch */
	}

	/*
	 * Now that memory locations are assigned,
	 * update the assigned address property.
	 */

	DEBUG2("updating %s for %x\n", prop_name, phys_spec.pci_phys_hi);

	if (pcicfg_remove_assigned_prop(dip, &phys_spec, prop_name)) {
		pcicfg_unmap_phys(&h, &config);
		return (1);
	}

	pcicfg_unmap_phys(&h, &config);

	return (0);
}

static int
pcicfg_remove_assigned_prop(dev_info_t *dip, pci_regspec_t *oldone,
	char *prop_name)
{
	int		alen, num_entries, i;
	pci_regspec_t	*assigned, *assigned_copy;
	uint_t		status;
	pci_regspec_t	*newone;

	status = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    prop_name, (caddr_t)&assigned, &alen);
	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			DEBUG1("no memory for %s property\n", prop_name);
			return (1);
		default:
			DEBUG1("%s property does not exist\n", prop_name);
			return (0);
	}

	/*
	 * Make a copy of old assigned-addresses property.
	 */
	assigned_copy = kmem_alloc(alen, KM_SLEEP);
	bcopy(assigned, assigned_copy, alen);

	status = ndi_prop_remove(DDI_DEV_T_NONE, dip, prop_name);

	if (status != DDI_PROP_SUCCESS) {
		/*
		 * If prop_name is retrieved from PROM, the
		 * ndi_prop_remove() will fail.
		 */
		DEBUG3("pcicfg_remove_assigned_prop:property %s"
		    "couldn't be removed. status returned is 0x%x"
		    " replacing entry 0x%x with null zero values\n", prop_name,
		    status, oldone->pci_phys_hi);
		newone = kmem_alloc(sizeof (pci_regspec_t), KM_SLEEP);
		newone->pci_phys_hi = oldone->pci_phys_hi;
		newone->pci_phys_mid = 0;
		newone->pci_phys_low = 0;
		newone->pci_size_hi = 0;
		newone->pci_size_low = 0;
		(void) pcicfg_update_assigned_prop(dip,
		    newone, prop_name);
		/*
		 * Free up allocated memory
		 */
		kmem_free(newone, sizeof (pci_regspec_t));
		kmem_free(assigned_copy, alen);
		kmem_free((caddr_t)assigned, alen);

		return (0);
	}
	DEBUG1("pcicfg_remove_assigned_prop: property %s has been removed\n",
	    prop_name);

	num_entries = alen / sizeof (pci_regspec_t);

	/*
	 * Rebuild the assigned-addresses property.
	 */
	for (i = 0; i < num_entries; i++) {
		if (assigned_copy[i].pci_phys_hi != oldone->pci_phys_hi) {
			(void) pcicfg_update_assigned_prop(dip,
			    &assigned_copy[i], prop_name);
		}
	}

	/*
	 * Free the copy of the original assigned-addresses.
	 */
	kmem_free(assigned_copy, alen);

	/*
	 * Don't forget to free up memory from ddi_getlongprop
	 */
	kmem_free((caddr_t)assigned, alen);

	return (0);
}

static int
pcicfg_map_phys(dev_info_t *dip, pci_regspec_t *phys_spec,
	caddr_t *addrp, ddi_device_acc_attr_t *accattrp,
	ddi_acc_handle_t *handlep)
{
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;
	int result;

	*handlep = impl_acc_hdl_alloc(KM_SLEEP, NULL);
	hp = impl_acc_hdl_get(*handlep);
	hp->ah_vers = VERS_ACCHDL;
	hp->ah_dip = dip;
	hp->ah_rnumber = 0;
	hp->ah_offset = 0;
	hp->ah_len = 0;
	hp->ah_acc = *accattrp;

	mr.map_op = DDI_MO_MAP_LOCKED;
	mr.map_type = DDI_MT_REGSPEC;
	mr.map_obj.rp = (struct regspec *)phys_spec;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;

	result = ddi_map(dip, &mr, 0, 0, addrp);

	if (result != DDI_SUCCESS) {
		impl_acc_hdl_free(*handlep);
		*handlep = (ddi_acc_handle_t)NULL;
	} else {
		hp->ah_addr = *addrp;
	}

	return (result);
}

void
pcicfg_unmap_phys(ddi_acc_handle_t *handlep,  pci_regspec_t *ph)
{
	ddi_map_req_t mr;
	ddi_acc_hdl_t *hp;

	hp = impl_acc_hdl_get(*handlep);
	ASSERT(hp);

	mr.map_op = DDI_MO_UNMAP;
	mr.map_type = DDI_MT_REGSPEC;
	mr.map_obj.rp = (struct regspec *)ph;
	mr.map_prot = PROT_READ | PROT_WRITE;
	mr.map_flags = DDI_MF_KERNEL_MAPPING;
	mr.map_handlep = hp;
	mr.map_vers = DDI_MAP_VERSION;

	(void) ddi_map(hp->ah_dip, &mr, hp->ah_offset,
	    hp->ah_len, &hp->ah_addr);

	impl_acc_hdl_free(*handlep);
	*handlep = (ddi_acc_handle_t)NULL;
}

static int
pcicfg_ari_configure(dev_info_t *dip)
{
	if (pcie_ari_supported(dip) == PCIE_ARI_FORW_NOT_SUPPORTED)
		return (DDI_FAILURE);

	/*
	 * Until we have resource balancing, dynamically configure
	 * ARI functions without firmware assistamce.
	 */
	return (DDI_FAILURE);
}

#ifdef DEBUG
static void
debug(char *fmt, uintptr_t a1, uintptr_t a2, uintptr_t a3,
	uintptr_t a4, uintptr_t a5)
{
	if (pcicfg_debug == 1) {
		prom_printf("pcicfg: ");
		prom_printf(fmt, a1, a2, a3, a4, a5);
	} else
		if (pcicfg_debug)
			cmn_err(CE_CONT, fmt, a1, a2, a3, a4, a5);
}
#endif

/*
 * Return true if the devinfo node is in a PCI Express hierarchy.
 */
static boolean_t
is_pcie_fabric(dev_info_t *dip)
{
	dev_info_t *root = ddi_root_node();
	dev_info_t *pdip;
	boolean_t found = B_FALSE;
	char *bus;

	/*
	 * Does this device reside in a pcie fabric ?
	 */
	for (pdip = dip; pdip && (pdip != root) && !found;
	    pdip = ddi_get_parent(pdip)) {
		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, pdip,
		    DDI_PROP_DONTPASS, "device_type", &bus) !=
		    DDI_PROP_SUCCESS)
			break;

		if (strcmp(bus, "pciex") == 0)
			found = B_TRUE;

		ddi_prop_free(bus);
	}

	return (found);
}

static void
pci_dump(ddi_acc_handle_t handle)
{
	uint8_t cap_ptr;
	uint8_t next_ptr;
	uint32_t msix_bar;
	uint32_t msix_ctrl;
	uint32_t tbl_offset;
	uint32_t tbl_bir;
	uint32_t pba_offset;
	uint32_t pba_bir;
	off_t offset;

	cmn_err(CE_CONT, "Begin dump PCI config space");

	cmn_err(CE_CONT,
	    "PCI_CONF_VENID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_VENID));
	cmn_err(CE_CONT,
	    "PCI_CONF_DEVID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_DEVID));
	cmn_err(CE_CONT,
	    "PCI_CONF_COMMAND:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_COMM));
	cmn_err(CE_CONT,
	    "PCI_CONF_STATUS:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_STAT));
	cmn_err(CE_CONT,
	    "PCI_CONF_REVID:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_REVID));
	cmn_err(CE_CONT,
	    "PCI_CONF_PROG_CLASS:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_PROGCLASS));
	cmn_err(CE_CONT,
	    "PCI_CONF_SUB_CLASS:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_SUBCLASS));
	cmn_err(CE_CONT,
	    "PCI_CONF_BAS_CLASS:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_BASCLASS));
	cmn_err(CE_CONT,
	    "PCI_CONF_CACHE_LINESZ:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_CACHE_LINESZ));
	cmn_err(CE_CONT,
	    "PCI_CONF_LATENCY_TIMER:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_LATENCY_TIMER));
	cmn_err(CE_CONT,
	    "PCI_CONF_HEADER_TYPE:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_HEADER));
	cmn_err(CE_CONT,
	    "PCI_CONF_BIST:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_BIST));
	cmn_err(CE_CONT,
	    "PCI_CONF_BASE0:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE0));
	cmn_err(CE_CONT,
	    "PCI_CONF_BASE1:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE1));
	cmn_err(CE_CONT,
	    "PCI_CONF_BASE2:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE2));

	/* MSI-X BAR */
	msix_bar = pci_config_get32(handle, PCI_CONF_BASE3);
	cmn_err(CE_CONT,
	    "PCI_CONF_BASE3:\t0x%x\n", msix_bar);

	cmn_err(CE_CONT,
	    "PCI_CONF_BASE4:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE4));
	cmn_err(CE_CONT,
	    "PCI_CONF_BASE5:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_BASE5));

	cmn_err(CE_CONT,
	    "PCI_CONF_CIS:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_CIS));
	cmn_err(CE_CONT,
	    "PCI_CONF_SUBVENID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_SUBVENID));
	cmn_err(CE_CONT,
	    "PCI_CONF_SUBSYSID:\t0x%x\n",
	    pci_config_get16(handle, PCI_CONF_SUBSYSID));
	cmn_err(CE_CONT,
	    "PCI_CONF_ROM:\t0x%x\n",
	    pci_config_get32(handle, PCI_CONF_ROM));

	cap_ptr = pci_config_get8(handle, PCI_CONF_CAP_PTR);

	cmn_err(CE_CONT,
	    "PCI_CONF_CAP_PTR:\t0x%x\n", cap_ptr);
	cmn_err(CE_CONT,
	    "PCI_CONF_ILINE:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_ILINE));
	cmn_err(CE_CONT,
	    "PCI_CONF_IPIN:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_IPIN));
	cmn_err(CE_CONT,
	    "PCI_CONF_MIN_G:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_MIN_G));
	cmn_err(CE_CONT,
	    "PCI_CONF_MAX_L:\t0x%x\n",
	    pci_config_get8(handle, PCI_CONF_MAX_L));

	/* Power Management */
	offset = cap_ptr;

	cmn_err(CE_CONT,
	    "PCI_PM_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);

	cmn_err(CE_CONT,
	    "PCI_PM_NEXT_PTR:\t0x%x\n", next_ptr);
	cmn_err(CE_CONT,
	    "PCI_PM_CAP:\t0x%x\n",
	    pci_config_get16(handle, offset + PCI_PMCAP));
	cmn_err(CE_CONT,
	    "PCI_PM_CSR:\t0x%x\n",
	    pci_config_get16(handle, offset + PCI_PMCSR));
	cmn_err(CE_CONT,
	    "PCI_PM_CSR_BSE:\t0x%x\n",
	    pci_config_get8(handle, offset + PCI_PMCSR_BSE));
	cmn_err(CE_CONT,
	    "PCI_PM_DATA:\t0x%x\n",
	    pci_config_get8(handle, offset + PCI_PMDATA));

	/* MSI Configuration */
	offset = next_ptr;

	cmn_err(CE_CONT,
	    "PCI_MSI_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);

	cmn_err(CE_CONT,
	    "PCI_MSI_NEXT_PTR:\t0x%x\n", next_ptr);
	cmn_err(CE_CONT,
	    "PCI_MSI_CTRL:\t0x%x\n",
	    pci_config_get16(handle, offset + PCI_MSI_CTRL));
	cmn_err(CE_CONT,
	    "PCI_MSI_ADDR:\t0x%x\n",
	    pci_config_get32(handle, offset + PCI_MSI_ADDR_OFFSET));
	cmn_err(CE_CONT,
	    "PCI_MSI_ADDR_HI:\t0x%x\n",
	    pci_config_get32(handle, offset + 0x8));
	cmn_err(CE_CONT,
	    "PCI_MSI_DATA:\t0x%x\n",
	    pci_config_get16(handle, offset + 0xC));

	/* MSI-X Configuration */
	offset = next_ptr;

	cmn_err(CE_CONT,
	    "PCI_MSIX_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset));

	next_ptr = pci_config_get8(handle, offset + 1);
	cmn_err(CE_CONT,
	    "PCI_MSIX_NEXT_PTR:\t0x%x\n", next_ptr);

	msix_ctrl = pci_config_get16(handle, offset + PCI_MSIX_CTRL);
	cmn_err(CE_CONT,
	    "PCI_MSIX_CTRL:\t0x%x\n", msix_ctrl);

	tbl_offset = pci_config_get32(handle, offset + PCI_MSIX_TBL_OFFSET);
	tbl_bir = tbl_offset & PCI_MSIX_TBL_BIR_MASK;
	tbl_offset = tbl_offset & ~PCI_MSIX_TBL_BIR_MASK;
	cmn_err(CE_CONT,
	    "PCI_MSIX_TBL_OFFSET:\t0x%x\n", tbl_offset);
	cmn_err(CE_CONT,
	    "PCI_MSIX_TBL_BIR:\t0x%x\n", tbl_bir);

	pba_offset = pci_config_get32(handle, offset + PCI_MSIX_PBA_OFFSET);
	pba_bir = pba_offset & PCI_MSIX_PBA_BIR_MASK;
	pba_offset = pba_offset & ~PCI_MSIX_PBA_BIR_MASK;
	cmn_err(CE_CONT,
	    "PCI_MSIX_PBA_OFFSET:\t0x%x\n", pba_offset);
	cmn_err(CE_CONT,
	    "PCI_MSIX_PBA_BIR:\t0x%x\n", pba_bir);

	/* PCI Express Configuration */
	offset = next_ptr;

	cmn_err(CE_CONT,
	    "PCIE_CAP_ID:\t0x%x\n",
	    pci_config_get8(handle, offset + PCIE_CAP_ID));

	next_ptr = pci_config_get8(handle, offset + PCIE_CAP_NEXT_PTR);

	cmn_err(CE_CONT,
	    "PCIE_CAP_NEXT_PTR:\t0x%x\n", next_ptr);
	cmn_err(CE_CONT,
	    "PCIE_PCIECAP:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_PCIECAP));
	cmn_err(CE_CONT,
	    "PCIE_DEVCAP:\t0x%x\n",
	    pci_config_get32(handle, offset + PCIE_DEVCAP));
	cmn_err(CE_CONT,
	    "PCIE_DEVCTL:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_DEVCTL));
	cmn_err(CE_CONT,
	    "PCIE_DEVSTS:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_DEVSTS));
	cmn_err(CE_CONT,
	    "PCIE_LINKCAP:\t0x%x\n",
	    pci_config_get32(handle, offset + PCIE_LINKCAP));
	cmn_err(CE_CONT,
	    "PCIE_LINKCTL:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_LINKCTL));
	cmn_err(CE_CONT,
	    "PCIE_LINKSTS:\t0x%x\n",
	    pci_config_get16(handle, offset + PCIE_LINKSTS));

}
