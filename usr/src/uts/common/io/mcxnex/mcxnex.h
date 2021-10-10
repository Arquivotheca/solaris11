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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MCXNEX_H
#define	_MCXNEX_H

/*
 * mcxnex.h
 *    Contains the #defines and typedefs necessary for the softstate
 *    structure and for proper attach() and detach() processing.  Also
 *    includes all the other Mcxnex header files (and so is the only header
 *    file that is directly included by the source files).
 *    Lastly, this file includes everything necessary for implementing the
 *    devmap interface and for maintaining the "mapped resource database".
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/taskq.h>
#include <sys/atomic.h>
#ifdef FMA_TEST
#include <sys/modhash.h>
#endif

#include <sys/ib/ibtl/ibci.h>
#include <sys/ib/ibtl/impl/ibtl_util.h>
#include <sys/ib/adapters/mlnx_umap.h>

/*
 * First include all the typedefs, then include all the other device
 * specific headers (many of which depend on the typedefs having
 * already been defined).
 */
#include "mcxnex_typedef.h"
#include "mcxnex_hw.h"

#include "mcxnex_cfg.h"
#include "mcxnex_cmd.h"
#include "mcxnex_cq.h"
#include "mcxnex_event.h"
#include "mcxnex_ioctl.h"
#include "mcxnex_misc.h"
#include "mcxnex_mr.h"
#include "mcxnex_wr.h"
#include "mcxnex_qp.h"
#include "mcxnex_srq.h"
#include "mcxnex_rsrc.h"
#include "mcxnex_fm.h"

/*
 * Number of initial states to setup. Used in call to ddi_soft_state_init()
 */
#define	MCXNEX_INITIAL_STATES		3

/*
 * Macro and defines used to calculate device instance number from minor
 * number (and vice versa).
 */
#define	MCXNEX_MINORNUM_SHIFT		3
#define	MCXNEX_DEV_INSTANCE(dev)	(getminor((dev)) &	\
	((1 << MCXNEX_MINORNUM_SHIFT) - 1))

/*
 * Locations for the various Mcxnex hardware CMD,UAR & MSIx PCIe BARs
 */
#define	MCXNEX_CMD_BAR			1 /* device config space */
#define	MCXNEX_UAR_BAR			2 /* UAR Region */
#define	MCXNEX_MSIX_BAR			3 /* MSI-X Table */

#define	MCXNEX_ONCLOSE_FLASH_INPROGRESS		(1 << 0)

#define	MCXNEX_MSIX_MAX			8 /* max # of interrupt vectors */

/*
 * VPD header size - or more rightfully, the area of interest for fwflash
 * 	There's more, but we don't need it for our use so we don't read it
 */
#define	MCXNEX_VPD_HDR_DWSIZE		0x10 /* 16 Dwords */
#define	MCXNEX_VPD_HDR_BSIZE		0x40 /* 64 Bytes */

/*
 * Offsets to be used w/ reset to save/restore PCI capability stuff
 */
#define	MCXNEX_PCI_CAP_DEV_OFFS		0x08
#define	MCXNEX_PCI_CAP_LNK_OFFS		0x10


/*
 * Some defines for the software reset.  These define the value that should
 * be written to begin the reset (MCXNEX_SW_RESET_START), the delay before
 * beginning to poll for completion (MCXNEX_SW_RESET_DELAY), the in-between
 * polling delay (MCXNEX_SW_RESET_POLL_DELAY), and the value that indicates
 * that the reset has not completed (MCXNEX_SW_RESET_NOTDONE).
 */
#define	MCXNEX_SW_RESET_START		0x00000001
#define	MCXNEX_SW_RESET_DELAY		1000000	 /* 1000 ms, per 0.36 PRM */
#define	MCXNEX_SW_RESET_POLL_DELAY	100	 /* 100 us */
#define	MCXNEX_SW_RESET_NOTDONE		0xFFFFFFFF

/*
 * These defines are used in the Mcxnex software reset operation.  They define
 * the total number PCI registers to read/restore during the reset.  And they
 * also specify two config registers which should not be read or restored.
 */
#define	MCXNEX_SW_RESET_NUMREGS		0x40
#define	MCXNEX_SW_RESET_REG22_RSVD	0x16	/* 22 dec */
#define	MCXNEX_SW_RESET_REG23_RSVD	0x17	/* 23 dec */

/*
 * Macro used to output HCA warning messages.  Note: HCA warning messages
 * are only generated when an unexpected condition has been detected.  This
 * can be the result of a software bug or some other problem, but it is more
 * often an indication that the HCA firmware (and/or hardware) has done
 * something unexpected.  This warning message means that the driver state
 * in unpredictable and that shutdown/restart is suggested.
 */
#define	MCXNEX_WARNING(state, ...)	\
	mcxnex_printf(state, CE_WARN, __VA_ARGS__)
#define	MCXNEX_NOTE(state, ...)		\
	mcxnex_printf(state, CE_NOTE, __VA_ARGS__)



/*
 * Macro used to set attach failure messages.  Also, the attach message buf
 * size is set here.
 */
#define	MCXNEX_ATTACH_MSGSIZE	80
#define	MCXNEX_ATTACH_MSG(attach_buf, attach_msg)		\
	(void) snprintf((attach_buf), MCXNEX_ATTACH_MSGSIZE, (attach_msg));
#define	MCXNEX_ATTACH_MSG_INIT(attach_buf)			\
	(attach_buf)[0] = '\0';

/*
 * Macros used for controlling whether or not event callbacks will be forwarded
 * to the IBTF.  This is necessary because there are certain race conditions
 * that can occur (e.g. calling IBTF with an asynch event before the IBTF
 * registration has successfully completed or handling an event after we've
 * detached from the IBTF.)
 *
 * MCXNEX_ENABLE_IBTF_CALLB() initializes the "hs_ibtfpriv" field in the Mcxnex
 *    softstate.  When "hs_ibtfpriv" is non-NULL, it is OK to forward asynch
 *    and CQ events to the IBTF.
 *
 * MCXNEX_DO_IBTF_ASYNC_CALLB() and MCXNEX_DO_IBTF_CQ_CALLB() both set and clear
 *    the "hs_in_evcallb" flag, as necessary, to indicate that an IBTF
 *    callback is currently in progress.  This is necessary so that we can
 *    block on this condition in mcxnex_detach().
 *
 * MCXNEX_QUIESCE_IBTF_CALLB() is used in mcxnex_detach() to set the
 *    "hs_ibtfpriv" to NULL (thereby disabling any further IBTF callbacks)
 *    and to poll on the "hs_in_evcallb" flag.  When this flag is zero, all
 *    IBTF callbacks have quiesced and it is safe to continue with detach
 *    (i.e. continue detaching from IBTF).
 */
#define	MCXNEX_ENABLE_IBTF_CALLB(state, tmp_ibtfpriv)		\
	(state)->hs_ibtfpriv = (tmp_ibtfpriv);

#define	MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, event)			\
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS((state)->hs_in_evcallb))	\
	(state)->hs_in_evcallb = 1;					\
	mcxnex_priv_async_handler(state, (type), (event));		\
	(state)->hs_in_evcallb = 0;

#define	MCXNEX_DO_IBTF_CQ_CALLB(state, cq)			\
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS((state)->hs_in_evcallb))	\
	(state)->hs_in_evcallb = 1;					\
	mcxnex_priv_cq_handler(state, cq);			\
	(state)->hs_in_evcallb = 0;

#define	MCXNEX_QUIESCE_IBTF_CALLB(state)			\
{									\
	uint_t		count = 0;					\
									\
	state->hs_ibtfpriv = NULL;					\
	while (((state)->hs_in_evcallb != 0) &&				\
	    (count++ < MCXNEX_QUIESCE_IBTF_CALLB_POLL_MAX)) {		\
		drv_usecwait(MCXNEX_QUIESCE_IBTF_CALLB_POLL_DELAY);	\
	}								\
}


/*
 * Defines used by the MCXNEX_QUIESCE_IBTF_CALLB() macro to determine the
 * duration and number of times (at maximum) to poll while waiting for IBTF
 * callbacks to quiesce.
 */
#define	MCXNEX_QUIESCE_IBTF_CALLB_POLL_DELAY	1
#define	MCXNEX_QUIESCE_IBTF_CALLB_POLL_MAX	1000000

/*
 * Macros to retrieve PCI id's of the device
 */
#define	MCXNEX_DDI_PROP_GET(dip, property) \
	(ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, \
	    property, -1))

#define	MCXNEX_GET_VENDOR_ID(dip)	MCXNEX_DDI_PROP_GET(dip, "vendor-id")
#define	MCXNEX_GET_DEVICE_ID(dip)	MCXNEX_DDI_PROP_GET(dip, "device-id")
#define	MCXNEX_GET_REVISION_ID(dip)	MCXNEX_DDI_PROP_GET(dip, "revision-id")



/*
 * Define used to determine the device mode to which Mcxnex driver has been
 * attached.  MCXNEX_IS_MAINTENANCE_MODE() returns true when the device has
 * come up in the "maintenance mode".  In this mode, no InfiniBand interfaces
 * are enabled, but the device's firmware can be updated/flashed (and
 * test/debug interfaces should be useable).
 * MCXNEX_IS_HCA_MODE() returns true when the device has come up in the
 * normal HCA mode.  In this mode, all necessary InfiniBand interfaces are
 * enabled (and, if necessary, MCXNEX firmware can be updated/flashed).
 */
#define	MCXNEX_IS_MAINTENANCE_MODE(dip)			\
	((ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_MAINT) &&			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"vendor-id", -1) == PCI_VENID_MLX))

#define	MCXNEX_IS_HCA_MODE(dip)			\
	(((ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_SDR) ||			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_DDR) ||			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_DDRG2) ||			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_QDRG2) ||			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_CX2) ||			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_ENVIRT) ||			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"device-id", -1) == PCI_DEVID_MCXNEX_EN10G)) &&			\
	(ddi_prop_get_int(DDI_DEV_T_ANY, (dip), DDI_PROP_DONTPASS,	\
	"vendor-id", -1) == PCI_VENID_MLX))

#define	MCXNEX_MAINTENANCE_MODE		1
#define	MCXNEX_HCA_MODE			2

#define	MCXNEX_FW_VER_VLAN_STRIP_CAP	2079000
#define	MCXNEX_FW_VER_NUM(state)				\
	((uint64_t)((state)->hs_fw.fw_rev_major * 1000000) +	\
	(state)->hs_fw.fw_rev_minor * 10000 +			\
	(state)->hs_fw.fw_rev_subminor)

/*
 * Used to determine if the device is operational, or not in maintenance mode.
 * This means either the driver has attached successfully against an mcxnex
 * device in mcxnex compatibility mode, or against a mcxnex device in full HCA
 * mode.
 */
#define	MCXNEX_IS_OPERATIONAL(mode)				\
	(mode == MCXNEX_HCA_MODE)

/*
 * The following define is used (in mcxnex_umap_db_set_onclose_cb()) to
 * indicate that a cleanup callback is needed to undo initialization done
 * by the firmware flash burn code.
 */
#define	MCXNEX_ONCLOSE_FLASH_INPROGRESS		(1 << 0)

/*
 * The following enumerated type and structures are used during driver
 * initialization.  Note: The MCXNEX_DRV_CLEANUP_ALL type is used as a marker
 * for end of the cleanup steps.  No cleanup steps should be added after
 * MCXNEX_DRV_CLEANUP_ALL.  Any addition steps should be added before it.
 */
typedef enum {
	MCXNEX_DRV_CLEANUP_LEVEL0,
	MCXNEX_DRV_CLEANUP_LEVEL1,
	MCXNEX_DRV_CLEANUP_LEVEL2,
	MCXNEX_DRV_CLEANUP_LEVEL3,
	MCXNEX_DRV_CLEANUP_LEVEL4,
	MCXNEX_DRV_CLEANUP_LEVEL5,
	MCXNEX_DRV_CLEANUP_LEVEL6,
	MCXNEX_DRV_CLEANUP_LEVEL7,
	MCXNEX_DRV_CLEANUP_LEVEL8,
	MCXNEX_DRV_CLEANUP_LEVEL9,
	MCXNEX_DRV_CLEANUP_LEVEL10,
	MCXNEX_DRV_CLEANUP_LEVEL11,
	MCXNEX_DRV_CLEANUP_LEVEL12,
	MCXNEX_DRV_CLEANUP_LEVEL13,
	MCXNEX_DRV_CLEANUP_LEVEL14,
	MCXNEX_DRV_CLEANUP_LEVEL15,
	MCXNEX_DRV_CLEANUP_LEVEL16,
	MCXNEX_DRV_CLEANUP_LEVEL17,
	MCXNEX_DRV_CLEANUP_LEVEL18,
	MCXNEX_DRV_CLEANUP_LEVEL19,
	/* No more driver cleanup steps below this point! */
	MCXNEX_DRV_CLEANUP_ALL
} mcxnex_drv_cleanup_level_t;

/*
 * The mcxnex_dma_info_t structure is used to store information related to
 * the various ICM resources' DMA allocations.  The related ICM table and
 * virtual address are stored here.  The DMA and Access handles are stored
 * here.  Also, the allocation length and virtual (host) address.
 */
struct mcxnex_dma_info_s {
	ddi_dma_handle_t	dma_hdl;
	ddi_acc_handle_t	acc_hdl;
	uint64_t		icmaddr;	/* ICM virtual address */
	uint64_t		vaddr;  	/* host virtual address */
	uint_t			length;		/* length requested */
	uint_t			icm_refcnt;	/* refcnt */
};
_NOTE(SCHEME_PROTECTS_DATA("safe sharing",
    mcxnex_dma_info_s::icm_refcnt))


/*
 * The mcxnex_cmd_reg_t structure is used to hold the address of the each of
 * the most frequently accessed hardware registers.  Specifically, it holds
 * the HCA Command Registers (HCR, used to pass command and mailbox
 * information back and forth to Mcxnex firmware) and the lock used to guarantee
 * mutually exclusive access to the registers.
 * Related to this, is the "clr_int" register which is used to clear the
 * interrupt once all EQs have been serviced.
 * Finally, there is the software reset register which is used to reinitialize
 * the Mcxnex device and to put it into a known state at driver startup time.
 * Below we also have the offsets (into the CMD register space) for each of
 * the various registers.
 */
typedef struct mcxnex_cmd_reg_s {
	mcxnex_hw_hcr_t	*hcr;
	kmutex_t	hcr_lock;
	uint64_t	*clr_intr;
	uint64_t	*eq_arm;
	uint64_t	*eq_set_ci;
	uint32_t	*sw_reset;
	uint32_t	*sw_semaphore;
	uint32_t	*fw_err_buf;
} mcxnex_cmd_reg_t;
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_cmd_reg_t::hcr_lock,
    mcxnex_cmd_reg_t::hcr))

/* SOME TEMPORARY PRINTING THINGS */
#define	MCXNEX_PRINT_CI		(0x01 << 0)
#define	MCXNEX_PRINT_MEM	(0x01 << 1)
#define	MCXNEX_PRINT_CQ		(0x01 << 2)


#define	HD_PRINT(state, mask)	\
	if (state->hs_debug_lev & mask)

/* END PRINTING THINGS */

/*
 * Parent data.  Used by nexus and children for ddi_parent_data.
 */
typedef struct mcxnex_ppd {
	mcxnex_state_t	*cp_state;
	int		cp_port;
} mcxnex_ppd_t;

/*
 * The mcxnex_state_t structure is the HCA software state structure.  It
 * contains all the pointers and placeholder for everything that the HCA
 * driver needs to properly operate.  One of these structures exists for
 * every instance of the HCA driver.
 */

struct mcxnex_state_s {
	dev_info_t		*hs_dip;
	int			hs_instance;
int			hs_debug;	/* for debug, a way of tracing */
uint32_t		hs_debug_lev;	/* for controlling prints, a bit mask */
					/* see mcxnex.c for setting it */
	/* PCI device, vendor, and revision IDs */
	uint16_t		hs_vendor_id;
	uint16_t		hs_device_id;
	uint8_t			hs_revision_id;

struct mcxnex_hw_qpc_s		hs_debug_qpc;
struct mcxnex_hw_cqc_s		hs_debug_cqc;
struct mcxnex_hw_eqc_s		hs_debug_eqc;

	mcxnex_hw_sm_perfcntr_t	hs_debug_perf;


	/*
	 * DMA information for the InfiniHost Context Memory (ICM),
	 * ICM Auxiliary allocation and the firmware. Also, record
	 * of ICM and ICMA sizes, in bytes.
	 */
	/* JBDB -- store here hs_icm_table, with hs_icm_dma in */

	uint64_t		hs_icm_sz;
	mcxnex_icm_table_t	*hs_icm;
	uint64_t		hs_icma_sz;
	mcxnex_dma_info_t	hs_icma_dma;
	mcxnex_dma_info_t	hs_fw_dma;

	/* Mcxnex interrupt/MSI information */
	int			hs_intr_types_avail;
	uint_t			hs_intr_type_chosen;
	int			hs_intrmsi_count;
	int			hs_intrmsi_avail;
	int			hs_intrmsi_allocd;
	ddi_intr_handle_t	hs_intrmsi_hdl[MCXNEX_MSIX_MAX];
	uint_t			hs_intrmsi_pri;
	int			hs_intrmsi_cap;

	/* assign EQs to CQs in a round robin fashion */
	uint_t			hs_eq_dist;	/* increment when used */

	/* mcxnex HCA name and HCA part number */
	char			hs_hca_name[64];
	char			hs_hca_pn[64];
	int			hs_hca_pn_len;

	/* Mcxnex device operational mode */
	int			hs_operational_mode;

	/* Attach buffer saved per state to store detailed attach errors */
	char			hs_attach_buf[MCXNEX_ATTACH_MSGSIZE];

	/* Mcxnex NodeGUID, SystemImageGUID, and NodeDescription */
	uint64_t		hs_nodeguid;
	uint64_t		hs_sysimgguid;
	char			hs_nodedesc[64];

	/* Info passed to IBTF during registration */
	ibc_hca_info_t		hs_ibtfinfo;
	/* ibc_clnt_hdl_t		hs_ibtfpriv; */
	void			*hs_ibtfpriv;

	/*
	 * Mcxnex register mapping.  Holds the device access attributes,
	 * kernel mapped addresses, and DDI access handles for both
	 * Mcxnex's CMD and UAR BARs.
	 */
	ddi_device_acc_attr_t	hs_reg_accattr;
	caddr_t			hs_reg_cmd_baseaddr;	/* Mcxnex CMD BAR */
	ddi_acc_handle_t	hs_reg_cmdhdl;
	caddr_t			hs_reg_uar_baseaddr;	/* Mcxnex UAR BAR */
	ddi_acc_handle_t	hs_reg_uarhdl;
	caddr_t			hs_reg_msi_baseaddr;	/* Mcxnex MSIx BAR */
	ddi_acc_handle_t	hs_reg_msihdl;

	/*
	 * Some additional things for UAR Pages
	 */
	uint64_t		hs_kernel_uar_index;	/* kernel UAR index */
	uint64_t		hs_bf_offset;		/* offset from UAR */
							/* Bar to Blueflame */
	caddr_t			hs_reg_bf_baseaddr;	/* blueflame base */
	ddi_acc_handle_t	hs_reg_bfhdl;  		/* blueflame handle */


	/*
	 * Mcxnex PCI config space registers.  This array is used to
	 * save and restore the PCI config registers before and after a
	 * software reset.
	 */
	uint32_t		hs_cfg_data[MCXNEX_SW_RESET_NUMREGS];
	/* for reset per Linux driver */
	uint32_t		hs_pci_cap_offset;
	uint32_t		hs_pci_cap_devctl;
	uint32_t		hs_pci_cap_lnkctl;

	/*
	 * Mcxnex UAR page resources.  Holds the resource pointers for
	 * UAR page #0 (reserved) and for UAR page #1 (used for kernel
	 * driver doorbells).  In addition, we save a pointer to the
	 * UAR page #1 doorbells which will be used throughout the driver
	 * whenever it is necessary to ring one of them.  And, in case we
	 * are unable to do 64-bit writes to the page (because of system
	 * architecture), we include a lock (to ensure atomic 64-bit access).
	 */
	mcxnex_rsrc_t		*hs_uarpg0_rsrc_rsrvd;
	mcxnex_rsrc_t		*hs_uarkpg_rsrc;
	mcxnex_hw_uar_t		*hs_uar;
	kmutex_t		hs_uar_lock;

	/*
	 * Used during a call to open() if we are in maintenance mode, this
	 * field serves as a semi-unique rolling count index value, used only
	 * in the setup of umap_db entries.  This is primarily needed to
	 * firmware device access ioctl operations can still be guaranteed to
	 * close in the event of an unplanned process exit, even in maintenance
	 * mode.
	 */
	uint_t			hs_open_ar_indx;

	/*
	 * Mcxnex Port types
	 *
	 */
	uint_t			hs_port_type[MCXNEX_MAX_PORTS];
	mcxnex_ppd_t		hs_ppd[MCXNEX_MAX_PORTS];

	/*
	 * Mcxnex command registers.  This structure contains the addresses
	 * for each of the most frequently accessed CMD registers.  Since
	 * almost all accesses to the Mcxnex hardware are through the Mcxnex
	 * command interface (i.e. the HCR), we save away the pointer to
	 * the HCR, as well as pointers to the ECR and INT registers (as
	 * well as their corresponding "clear" registers) for interrupt
	 * processing.  And we also save away a pointer to the software
	 * reset register (see above).
	 */
	mcxnex_cmd_reg_t	hs_cmd_regs;
	uint32_t		hs_cmd_toggle;

	/*
	 * Mcxnex resource pointers.  The following are pointers to the
	 * kmem cache (from which the Mcxnex resource handles are allocated),
	 * and the array of "resource pools" (which store all the pertinent
	 * information necessary to manage each of the various types of
	 * resources that are used by the driver.  See mcxnex_rsrc.h for
	 * more detail.
	 */
	kmem_cache_t		*hs_rsrc_cache;
	mcxnex_rsrc_pool_info_t	*hs_rsrc_hdl;

	/*
	 * Mailbox lists.  These hold the information necessary to
	 * manage the pools of pre-allocated mailboxes (both "In" and
	 * "Out" type).  See mcxnex_cmd.h for more detail.
	 */
	mcxnex_mboxlist_t		hs_in_mblist;
	mcxnex_mboxlist_t		hs_out_mblist;

	/*
	 * Interrupt mailbox lists.  We allocate both an "In" mailbox
	 * and an "Out" type mailbox for the interrupt context.  This is in
	 * order to guarantee that a mailbox entry will always be available in
	 * the interrupt context, and we can NOSLEEP without having to worry
	 * about possible failure allocating the mbox.  We create this as an
	 * mboxlist so that we have the potential for having multiple mboxes
	 * available based on the number of interrupts we can receive at once.
	 */
	mcxnex_mboxlist_t		hs_in_intr_mblist;
	mcxnex_mboxlist_t		hs_out_intr_mblist;

	/*
	 * Mcxnex outstanding command list.  Used to hold all the information
	 * necessary to manage the Mcxnex "outstanding command list".  See
	 * mcxnex_cmd.h for more detail.
	 */
	mcxnex_cmdlist_t	hs_cmd_list;

	/*
	 * This structure contains the Mcxnex driver's "configuration profile".
	 * This is the collected set of configuration information, such as
	 * number of QPs, CQs, mailboxes and other resources, sizes of
	 * individual resources, other system level configuration information,
	 * etc.  See mcxnex_cfg.h for more detail.
	 */
	mcxnex_cfg_profile_t	*hs_cfg_profile;

	/*
	 * This flag contains the profile setting, selecting which profile the
	 * driver would use.  This is needed in the case where we have to
	 * fallback to a smaller profile based on some DDR conditions.  If we
	 * don't fallback, then it is set to the size of DDR in the system.
	 */
	uint32_t		hs_cfg_profile_setting;

	/*
	 * The following are a collection of resource handles used by the
	 * Mcxnex driver (internally).  First is the protection domain (PD)
	 * handle that is used when mapping all kernel memory (work queues,
	 * completion queues, etc).  Next is an array of EQ handles.  This
	 * array is indexed by EQ number and allows the Mcxnex driver to quickly
	 * convert an EQ number into the software structure associated with the
	 * given EQ.  Likewise, we have three arrays for CQ, QP and SRQ
	 * handles.  These arrays are also indexed by CQ, QP or SRQ number and
	 * allow the driver to quickly find the corresponding CQ, QP or SRQ
	 * software structure.  Note: while the EQ table is of fixed size
	 * (because there are a maximum of 64 EQs), each of the CQ, QP and SRQ
	 * handle lists must be allocated at driver startup.
	 */
	mcxnex_pdhdl_t		hs_pdhdl_internal;
	mcxnex_eqhdl_t		hs_eqhdl[MCXNEX_NUM_EQ];
	mcxnex_cqhdl_t		*hs_cqhdl;
	mcxnex_qphdl_t		*hs_qphdl;
	mcxnex_srqhdl_t		*hs_srqhdl;
	kmutex_t		hs_dbr_lock;	/* lock for dbr mgmt */

	/* linked list of kernel dbr resources */
	mcxnex_dbr_info_t	*hs_kern_dbr;

	/* linked list of non-kernel dbr resources */
	mcxnex_user_dbr_t	*hs_user_dbr;

	/*
	 * The AVL tree is used to store information regarding QP number
	 * allocations.  The lock protects access to the AVL tree.
	 */
	avl_tree_t		hs_qpn_avl;
	kmutex_t		hs_qpn_avl_lock;

	/*
	 * This field is used to indicate whether or not the Mcxnex driver is
	 * currently in an IBTF event callback elsewhere in the system.  Note:
	 * It is "volatile" because we intend to poll on this value - in
	 * mcxnex_detach() - until we are assured that no further IBTF callbacks
	 * are currently being processed.
	 */
	volatile uint32_t	hs_in_evcallb;

	/*
	 * The following structures are used to store the results of several
	 * device query commands passed to the Mcxnex hardware at startup.
	 * Specifically, we have hung onto the results of QUERY_DDR (which
	 * gives information about how much DDR memory is present and where
	 * it is located), QUERY_FW (which gives information about firmware
	 * version numbers and the location and extent of firmware's footprint
	 * in DDR, QUERY_DEVLIM (which gives the device limitations/resource
	 * maximums) and QUERY_PORT (where some of the specs from DEVLIM moved),
	 * QUERY_ADAPTER (which gives additional miscellaneous
	 * information), and INIT/QUERY_HCA (which serves the purpose of
	 * recording what configuration information was passed to the firmware
	 * when the HCA was initialized).
	 */
	struct mcxnex_hw_queryfw_s	hs_fw;
	struct mcxnex_hw_querydevlim_s	hs_devlim;
	struct mcxnex_hw_query_port_s	hs_queryport;
	struct mcxnex_hw_set_port_s 	*hs_initport;
	struct mcxnex_hw_queryadapter_s	hs_adapter;
	struct mcxnex_hw_initqueryhca_s	hs_hcaparams;

	/*
	 * The following are used for managing special QP resources.
	 * Specifically, we have a lock, a set of flags (in "hs_spec_qpflags")
	 * used to track the special QP resources, and two Mcxnex resource
	 * handle pointers.  Each resource handle actually corresponds to two
	 * consecutive QP contexts (one per port) for each special QP type.
	 */
	kmutex_t		hs_spec_qplock;
	uint_t			hs_spec_qpflags;
	mcxnex_rsrc_t		*hs_spec_qp0;
	mcxnex_rsrc_t		*hs_spec_qp1;
	/*
	 * For Mcxnex, you have to alloc 8 qp's total, but the last 4 are
	 * unused/reserved.  The following represents the handle for those
	 * last 4 qp's
	 */
	mcxnex_rsrc_t		*hs_spec_qp_unused;

	/*
	 * Multicast group lists.  These are used to track the "shadow" MCG
	 * lists that speed up the processing of attach and detach multicast
	 * group operations.  See mcxnex_misc.h for more details.  Note: we
	 * need the pointer to the "temporary" MCG entry here primarily
	 * because the size of a given MCG entry is configurable.  Therefore,
	 * it is impossible to put this variable on the stack.  And rather
	 * than allocate and deallocate the entry multiple times, we choose
	 * instead to preallocate it once and reuse it over and over again.
	 */
	kmutex_t		hs_mcglock;
	mcxnex_mcghdl_t		hs_mcghdl;
	mcxnex_hw_mcg_t		*hs_mcgtmp;

	/*
	 * Cache of the pkey table, sgid (guid-only) tables, and
	 * sgid (subnet) prefix.  These arrays are set
	 * during port_query, and mainly used for generating MLX GSI wqes.
	 */
	ib_pkey_t		*hs_pkey[MCXNEX_MAX_PORTS];
	ib_sn_prefix_t		hs_sn_prefix[MCXNEX_MAX_PORTS];
	ib_guid_t		*hs_guid[MCXNEX_MAX_PORTS];

	/*
	 * Used for tracking Mcxnex kstat information
	 */
	mcxnex_ks_info_t	*hs_ks_info;

	/*
	 * Used for Mcxnex info ioctl used by VTS
	 */
	kmutex_t		hs_info_lock;

	/*
	 * Used for Mcxnex FW flash burning.  They are used exclusively
	 * within the ioctl calls for use when accessing the mcxnex
	 * flash device.
	 */
	kmutex_t		hs_fw_flashlock;
	int			hs_fw_flashstarted;
	dev_t			hs_fw_flashdev;
	uint32_t		hs_fw_log_sector_sz;
	uint32_t		hs_fw_device_sz;
	uint32_t		hs_fw_flashbank;
	uint32_t		*hs_fw_sector;
	uint32_t		hs_fw_gpio[4];
	int			hs_fw_cmdset;

	/*
	 * Used for Mcxnex FM. They are basically used to manage
	 * the toggle switch to enable/disable Mcxnex FM.
	 * Please see the comment in mcxnex_fm.c.
	 */
	int			hs_fm_capabilities; /* FM capabilities */
	int			hs_fm_disable;	/* Mcxnex FM disable flag */
	int			hs_fm_state;	/* Mcxnex FM state */
	boolean_t		hs_fm_async_fatal; /* async internal error */
	uint32_t		hs_fm_async_errcnt; /* async error count */
	boolean_t		hs_fm_poll_suspend; /* poll thread suspend */
	kmutex_t		hs_fm_lock;	/* mutex for state */
	mcxnex_hca_fm_t		*hs_fm_hca_fm;	/* HCA FM pointer */
	ddi_acc_handle_t	hs_fm_cmdhdl;	/* fm-protected CMD hdl */
	ddi_acc_handle_t	hs_fm_uarhdl;	/* fm-protected UAR hdl */
	ddi_device_acc_attr_t	hs_fm_accattr;	/* fm-protected acc attr */
	ddi_periodic_t		hs_fm_poll_thread; /* fma poll thread */
	int32_t			hs_fm_degraded_reason;	/* degradation cause */
#ifdef FMA_TEST
	mod_hash_t		*hs_fm_test_hash; /* testset */
	mod_hash_t		*hs_fm_id_hash;	/* testid */
#endif
	/*
	 * Mcxnex fastreboot support. To sw-reset Mcxnex HCA, the driver
	 * needs to save/restore MSI-X tables and PBA. Those members are
	 * used for the purpose.
	 */
	/* Access handle for PCI config space */
	ddi_acc_handle_t	hs_reg_pcihdl;		/* PCI cfg handle */
	ddi_acc_handle_t	hs_fm_pcihdl;		/* 	fm handle */
	ushort_t		hs_caps_ptr;		/* MSI-X caps */
	ushort_t		hs_msix_ctrl;		/* MSI-X ctrl */

	/* members to handle MSI-X tables */
	ddi_acc_handle_t	hs_reg_msix_tblhdl;	/* MSI-X table handle */
	ddi_acc_handle_t	hs_fm_msix_tblhdl;	/* 	fm handle */
	char 			*hs_msix_tbl_addr;	/* MSI-X table addr */
	char 			*hs_msix_tbl_entries;	/* MSI-X table entry */
	size_t			hs_msix_tbl_size;	/* MSI-X table size */
	uint32_t		hs_msix_tbl_offset;	/* MSI-X table offset */
	uint32_t		hs_msix_tbl_rnumber;	/* MSI-X table reg# */

	/* members to handle MSI-X PBA */
	ddi_acc_handle_t	hs_reg_msix_pbahdl;	/* MSI-X PBA handle */
	ddi_acc_handle_t	hs_fm_msix_pbahdl;	/* 	fm handle */
	char 			*hs_msix_pba_addr;	/* MSI-X PBA addr */
	char 			*hs_msix_pba_entries;	/* MSI-X PBA entry */
	size_t			hs_msix_pba_size;	/* MSI-X PBA size */
	uint32_t		hs_msix_pba_offset;	/* MSI-X PBA offset */
	uint32_t		hs_msix_pba_rnumber;	/* MSI-X PBA reg# */

	mcxnex_priv_async_cb_t	async_cb;
	boolean_t		hs_quiescing;		/* in fastreboot */
	boolean_t		hs_vlan_strip_off_cap;
};
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_state_s::hs_fw_flashlock,
    mcxnex_state_s::hs_fw_flashstarted
    mcxnex_state_s::hs_fw_flashdev
    mcxnex_state_s::hs_fw_log_sector_sz
    mcxnex_state_s::hs_fw_device_sz))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_state_s::hs_spec_qplock,
    mcxnex_state_s::hs_spec_qpflags
    mcxnex_state_s::hs_spec_qp0
    mcxnex_state_s::hs_spec_qp1))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_state_s::hs_mcglock,
    mcxnex_state_s::hs_mcghdl
    mcxnex_state_s::hs_mcgtmp))
_NOTE(DATA_READABLE_WITHOUT_LOCK(mcxnex_state_s::hs_in_evcallb
    mcxnex_state_s::hs_fw_log_sector_sz
    mcxnex_state_s::hs_fw_device_sz
    mcxnex_state_s::hs_spec_qpflags
    mcxnex_state_s::hs_spec_qp0
    mcxnex_state_s::hs_spec_qp1))
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_state_s::hs_qpn_avl_lock,
    mcxnex_state_s::hs_qpn_avl))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing",
    mcxnex_state_s::hs_fm_async_fatal
    mcxnex_state_s::hs_fw_sector))

/*
 * MCXNEX_IN_FASTREBOOT() shows if Mcxnex driver is at fastreboot.
 * This macro should be used to check if the mutex lock can be used
 * since the lock cannot be used if the driver is in the quiesce mode.
 */
#define	MCXNEX_IN_FASTREBOOT(state)	(state->hs_quiescing == B_TRUE)

/*
 * Bit positions in the "hs_spec_qpflags" field above.  The flags are (from
 * least significant to most): (QP0,Port1), (QP0,Port2), (QP1,Port1), and
 * (QP1,Port2).  The masks are there to help with some specific allocation
 * and freeing operations
 */
#define	MCXNEX_SPECIAL_QP0_RSRC		0
#define	MCXNEX_SPECIAL_QP0_RSRC_MASK	0x3
#define	MCXNEX_SPECIAL_QP1_RSRC		2
#define	MCXNEX_SPECIAL_QP1_RSRC_MASK	0xC


/*
 * These flags specifies additional behaviors on database access.
 * MCXNEX_UMAP_DB_REMOVE, for example, specifies that (if found) the database
 * entry should be removed from the database.  MCXNEX_UMAP_DB_IGNORE_INSTANCE
 * specifies that a particular database query should ignore value in the
 * "tdb_instance" field as a criterion for the search.
 */
#define	MCXNEX_UMAP_DB_REMOVE		(1 << 0)
#define	MCXNEX_UMAP_DB_IGNORE_INSTANCE	(1 << 1)

/*
 * The mcxnex_umap_db_t structure contains what is referred to throughout the
 * driver code as the "userland resources database".  This structure contains
 * all the necessary information to track resources that have been prepared
 * for direct-from-userland access.  There is an AVL tree ("hdl_umapdb_avl")
 * which consists of the "mcxnex_umap_db_entry_t" (below) and a lock to ensure
 * atomic access when adding or removing entries from the database.
 */
typedef struct mcxnex_umap_db_s {
	kmutex_t		hdl_umapdb_lock;
	avl_tree_t		hdl_umapdb_avl;
} mcxnex_umap_db_t;

/*
 * The mcxnex_umap_db_priv_t structure currently contains information necessary
 * to provide the "on close" callback to the firmware flash interfaces.  It
 * is intended that this structure could be extended to enable other "on
 * close" callbacks as well.
 */
typedef struct mcxnex_umap_db_priv_s {
	int		(*hdp_cb)(void *);
	void		*hdp_arg;
} mcxnex_umap_db_priv_t;

/*
 * The mcxnex_umap_db_common_t structure contains fields which are common
 * between the database entries ("mcxnex_umap_db_entry_t") and the structure
 * used to contain the search criteria ("mcxnex_umap_db_query_t").  This
 * structure contains a key, a resource type (described above), an instance
 * (corresponding to the driver instance which inserted the database entry),
 * and a "value" field.  Typically, "hdb_value" is a pointer to a Mcxnex
 * resource object.  Although for memory regions, the value field corresponds
 * to the ddi_umem_cookie_t for the pinned userland memory.
 * The structure also includes a placeholder for private data ("hdb_priv").
 * Currently this data is being used for holding "on close" callback
 * information to allow certain kinds of cleanup even if a userland process
 * prematurely exits.
 */
typedef struct mcxnex_umap_db_common_s {
	uint64_t		hdb_key;
	uint64_t		hdb_value;
	uint_t			hdb_type;
	uint_t			hdb_instance;
	void			*hdb_priv;
} mcxnex_umap_db_common_t;

/*
 * The mcxnex_umap_db_entry_t structure is the entry in "userland resources
 * database".  As required by the AVL framework, each entry contains an
 * "avl_node_t".  Then, as required to implement the database, each entry
 * contains a "mcxnex_umap_db_common_t" structure used to contain all of the
 * relevant entries.
 */
typedef struct mcxnex_umap_db_entry_s {
	avl_node_t		hdbe_avlnode;
	mcxnex_umap_db_common_t	hdbe_common;
} mcxnex_umap_db_entry_t;

/*
 * The mcxnex_umap_db_query_t structure is used in queries to the "userland
 * resources database".  In addition to the "mcxnex_umap_db_common_t" structure
 * used to contain the various search criteria, this structure also contains
 * a flags field "hqdb_flags" which can be used to specify additional behaviors
 * (as described above).  Specifically, the flags field can be used to specify
 * that an entry should be removed from the database, if found, and to
 * specify whether the database lookup should consider "tdb_instance" in the
 * search.
 */
typedef struct mcxnex_umap_db_query_s {
	uint_t			hqdb_flags;
	mcxnex_umap_db_common_t	hqdb_common;
} mcxnex_umap_db_query_t;
_NOTE(MUTEX_PROTECTS_DATA(mcxnex_umap_db_s::hdl_umapdb_lock,
    mcxnex_umap_db_entry_s::hdbe_avlnode
    mcxnex_umap_db_entry_s::hdbe_common.hdb_key
    mcxnex_umap_db_entry_s::hdbe_common.hdb_value
    mcxnex_umap_db_entry_s::hdbe_common.hdb_type
    mcxnex_umap_db_entry_s::hdbe_common.hdb_instance))

/*
 * The mcxnex_devmap_track_t structure contains all the necessary information
 * to track resources that have been mapped through devmap.  There is a
 * back-pointer to the Mcxnex softstate, the logical offset corresponding with
 * the mapped resource, the size of the mapped resource (zero indicates an
 * "invalid mapping"), and a reference count and lock used to determine when
 * to free the structure (specifically, this is necessary to handle partial
 * unmappings).
 */
typedef struct mcxnex_devmap_track_s {
	mcxnex_state_t	*hdt_state;
	uint64_t	hdt_offset;
	uint_t		hdt_size;
	int		hdt_refcnt;
	kmutex_t	hdt_lock;
} mcxnex_devmap_track_t;

#define	MCXNEX_ICM_SPLIT	64
#define	MCXNEX_ICM_SPAN		4096

#define	mcxnex_bitmap(bitmap, dma_info, icm_table, split_index)	\
	bitmap = (icm_table)->icm_bitmap[split_index];		\
	if (bitmap == NULL) {					\
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*(icm_table))) \
		int num_spans = (icm_table)->num_spans;		\
		bitmap =					\
		(icm_table)->icm_bitmap[split_index] =		\
		    kmem_zalloc((num_spans + 7) / 8, KM_SLEEP);	\
		ASSERT((icm_table)->icm_dma[split_index] == NULL); \
		(icm_table)->icm_dma[split_index] =		\
		    kmem_zalloc(num_spans * sizeof (mcxnex_dma_info_t), \
		    KM_SLEEP);					\
	}							\
	dma_info = (icm_table)->icm_dma[split_index]

/*
 * The mcxnex_icm_table_t encodes data pertaining to a given ICM table, and
 * holds an array of mcxnex_dma_info_t's related to its backing memory. Each
 * ICM table is sized during initialization, but real memory is allocated
 * and mapped into and out of ICM in the device throughout the life of the
 * instance. We use a bitmap to determine whether or not a given ICM object
 * has memory backing it or not, and an array of mcxnex_dma_info_t's to house
 * the actual allocations. Memory is allocated in chunks of span_size, stored
 * in the icm_dma array, and can later be looked up by using the bitmap index.
 * The total number of ICM spans is equal to table_size / span_size. We also
 * keep track of the ICM characteristics, such as ICM object size and the
 * number of entries in the ICM area.
 */
struct mcxnex_icm_table_s {
	kmutex_t		icm_table_lock;
	kcondvar_t		icm_table_cv;
	uint8_t			icm_busy;
	mcxnex_rsrc_type_t	icm_type;
	uint64_t		icm_baseaddr;
	uint64_t		table_size;
	uint64_t		num_entries;	/* maximum #entries */
	uint32_t		object_size;
	uint32_t		span;		/* #rsrc's per span */
	uint32_t		num_spans;	/* #dmainfos in icm_dma */
	uint32_t		split_shift;
	uint32_t		span_mask;
	uint32_t		span_shift;
	uint32_t		rsrc_mask;
	uint16_t		log_num_entries;
	uint16_t		log_object_size;
	/* two arrays of pointers, each pointer points to arrays */
	uint8_t			*icm_bitmap[MCXNEX_ICM_SPLIT];
	mcxnex_dma_info_t	*icm_dma[MCXNEX_ICM_SPLIT];
};

/*
 * Split the rsrc index into three pieces:
 *
 *      index1 - icm_bitmap[MCXNEX_ICM_SPLIT], icm_dma[MCXNEX_ICM_SPLIT]
 *      index2 - bitmap[], dma[]
 *      offset - rsrc within the icm mapping
 */
#define	mcxnex_index(index1, index2, rindx, table, offset)		\
	index1 = (rindx) >> table->split_shift;				\
	index2 = ((rindx) & table->span_mask) >> table->span_shift;	\
	offset = (rindx) & table->rsrc_mask

/* Defined in mcxnex.c */
int mcxnex_dma_alloc(mcxnex_state_t *state, mcxnex_dma_info_t *dma_info,
    uint16_t opcode);
void mcxnex_dma_attr_init(mcxnex_state_t *state, ddi_dma_attr_t *dma_attr);
void mcxnex_dma_free(mcxnex_dma_info_t *info);
int mcxnex_icm_alloc(mcxnex_state_t *state, mcxnex_rsrc_type_t type,
    uint32_t icm_index1, uint32_t icm_index2);
void mcxnex_icm_free(mcxnex_state_t *state, mcxnex_rsrc_type_t type,
    uint32_t icm_index1, uint32_t icm_index2);
void mcxnex_printf(mcxnex_state_t *, int, const char *, ...);

/* Defined in mcxnex_umap.c */
int mcxnex_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
    size_t *maplen, uint_t model);
ibt_status_t mcxnex_umap_ci_data_in(mcxnex_state_t *state,
    ibt_ci_data_flags_t flags, ibt_object_type_t object, void *hdl,
    void *data_p, size_t data_sz);
ibt_status_t mcxnex_umap_ci_data_out(mcxnex_state_t *state,
    ibt_ci_data_flags_t flags, ibt_object_type_t object, void *hdl,
    void *data_p, size_t data_sz);
void mcxnex_umap_db_init(void);
void mcxnex_umap_db_fini(void);
mcxnex_umap_db_entry_t *mcxnex_umap_db_alloc(uint_t instance, uint64_t key,
    uint_t type, uint64_t value);
void mcxnex_umap_db_free(mcxnex_umap_db_entry_t *umapdb);
void mcxnex_umap_db_add(mcxnex_umap_db_entry_t *umapdb);
void mcxnex_umap_db_add_nolock(mcxnex_umap_db_entry_t *umapdb);
int mcxnex_umap_db_find(uint_t instance, uint64_t key, uint_t type,
    uint64_t *value, uint_t flags, mcxnex_umap_db_entry_t **umapdb);
int mcxnex_umap_db_find_nolock(uint_t instance, uint64_t key, uint_t type,
    uint64_t *value, uint_t flags, mcxnex_umap_db_entry_t **umapdb);
void mcxnex_umap_umemlock_cb(ddi_umem_cookie_t *umem_cookie);
int mcxnex_umap_db_set_onclose_cb(dev_t dev, uint64_t flag,
    int (*callback)(void *), void *arg);
int mcxnex_umap_db_clear_onclose_cb(dev_t dev, uint64_t flag);
int mcxnex_umap_db_handle_onclose_cb(mcxnex_umap_db_priv_t *priv);
int mcxnex_rsrc_hw_entries_init(mcxnex_state_t *state,
    mcxnex_rsrc_hw_entry_info_t *info);
void mcxnex_rsrc_hw_entries_fini(mcxnex_state_t *state,
    mcxnex_rsrc_hw_entry_info_t *info);

#endif	/* _MCXNEX_H */
