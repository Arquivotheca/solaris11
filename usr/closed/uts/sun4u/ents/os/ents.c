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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/sunndi.h>

#include <sys/platform_module.h>
#include <sys/errno.h>
#include <sys/cpu_sgnblk_defs.h>
#include <sys/rmc_comm_dp.h>
#include <sys/rmc_comm_drvintf.h>
#include <sys/utsname.h>
#include <sys/modctl.h>
#include <sys/plat_ecc_unum.h>
#include <sys/lgrp.h>
#include <sys/memnode.h>
#include <sys/promif.h>

#define	SHARED_PCF8584_PATH "/pci@1e,600000/isa@7/i2c@0,320"
static dev_info_t *shared_pcf8584_dip;
static kmutex_t ents_pcf8584_mutex;

int (*p2get_mem_unum)(int, uint64_t, char *, int, int *);

static void cpu_sgn_update(ushort_t sgn, uchar_t state,
    uchar_t sub_state, int cpuid);

void
startup_platform(void)
{
	mutex_init(&ents_pcf8584_mutex, NULL, NULL, NULL);
}

int
set_platform_tsb_spares(void)
{
	return (0);
}

void
set_platform_defaults(void)
{
	extern char *tod_module_name;
	extern int watchdog_enable, watchdog_available;
	extern int disable_watchdog_on_exit;

	/*
	 * Disable an active h/w watchdog timer upon exit to OBP.
	 */
	disable_watchdog_on_exit = 1;

	watchdog_enable = 1;
	watchdog_available = 1;
	cpu_sgn_func = cpu_sgn_update;
	tod_module_name = "todm5819p_rmc";
}

/*
 * Definitions for accessing the pci config space of the isa node
 * of Southbridge.
 */
#define	ENCHILADA_ISA_PATHNAME	"/pci@1e,600000/isa@7"
static ddi_acc_handle_t isa_handle;		/* handle for isa pci space */

/*
 * Definitions for accessing rmclomv
 */
#define	RMCLOMV_PATHNAME	"/pseudo/rmclomv@0"

void
load_platform_drivers(void)
{
	dev_info_t 		*dip;		/* dip of the isa driver */
	dev_info_t		*rmclomv_dip;

	/*
	 * Install ISA driver. This is required for the southbridge IDE
	 * workaround - to reset the IDE channel during IDE bus reset.
	 * Panic the system in case ISA driver could not be loaded or
	 * any problem in accessing its pci config space. Since the register
	 * to reset the channel for IDE is in ISA config space!.
	 */

	dip = e_ddi_hold_devi_by_path(ENCHILADA_ISA_PATHNAME, 0);
	if (dip == NULL) {
		cmn_err(CE_PANIC, "Could not install the isa driver\n");
		return;
	}

	if (pci_config_setup(dip, &isa_handle) != DDI_SUCCESS) {
		cmn_err(CE_PANIC, "Could not get the config space of isa\n");
		return;
	}

	/*
	 * mc-us3i must stay loaded for plat_get_mem_unum()
	 */
	if (i_ddi_attach_hw_nodes("mc-us3i") != DDI_SUCCESS)
		cmn_err(CE_WARN, "mc-us3i driver failed to install");
	(void) ddi_hold_driver(ddi_name_to_major("mc-us3i"));

	/*
	 * load the power button driver
	 */
	if (i_ddi_attach_hw_nodes("power") != DDI_SUCCESS)
		cmn_err(CE_WARN, "power button driver failed to install");
	(void) ddi_hold_driver(ddi_name_to_major("power"));

	/*
	 * Load the environmentals driver (rmclomv)
	 *
	 * We need this driver to handle events from the RMC when state
	 * changes occur in the environmental data.
	 */
	if (i_ddi_attach_hw_nodes("rmc_comm") != DDI_SUCCESS)
		cmn_err(CE_WARN, "rmc_comm failed to install");
	(void) ddi_hold_driver(ddi_name_to_major("rmc_comm"));

	if (i_ddi_attach_hw_nodes("pmugpio") != DDI_SUCCESS)
		cmn_err(CE_WARN, "pmugpio failed to install");
	(void) ddi_hold_driver(ddi_name_to_major("pmugpio"));

	rmclomv_dip = e_ddi_hold_devi_by_path(RMCLOMV_PATHNAME, 0);
	if (rmclomv_dip == NULL) {
		cmn_err(CE_WARN, "Could not install rmclomv driver\n");
	}

	/*
	 * Figure out which pcf8584 dip is shared with OBP for the nvram
	 * device, so the lock can be acquired.
	 */
	shared_pcf8584_dip = e_ddi_hold_devi_by_path(SHARED_PCF8584_PATH, 0);
}

/*
 * This routine provides a workaround for a bug in the SB chip which
 * can cause data corruption. Will be invoked from the IDE HBA driver for
 * Acer SouthBridge at the time of IDE bus reset.
 */
/*ARGSUSED*/
int
plat_ide_chipreset(dev_info_t *dip, int chno)
{
	uint8_t	val;
	int	ret = DDI_SUCCESS;

	if (isa_handle == NULL) {
		return (DDI_FAILURE);
	}

	val = pci_config_get8(isa_handle, 0x58);
	/*
	 * The dip passed as the argument is not used here.
	 * This will be needed for platforms which have multiple on-board SB,
	 * The dip passed will be used to match the corresponding ISA node.
	 */
	switch (chno) {
	case 0:
		/*
		 * First disable the primary channel then re-enable it.
		 * As per ALI no wait should be required in between have
		 * given 1ms delay in between to be on safer side.
		 * bit 2 of register 0x58 when 0 disable the channel 0.
		 * bit 2 of register 0x58 when 1 enables the channel 0.
		 */
		pci_config_put8(isa_handle, 0x58, val & 0xFB);
		drv_usecwait(1000);
		pci_config_put8(isa_handle, 0x58, val);
		break;
	case 1:
		/*
		 * bit 3 of register 0x58 when 0 disable the channel 1.
		 * bit 3 of register 0x58 when 1 enables the channel 1.
		 */
		pci_config_put8(isa_handle, 0x58, val & 0xF7);
		drv_usecwait(1000);
		pci_config_put8(isa_handle, 0x58, val);
		break;
	default:
		/*
		 * Unknown channel number passed. Return failure.
		 */
		ret = DDI_FAILURE;
	}

	return (ret);
}

/*ARGSUSED*/
int
plat_cpu_poweron(struct cpu *cp)
{
	return (ENOTSUP);	/* not supported on this platform */
}

/*ARGSUSED*/
int
plat_cpu_poweroff(struct cpu *cp)
{
	return (ENOTSUP);	/* not supported on this platform */
}

/*ARGSUSED*/
void
plat_freelist_process(int mnode)
{
}

char *platform_module_list[] = {
	(char *)0
};

/*ARGSUSED*/
void
plat_tod_fault(enum tod_fault_type tod_bad)
{
}

/*ARGSUSED*/
int
plat_get_mem_unum(int synd_code, uint64_t flt_addr, int flt_bus_id,
    int flt_in_memory, ushort_t flt_status, char *buf, int buflen, int *lenp)
{
	if (flt_in_memory && (p2get_mem_unum != NULL))
		return (p2get_mem_unum(synd_code, P2ALIGN(flt_addr, 8),
		    buf, buflen, lenp));
	else
		return (ENOTSUP);
}

/*
 * This platform hook gets called from mc_add_mem_unum_label() in the mc-us3i
 * driver giving each platform the opportunity to add platform
 * specific label information to the unum for ECC error logging purposes.
 */
/*ARGSUSED*/
void
plat_add_mem_unum_label(char *unum, int mcid, int bank, int dimm)
{
	char old_unum[UNUM_NAMLEN];

	(void) snprintf(old_unum, UNUM_NAMLEN, "%s", unum);
	if (dimm == -1)
		(void) snprintf(unum, UNUM_NAMLEN, "MB/DIMM%d MB/DIMM%d: %s",
		    (mcid * 2 + bank) * 2, (mcid * 2 + bank) * 2 + 1, old_unum);
	else
		(void) snprintf(unum, UNUM_NAMLEN, "MB/DIMM%d: %s",
		    (mcid * 2 + bank) * 2 + dimm, old_unum);
}

/*ARGSUSED*/
int
plat_get_cpu_unum(int cpuid, char *buf, int buflen, int *lenp)
{
	if (snprintf(buf, buflen, "MB") >= buflen) {
		return (ENOSPC);
	} else {
		*lenp = strlen(buf);
		return (0);
	}
}

/*
 * Our nodename has been set, pass it along to the RMC.
 */
void
plat_nodename_set(void)
{
	rmc_comm_msg_t	req;	/* request */
	int (*rmc_req_res)(rmc_comm_msg_t *, rmc_comm_msg_t *, time_t) = NULL;

	/*
	 * find the symbol for the mailbox routine
	 */
	rmc_req_res = (int (*)(rmc_comm_msg_t *, rmc_comm_msg_t *, time_t))
	    modgetsymvalue("rmc_comm_request_response", 0);

	if (rmc_req_res == NULL) {
		return;
	}

	/*
	 * construct the message telling the RMC our nodename
	 */
	req.msg_type = DP_SET_CPU_NODENAME;
	req.msg_len = strlen(utsname.nodename) + 1;
	req.msg_bytes = 0;
	req.msg_buf = (caddr_t)utsname.nodename;

	/*
	 * ship it
	 */
	(void) (rmc_req_res)(&req, NULL, 2000);
}

sig_state_t current_sgn;

/*
 * cpu signatures - we're only interested in the overall system
 * "signature" on this platform - not individual cpu signatures
 */
/*ARGSUSED*/
static void
cpu_sgn_update(ushort_t sig, uchar_t state, uchar_t sub_state, int cpuid)
{
	dp_cpu_signature_t signature;
	rmc_comm_msg_t	req;	/* request */
	int (*rmc_req_now)(rmc_comm_msg_t *, uint8_t) = NULL;

	/*
	 * Differentiate a panic reboot from a non-panic reboot in the
	 * setting of the substate of the signature.
	 *
	 * If the new substate is REBOOT and we're rebooting due to a panic,
	 * then set the new substate to a special value indicating a panic
	 * reboot, SIGSUBST_PANIC_REBOOT.
	 *
	 * A panic reboot is detected by a current (previous) signature
	 * state of SIGST_EXIT, and a new signature substate of SIGSUBST_REBOOT.
	 * The domain signature state SIGST_EXIT is used as the panic flow
	 * progresses.
	 *
	 * At the end of the panic flow, the reboot occurs but we should know
	 * one that was involuntary, something that may be quite useful to know
	 * at OBP level.
	 */
	if (state == SIGST_EXIT && sub_state == SIGSUBST_REBOOT) {
		if (current_sgn.state_t.state == SIGST_EXIT &&
		    current_sgn.state_t.sub_state != SIGSUBST_REBOOT)
			sub_state = SIGSUBST_PANIC_REBOOT;
	}

	/*
	 * offline and detached states only apply to a specific cpu
	 * so ignore them.
	 */
	if (state == SIGST_OFFLINE || state == SIGST_DETACHED) {
		return;
	}

	current_sgn.signature = CPU_SIG_BLD(sig, state, sub_state);

	/*
	 * find the symbol for the mailbox routine
	 */
	rmc_req_now = (int (*)(rmc_comm_msg_t *, uint8_t))
	    modgetsymvalue("rmc_comm_request_nowait", 0);
	if (rmc_req_now == NULL) {
		return;
	}

	signature.cpu_id = -1;
	signature.sig = sig;
	signature.states = state;
	signature.sub_state = sub_state;
	req.msg_type = DP_SET_CPU_SIGNATURE;
	req.msg_len = (int)(sizeof (signature));
	req.msg_bytes = 0;
	req.msg_buf = (caddr_t)&signature;

	/*
	 * We need to tell the SP that the host is about to stop running.  The
	 * SP will then allow the date to be set at its console, it will change
	 * state of the activity indicator, it will display the correct host
	 * status, and it will stop sending console messages and alerts to the
	 * host communication channel.
	 *
	 * This requires the RMC_COMM_DREQ_URGENT as we want to
	 * be sure activity indicators will reflect the correct status.
	 *
	 * When sub_state SIGSUBST_DUMP is sent, the urgent flag
	 * (RMC_COMM_DREQ_URGENT) is not required as SIGSUBST_PANIC_REBOOT
	 * has already been sent and changed activity indicators.
	 */
	if (state == SIGST_EXIT && (sub_state == SIGSUBST_HALT ||
	    sub_state == SIGSUBST_REBOOT || sub_state == SIGSUBST_ENVIRON ||
	    sub_state == SIGSUBST_PANIC_REBOOT))
		(void) (rmc_req_now)(&req, RMC_COMM_DREQ_URGENT);
	else
		(void) (rmc_req_now)(&req, 0);
}

/*
 * Enchilada Tower's BBC pcf8584 controller is used by both OBP and the OS's i2c
 * drivers.  The 'eeprom' command executes OBP code to handle property requests.
 * If eeprom didn't do this, or if the controllers were partitioned so that all
 * devices on a given controller were driven by either OBP or the OS, this
 * wouldn't be necessary.
 *
 * Note that getprop doesn't have the same issue as it reads from cached
 * memory in OBP.
 */

/*
 * Common locking enter code
 */
void
plat_setprop_enter(void)
{
	mutex_enter(&ents_pcf8584_mutex);
}

/*
 * Common locking exit code
 */
void
plat_setprop_exit(void)
{
	mutex_exit(&ents_pcf8584_mutex);
}

/*
 * Called by pcf8584 driver
 */
void
plat_shared_i2c_enter(dev_info_t *i2cnexus_dip)
{
	if (i2cnexus_dip == shared_pcf8584_dip) {
		plat_setprop_enter();
	}
}

/*
 * Called by pcf8584 driver
 */
void
plat_shared_i2c_exit(dev_info_t *i2cnexus_dip)
{
	if (i2cnexus_dip == shared_pcf8584_dip) {
		plat_setprop_exit();
	}
}

/*
 * Fiesta support for lgroups.
 *
 * On fiesta platform, an lgroup platform handle == CPU id
 */

/*
 * Macro for extracting the CPU number from the CPU id
 */
#define	CPUID_TO_LGRP(id)	((id) & 0x7)
#define	ENCHILADA_MC_SHIFT	36

/*
 * Return the platform handle for the lgroup containing the given CPU
 */
lgrp_handle_t
plat_lgrp_cpu_to_hand(processorid_t id)
{
	return (CPUID_TO_LGRP(id));
}

/*
 * Platform specific lgroup initialization
 */
void
plat_lgrp_init(void)
{
	pnode_t		curnode;
	char		tmp_name[MAXSYSNAME];
	int		portid;
	int		cpucnt = 0;
	int		max_portid = -1;
	extern uint32_t lgrp_expand_proc_thresh;
	extern uint32_t lgrp_expand_proc_diff;
	extern pgcnt_t	lgrp_mem_free_thresh;
	extern uint32_t lgrp_loadavg_tolerance;
	extern uint32_t lgrp_loadavg_max_effect;
	extern uint32_t lgrp_load_thresh;
	extern lgrp_mem_policy_t  lgrp_mem_policy_root;

	/*
	 * Count the number of CPUs installed to determine if
	 * NUMA optimization should be enabled or not.
	 *
	 * All CPU nodes reside in the root node and have a
	 * device type "cpu".
	 */
	curnode = prom_rootnode();
	for (curnode = prom_childnode(curnode); curnode;
	    curnode = prom_nextnode(curnode)) {
		bzero(tmp_name, MAXSYSNAME);
		if (prom_getprop(curnode, OBP_NAME, (caddr_t)tmp_name) == -1 ||
		    prom_getprop(curnode, OBP_DEVICETYPE, tmp_name) == -1 ||
		    strcmp(tmp_name, "cpu") != 0)
			continue;

		cpucnt++;
		if (prom_getprop(curnode, "portid", (caddr_t)&portid) != -1 &&
		    portid > max_portid)
			max_portid = portid;
	}
	if (cpucnt <= 1)
		max_mem_nodes = 1;
	else if (max_portid >= 0 && max_portid < MAX_MEM_NODES)
		max_mem_nodes = max_portid + 1;

	/*
	 * Set tuneables for fiesta architecture
	 *
	 * lgrp_expand_proc_thresh is the minimum load on the lgroups
	 * this process is currently running on before considering
	 * expanding threads to another lgroup.
	 *
	 * lgrp_expand_proc_diff determines how much less the remote lgroup
	 * must be loaded before expanding to it.
	 *
	 * Optimize for memory bandwidth by spreading multi-threaded
	 * program to different lgroups.
	 */
	lgrp_expand_proc_thresh = lgrp_loadavg_max_effect - 1;
	lgrp_expand_proc_diff = lgrp_loadavg_max_effect / 2;
	lgrp_loadavg_tolerance = lgrp_loadavg_max_effect / 2;
	lgrp_mem_free_thresh = 1;	/* home lgrp must have some memory */
	lgrp_expand_proc_thresh = lgrp_loadavg_max_effect - 1;
	lgrp_mem_policy_root = LGRP_MEM_POLICY_NEXT;
	lgrp_load_thresh = 0;

	mem_node_pfn_shift = ENCHILADA_MC_SHIFT - MMU_PAGESHIFT;
}

/*
 * Return latency between "from" and "to" lgroups
 *
 * This latency number can only be used for relative comparison
 * between lgroups on the running system, cannot be used across platforms,
 * and may not reflect the actual latency.  It is platform and implementation
 * specific, so platform gets to decide its value.  It would be nice if the
 * number was at least proportional to make comparisons more meaningful though.
 * NOTE: The numbers below are supposed to be load latencies for uncached
 * memory divided by 10.
 */
int
plat_lgrp_latency(lgrp_handle_t from, lgrp_handle_t to)
{
	/*
	 * Return remote latency when there are more than two lgroups
	 * (root and child) and getting latency between two different
	 * lgroups or root is involved
	 */
	if (lgrp_optimizations() && (from != to ||
	    from == LGRP_DEFAULT_HANDLE || to == LGRP_DEFAULT_HANDLE))
		return (17);
	else
		return (12);
}

int
plat_pfn_to_mem_node(pfn_t pfn)
{
	ASSERT(max_mem_nodes > 1);
	return (pfn >> mem_node_pfn_shift);
}

/*
 * Assign memnode to lgroups
 */
void
plat_fill_mc(pnode_t nodeid)
{
	int		portid;

	/*
	 * Enchilada memory controller portid == global CPU id
	 */
	if ((prom_getprop(nodeid, "portid", (caddr_t)&portid) == -1) ||
	    (portid < 0))
		return;

	if (portid < max_mem_nodes)
		plat_assign_lgrphand_to_mem_node(portid, portid);
}