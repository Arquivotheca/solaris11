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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * sol_uverbs.c
 *
 * Solaris OFED User Verbs kernel agent module
 *
 */
#include <sys/devops.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/semaphore.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/priv_const.h>
#include <sys/ib/clients/of/ofa_solaris.h>

#include <sys/ib/ibtl/ibvti.h>
#include <sys/ib/clients/of/sol_ofs/sol_ofs_common.h>
#include <sys/ib/clients/of/ofed_kernel.h>
#include <sys/ib/clients/of/sol_uverbs/sol_uverbs.h>
#include <sys/ib/clients/of/sol_ofs/sol_kverb_impl.h>
#include <sys/ib/clients/of/sol_uverbs/sol_uverbs_event.h>
#include <sys/ib/clients/of/sol_uverbs/sol_uverbs_comp.h>
#include <sys/ib/clients/of/sol_uverbs/sol_uverbs_qp.h>
#include <sys/ib/clients/of/sol_uverbs/sol_uverbs_ioctl.h>

static void *statep;
static ibt_clnt_hdl_t	sol_uverbs_ib_clntp = NULL;
uverbs_module_context_t *sol_uverbs_mod_ctxt = NULL;
static dev_info_t 	*sol_uverbs_dip = NULL;

char	*sol_uverbs_dbg_str = "sol_uverbs";

/*
 * Globals for managing the list of HCA's and the registered clients.
 */
sol_uverbs_hca_t *sol_uverbs_hcas = NULL;

static int sol_uverbs_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int sol_uverbs_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int sol_uverbs_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **resultp);
static int sol_uverbs_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
    int flags, char *name, caddr_t valuep, int *lengthp);
static int sol_uverbs_open(dev_t *devp, int flag, int otyp, cred_t *cred);
static int sol_uverbs_close(dev_t dev, int flag, int otyp, cred_t *cred);
static int sol_uverbs_poll(dev_t, short, int, short *, struct pollhead **);
static int sol_uverbs_read(dev_t dev, struct uio *uiop, cred_t *credp);
static int sol_uverbs_mmap(dev_t dev, off_t sol_uverbs_mmap, int prot);
static int sol_uverbs_write(dev_t dev, struct uio *uiop, cred_t *credp);
static int sol_uverbs_ioctl(dev_t dev, int cmd, intptr_t arg, int mode,
    cred_t *credp, int *rvalp);

static struct cb_ops sol_uverbs_cb_ops = {
	.cb_open	= sol_uverbs_open,
	.cb_close	= sol_uverbs_close,
	.cb_strategy	= nodev,
	.cb_print	= nodev,
	.cb_dump	= nodev,
	.cb_read	= sol_uverbs_read,
	.cb_write	= sol_uverbs_write,
	.cb_ioctl	= sol_uverbs_ioctl,
	.cb_devmap	= nodev,
	.cb_mmap	= sol_uverbs_mmap,
	.cb_segmap	= nodev,
	.cb_chpoll	= sol_uverbs_poll,
	.cb_prop_op	= sol_uverbs_prop_op,
	.cb_str		= NULL,
	.cb_flag	= D_NEW | D_MP,
	.cb_rev		= CB_REV,
	.cb_aread	= nodev,
	.cb_awrite	= nodev
};

static struct dev_ops sol_uverbs_dev_ops = {
	.devo_rev	= DEVO_REV,
	.devo_refcnt	= 0,
	.devo_getinfo	= sol_uverbs_getinfo,
	.devo_identify	= nulldev,
	.devo_probe	= nulldev,
	.devo_attach	= sol_uverbs_attach,
	.devo_detach	= sol_uverbs_detach,
	.devo_reset	= nodev,
	.devo_cb_ops	= &sol_uverbs_cb_ops,
	.devo_bus_ops	= NULL,
	.devo_power	= nodev,
	.devo_quiesce	= ddi_quiesce_not_needed
};

static struct modldrv modldrv = {
	.drv_modops	= &mod_driverops,
	.drv_linkinfo	= "Solaris User Verbs driver",
	.drv_dev_ops	= &sol_uverbs_dev_ops
};

static struct modlinkage modlinkage = {
	.ml_rev			= MODREV_1,
	.ml_linkage = {
		[0]		= &modldrv,
		[1]		= NULL,
	}
};

/*
 * User Object Tables for management of user resources. The tables are driver
 * wide, but each user context maintains a list of the objects it has created
 * that is used in cleanup.
 */
sol_ofs_uobj_table_t uverbs_uctxt_uo_tbl;
sol_ofs_uobj_table_t uverbs_upd_uo_tbl;
sol_ofs_uobj_table_t uverbs_uah_uo_tbl;
sol_ofs_uobj_table_t uverbs_umr_uo_tbl;
sol_ofs_uobj_table_t uverbs_ucq_uo_tbl;
sol_ofs_uobj_table_t uverbs_usrq_uo_tbl;
sol_ofs_uobj_table_t uverbs_uqp_uo_tbl;
sol_ofs_uobj_table_t uverbs_ufile_uo_tbl;

static void sol_uverbs_user_objects_init(void);
static void sol_uverbs_user_objects_fini(void);

/*
 * Open Fabric User Verbs API, command table. See ib_user_verbs.h for
 * definitions.
 */
static int (*uverbs_cmd_table[])(uverbs_uctxt_uobj_t *uctxt, char *buf,
	int in_len, int out_len) = {

	[IB_USER_VERBS_CMD_GET_CONTEXT]   	= sol_uverbs_get_context,
	[IB_USER_VERBS_CMD_QUERY_DEVICE]  	= sol_uverbs_query_device,
	[IB_USER_VERBS_CMD_QUERY_PORT]    	= sol_uverbs_query_port,
	[IB_USER_VERBS_CMD_ALLOC_PD]		= sol_uverbs_alloc_pd,
	[IB_USER_VERBS_CMD_DEALLOC_PD]		= sol_uverbs_dealloc_pd,
	[IB_USER_VERBS_CMD_REG_MR]		= sol_uverbs_reg_mr,
	[IB_USER_VERBS_CMD_DEREG_MR]		= sol_uverbs_dereg_mr,
	[IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL] =
					sol_uverbs_create_comp_channel,
	[IB_USER_VERBS_CMD_CREATE_CQ]		= sol_uverbs_create_cq,
	[IB_USER_VERBS_CMD_RESIZE_CQ]		= sol_uverbs_resize_cq,
	[IB_USER_VERBS_CMD_POLL_CQ]		= sol_uverbs_poll_cq,
	[IB_USER_VERBS_CMD_REQ_NOTIFY_CQ]	= sol_uverbs_req_notify_cq,
	[IB_USER_VERBS_CMD_DESTROY_CQ]    	= sol_uverbs_destroy_cq,
	[IB_USER_VERBS_CMD_CREATE_QP]		= sol_uverbs_create_qp,
	[IB_USER_VERBS_CMD_QUERY_QP]		= sol_uverbs_query_qp,
	[IB_USER_VERBS_CMD_MODIFY_QP]		= sol_uverbs_modify_qp,
	[IB_USER_VERBS_CMD_DESTROY_QP]    	= sol_uverbs_destroy_qp,
	[IB_USER_VERBS_CMD_POST_SEND]    	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_POST_RECV]    	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_POST_SRQ_RECV]    	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_CREATE_AH]    	= sol_uverbs_create_ah,
	[IB_USER_VERBS_CMD_DESTROY_AH]    	= sol_uverbs_destroy_ah,
	[IB_USER_VERBS_CMD_ATTACH_MCAST]  	= sol_uverbs_attach_mcast,
	[IB_USER_VERBS_CMD_DETACH_MCAST]  	= sol_uverbs_detach_mcast,
	[IB_USER_VERBS_CMD_CREATE_SRQ]    	= sol_uverbs_create_srq,
	[IB_USER_VERBS_CMD_MODIFY_SRQ]		= sol_uverbs_modify_srq,
	[IB_USER_VERBS_CMD_QUERY_SRQ]		= sol_uverbs_query_srq,
	[IB_USER_VERBS_CMD_DESTROY_SRQ]   	= sol_uverbs_destroy_srq,

		/* TODO - XRC */

	[IB_USER_VERBS_CMD_CREATE_XRC_SRQ]   	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_OPEN_XRC_DOMAIN]   	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_CLOSE_XRC_DOMAIN]   	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_CREATE_XRC_RCV_QP]  	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_MODIFY_XRC_RCV_QP]  	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_QUERY_XRC_RCV_QP]   	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_REG_XRC_RCV_QP]   	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_UNREG_XRC_RCV_QP]   	= sol_uverbs_dummy_command,
	[IB_USER_VERBS_CMD_QUERY_GID]		= sol_uverbs_query_gid,
	[IB_USER_VERBS_CMD_QUERY_PKEY]		= sol_uverbs_query_pkey,
};

static void sol_uverbs_common_hca_fini();

/*
 * Function:
 *	_init
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS  on success, else error code.
 * Description:
 * 	Perform Solaris OFED user verbs kernel agent driver initialization.
 */
int
_init(void)
{
	int	error;

	error = ddi_soft_state_init(&statep,
	    sizeof (uverbs_module_context_t), 0);

	if (error != 0) {
		return (error);
	}

	sol_uverbs_user_objects_init();

	error = mod_install(&modlinkage);
	if (error != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs: mod_install failed!!");
		sol_uverbs_common_hca_fini();
		sol_uverbs_user_objects_fini();
		ddi_soft_state_fini(&statep);
	}
	return (error);
}

/*
 * Function:
 *	_info
 * Input:
 *	modinfop	- Pointer to an opqque modinfo structure.
 * Output:
 *	modinfop	- Updated structure.
 * Returns:
 *	The mod_info() return code.
 * Description:
 * 	Return information about the loadable module via the mod_info()
 *	kernel function call.
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Function:
 *	_fini
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS  on success, else error code returned by
 *	mod_remove kernel function.
 * Description:
 * 	Perform Solaris OFED user verbs kernel agent driver cleanup.
 */
int
_fini(void)
{
	int    rc;

	rc = mod_remove(&modlinkage);
	if (!rc) {
		sol_uverbs_common_hca_fini();
		sol_uverbs_user_objects_fini();
		ddi_soft_state_fini(&statep);
	}
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_common_hca_fini
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	None
 * Description:
 *	Perform cleanup required by the common hca client API.
 */
static void
sol_uverbs_common_hca_fini()
{
	if (sol_uverbs_hcas == NULL)
		return;

	kmem_free(sol_uverbs_hcas, SOL_UVERBS_DRIVER_MAX_HCA_MINOR *
	    sizeof (sol_uverbs_hca_t));

	sol_uverbs_hcas = NULL;
}

/*
 * Function:
 *	sol_uverbs_uctxt_uobj_free
 * Input:
 *	uobj	-  A pointer to the Solaris User Verbs kernel agent user
 *	           object associated with the uverbs uctxt to be freed.
 * Output:
 *	None.
 * Returns:
 *	None.
 * Description:
 * 	Called when the last reference to the user uctxt is being released,
 * 	typically when the uverbs HCA device's user minor node is being closed
 * 	or when the delayed QP close for the device node is completed. This
 * 	routine releases one HCA reference acquired during the open of the
 * 	corresponding HCA uverbs minor node.
 */
void
sol_uverbs_uctxt_uobj_free(sol_ofs_uobj_t *uobj)
{
	uverbs_uctxt_uobj_t	*uctxt = (uverbs_uctxt_uobj_t *)uobj;
	sol_uverbs_hca_t	*hcap = uctxt->hca;

	mutex_enter(&hcap->hca_lock);
	if (hcap->refcnt != 0)  {
		hcap->refcnt--;
	} else {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uctxt_uobj_free: hca->refcnt already 0");
	}
	mutex_exit(&hcap->hca_lock);

	sol_ofs_uobj_free(uobj);
}

static void
uverbs_add_hca(struct ib_device *device)
{
	int			rc, hca_ndx;
	char			name[MAXNAMELEN];
	sol_uverbs_hca_t	*hcap;
	uverbs_module_context_t *mod_ctxt = sol_uverbs_mod_ctxt;
	dev_info_t		*dip = sol_uverbs_dip;
	struct ib_event_handler uverbs_async_evt_hdlr;
	struct ib_event_handler *uverbs_async_evt_hdlrp;

	uverbs_async_evt_hdlrp = &uverbs_async_evt_hdlr;

	INIT_IB_EVENT_HANDLER(uverbs_async_evt_hdlrp, device,
	    uverbs_async_event_handler);
	rc = ib_register_event_handler(uverbs_async_evt_hdlrp);
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "hca_open: ib_register_event_handler "
		    "failed %d", rc);
		return;
	}

	/*
	 * Use the hca_id from sol_ofs as the index into the hcas array.
	 */
	mutex_enter(&mod_ctxt->lock);
	hca_ndx = device->ofusr_hca_idx;
	hcap = &mod_ctxt->hcas[hca_ndx];

	mutex_enter(&hcap->hca_lock);
	if (hcap->state == SOL_UVERBS_HCA_ATTACHED) {
		mutex_exit(&hcap->hca_lock);
		mutex_exit(&mod_ctxt->lock);
		(void) ib_unregister_event_handler(uverbs_async_evt_hdlrp);
		return;
	}

	hcap->hca_ib_devp = device;

	/*
	 * Create the infiniband/uverbsN device node for this HCA.
	 */
	(void) snprintf(name, MAXNAMELEN, "uverbs%d", hca_ndx);
	rc = ddi_create_minor_node(dip, name, S_IFCHR, hca_ndx,
	    DDI_PSEUDO, 0);
	if (rc != DDI_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "hca_open: could not add character node");
		mutex_exit(&hcap->hca_lock);
		mutex_exit(&mod_ctxt->lock);
		(void) ib_unregister_event_handler(uverbs_async_evt_hdlrp);
		return;
	}

	hcap->state = SOL_UVERBS_HCA_ATTACHED;
	mutex_exit(&hcap->hca_lock);

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "hca_add: HCA index %d, HCA GUID: 0x%016llX",
	    hca_ndx, (u_longlong_t)device->node_guid);

	mod_ctxt->hca_count++;
	mutex_exit(&mod_ctxt->lock);
}

/*
 * Function:
 *	sol_uverbs_hca_close
 * Input:
 *	mod_ctxt	- Pointer to the module context.
 *	hcap		- pointer to the hca being closed.
 * Output:
 *	None
 * Returns:
 *	0 on success EBUSY  or EINVAL on failure.
 * Description:
 * 	Close the HCA opened by the driver. Will return EBUSY if handler/data
 * 	list is non null or if hca close returns IBT_HCA_RESOURCES_NOT_FREED.
 * 	Will return EINVAL if hca close fails for any other reason.
 *
 * 	NOTE: This function should be called with the HCA's hca_lock held.
 */
int
sol_uverbs_hca_close(sol_uverbs_hca_t *hcap)
{

	/*
	 * The uverbs HCA's refcnt is incremented on each open of the uverbs
	 * HCA minor node. The reference is released on the corresponding close
	 * (or when the last associated QP is closed after the minor node's
	 * close). If the refcnt is not 0, then some user process has the uvebs
	 * HCA node open even if it has not allocated any resources from the
	 * HCA. So we fail the HCA close till the refcnt goes to zero.
	 */
	if (hcap->refcnt != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str, "sol_uverbs_hca_close: "
		    "HCA %s%d: HCA in use (refcnt=%d)",
		    hcap->hca_ib_devp->ofusr_hca_drv_name,
		    hcap->hca_ib_devp->ofusr_hca_drv_inst, hcap->refcnt);
		return (EBUSY);
	}

	hcap->state = SOL_UVERBS_HCA_DETACHED;

	return (0);
}

static void
uverbs_remove_hca(struct ib_device *device)
{
	sol_uverbs_hca_t	*hcap;
	char			name[MAXNAMELEN];
	int			idx = device->ofusr_hca_idx, rc;
	uverbs_module_context_t *mod_ctxt = sol_uverbs_mod_ctxt;
	struct ib_event_handler uverbs_async_evt_hdlr;
	struct ib_event_handler *uverbs_async_evt_hdlrp;

	uverbs_async_evt_hdlrp = &uverbs_async_evt_hdlr;

	ASSERT(mod_ctxt != NULL);
	mutex_enter(&mod_ctxt->lock);
	hcap = &mod_ctxt->hcas[idx];

	/*
	 * If ib_device struct doesn't match return immediately.
	 */
	mutex_enter(&hcap->hca_lock);
	if (hcap->hca_ib_devp != device ||
	    ((hcap->state & SOL_UVERBS_HCA_DETACHED) != 0)) {
		mutex_exit(&hcap->hca_lock);
		mutex_exit(&mod_ctxt->lock);
		return;
	}

	INIT_IB_EVENT_HANDLER(uverbs_async_evt_hdlrp, device,
	    uverbs_async_event_handler);
	rc = ib_unregister_event_handler(uverbs_async_evt_hdlrp);
	if (rc !=  0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "HCA_DETACH_EVENT : unreg_event_handler failed");
		mutex_exit(&hcap->hca_lock);
		mutex_exit(&mod_ctxt->lock);
		return;
	}

	/* Close the HCA and delete the uverbs HCA minor node. */
	rc = sol_uverbs_hca_close(hcap);
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "HCA_DETACH_EVENT : HCA close failed for %s%d",
		    hcap->hca_ib_devp->ofusr_hca_drv_name,
		    hcap->hca_ib_devp->ofusr_hca_drv_inst);
		(void) ib_register_event_handler(uverbs_async_evt_hdlrp);
		mutex_exit(&hcap->hca_lock);
		mutex_exit(&mod_ctxt->lock);
		return;
	}
	hcap->hca_ib_devp = NULL;
	mutex_exit(&hcap->hca_lock);

	(void) snprintf(name, MAXNAMELEN, "%s%d", "uverbs", idx);
	ddi_remove_minor_node(mod_ctxt->dip, name);

	/* Account for the HCA that is being removed in the mod_ctxt */
	mod_ctxt->hca_count--;
	mutex_exit(&mod_ctxt->lock);
}

/*
 * Function:
 *	sol_uverbs_close_hcas
 * Input:
 *	mod_ctxt	- Pointer to the module context.
 * Output:
 *	None
 * Returns:
 *	0 on success EBUSY  or EINVAL on failure.
 * Description:
 * 	Close all the HCAs opened by the driver.
 */
static int
sol_uverbs_close_hcas(uverbs_module_context_t *mod_ctxt)
{
	int			idx, rc;
	sol_uverbs_hca_t	*hcap;

	if (mod_ctxt->hcas == NULL)
		return (0);

	/*
	 * With DR operations, we may not have all the open HCAs in the first
	 * hca_count entries in the hcas[] array. So traverse the entire array
	 * for any HCA entry with SOL_UVERBS_HCA_ATTACHED state and close it.
	 */
	mutex_enter(&mod_ctxt->lock);
	for (idx = 0; idx < SOL_UVERBS_DRIVER_MAX_HCA_MINOR; idx++) {
		hcap = &mod_ctxt->hcas[idx];

		mutex_enter(&hcap->hca_lock);
		if (hcap->state != SOL_UVERBS_HCA_ATTACHED) {
			mutex_exit(&hcap->hca_lock);
			continue;
		}

		rc = sol_uverbs_hca_close(hcap);
		if (rc != 0) {
			mutex_exit(&hcap->hca_lock);
			mutex_exit(&mod_ctxt->lock);
			SOL_OFS_DPRINTF_L4(sol_uverbs_dbg_str,
			    "uverbs_close_hcas: HCA %s%d close failed",
			    hcap->hca_ib_devp->ofusr_hca_drv_name,
			    hcap->hca_ib_devp->ofusr_hca_drv_inst);
			return (rc);
		}
		/* Let's not destroy hca_lock for now. Just release the lock. */
		mutex_exit(&hcap->hca_lock);

	}
	mutex_exit(&mod_ctxt->lock);
	return (0);
}


/*
 * Function:
 *	sol_uverbs_init_soft_state
 * Input:
 *	dip	- Pointer to the dev_info structure of the sol_uverbs driver
 *		  instance.
 * Output:
 *	None
 * Returns:
 *	mod_ctxt structure which is the sol_uverbs instance's soft state.
 * Description:
 * 	Allocate the soft state structure for this instance of sol_uverbs.
 */
static uverbs_module_context_t *
sol_uverbs_init_soft_state(dev_info_t *dip)
{
	int			instance;
	uverbs_module_context_t	*mod_ctxt;

	/*
	 * Allocate a soft state structure based on this dev info. We have only
	 * one instance of sol_uverbs currently. This instance manages all user
	 * procs access to al the HCAs in the system. Each HCA present in the
	 * system gets a sol_uverbs minor node (0 to SOL_UVERBS_DRIVER_MAX_HCA_
	 * MINOR - 1). Each user process's open results in a clone minor node
	 * being allocated for it (starting after the 2 control minor nodes,
	 * "ucma" and "event" right after the HCA nodes.)
	 */
	instance = ddi_get_instance(dip);
	if (instance != 0) {
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "attach: bad instance number %d", instance);
		return (NULL);
	}
	ASSERT(sol_uverbs_dip == NULL);
	sol_uverbs_dip = dip;

	if (ddi_soft_state_zalloc(statep, instance) != DDI_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: bad state zalloc");
		return (NULL);
	}

	mod_ctxt = ddi_get_soft_state(statep, instance);
	if (mod_ctxt == NULL) {
		ddi_soft_state_free(statep, instance);
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: cannot get soft state");
		return (NULL);
	}

	mod_ctxt->dip = dip;
	/* Save off our private context in the dev_info */
	ddi_set_driver_private(dip, mod_ctxt);
	sol_uverbs_mod_ctxt = mod_ctxt;

	return (mod_ctxt);
}

/*
 * Function:
 *	sol_uverbs_ib_register
 * Input:
 * 	mod_ctxt	- The sol_uverbs driver instance's module context.
 * Output:
 *	None
 * Returns:
 *	0 on success. ENODEV if ib_register() fails.
 * Description:
 * 	Allocate resources for the HCAs. (Currently we support only
 * 	SOL_UVERBS_DRIVER_MAX_HCA_MINOR HCAs in the system.)
 * 	Register with sol_ofs which attaches sol_uverbs to IBTF.
 */
static int
sol_uverbs_ib_register(uverbs_module_context_t *mod_ctxt)
{
	int			idx, status;
	sol_uverbs_hca_t	*hcap;

	/*
	 * Initialize mod_ctxt.
	 * Set DR callback functions (uverbs_add_hca and uverbs_remove_hca).
	 */
	mod_ctxt->hca_count	= 0;
	mod_ctxt->hcas		= NULL;

	mod_ctxt->client_info.name = "sol_uverbs";
	mod_ctxt->client_info.add = uverbs_add_hca;
	mod_ctxt->client_info.remove = uverbs_remove_hca;
	mod_ctxt->client_info.dip = mod_ctxt->dip;
	mod_ctxt->client_info.clnt_hdl = NULL;
	mod_ctxt->client_info.state = IB_CLNT_UNINITIALIZED;

	/*
	 * We want to maintain the  minor numbers allocated to the uverbs HCA
	 * minor node across sol_uverbs detach. The sol_uverbs's detach will
	 * be called when the last HCA in the system is DR'ed out (among other
	 * possibilities). If a new HCA is added to the existing set, the minor
	 * number allocated to the HCA nodes can change. To prevent this
	 * allocate the hcas[] array on the first attach and free it only on
	 * _fini. The entry in the hcas[] array is determined using the HCA
	 * driver (name, instance) tuple. Using HCA GUID would have resulted in
	 * a different entry (and hence different minor number) when a HCA is
	 * DR'ed out and replaced with an identical replacement (same vendor-id,
	 * device-id). By using the HCA driver (name, instance) tuple, we let
	 * the DDI layer determine whether it is an exact replacement or
	 * different one, using the /etc/path_to_inst persistent storage.
	 */
	if (sol_uverbs_hcas == NULL)
		sol_uverbs_hcas = kmem_zalloc(SOL_UVERBS_DRIVER_MAX_HCA_MINOR *
		    sizeof (sol_uverbs_hca_t), KM_SLEEP);

	/*
	 * Point hcas to the sol_uverbs_hcas global for managing the list of
	 * HCA's.
	 */
	mod_ctxt->hcas =  sol_uverbs_hcas;

	for (idx = 0; idx < SOL_UVERBS_DRIVER_MAX_HCA_MINOR; idx++) {
		hcap = &mod_ctxt->hcas[idx];
		hcap->state = SOL_UVERBS_HCA_UNINITIALIZED;
		mutex_init(&hcap->hca_lock, NULL, MUTEX_DRIVER, NULL);
	}

	status = ib_register_client(&mod_ctxt->client_info);
	if (status != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "hca_open:ibt_attach fail %d", status);
		return (ENODEV);
	}

	sol_uverbs_ib_clntp  = mod_ctxt->client_info.clnt_hdl->ibt_hdl;
	return (0);
}

/*
 * Function:
 *	sol_uverbs_create_control_nodes
 * Input:
 * 	dip	- Pointer to the dev_info structure of sol_uverbs instance.
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS on success. DDI_FAILURE if we couldn't create any of the
 *	control nodes.
 * Description:
 * 	Create the "ucma" and "event" control nodes.
 */
static int
sol_uverbs_create_control_nodes(dev_info_t *dip)
{
	int rc;

	rc = ddi_create_minor_node(dip, "ucma",  S_IFCHR,
	    SOL_UVERBS_DRIVER_MAX_HCA_MINOR, DDI_PSEUDO, 0);

	if (rc != DDI_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: could not add minor for ucma");
		return (rc);
	}

	rc = ddi_create_minor_node(dip, "event",  S_IFCHR,
	    SOL_UVERBS_DRIVER_EVENT_MINOR, DDI_PSEUDO, 0);

	if (rc != DDI_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: could not add minor for events");
		return (rc);
	}

	return (DDI_SUCCESS);
}

/*
 * Function:
 *	sol_uverbs_attach
 * Input:
 *	dip	- A pointer to the sol_uverbs instance's dev_info_t structure.
 *	cmd	- Type of attach (DDI_ATTACH or DDI_RESUME).
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	Calls sol_uverbs_ib_register to register this sol_uverbs instance
 * 	as a client of sol_ofs.  The sol_uverbs attach also creates two
 * 	control nodes "ucma" and "event" which are common to all HCA nodes.
 *
 * 	(currently we support only SOL_UVERBS_DRIVER_MAX_HCA_MINOR HCAs in the
 * 	system.)
 */
int
sol_uverbs_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int			rc;
	uverbs_module_context_t	*mod_ctxt;

	switch (cmd) {
		case DDI_ATTACH:
			break;
		case DDI_RESUME:
			return (DDI_SUCCESS);
		default:
			return (DDI_FAILURE);
	}

	/* Initialize the sol_uverbs instances soft state */
	mod_ctxt = sol_uverbs_init_soft_state(dip);
	if (mod_ctxt == NULL)
		return (DDI_FAILURE);

	/*
	 * Register with sol_ofs and allocate the HCA resources for this
	 * instance.
	 */
	rc = sol_uverbs_ib_register(mod_ctxt);
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: IBTL initialization failed");
		goto error;
	}

	/* Create the "ucms" and "event" control uverbs minor nodes */
	rc = sol_uverbs_create_control_nodes(dip);
	if (rc != DDI_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: Creation of ucma/event minor nodes failed");
		goto error;
	}

	/* Announce the availability of uverbs */
	ddi_report_dev(dip);

	return (DDI_SUCCESS);

error:
	/*
	 * Cleanup any resources and dettach.
	 */
	rc = sol_uverbs_close_hcas(mod_ctxt);
	if (rc != 0)
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "attach: could not close all HCAs on attach "
		    "failure");
	sol_uverbs_hcas = NULL;

	if (mod_ctxt->client_info.clnt_hdl->ibt_hdl != NULL) {
		ib_unregister_client(&mod_ctxt->client_info);
		sol_uverbs_ib_clntp  = NULL;
	}
	ddi_remove_minor_node(dip, NULL);
	ddi_soft_state_free(statep, ddi_get_instance(dip));

	return (DDI_FAILURE);
}

/*
 * Function:
 *	sol_uverbs_detach
 * Input:
 *	dip	- A pointer to the devices dev_info_t structure.
 *	cmd	- Type of detach (DDI_DETACH or DDI_SUSPEND).
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	Detaches thea driver module and will cause the driver to close
 *	the underlying IBT HCA and detach from the IBT driver.  Note
 *	that this call will fail if user verb consumers or ucma have a
 *	sol_uverbs device open.
 */
static int
sol_uverbs_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int			instance;
	uverbs_module_context_t	*mod_ctxt;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "detach()");

	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	if (instance != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "detach: bad instance number 0x%x", instance);
		return (DDI_FAILURE);
	}

	rw_enter(&uverbs_uctxt_uo_tbl.uobj_tbl_lock, RW_WRITER);
	if (uverbs_uctxt_uo_tbl.uobj_tbl_uo_cnt > 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "detach(): device in use");
		rw_exit(&uverbs_uctxt_uo_tbl.uobj_tbl_lock);
		return (DDI_FAILURE);
	}
	rw_exit(&uverbs_uctxt_uo_tbl.uobj_tbl_lock);

	mod_ctxt = ddi_get_soft_state(statep, instance);

	/*
	 * Hca close will perform the detach from IBTF.
	 */
	if (mod_ctxt->hcas != NULL) {
		if (sol_uverbs_close_hcas(mod_ctxt) != 0) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "detach(): failed to close all HCAs");
			return (DDI_FAILURE);
		}
	}

	if (mod_ctxt->client_info.clnt_hdl->ibt_hdl != NULL)
		ib_unregister_client(&mod_ctxt->client_info);

	sol_uverbs_ib_clntp  = NULL;
	ddi_soft_state_free(statep, instance);
	sol_uverbs_mod_ctxt = NULL;
	sol_uverbs_dip = NULL;
	ddi_remove_minor_node(dip, NULL);
	return (DDI_SUCCESS);
}

/*
 * Function:
 *	sol_uverbs_getinfo
 * Input:
 *	dip     - Deprecated, do not use.
 *	cmd     - Command argument (DDI_INFO_DEVT2DEVINFO or
 *	          DDI_INFO_DEVT2INSTANCE).
 *	arg     - Command specific argument.
 *	resultp - Pointer to place results.
 * Output:
 *	resultp	- Location is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 *	Depending on the request (cmd) return either the dev_info_t pointer
 *	associated with the dev_info_t specified, or the instance.  Note
 *	that we have only a single instance.
 */
/* ARGSUSED */
static int
sol_uverbs_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **resultp)
{
	uverbs_module_context_t	*mod_ctxt;

	switch (cmd) {
		case DDI_INFO_DEVT2DEVINFO:
			mod_ctxt = ddi_get_soft_state(statep, 0);
			if (!mod_ctxt) {
				return (DDI_FAILURE);
			}
			*resultp = (void *)mod_ctxt->dip;
			return (DDI_SUCCESS);

		case DDI_INFO_DEVT2INSTANCE:
			*resultp = 0;
			return (DDI_SUCCESS);

		default:
			return (DDI_FAILURE);
	}
}

/*
 * Function:
 *	sol_uverbs_prop_op
 * Input:
 *	dev	- The device number associated with this device.
 *	dip	- A pointer to the device information structure for this device.
 *	prop_op - Property operator (PROP_LEN, PROP_LEN_AND_VAL_BUF, or
 *	          PROP_LEN_AND_VAL_ALLOC).
 *	flags	- Only possible flag value is DDI_PROP_DONTPASS.
 *	name    - Pointer to the property to be interrogated.
 *	valuep	- Address of pointer if ALLOC, otherwise a pointer to the
 *	          users buffer.
 *	lengthp	- Pointer to update with property length.
 * Output:
 *	valuep	- Updated with the property value.
 *	lengthp	- Updated with the property length.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 *	Driver entry point to report the values of certain properties of the
 *	driver or  device.
 */
static int
sol_uverbs_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int flags,
    char *name, caddr_t valuep, int *lengthp)
{
	return (ddi_prop_op(dev, dip, prop_op, flags, name, valuep, lengthp));

}

static uverbs_uctxt_uobj_t *sol_uverbs_alloc_uctxt(dev_t *,
    uverbs_module_context_t *, minor_t);

/*
 * Function:
 *	sol_uverbs_open
 * Input:
 *	devp	- A pointer to the device number.
 *	flag	- Flags specified by caller (FEXCL, FNDELAY, FREAD, FWRITE).
 *	otyp	- Open type (OTYP_BLK, OTYP_CHR, OTYP_LYR).
 *	cred	- Pointer to the callers credentials.
 * Output:
 *	devp	- On success devp has been cloned to point to a unique minor
 *		  device.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	Handles a user process open of a specific user verbs minor device by
 *	allocating a user context user object and creating a unique device
 *	to identify the user.  Note: The first SOL_UVERBS_DRIVER_MAX_MINOR
 *	minor numbers are reserved for :
 *		0 to SOL_UVERBS_DRIVER_MAX_HCA_MINOR - 1 : actual HCA devices
 *		SOL_UVERBS_DRIVER_MAX_HCA_MINOR		 : UCMA node
 *		SOL_UVERBS_DRIVER_EVENT_MINOR		 :
 *			Event file for opening an event file for completion
 *			or async notifications.
 */
/* ARGSUSED */
static int
sol_uverbs_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
	uverbs_module_context_t	*mod_ctxt;
	uverbs_uctxt_uobj_t	*uctxt;
	sol_uverbs_hca_t	*hcap;
	int			minor;

	/* Char only */
	if (otyp != OTYP_CHR) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "open: not CHR");
		return (EINVAL);
	}

	mod_ctxt = ddi_get_soft_state(statep, 0);
	if (mod_ctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "open: get soft state failed");
		return (ENXIO);
	}

	minor = getminor(*devp);

	/*
	 * Special case of ucma module.
	 */
	if (minor == SOL_UVERBS_DRIVER_MAX_HCA_MINOR) {
		extern cred_t	*kcred;

		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "open: ucma_open");
		if (cred != kcred) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "open: ucma_open non-kernel context");
			return (ENOTSUP);
		}

		return (DDI_SUCCESS);
	}

	/*
	 * If this is not an open for sol_uverbs event file,
	 * A device minor number must be less than the user verb max
	 * minor device number.
	 */
	if (minor != SOL_UVERBS_DRIVER_EVENT_MINOR &&
	    minor >= SOL_UVERBS_DRIVER_MAX_HCA_MINOR) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "open: bad minor %d", minor);
		return (ENODEV);
	}
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "open() - minor %d", minor);

	/*
	 * Check if the HCA corresponding to the minor is ATTACHED and increase
	 * the refcnt if so. Typically, we should not have a minor device for a
	 * HCA that is not attached but if there as race between open and DR
	 * operation, we need this check.
	 *
	 * The HCA refcnt held here should be released by sol_ofs_uobj_decref
	 * when the UVERBS_UCTXT's uobj refcnt goes to zero (typicall during
	 * sol_uverbs_hca_close).
	 */
	if (minor != SOL_UVERBS_DRIVER_EVENT_MINOR) {
		hcap = &mod_ctxt->hcas[minor];
		mutex_enter(&hcap->hca_lock);
		if (hcap->state != SOL_UVERBS_HCA_ATTACHED) {
			mutex_exit(&hcap->hca_lock);
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "open: HCA not attached (minor=%d)", minor);
			return (ENODEV);
		}
		hcap->refcnt++;
		mutex_exit(&hcap->hca_lock);
	}

	/*
	 * Allocate a user context and return a unique ID that can be used
	 * in identify the new user context object.  Create a clone device
	 * that uses this unique ID as the minor number.  Allocation of the
	 * user context object places one reference against it; which will
	 * be held until the device is closed.
	 *
	 * sol_uverbs_alloc_uctxt() returns a sucessful allocation of uctx
	 * with the uobj uo_lock held for WRITTER.
	 */
	uctxt = sol_uverbs_alloc_uctxt(devp, mod_ctxt, minor);
	if (!uctxt)  {
		if (minor != SOL_UVERBS_DRIVER_EVENT_MINOR)
			atomic_dec_uint(&hcap->refcnt);
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "open: user context alloc failed");
		return (ENODEV);
	}

	/*
	 * Indicate the object is alive and release the user object write lock
	 * which was placed on the user context at allocation.
	 */
	uctxt->uobj.uo_live = 1;
	rw_exit(&uctxt->uobj.uo_lock);

	return (DDI_SUCCESS);
}

/*
 * Function:
 *	sol_uverbs_close
 * Input:
 *	dev	- Device number.
 *	flag	- File status flag.
 *	otyp	- Open type.
 *	cred	- A pointer to the callers credientials.
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	Handles a user process close of a specific user verbs minor device by
 *	freeing any user objects this process may still have allocated and
 * 	deleting the associated user context object.
 */
/* ARGSUSED */
static int
sol_uverbs_close(dev_t dev, int flag, int otyp, cred_t *cred)
{
	minor_t			id = getminor(dev);
	genlist_entry_t		*entry, *new_entry;
	uverbs_uctxt_uobj_t	*uctxt;
	int			rc;
	genlist_t		tmp_genlist;
	ibt_status_t		status;

	/*
	 * HCA specific device nodes created during attach are been
	 * closed. Return SUCCESS.
	 */
	if (id < SOL_UVERBS_DRIVER_MAX_MINOR) {
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "uverbs_close: dev_t %x, minor %x < %x",
		    dev, id, SOL_UVERBS_DRIVER_MAX_MINOR);
		return (0);
	}

	/*
	 * Must be a user or kernel open, i.e. not a minor node that
	 * that represents a user verbs device.  If it is the UCMA
	 * nothing needs to be done.
	 */
	if (id == SOL_UVERBS_DRIVER_MAX_HCA_MINOR) {
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "uverbs_close: ucma close");
		return (DDI_SUCCESS);
	}

	uctxt = uverbs_uobj_get_uctxt_write(id - SOL_UVERBS_DRIVER_MAX_MINOR);
	if (uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_close: Unknown user context");
		return (ENXIO);
	}
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "uverbs_close- "
	    "uctxt %p", uctxt);

	/*
	 * Remove from the user context resource table, cleanup all
	 * user resources that may still be hanging around.
	 */
	if (!sol_ofs_uobj_remove(&uverbs_uctxt_uo_tbl, &uctxt->uobj)) {
		/*
		 * It was already removed, drop the lock held from
		 * get above and exit.
		 */
		sol_ofs_uobj_put(&uctxt->uobj);
		return (ENXIO);
	}

	if (uctxt->uctxt_type == SOL_UVERBS_UCTXT_ASYNC ||
	    uctxt->uctxt_type == SOL_UVERBS_UCTXT_COMPL) {
		uverbs_uctxt_uobj_t	*verbs_uctxt;

		SOL_OFS_DPRINTF_L4(sol_uverbs_dbg_str,
		    "uverbs_close: Async or Compl user context");

		/*
		 * Verbs uctxt has already been freed, just return.
		 */
		if (!uctxt->uctxt_verbs_id) {
			sol_ofs_uobj_put(&uctxt->uobj);
			sol_ofs_uobj_deref(&uctxt->uobj, sol_ofs_uobj_free);
			return (0);
		}

		/*
		 * Verbs uctxt has not been freed. Close the ufile. This
		 * also frees the ufile if reference count is 0.
		 */
		verbs_uctxt = uverbs_uobj_get_uctxt_write(
		    uctxt->uctxt_verbs_id - SOL_UVERBS_DRIVER_MAX_MINOR);

		if (verbs_uctxt &&
		    uctxt->uctxt_type == SOL_UVERBS_UCTXT_ASYNC) {
			sol_uverbs_event_file_close(verbs_uctxt->async_evfile);
			verbs_uctxt->async_evfile = NULL;
		} else if (uctxt->comp_evfile) {
			uctxt->comp_evfile = NULL;
		}
		if (verbs_uctxt)
			sol_ofs_uobj_put(&verbs_uctxt->uobj);

		sol_ofs_uobj_put(&uctxt->uobj);
		sol_ofs_uobj_deref(&uctxt->uobj, sol_ofs_uobj_free);
		return (0);
	} else if (uctxt->uctxt_type == SOL_UVERBS_UCTXT_EVENT) {
		sol_ofs_uobj_put(&uctxt->uobj);
		sol_ofs_uobj_deref(&uctxt->uobj, sol_ofs_uobj_free);
		return (0);
	}

	ASSERT(uctxt->hca != NULL);

	/*
	 * Release resources that may still be held by this user context.
	 * Remove the resources from the associated resource managment
	 * table and free it.
	 */
	mutex_enter(&uctxt->lock);

	entry = remove_genlist_head(&uctxt->ah_list);
	while (entry) {
		uverbs_uah_uobj_t *uah = (uverbs_uah_uobj_t *)entry->data;

		rw_enter(&(uah->uobj.uo_lock), RW_WRITER);
		(void) sol_ofs_uobj_remove(&uverbs_uah_uo_tbl, &uah->uobj);
		rw_exit(&(uah->uobj.uo_lock));
		if ((status = ibt_free_ah(
		    uctxt->hca->hca_ib_devp->hca_hdl, uah->ah)) !=
		    IBT_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_close(). ibt_free_ah(%p) failed status %d",
			    uah->ah, status);
			return (sol_uverbs_ibt_to_kernel_status(status));
		}
		sol_ofs_uobj_free(&uah->uobj);

		kmem_free((void *)entry, sizeof (genlist_entry_t));
		entry = remove_genlist_head(&uctxt->ah_list);
	}

	init_genlist(&tmp_genlist);
	entry = remove_genlist_head(&uctxt->qp_list);
	while (entry) {
		uverbs_uqp_uobj_t *uqp = (uverbs_uqp_uobj_t *)entry->data;

		/* Free unreaped asynchronous events.  */
		uverbs_release_uqp_uevents(uctxt->async_evfile, uqp);

		/*
		 * If ucma has disabled QP free for this QP, set the
		 * uqp_free_state to FREE_PENDING. Free QP if not.
		 */
		rw_enter(&(uqp->uobj.uo_lock), RW_WRITER);
		if (uqp->uqp_free_state != SOL_UVERBS2UCMA_ENABLE_QP_FREE) {
			new_entry = add_genlist(&tmp_genlist, entry->data,
			    entry->data_context);
			uqp->list_entry = new_entry;
			uqp->uqp_free_state = SOL_UVERBS2UCMA_FREE_PENDING;
			rw_exit(&(uqp->uobj.uo_lock));
		} else {
			uqp->list_entry = NULL;
			mutex_exit(&uctxt->lock);
			sol_ofs_uobj_ref(&uqp->uobj);
			rc = uverbs_uqp_free(uqp, uctxt);
			mutex_enter(&uctxt->lock);
			if (rc)
				SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
				    "uqp_free(%p) failed", uqp);
		}
		kmem_free(entry, sizeof (genlist_entry_t));
		entry = remove_genlist_head(&uctxt->qp_list);
	}
	(uctxt->qp_list).count = tmp_genlist.count;
	(uctxt->qp_list).head = tmp_genlist.head;
	(uctxt->qp_list).tail = tmp_genlist.tail;

	init_genlist(&tmp_genlist);
	entry = remove_genlist_head(&uctxt->cq_list);
	while (entry) {
		uverbs_ucq_uobj_t *ucq = (uverbs_ucq_uobj_t *)entry->data;

		rw_enter(&(ucq->uobj.uo_lock), RW_WRITER);

		/* Free events associated with the CQ.  */
		uverbs_release_ucq_channel(uctxt, ucq->comp_chan, ucq);

		if (ucq->active_qp_cnt) {
			new_entry = add_genlist(&tmp_genlist, entry->data,
			    entry->data_context);
			ucq->list_entry = new_entry;
			ucq->free_pending = 1;
			rw_exit(&(ucq->uobj.uo_lock));
		} else {
			ucq->list_entry = NULL;
			sol_ofs_uobj_ref(&ucq->uobj);
			mutex_exit(&uctxt->lock);
			rc = uverbs_ucq_free(ucq, uctxt);
			mutex_enter(&uctxt->lock);
			if (rc)
				SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
				    "ucq_free(%p) failed", ucq);
		}

		kmem_free((void *)entry, sizeof (genlist_entry_t));
		entry = remove_genlist_head(&uctxt->cq_list);
	}
	(uctxt->cq_list).count = tmp_genlist.count;
	(uctxt->cq_list).head = tmp_genlist.head;
	(uctxt->cq_list).tail = tmp_genlist.tail;

	init_genlist(&tmp_genlist);
	entry = remove_genlist_head(&uctxt->srq_list);
	while (entry) {
		uverbs_usrq_uobj_t *usrq = (uverbs_usrq_uobj_t *)entry->data;

		rw_enter(&(usrq->uobj.uo_lock), RW_WRITER);

		/* Free unreaped asynchronous events.  */
		uverbs_release_usrq_uevents(uctxt->async_evfile, usrq);

		if (usrq->active_qp_cnt) {
			new_entry = add_genlist(&tmp_genlist, entry->data,
			    entry->data_context);
			usrq->list_entry = new_entry;
			usrq->free_pending = 1;
			rw_exit(&(usrq->uobj.uo_lock));
		} else {
			usrq->list_entry = NULL;
			sol_ofs_uobj_ref(&usrq->uobj);
			mutex_exit(&uctxt->lock);
			rc = uverbs_usrq_free(usrq, uctxt);
			mutex_enter(&uctxt->lock);
			if (rc)
				SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
				    "usrq_free(%p) failed", usrq);
		}

		kmem_free((void *)entry, sizeof (genlist_entry_t));
		entry = remove_genlist_head(&uctxt->srq_list);
	}
	(uctxt->srq_list).count = tmp_genlist.count;
	(uctxt->srq_list).head = tmp_genlist.head;
	(uctxt->srq_list).tail = tmp_genlist.tail;

	entry = remove_genlist_head(&uctxt->mr_list);
	while (entry) {
		uverbs_umr_uobj_t *umr = (uverbs_umr_uobj_t *)entry->data;

		rw_enter(&(umr->uobj.uo_lock), RW_WRITER);
		(void) sol_ofs_uobj_remove(&uverbs_umr_uo_tbl, &umr->uobj);
		rw_exit(&(umr->uobj.uo_lock));

		if ((status = ibt_deregister_mr(uctxt->hca->drv_hca_hdl,
		    umr->mr)) != IBT_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_close(): ibt_deregister_mr(%p) failed "
			    "status %d", umr->mr, status);
			return (sol_uverbs_ibt_to_kernel_status(status));
		}

		sol_ofs_uobj_free(&umr->uobj);

		kmem_free((void *)entry, sizeof (genlist_entry_t));
		entry = remove_genlist_head(&uctxt->mr_list);
	}

	entry = remove_genlist_head(&uctxt->pd_list);
	while (entry) {
		uverbs_upd_uobj_t *upd = (uverbs_upd_uobj_t *)entry->data;

		rw_enter(&(upd->uobj.uo_lock), RW_WRITER);
		if (upd->active_qp_cnt) {
			new_entry = add_genlist(&tmp_genlist, entry->data,
			    entry->data_context);
			upd->list_entry = new_entry;
			upd->free_pending = 1;
			rw_exit(&(upd->uobj.uo_lock));
		} else {
			upd->list_entry = NULL;
			sol_ofs_uobj_ref(&upd->uobj);
			mutex_exit(&uctxt->lock);
			rc = uverbs_upd_free(upd, uctxt);
			mutex_enter(&uctxt->lock);
			if (rc)
				SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
				    "upd_free(%p) failed", upd);
		}

		kmem_free((void *)entry, sizeof (genlist_entry_t));
		entry = remove_genlist_head(&uctxt->pd_list);
	}
	(uctxt->pd_list).count = tmp_genlist.count;
	(uctxt->pd_list).head = tmp_genlist.head;
	(uctxt->pd_list).tail = tmp_genlist.tail;

	mutex_exit(&uctxt->lock);

	/*
	 * Release the user file structure to the async file if it
	 * has not be released yet. The uctxt for async file will
	 * be closed when the async file is closed.
	 */
	if (uctxt->async_evfile) {
		uverbs_uctxt_uobj_t	*async_uctxt;

		async_uctxt = uverbs_uobj_get_uctxt_write(
		    uctxt->uctxt_async_id -
		    SOL_UVERBS_DRIVER_MAX_MINOR);
		if (!async_uctxt) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_close: async_id already closed");
			sol_ofs_uobj_put(&uctxt->uobj);
			return (ENXIO);
		}

		async_uctxt->uctxt_verbs_id = 0;
		sol_uverbs_event_file_close(uctxt->async_evfile);
		uctxt->async_evfile = NULL;
		sol_ofs_uobj_put(&async_uctxt->uobj);
	} else if (uctxt->comp_evfile) {
		uverbs_uctxt_uobj_t	*comp_uctxt;

		comp_uctxt = uverbs_uobj_get_uctxt_write(
		    uctxt->uctxt_comp_id -
		    SOL_UVERBS_DRIVER_MAX_MINOR);
		if (!comp_uctxt) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_close: comp_id already closed");
			sol_ofs_uobj_put(&uctxt->uobj);
			return (ENXIO);
		}
		comp_uctxt->uctxt_verbs_id = 0;
		sol_uverbs_event_file_close(uctxt->comp_evfile);
		uctxt->comp_evfile = NULL;
		sol_ofs_uobj_put(&comp_uctxt->uobj);
	}

	/*
	 * Release the write lock and the reference from the get above, and
	 * release the reference placed on the user context as process open
	 * to release context.
	 */
	sol_ofs_uobj_put(&uctxt->uobj);

	/*
	 * If some QPs have not been freed, donot free the uctxt.
	 * Set uctxt_free_pending flag. This will be freed when
	 * the QP will be freed.
	 */
	if ((uctxt->qp_list).count) {
		SOL_OFS_DPRINTF_L3(sol_uverbs_dbg_str,
		    "close: uctxt %p, has pending uqp", uctxt);
		uctxt->uctxt_free_pending = 1;
		return (0);
	}

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "close: deallocated user context: %p, ref = %d",
	    (void *)uctxt, uctxt->uobj.uo_refcnt);

	sol_ofs_uobj_deref(&uctxt->uobj, sol_uverbs_uctxt_uobj_free);

	return (0);
}

/*
 * Function:
 *	sol_uverbs_read
 * Input:
 *	dev	- Device number.
 *	uiop	- Pointer to the uio structgure where data is to be stored.
 *	credp	- A pointer to the credentials for the I/O transaction.
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User process read stub.
 */
static int
sol_uverbs_read(dev_t dev, struct uio *uiop, cred_t *credp)
{
	minor_t			id = getminor(dev);
	uverbs_uctxt_uobj_t	*uctxt, *verbs_uctxt;
	int			rc;
	uverbs_ufile_uobj_t	*ufilep;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "uverbs_read(%x, %p, %p)",
	    dev, uiop, credp);

	ASSERT(id >= SOL_UVERBS_DRIVER_MAX_MINOR);
	uctxt = uverbs_uobj_get_uctxt_read(id - SOL_UVERBS_DRIVER_MAX_MINOR);
	if (uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_read: Failed get user context");
		return (ENXIO);
	}

	if (uctxt->uctxt_verbs_id < SOL_UVERBS_DRIVER_MAX_MINOR) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_read: Invalid Verbs user context id, %x",
		    uctxt->uctxt_verbs_id);
		sol_ofs_uobj_put(&uctxt->uobj);
		return (ENXIO);
	}
	verbs_uctxt = uverbs_uobj_get_uctxt_read(uctxt->uctxt_verbs_id
	    - SOL_UVERBS_DRIVER_MAX_MINOR);
	if (verbs_uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_read: Failed get verbs user context");
		sol_ofs_uobj_put(&uctxt->uobj);
		return (ENXIO);
	}
	if (uctxt->uctxt_type == SOL_UVERBS_UCTXT_ASYNC) {
		ASSERT(verbs_uctxt->async_evfile);
		ufilep = verbs_uctxt->async_evfile;
		sol_ofs_uobj_put(&uctxt->uobj);
		sol_ofs_uobj_put(&verbs_uctxt->uobj);
		rc = sol_uverbs_event_file_read(ufilep,
		    uiop, credp);
	} else if (uctxt->uctxt_type == SOL_UVERBS_UCTXT_COMPL) {
		ufilep = uctxt->comp_evfile;
		sol_ofs_uobj_put(&uctxt->uobj);
		sol_ofs_uobj_put(&verbs_uctxt->uobj);
		rc = sol_uverbs_event_file_read(ufilep,
		    uiop, credp);
	} else {
		sol_ofs_uobj_put(&uctxt->uobj);
		sol_ofs_uobj_put(&verbs_uctxt->uobj);
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_read: invalid user context type %x",
		    uctxt->uctxt_type);
		rc = ENXIO;
	}

	return (rc);
}

/*
 * Function:
 *	sol_uverbs_mmap
 * Input:
 *	dev		- Device whose memory is to be mapped.
 *	sol_uverbs_mmap	- Offset within the device memory at which mapping
 *			  begins.
 *	prot		- Bitmask specifying protection.
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User process mmap stub.  Mmap operations are performed directly
 *	by the underlying IB HCA driver, bypassing the user verbs.
 */
/* ARGSUSED */
static int
sol_uverbs_mmap(dev_t dev, off_t mmap_offset, int prot)
{
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "sol_uverbs_mmap(%d)-  not yet used", mmap_offset);
	return (DDI_SUCCESS);
}

/*
 * Function:
 *	sol_uverbs_ioctl
 * Input:
 *	cmd		- icotl command
 *	arg		- input argument
 *	rvalp		- return value pointer
 * Output:
 *	NONE
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 *	ioctl handler
 */
/* ARGSUSED */
static int
sol_uverbs_ioctl(dev_t dev, int cmd, intptr_t arg, int mode,
    cred_t *credp, int *rvalp)
{
	uverbs_module_context_t	*mod_ctxt;
	sol_uverbs_hca_t	*hcap;
	sol_uverbs_hca_info_t	*hip;
	sol_uverbs_info_t uverbs_info, *infop;
	int i, idx, hca_cnt, bufsize;
	int status = DDI_SUCCESS;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "ioctl: cmd %x", cmd);

	switch (cmd) {
	case UVERBS_IOCTL_GET_HCA_INFO:
		mod_ctxt = ddi_get_soft_state(statep, 0);
		if (mod_ctxt == NULL) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_ioctl: EPADF - mod_ctxt == NULL");
			return (EBADF);
		}

		status = copyin((void*)arg, (void*)&uverbs_info,
		    sizeof (sol_uverbs_info_t));

		if (status != 0) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_ioctl: copyin (status=%d)", status);
			return (EFAULT);
		}

		hca_cnt = uverbs_info.uverbs_hca_cnt;

		bufsize = sizeof (sol_uverbs_info_t) +
		    sizeof (sol_uverbs_hca_info_t) * hca_cnt;
		infop = (sol_uverbs_info_t *)kmem_zalloc(bufsize, KM_NOSLEEP);

		if (infop == NULL) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_ioctl: kmem_zalloc failed");
			return (ENOMEM);
		}

		infop->uverbs_abi_version = IB_USER_VERBS_ABI_VERSION;
		infop->uverbs_solaris_abi_version =
		    IB_USER_VERBS_SOLARIS_ABI_VERSION;
		hip = infop->uverbs_hca_info;

		if (hca_cnt == 1)
			idx = getminor(dev);
		else
			idx = 0;
		mutex_enter(&mod_ctxt->lock);
		for (i = 0; i < mod_ctxt->hca_count; idx++) {
			hcap = &mod_ctxt->hcas[idx];
			mutex_enter(&hcap->hca_lock);
			if (hcap->state != SOL_UVERBS_HCA_ATTACHED) {
				mutex_exit(&hcap->hca_lock);
				continue;
			}

			hip->uverbs_hca_vendorid =
			    hcap->hca_ib_devp->ofusr_hca_vendorid;
			hip->uverbs_hca_deviceid =
			    hcap->hca_ib_devp->ofusr_hca_deviceid;
			hip->uverbs_hca_devidx = (uint8_t)idx;
			hip->uverbs_hca_driver_instance =
			    hcap->hca_ib_devp->ofusr_hca_drv_inst;

			(void) strcpy(hip->uverbs_hca_ibdev_name,
			    hcap->hca_ib_devp->ofusr_name);
			(void) strcpy(hip->uverbs_hca_driver_name,
			    hcap->hca_ib_devp->ofusr_hca_drv_name);
			mutex_exit(&hcap->hca_lock);
			hip++;
			i++;
			if (i >= hca_cnt)
				break;
		}
		mutex_exit(&mod_ctxt->lock);

		infop->uverbs_hca_cnt = (int16_t)i;
		if (ddi_copyout((void *) infop,
		    (void *) arg, sizeof (sol_uverbs_info_t) + i *
		    sizeof (sol_uverbs_hca_info_t), mode) != 0) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "uverbs_ioctl: EFAULT - copyout failure");
			status = EFAULT;
		}
		kmem_free(infop, bufsize);
		break;
	default:
		status = ENOTTY;
		break;
	}

	*rvalp = status;
	return (status);
}

/*
 * Function:
 *	sol_uverbs_get_context
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to return the unique user context to the process
 *	that opened the associated user verb driver instance.  Note that upon
 *	entry a reference will have already been placed on the user
 *	context user space object, so an additional reference is not
 *	required here.
 */
int
sol_uverbs_get_context(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct mthca_alloc_ucontext_resp	uresp;
	struct ib_uverbs_get_context		cmd;
	struct ib_uverbs_get_context_resp	resp;
	struct ib_udata				udata;
	int					rc;
	minor_t					async_id;
	uverbs_uctxt_uobj_t			*async_uctxt;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_get_context() - buf %p, sizeof (cmd) %d",
	    buf, sizeof (cmd));

	ASSERT(uctxt->hca);

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));

	udata.inbuf  = (void *)(buf + sizeof (cmd));
#ifdef	_LP64
	udata.outbuf = (void *)(cmd.response.r_laddr + sizeof (resp));
#else
	udata.outbuf = (void *)(cmd.response.r_addr + sizeof (resp));
#endif
	udata.inlen  = in_len - sizeof (cmd);
	udata.outlen = out_len - sizeof (resp);

	/*
	 * libibverbs will have passed minor of the async file in
	 * resp.fd. Use this to determine the uctxt created for
	 * asyncs.
	 */
#ifdef	_LP64
	rc = copyin((void*)cmd.response.r_laddr, (void*)&resp, sizeof (resp));
#else
	rc = copyin((void*)cmd.response.r_addr, (void*)&resp, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: copyin (rc=%d)", rc);
		rc = EFAULT;
		goto out;
	}
	async_id = resp.async_fd;
	if (async_id < SOL_UVERBS_DRIVER_MAX_MINOR) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: Invalid async user context "
		    "id %x", async_id);
		return (ENXIO);
	}

	async_uctxt = uverbs_uobj_get_uctxt_read(async_id -
	    SOL_UVERBS_DRIVER_MAX_MINOR);
	if (async_uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: Failed get async user context");
		return (ENXIO);
	}
	if (async_uctxt->uctxt_type != SOL_UVERBS_UCTXT_EVENT ||
	    async_uctxt->uctxt_verbs_id != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: Invalid user context - "
		    "possibly reused");
		return (ENXIO);
	}
	async_uctxt->uctxt_type = SOL_UVERBS_UCTXT_ASYNC;
	async_uctxt->uctxt_verbs_id = uctxt->uobj.uo_id +
	    SOL_UVERBS_DRIVER_MAX_MINOR;
	uctxt->uctxt_async_id = async_id;
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "get_context: uctxt %p, async_uctxt %p, async_id %x",
	    uctxt, async_uctxt, async_id);
	sol_ofs_uobj_put(&async_uctxt->uobj);

	uctxt->async_evfile = uverbs_alloc_event_file(uctxt, 1);
	if (!uctxt->async_evfile) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: async event file allocation failed");
		goto out;
	}

	(void) memset(&resp, 0, sizeof (resp));
	resp.num_comp_vectors 	= 1;

#ifdef	_LP64
	rc = copyout((void*)&resp, (void*)cmd.response.r_laddr, sizeof (resp));
#else
	rc = copyout((void*)&resp, (void*)cmd.response.r_addr, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: copyout (rc=%d)", rc);
		rc = EFAULT;
		goto out;
	}

	/*
	 * This unfortunately is Mellanox specific, we need to consider moving
	 * this directly into the command response as opaque data, instead of
	 * using this method.
	 */
	(void) memset(&uresp, 0, sizeof (uresp));
	uresp.uarc_size   = 0;
	rc = ib_get_hca_max_chans(uctxt->hca->hca_ib_devp,
	    &uresp.qp_tab_size);
	if (rc) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: get_hca_max failed");
		goto out;
	}

	rc = copyout((void*)&uresp, (void*)udata.outbuf, sizeof (uresp));
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "get_context: copyout outbuf (rc=%d)", rc);
		rc = EFAULT;
		goto out;
	}
	rc = DDI_SUCCESS;

out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_alloc_pd
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing a alloc PD command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to allocate a device protection domain.
 */
/* ARGSUSED */
int
sol_uverbs_alloc_pd(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_alloc_pd	cmd;
	struct ib_uverbs_alloc_pd_resp	resp;
	uverbs_upd_uobj_t		*upd;
	int				rc;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "alloc_pd()");

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));
	(void) memset(&resp, 0, sizeof (resp));

	upd = kmem_zalloc(sizeof (*upd), KM_NOSLEEP);
	if (upd == NULL) {
		rc = ENOMEM;
		goto out;
	}
	sol_ofs_uobj_init(&upd->uobj, 0, SOL_UVERBS_UPD_UOBJ_TYPE);
	rw_enter(&upd->uobj.uo_lock, RW_WRITER);

	rc = ibt_alloc_pd(uctxt->hca->drv_hca_hdl, IBT_PD_NO_FLAGS, &upd->pd);
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: ibt_alloc_pd() (rc=%d)", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		upd->uobj.uo_uobj_sz = sizeof (uverbs_upd_uobj_t);
		goto alloc_err;
	}

	if (sol_ofs_uobj_add(&uverbs_upd_uo_tbl, &upd->uobj) != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: User object add failed");
		rc = ENOMEM;
		goto err_add_uobj;
	}
	resp.pd_handle = upd->uobj.uo_id;

	/*
	 * Query underlying hardware driver for data that may be required
	 * when using the PD in an OS Bypass creation of UD address vectors.
	 */
	rc = ibt_ci_data_out(uctxt->hca->drv_hca_hdl, IBT_CI_NO_FLAGS,
	    IBT_HDL_PD, (void *)upd->pd, &resp.drv_out,
	    sizeof (resp.drv_out));
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: ibt_ci_data_out() (rc=%d)", rc);
		rc = EFAULT;
		goto err_response;
	}

#ifdef	_LP64
	rc = copyout((void*)&resp, (void*)cmd.response.r_laddr, sizeof (resp));
#else
	rc = copyout((void*)&resp, (void*)cmd.response.r_addr, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: copyout fail (rc=%d)", rc);
		rc = EFAULT;
		goto err_response;
	}

	mutex_enter(&uctxt->lock);
	upd->list_entry = add_genlist(&uctxt->pd_list, (uintptr_t)upd, uctxt);
	mutex_exit(&uctxt->lock);

	if (!upd->list_entry) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: Error adding upd to pd_list\n");
		rc = ENOMEM;
		goto err_response;
	}

	upd->uobj.uo_live = 1;
	rw_exit(&upd->uobj.uo_lock);
	return (DDI_SUCCESS);

err_response:
	/*
	 * Need to set uo_live, so sol_ofs_uobj_remove() will
	 * remove the object from the object table.
	 */
	upd->uobj.uo_live = 1;
	(void) sol_ofs_uobj_remove(&uverbs_upd_uo_tbl, &upd->uobj);

err_add_uobj:
	if (ibt_free_pd(uctxt->hca->drv_hca_hdl, upd->pd) != IBT_SUCCESS)
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_pd: ibt_free_pd(%p) failed", upd->pd);

alloc_err:
	rw_exit(&upd->uobj.uo_lock);
	sol_ofs_uobj_deref(&upd->uobj, sol_ofs_uobj_free);
out:
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "alloc_pd:error (rc=%d)", rc);
	return (rc);
}

int
uverbs_upd_free(uverbs_upd_uobj_t *upd, uverbs_uctxt_uobj_t *uctxt)
{
	int	rc;

	rc = ibt_free_pd(uctxt->hca->drv_hca_hdl, upd->pd);
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_upd_free: ibt_free_pd() failed %d", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		sol_ofs_uobj_put(&upd->uobj);
		return (rc);
	}

	/*
	 * Remove from the list of this contexts PD resources, then remove from
	 * the resource managment table and the reference placed on the user
	 * object at PD allocation.
	 */
	upd->pd = NULL;
	if (upd->list_entry) {
		mutex_enter(&uctxt->lock);
		delete_genlist(&uctxt->pd_list, upd->list_entry);
		mutex_exit(&uctxt->lock);
	}

	/*
	 * list_entry is NULL when called from sol_uverbs_close. Remove
	 * from upd_uo_tbl and free upd, when called from close also.
	 */
	sol_ofs_uobj_put(&upd->uobj);
	(void) sol_ofs_uobj_remove(&uverbs_upd_uo_tbl, &upd->uobj);
	sol_ofs_uobj_deref(&upd->uobj, sol_ofs_uobj_free);
	return (0);
}

/*
 * Function:
 *	sol_uverbs_dealloc_pd
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing dealloc PD command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to de-allocate a device protection domain.
 */
/* ARGSUSED */
int
sol_uverbs_dealloc_pd(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_dealloc_pd	cmd;
	uverbs_upd_uobj_t		*upd;
	int				rc = 0;

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "dealloc_pd: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto err_out1;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "dealloc_pd(%d)", cmd.pd_handle);

	upd = uverbs_uobj_get_upd_write(cmd.pd_handle);
	if (upd == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "dealloc_pd(%d) : invalid hdl", cmd.pd_handle);
		rc = EINVAL;
		goto err_out1;
	}

	if (upd->active_qp_cnt) {
		sol_ofs_uobj_put(&upd->uobj);
		rc = EBUSY;
	} else {
		rc = uverbs_upd_free(upd, uctxt);
	}
	cmd.pd_handle = 0;
	return (rc);

err_out1:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_query_device
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing query device command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to query device attributes.
 */
/* ARGSUSED */
int
sol_uverbs_query_device(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
	int out_len)
{
	struct ib_uverbs_query_device		cmd;
	struct ib_uverbs_query_device_resp	resp;
	ibt_hca_attr_t				hca_attr;
	int					rc;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "query_device()");

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_device: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));
	rc = ibt_query_hca(uctxt->hca->drv_hca_hdl, &hca_attr);
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_device: ibt_query_hca() (rc=%d)", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		goto out;
	}

	(void) memset(&resp, 0, sizeof (resp));

	resp.fw_ver = ((uint64_t)hca_attr.hca_fw_major_version << 32) |
	    ((uint64_t)hca_attr.hca_fw_minor_version << 16) |
	    ((uint64_t)hca_attr.hca_fw_micro_version);

	/*
	 * NOTE: node guid and system image guid must be returned in big
	 * endian (network order).  On solaris these are in host
	 * order, so we swap it back here.
	 */
	resp.node_guid			= htonll(hca_attr.hca_node_guid);
	resp.sys_image_guid		= htonll(hca_attr.hca_si_guid);

	resp.max_mr_size		= hca_attr.hca_max_memr_len;

	resp.page_size_cap =
	    sol_uverbs_ibt_to_of_page_sz(hca_attr.hca_page_sz);

	resp.vendor_id			= hca_attr.hca_vendor_id;
	resp.vendor_part_id		= hca_attr.hca_device_id;
	resp.hw_ver			= hca_attr.hca_version_id;
	resp.max_qp			= hca_attr.hca_max_chans;
	resp.max_qp_wr			= hca_attr.hca_max_chan_sz;

	resp.device_cap_flags		=
	    sol_uverbs_ibt_to_of_device_cap_flags(hca_attr.hca_flags,
	    hca_attr.hca_flags2);

	resp.max_sge			= hca_attr.hca_max_sgl;
	resp.max_sge_rd			= hca_attr.hca_max_sgl;
	resp.max_cq			= hca_attr.hca_max_cq;
	resp.max_cqe			= hca_attr.hca_max_cq_sz;
	resp.max_mr			= hca_attr.hca_max_memr;
	resp.max_pd			= hca_attr.hca_max_pd;
	resp.max_qp_rd_atom		= hca_attr.hca_max_rdma_in_chan;
	resp.max_ee_rd_atom		= 0;
	resp.max_res_rd_atom		= hca_attr.hca_max_rsc;
	resp.max_qp_init_rd_atom	= hca_attr.hca_max_rdma_out_chan;
	resp.max_ee_init_rd_atom	= 0;
	if (hca_attr.hca_flags & IBT_HCA_ATOMICS_GLOBAL) {
		resp.atomic_cap = IB_ATOMIC_GLOB;
	} else if (hca_attr.hca_flags & IBT_HCA_ATOMICS_HCA) {
		resp.atomic_cap = IB_ATOMIC_HCA;
	} else {
		resp.atomic_cap = IB_ATOMIC_NONE;
	}
	resp.max_ee			= 0;
	resp.max_rdd			= 0;
	resp.max_mw			= hca_attr.hca_max_mem_win;
	resp.max_raw_ipv6_qp		= hca_attr.hca_max_ipv6_chan;
	resp.max_raw_ethy_qp		= hca_attr.hca_max_ether_chan;
	resp.max_mcast_grp		= hca_attr.hca_max_mcg;
	resp.max_mcast_qp_attach	= hca_attr.hca_max_chan_per_mcg;
	resp.max_total_mcast_qp_attach	= hca_attr.hca_max_mcg_chans;
	resp.max_ah			= hca_attr.hca_max_ud_dest;
	resp.max_fmr			= hca_attr.hca_max_fmrs;
	resp.max_map_per_fmr		= 0;
	resp.max_srq			= hca_attr.hca_max_srqs;
	resp.max_srq_wr			= hca_attr.hca_max_srqs_sz;
	resp.max_srq_sge		= hca_attr.hca_max_srq_sgl;
	resp.max_pkeys			= hca_attr.hca_max_port_pkey_tbl_sz;
	resp.local_ca_ack_delay		= hca_attr.hca_local_ack_delay;
	resp.phys_port_cnt		= hca_attr.hca_nports;

#ifdef	_LP64
	rc = copyout((void*)&resp, (void*)cmd.response.r_laddr, sizeof (resp));
#else
	rc = copyout((void*)&resp, (void*)cmd.response.r_addr, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_device: Error writing resp data (rc=%d)", rc);
		rc = EFAULT;
		goto out;
	}

	rc = DDI_SUCCESS;

out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_query_port
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing query port command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to query a device port attributes.
 */
/* ARGSUSED */
int
sol_uverbs_query_port(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_query_port		cmd;
	struct ib_uverbs_query_port_resp	resp;
	ibt_hca_portinfo_t			*port_info;
	uint_t					port_info_n;
	uint_t					port_info_size;
	int					rc;

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_port: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "sol_uverbs_query_port: %d",
	    cmd.port_num);

	if (!cmd.port_num || cmd.port_num > uctxt->hca->drv_hca_nports) {
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "query_port: Invalid port specified");

		rc = EINVAL;
		goto out;
	}

	rc = ibt_query_hca_ports(uctxt->hca->drv_hca_hdl, cmd.port_num,
	    &port_info, &port_info_n, &port_info_size);

	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_port: ibt_query_hca_ports() (rc=%d)", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		goto out;
	}
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "query_port: "
	    "port_num %d, port_info %x, lid %x, sm_lid %x",
	    cmd.port_num, port_info, port_info->p_opaque1,
	    port_info->p_sm_lid);

	(void) memset(&resp, 0, sizeof (resp));

	resp.state			= port_info->p_linkstate;
	resp.max_mtu			= port_info->p_mtu;
	resp.active_mtu			= port_info->p_mtu;
	resp.gid_tbl_len		= port_info->p_sgid_tbl_sz;
	resp.port_cap_flags  		= port_info->p_capabilities;
	resp.max_msg_sz			= port_info->p_msg_sz;
	resp.bad_pkey_cntr   		= port_info->p_pkey_violations;
	resp.qkey_viol_cntr  		= port_info->p_qkey_violations;
	resp.pkey_tbl_len    		= port_info->p_pkey_tbl_sz;
	resp.lid			= port_info->p_opaque1;
	resp.sm_lid			= port_info->p_sm_lid;
	resp.lmc			= port_info->p_lmc;
	resp.max_vl_num			= port_info->p_max_vl;
	resp.sm_sl			= port_info->p_sm_sl;
	resp.subnet_timeout  		= port_info->p_subnet_timeout;
	resp.init_type_reply 		= port_info->p_init_type_reply;
	resp.active_width    		= port_info->p_width_active;
	resp.active_speed    		= port_info->p_speed_active;
	resp.phys_state			= port_info->p_phys_state;

	ibt_free_portinfo(port_info, port_info_size);

#ifdef	_LP64
	rc = copyout((void*)&resp, (void*)cmd.response.r_laddr, sizeof (resp));
#else
	rc = copyout((void*)&resp, (void*)cmd.response.r_addr, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_port : copyout fail %x", rc);
		rc = EFAULT;
		goto out;
	}

	rc = DDI_SUCCESS;

out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_query_gid
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing query gid command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to query the device gid for the specified
 *	port and gid index.
 */
/* ARGSUSED */
int
sol_uverbs_query_gid(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_query_gid	cmd;
	void				*resp_p;
	uint8_t				*gid_p;
	ibt_hca_portinfo_t		*port_info;
	uint_t				port_info_n;
	uint_t				port_info_size;
	int				rc;
	uint64_t			temp;
	int				start_gid, num_gids, g;


	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "query_gid(%p, %p, %x, %x)", uctxt, buf, in_len, out_len);
	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_gid: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}
	(void) memcpy(&cmd, buf, sizeof (cmd));


	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "query_gid() : port_num %x, gid_index %x",
	    cmd.port_num, cmd.gid_index);

	/*
	 * If this is a table look up the the high bit of port is set
	 * and cmd.gid_index is the number of gids to query.
	 */
	if (cmd.port_num & 0x80) {
		cmd.port_num &= 0x7f;
		start_gid = 0;
		num_gids = cmd.gid_index;
	} else {
		start_gid = cmd.gid_index;
		num_gids = 1;
	}

	if (!cmd.port_num || cmd.port_num > uctxt->hca->drv_hca_nports) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_gid: Invalid port specified");
		rc = EINVAL;
		goto out;
	}

	if (out_len < num_gids * sizeof (struct ib_uverbs_query_gid_resp)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_gid: response buffer too small");
		rc = EINVAL;
		goto out;
	}

	rc = ibt_query_hca_ports(uctxt->hca->drv_hca_hdl, cmd.port_num,
	    &port_info, &port_info_n, &port_info_size);
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_gid: ibt_query_hca_ports() (rc=%d)", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		goto out;
	}

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "number of "
	    "gid entries %d", cmd.port_num, cmd.gid_index,
	    port_info->p_sgid_tbl_sz);

	if (start_gid < 0 || num_gids < 0 ||
	    (start_gid + num_gids) > port_info->p_sgid_tbl_sz) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_gid: query_gids start_gid %x, num_gids req %x "
		    "num gids present %x", start_gid, num_gids,
		    port_info->p_sgid_tbl_sz);
		rc = EINVAL;
		ibt_free_portinfo(port_info, port_info_size);
		goto out;
	}

	resp_p = kmem_zalloc(out_len, KM_SLEEP);
	gid_p = (uint8_t *)resp_p;
	for (g = start_gid; g < num_gids; g++) {
		temp =
		    htonll(port_info->p_sgid_tbl[g].gid.ucast_gid.ugid_prefix);
		(void) memcpy(gid_p, &temp, sizeof (temp));
		gid_p += 8;
		temp = htonll(port_info->p_sgid_tbl[g].gid.ucast_gid.ugid_guid);
		(void) memcpy(gid_p, &temp, sizeof (temp));
		gid_p += 8;
	}

	ibt_free_portinfo(port_info, port_info_size);

#ifdef	_LP64
	rc = copyout(resp_p, (void*)cmd.response.r_laddr, out_len);
#else
	rc = copyout(resp_p, (void*)cmd.response.r_addr, out_len);
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str, "query_gid: copyout %d",
		    rc);
		rc = EFAULT;
		kmem_free(resp_p, out_len);
		goto out;
	}

	kmem_free(resp_p, out_len);
	rc = 0;
out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_query_pkey
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing a query pkey command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to query a device for the pkey at the specified
 *	port and pkey index.
 */
/* ARGSUSED */
int
sol_uverbs_query_pkey(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_query_pkey	cmd;
	void 				*resp_p;
	uint16_t			*pkey_p;
	ibt_hca_portinfo_t		*port_info;
	uint_t				port_info_n;
	uint_t				port_info_size;
	int				rc;
	int				p, start_pkey, num_pkeys;

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_pkey: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}
	(void) memcpy(&cmd, buf, sizeof (cmd));

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "query_pkey: entry, port = %d, pkey index = %d",
	    cmd.port_num, cmd.pkey_index);

	/*
	 * If this is a table look up the the high bit of port is set
	 * and cmd.pkey_index is the number of pkeys to query.
	 */
	if (cmd.port_num & 0x80) {
		cmd.port_num &= 0x7f;
		start_pkey = 0;
		num_pkeys = cmd.pkey_index;
	} else {
		start_pkey = cmd.pkey_index;
		num_pkeys = 1;
	}

	if (!cmd.port_num || cmd.port_num > uctxt->hca->drv_hca_nports) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_pkey: Invalid port specified");

		rc = EINVAL;
		goto out;
	}

	/*
	 * For single pkey lookup the response buffer has a 16 bit
	 * reserved padding field. For table look up we don't use the
	 * padding/reserved field.
	 */
	if (num_pkeys == 1) {
		if (out_len < sizeof (struct ib_uverbs_query_pkey_resp)) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "query_pkey: response buffer too small");
			rc = EINVAL;
			goto out;
		}
	} else {
		if (out_len <
		    num_pkeys * (sizeof (struct ib_uverbs_query_pkey_resp)/2)) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "query_pkey: response buffer too small");
			rc = EINVAL;
			goto out;
		}
	}

	rc = ibt_query_hca_ports(uctxt->hca->drv_hca_hdl, cmd.port_num,
	    &port_info, &port_info_n, &port_info_size);
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_pkey: ibt_query_hca_ports() %d", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		goto out;
	}

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "query_pkey: port %d, requested index %d, number of pkey entries "
	    "%d", cmd.port_num, cmd.pkey_index, port_info->p_pkey_tbl_sz);

	if (start_pkey < 0 || num_pkeys < 0 ||
	    start_pkey + num_pkeys > port_info->p_pkey_tbl_sz) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "query_pkeys : pkey requested start %x, num %x, %x != "
		    "HCA num pkeys %x\n", start_pkey, num_pkeys,
		    port_info->p_pkey_tbl_sz);
			ibt_free_portinfo(port_info, port_info_size);
		rc = EINVAL;
		goto out;
	}

	resp_p = kmem_zalloc(out_len, KM_SLEEP);
	pkey_p = (uint16_t *)resp_p;
	for (p = start_pkey; p < num_pkeys; p++, pkey_p++)
		*pkey_p = port_info->p_pkey_tbl[p];

	ibt_free_portinfo(port_info, port_info_size);

#ifdef	_LP64
	rc = copyout(resp_p, (void*)cmd.response.r_laddr, out_len);
#else
	rc = copyout(resp_p, (void*)cmd.response.r_addr, out_len);
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str, "query_pkey: copyout %d",
		    rc);
		rc = EFAULT;
		kmem_free(resp_p, out_len);
		goto out;
	}
	kmem_free(resp_p, out_len);
	rc = 0;
out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_reg_mr
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to register a memory region.
 */
/* ARGSUSED */
int
sol_uverbs_reg_mr(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_reg_mr		cmd;
	struct ib_uverbs_reg_mr_resp	resp;
	uverbs_upd_uobj_t		*upd;
	uverbs_umr_uobj_t		*umr;
	ibt_mr_attr_t			new_mem_attr;
	ibt_mr_desc_t			new_mr_desc;
	int				rc;

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto out;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));
	(void) memset(&resp, 0, sizeof (resp));
	(void) memset(&new_mem_attr, 0, sizeof (new_mem_attr));
	(void) memset(&new_mr_desc, 0, sizeof (new_mr_desc));

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "reg_mr()");

	new_mem_attr.mr_vaddr	= cmd.start;
	new_mem_attr.mr_len	= cmd.length;
	new_mem_attr.mr_as	= curproc->p_as;
	new_mem_attr.mr_flags	= IBT_MR_NOSLEEP;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "reg_mr : "
	    "mr_vaddr 0x%0lX, mr_len %d, mr_as %d, mr_flags %d",
	    new_mem_attr.mr_vaddr, new_mem_attr.mr_len,
	    new_mem_attr.mr_as, new_mem_attr.mr_flags);

	if ((cmd.access_flags & IB_ACCESS_LOCAL_WRITE) ==
	    IB_ACCESS_LOCAL_WRITE) {
		new_mem_attr.mr_flags |= IBT_MR_ENABLE_LOCAL_WRITE;
	}
	if ((cmd.access_flags & IB_ACCESS_REMOTE_WRITE) ==
	    IB_ACCESS_REMOTE_WRITE) {
		new_mem_attr.mr_flags |= IBT_MR_ENABLE_REMOTE_WRITE;
	}
	if ((cmd.access_flags & IB_ACCESS_REMOTE_READ) ==
	    IB_ACCESS_REMOTE_READ) {
		new_mem_attr.mr_flags |= IBT_MR_ENABLE_REMOTE_READ;
	}
	if ((cmd.access_flags & IB_ACCESS_REMOTE_ATOMIC) ==
	    IB_ACCESS_REMOTE_ATOMIC) {
		new_mem_attr.mr_flags |= IBT_MR_ENABLE_REMOTE_ATOMIC;
	}
	if ((cmd.access_flags & IB_ACCESS_MW_BIND) == IB_ACCESS_MW_BIND) {
		new_mem_attr.mr_flags |= IBT_MR_ENABLE_WINDOW_BIND;
	}
	if ((cmd.access_flags & IB_ACCESS_SO) == IB_ACCESS_SO) {
		new_mem_attr.mr_flags |= IBT_MR_DISABLE_RO;
	}

	umr = kmem_zalloc(sizeof (*umr), KM_NOSLEEP);
	if (umr == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: User object mem allocation error");
		rc = ENOMEM;
		goto out;
	}
	sol_ofs_uobj_init(&umr->uobj, 0, SOL_UVERBS_UMR_UOBJ_TYPE);
	rw_enter(&umr->uobj.uo_lock, RW_WRITER);

	upd = uverbs_uobj_get_upd_read(cmd.pd_handle);
	if (upd == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: PD invalid");
		rc = EINVAL;
		goto bad_pd;
	}

	rc = ibt_register_mr(uctxt->hca->drv_hca_hdl, upd->pd,
	    &new_mem_attr, &umr->mr, &new_mr_desc);

	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: ibt_register_mr() (rc=%d)", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		umr->uobj.uo_uobj_sz = sizeof (uverbs_umr_uobj_t);
		goto err_register;
	}

	if (sol_ofs_uobj_add(&uverbs_umr_uo_tbl, &umr->uobj) != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: User object add failed");
		rc = ENOMEM;
		goto err_add_uobj;
	}

	resp.mr_handle  = umr->uobj.uo_id;
	resp.lkey	= new_mr_desc.md_lkey;
	resp.rkey	= new_mr_desc.md_rkey;

#ifdef	_LP64
	rc = copyout((void*)&resp, (void*)cmd.response.r_laddr, sizeof (resp));
#else
	rc = copyout((void*)&resp, (void*)cmd.response.r_addr, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: Error writing resp data (rc=%d)", rc);
		rc = EFAULT;
		goto err_response;
	}

	mutex_enter(&uctxt->lock);
	umr->list_entry  = add_genlist(&uctxt->mr_list, (uintptr_t)umr, uctxt);
	mutex_exit(&uctxt->lock);

	if (!umr->list_entry) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "reg_mr: Error adding umr to mr_list\n");
		rc = ENOMEM;
		goto err_response;
	}

	umr->uobj.uo_live = 1;
	rw_exit(&umr->uobj.uo_lock);

	sol_ofs_uobj_put(&upd->uobj);

	return (DDI_SUCCESS);

err_response:
	/*
	 * Need to set uo_live, so sol_ofs_uobj_remove() will
	 * remove the object from the object table.
	 */
	umr->uobj.uo_live = 1;
	(void) sol_ofs_uobj_remove(&uverbs_umr_uo_tbl, &umr->uobj);

err_add_uobj:
	if (ibt_deregister_mr(uctxt->hca->drv_hca_hdl, umr->mr) != IBT_SUCCESS)
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_reg_mr: ibt_deregister_mr(%p) failed", umr->mr);

err_register:
	sol_ofs_uobj_put(&upd->uobj);

bad_pd:
	rw_exit(&umr->uobj.uo_lock);
	sol_ofs_uobj_deref(&umr->uobj, sol_ofs_uobj_free);

out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_dereg_mr
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to de-register a memory region.
 */
/* ARGSUSED */
int
sol_uverbs_dereg_mr(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	struct ib_uverbs_dereg_mr	cmd;
	uverbs_umr_uobj_t		*umr;
	int				rc;

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "dereg_mr: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		goto err_out;
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "dereg_mr(mr_handle=%d)", cmd.mr_handle);

	umr = uverbs_uobj_get_umr_write(cmd.mr_handle);
	if (umr == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "dereg_mr: Invalid handle");
		rc = EINVAL;
		goto err_out;
	}

	rc = ibt_deregister_mr(uctxt->hca->drv_hca_hdl, umr->mr);

	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "dereg_mr: ibt_deregister_mr() (rc=%d)", rc);
		rc = sol_uverbs_ibt_to_kernel_status(rc);
		goto err_deregister;
	}

	/*
	 * Remove from the list of this contexts MR resources, then remove from
	 * the resource management table and the reference placed on the user
	 * object at MR creation.
	 */
	mutex_enter(&uctxt->lock);
	delete_genlist(&uctxt->mr_list, umr->list_entry);
	mutex_exit(&uctxt->lock);

	(void) sol_ofs_uobj_remove(&uverbs_umr_uo_tbl, &umr->uobj);

	/*
	 * Drop the lock and ref held by get_umr_write.
	 */
	sol_ofs_uobj_put(&umr->uobj);

	sol_ofs_uobj_deref(&umr->uobj, sol_ofs_uobj_free);

	cmd.mr_handle = 0;
	return (DDI_SUCCESS);

err_deregister:
	/*
	 * Drop the lock and ref held by get_umr_write.
	 */
	sol_ofs_uobj_put(&umr->uobj);

err_out:
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_create_ah
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to for devices that require kernel AH creation.
 */
/* ARGSUSED */
int
sol_uverbs_create_ah(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	SOL_OFS_DPRINTF_L3(sol_uverbs_dbg_str,
	    "create_ah: kernel user verb not implemented");
	return (ENOTSUP);
}

/*
 * Function:
 *	sol_uverbs_destroy_ah
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to for devices that require kernel AH deletion.
 */
/* ARGSUSED */
int
sol_uverbs_destroy_ah(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	SOL_OFS_DPRINTF_L3(sol_uverbs_dbg_str,
	    "destroy_ah: kernel user verb not implemented");
	return (ENOTSUP);
}

/*
 * Function:
 *	sol_uverbs_create_comp_channel
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb entry point to create a completion event channel.
 */
int
sol_uverbs_create_comp_channel(uverbs_uctxt_uobj_t *uctxt, char *buf,
    int in_len, int out_len)
{
	struct ib_uverbs_create_comp_channel		cmd;
	struct ib_uverbs_create_comp_channel_resp	resp;
	int						rc;
	minor_t						compl_id;
	uverbs_uctxt_uobj_t				*compl_uctxt;

	if (in_len < sizeof (cmd)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp_chan: in_len %x, cmd size %lx", in_len,
		    sizeof (cmd));
		rc = EINVAL;
		return (rc);
	}

	(void) memcpy(&cmd, buf, sizeof (cmd));
	(void) memset(&resp, 0, sizeof (resp));

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "create_comp_chan: entry, in_len=%d, out_len=%d",
	    in_len, out_len);

	/*
	 * libibverbs will have passed minor of the compl file in
	 * resp.fd. Use this to determine the uctxt created for
	 * completions.
	 */
#ifdef	_LP64
	rc = copyin((void*)cmd.response.r_laddr, (void*)&resp, sizeof (resp));
#else
	rc = copyin((void*)cmd.response.r_addr, (void*)&resp, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp: copyin (rc=%d)", rc);
		rc = EFAULT;
		return (rc);
	}
	compl_id = resp.fd;
	if (compl_id < SOL_UVERBS_DRIVER_MAX_MINOR) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp: Invalid compl user context id %x",
		    compl_id);
		return (ENXIO);
	}

	compl_uctxt = uverbs_uobj_get_uctxt_read(compl_id -
	    SOL_UVERBS_DRIVER_MAX_MINOR);
	if (compl_uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp: Failed get compl user context");
		return (ENXIO);
	}
	if (compl_uctxt->uctxt_type != SOL_UVERBS_UCTXT_EVENT ||
	    compl_uctxt->uctxt_verbs_id != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp_chan: Invalid user context - "
		    "possibly reused");
		return (ENXIO);
	}
	compl_uctxt->uctxt_type = SOL_UVERBS_UCTXT_COMPL;
	compl_uctxt->uctxt_verbs_id = uctxt->uobj.uo_id +
	    SOL_UVERBS_DRIVER_MAX_MINOR;
	uctxt->uctxt_comp_id = compl_id;
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "create_comp_chan: "
	    "uctxt %p, compl_uctxt %p, compl_id %x", uctxt,
	    compl_uctxt, compl_id);

	/*
	 * Allocate an event file to be used for completion
	 * event notification.
	 */
	compl_uctxt->comp_evfile = uverbs_alloc_event_file(uctxt, 0);
	if (compl_uctxt->comp_evfile == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp_chan: Event file alloc error");
		rc = EINVAL;
		sol_ofs_uobj_put(&compl_uctxt->uobj);
		return (rc);
	}

	/*
	 * Place an extra reference on the compl event file.  These will
	 * be used to handle the natural race of between the closing of
	 * the compl event file and uverbs device file that can occur.
	 */
	sol_ofs_uobj_ref(&compl_uctxt->comp_evfile->uobj);

	sol_ofs_uobj_put(&compl_uctxt->uobj);

#ifdef	_LP64
	rc = copyout((void*)&resp, (void*)cmd.response.r_laddr, sizeof (resp));
#else
	rc = copyout((void*)&resp, (void*)cmd.response.r_addr, sizeof (resp));
#endif
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "create_comp_chan: copyout %d", rc);
		rc = EFAULT;
		return (rc);
	}

	return (0);
}

/*
 * Function:
 *	sol_uverbs_dummy_command
 * Input:
 *	uctxt   - Pointer to the callers user context.
 *	buf     - Pointer to kernel buffer containing command.
 *	in_len  - Length in bytes of input command buffer.
 *	out_len - Length in bytes of output response buffer.
 * Output:
 *	The command output buffer is updated with command results.
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb generic place holder stub.
 */
/* ARGSUSED */
int
sol_uverbs_dummy_command(uverbs_uctxt_uobj_t *uctxt, char *buf, int in_len,
    int out_len)
{
	SOL_OFS_DPRINTF_L4(sol_uverbs_dbg_str,
	    "sol_uverbs_dummy_command invoked");

	return (0);
}

/*
 * Function:
 *	sol_uverbs_write
 * Input:
 *	dev	- Device number.
 *	uiop	- Pointer to the uio structure that describes the data (i.e.
 *                Solaris User Verbs command).
 *	credp	- A pointer to the user credentials for the I/O transaction.
 * Output:
 *	uiop	-
 * Returns:
 *	DDI_SUCCESS on success, else error code.
 * Description:
 * 	User verb write entry point.  A user deivce libraries use this
 *	entry point to execute a kernel agent user verbs call.  During
 *	the course of the call the user process will hold a read reference
 *	to the associated user context.
 */
#define	SOL_UVERBS_MAX_CMD_PAYLOAD    512
/* ARGSUSED */
static int
sol_uverbs_write(dev_t dev, struct uio *uiop, cred_t *credp)
{
	uverbs_uctxt_uobj_t		*uctxt;
	size_t				data_len, len;
	int				rc;
	struct ib_uverbs_cmd_hdr	hdr;
	char				payload[SOL_UVERBS_MAX_CMD_PAYLOAD];
	minor_t				id = getminor(dev);

	data_len = len = uiop->uio_resid;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_write: entry (len=%d)", len);

	ASSERT(id >= SOL_UVERBS_DRIVER_MAX_MINOR);

	uctxt = uverbs_uobj_get_uctxt_read(id - SOL_UVERBS_DRIVER_MAX_MINOR);
	if (uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Failed get user context");
		return (ENXIO);
	}

	if (uctxt->uctxt_type != SOL_UVERBS_UCTXT_VERBS) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: write() on invalid uctxt type %x",
		    uctxt->uctxt_type);
		rc = ENXIO;
		goto out;
	}

	if (len < sizeof (hdr)) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Header too small");
		rc =  EINVAL;
		goto out;
	}

	hdr.command	= -1;
	hdr.in_words	= 0;
	hdr.out_words	= 0;

	if (uiomove(&hdr, sizeof (hdr), UIO_WRITE, uiop) != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Error reading header");
		rc = EFAULT;
		goto out;
	}

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_write:  hdr.command   = %d", hdr.command);
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_write:  hdr.command   = %d", hdr.command);
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_write:  hdr.in_words  = %d", hdr.in_words);
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_write:  hdr.out_words = %d", hdr.out_words);

	if (hdr.in_words * 4 != len) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Invalid header size");
		rc = EINVAL;
		goto out;
	}

	if (hdr.command >=
	    sizeof (uverbs_cmd_table)/sizeof (uverbs_cmd_table[0]) ||
	    !uverbs_cmd_table[hdr.command]) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Invalid command (%d)", hdr.command);
		rc = EINVAL;
		goto out;
	}

	len -= sizeof (hdr);
	if (len > SOL_UVERBS_MAX_CMD_PAYLOAD) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Payload too large");
		rc = EINVAL;
		goto out;
	}

	if (uiomove(&payload, len, UIO_WRITE, uiop) != 0) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_write: Error reading payload");
		rc = EFAULT;
		goto out;
	}

#ifdef DEBUG
	unsigned int	*payload_int = (unsigned int *)payload;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "payload:   %08x,    %08x,    %08x,    %08x",
	    payload_int[0], payload_int[1],
	    payload_int[2], payload_int[3]);
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "payload:   %08x,    %08x,    %08x,    %08x",
	    payload_int[4], payload_int[5],
	    payload_int[6], payload_int[7]);
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "payload:   %08x,    %08x,    %08x,    %08x",
	    payload_int[8], payload_int[9],
	    payload_int[10], payload_int[11]);
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "payload:   %08x,    %08x,    %08x",
	    payload_int[12], payload_int[13], payload_int[14]);
#endif

	/*
	 * For Modify QP - Check privileges if priviliged Q_KEY
	 * is been set. This is for UD QPs only.
	 */
	if (hdr.command == IB_USER_VERBS_CMD_MODIFY_QP) {
		struct ib_uverbs_modify_qp	*modify_qpp;

		modify_qpp = (struct ib_uverbs_modify_qp *)payload;
		if ((modify_qpp->attr_mask & IB_QP_QKEY) &&
		    (modify_qpp->qkey & 0x80000000)) {
			uverbs_uqp_uobj_t	*uqp;

			uqp = uverbs_uobj_get_uqp_write(modify_qpp->qp_handle);
			if (uqp == NULL) {
				SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
				    "uverbs_write: modify_qp -"
				    "List lookup failure");
				rc = EINVAL;
				goto out;
			}
			if (uqp->ofa_qp_type == IB_QPT_UD &&
			    priv_policy(credp, PRIV_NET_PRIVADDR, B_FALSE,
			    EACCES, NULL) != 0) {
				SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
				    "uverbs_write: Setting privilege QKey "
				    "privilege failed");
				sol_ofs_uobj_put(&uqp->uobj);
				rc = EACCES;
				goto out;
			}
			sol_ofs_uobj_put(&uqp->uobj);
		}
	}

	rc = uverbs_cmd_table[hdr.command](uctxt, &payload[0], hdr.in_words * 4,
	    hdr.out_words * 4);

	if (rc)
		uiop->uio_resid += data_len;

out:
	sol_ofs_uobj_put(&uctxt->uobj);

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "uverbs_write: rc = %d", rc);

	return (rc);
}

static int
sol_uverbs_poll(dev_t dev, short events, int anyyet,
    short *reventsp, struct pollhead **phpp)
{
	minor_t			id = getminor(dev);
	uverbs_uctxt_uobj_t	*uctxt, *verbs_uctxt;
	int			rc;

#ifdef DEBUG
	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "uverbs_poll(%p, %x, %x, "
	    "%p, %p)", dev, events, anyyet, reventsp, phpp);
#endif

	ASSERT(id >= SOL_UVERBS_DRIVER_MAX_MINOR);

	uctxt = uverbs_uobj_get_uctxt_read(id - SOL_UVERBS_DRIVER_MAX_MINOR);
	if (uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_poll: Failed get user context");
		return (ENXIO);
	}

	if (uctxt->uctxt_verbs_id < SOL_UVERBS_DRIVER_MAX_MINOR) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_poll: Invalid Verbs user context id, %x",
		    uctxt->uctxt_verbs_id);
		sol_ofs_uobj_put(&uctxt->uobj);
		return (ENXIO);
	}
	verbs_uctxt = uverbs_uobj_get_uctxt_read(uctxt->uctxt_verbs_id
	    - SOL_UVERBS_DRIVER_MAX_MINOR);
	if (verbs_uctxt == NULL) {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_poll: Failed get verbs user context");
		sol_ofs_uobj_put(&uctxt->uobj);
		return (ENXIO);
	}
	if (uctxt->uctxt_type == SOL_UVERBS_UCTXT_ASYNC) {
		ASSERT(verbs_uctxt->async_evfile);
		rc = sol_uverbs_event_file_poll(verbs_uctxt->async_evfile,
		    events, anyyet, reventsp, phpp);
	} else if (uctxt->uctxt_type == SOL_UVERBS_UCTXT_COMPL) {
		ASSERT(uctxt->comp_evfile);
		rc = sol_uverbs_event_file_poll(uctxt->comp_evfile,
		    events, anyyet, reventsp, phpp);
	} else {
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "uverbs_poll: poll user context type %d",
		    uctxt->uctxt_type);
		rc = ENXIO;
	}

	sol_ofs_uobj_put(&verbs_uctxt->uobj);
	sol_ofs_uobj_put(&uctxt->uobj);
	return (rc);
}

/*
 * Function:
 *	sol_uverbs_alloc_uctxt
 * Input:
 *	devp	 - A pointer to the device number associated with the open.
 *	mod_ctxt - A pointer to the drivers module context.
 *	minor    - The minor device number.
 * Output:
 *	None.
 * Returns:
 *	On success a new user context user resource object associated with
 *	the device passed via devp. NULL on error.
 * Description:
 * 	Allocate a new user context user resource object and initialize it.
 *	The users asynchronous event file is created as part of this. On
 *	successful allocation, the user context is returned with the
 *	associated write lock enabled.
 */
static uverbs_uctxt_uobj_t *
sol_uverbs_alloc_uctxt(dev_t *devp, uverbs_module_context_t *mod_ctxt,
    minor_t minor)
{
	uverbs_uctxt_uobj_t *uctxt = NULL;

	uctxt = kmem_zalloc(sizeof (uverbs_uctxt_uobj_t), KM_SLEEP);
	ASSERT(uctxt != NULL);
	sol_ofs_uobj_init(&uctxt->uobj, 0, SOL_UVERBS_UCTXT_UOBJ_TYPE);
	rw_enter(&uctxt->uobj.uo_lock, RW_WRITER);
	if (sol_ofs_uobj_add(&uverbs_uctxt_uo_tbl, &uctxt->uobj) != 0) {
		/*
		 * The initialization routine set's the initial reference,
		 * we dereference the object here to clean it up.
		 */
		SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
		    "alloc_uctxt: Object add failed");
		rw_exit(&uctxt->uobj.uo_lock);
		sol_ofs_uobj_free(&uctxt->uobj);
		return (NULL);
	}

	/*
	 * Create the new clone for this user context using the
	 * object id as the minor number.   Note we offset beyond all
	 * real minor device numbers.
	 */
	*devp = makedevice(getmajor(*devp),
	    uctxt->uobj.uo_id + SOL_UVERBS_DRIVER_MAX_MINOR);

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str, "uverbs_open : "
	    "uctxt %p, minor %x- alloced", uctxt,
	    uctxt->uobj.uo_id + SOL_UVERBS_DRIVER_MAX_MINOR);

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "alloc_uctxt: user context allocated: %p, ref = %d",
	    (void *)uctxt, uctxt->uobj.uo_refcnt);

	mutex_init(&uctxt->lock, NULL, MUTEX_DRIVER, NULL);
	uctxt->mod_ctxt	= mod_ctxt;
	if (minor == SOL_UVERBS_DRIVER_EVENT_MINOR) {
		uctxt->uctxt_type = SOL_UVERBS_UCTXT_EVENT;
	} else {
		uctxt->uctxt_type = SOL_UVERBS_UCTXT_VERBS;
		uctxt->hca = &mod_ctxt->hcas[minor];
	}

	init_genlist(&uctxt->pd_list);
	init_genlist(&uctxt->mr_list);
	init_genlist(&uctxt->cq_list);
	init_genlist(&uctxt->srq_list);
	init_genlist(&uctxt->qp_list);
	init_genlist(&uctxt->ah_list);

	/* Return with uobj uo_lock held for WRITTER. */
	return (uctxt);
}

/*
 * Function:
 *	sol_uverbs_qpnum2uqpid
 * Input:
 *	qp_num	- used to find the user object that mapped to this qp_num
 * Output:
 *	None
 * Returns:
 *	DDI_FAILURE if not found else
 *	the uo_id in the user object that matches the qp_num
 * Description:
 * 	Find the uo_id of the user object which mapped to the input qp_num
 */
uint32_t
sol_uverbs_qpnum2uqpid(uint32_t qp_num)
{
	sol_ofs_uobj_table_t	*uo_tbl;
	sol_ofs_uobj_t		*uobj;
	uverbs_uqp_uobj_t	*uqp;
	int			i, j;
	sol_ofs_uobj_blk_t	*blk;

	uo_tbl = &uverbs_uqp_uo_tbl;
	rw_enter(&uo_tbl->uobj_tbl_lock, RW_READER);

	/*
	 * Try to find an empty slot for the new user object.
	 */
	for (i = 0; i < uo_tbl->uobj_tbl_used_blks; i++) {
		blk = uo_tbl->uobj_tbl_uo_root[i];
		if (blk != NULL) {
			for (j = 0; j < SOL_OFS_UO_BLKSZ; j++) {
				if ((uobj = blk->ofs_uoblk_blks[j]) != NULL) {
					uqp = (uverbs_uqp_uobj_t *)uobj;
					if (uqp->qp_num == qp_num) {
						rw_exit(&uo_tbl->uobj_tbl_lock);
						SOL_OFS_DPRINTF_L5(
						    sol_uverbs_dbg_str,
						    "qpnum2uqpid(%x) ret %x",
						    qp_num, uobj->uo_id);
						return (uobj->uo_id);
					}
				}
			}
		}
	}

	rw_exit(&uo_tbl->uobj_tbl_lock);
	SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str, "qpnum2uqpid(%x) ret %x",
	    qp_num, DDI_FAILURE);
	return (DDI_FAILURE);
}

void
sol_uverbs_get_clnt_hdl(void **ibclnt_hdl, void **iwclnt_hdl)
{
	*ibclnt_hdl = sol_uverbs_ib_clntp;
	*iwclnt_hdl = NULL;
}

void *
sol_uverbs_qpnum2qphdl(uint32_t qpnum)
{
	int32_t	uqpid;

	uqpid = sol_uverbs_qpnum2uqpid(qpnum);
	if (uqpid == DDI_FAILURE)
		return (NULL);
	return (sol_uverbs_uqpid_to_ibt_handle(uqpid));
}

int
sol_uverbs_disable_uqpn_modify(uint32_t qpnum)
{
	int32_t	uqpid;

	uqpid = sol_uverbs_qpnum2uqpid(qpnum);
	if (uqpid == DDI_FAILURE)
		return (-1);

	return (sol_uverbs_disable_user_qp_modify(uqpid));
}

extern int uverbs_uqpn_cq_ctrl(uint32_t, sol_uverbs_cq_ctrl_t);

int
sol_uverbs_uqpn_cq_ctrl(uint32_t qpnum, sol_uverbs_cq_ctrl_t ctrl)
{
	int32_t	uqpid;

	uqpid = sol_uverbs_qpnum2uqpid(qpnum);
	if (uqpid == DDI_FAILURE)
		return (-1);

	return (uverbs_uqpn_cq_ctrl(uqpid, ctrl));
}

void
sol_uverbs_set_qp_free_state(sol_uverbs_qp_free_state_t qp_free_state,
    uint32_t qpnum, void *qphdl)
{
	int32_t			uqpid;
	uverbs_uqp_uobj_t	*uqp;

	SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
	    "sol_uverbs_set_qp_free_state(%x, %x, %p)",
	    qp_free_state, qpnum, qphdl);
	if (qp_free_state == SOL_UVERBS2UCMA_DISABLE_QP_FREE) {
		uqpid = sol_uverbs_qpnum2uqpid(qpnum);
		if (uqpid == DDI_FAILURE) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "set_qp_free_state(%d)-invalid qpnum",
			    qpnum);
			return;
		}

		uqp = uverbs_uobj_get_uqp_write(uqpid);
		if (uqp == NULL) {
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "set_qp_free_state(%d)-uqp lookup failure", qpnum);
			return;
		}
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "set_qp_free_state : uqp %p, setting Disable QP Free", uqp);
		uqp->uqp_free_state  = SOL_UVERBS2UCMA_DISABLE_QP_FREE;
		sol_ofs_uobj_put(&uqp->uobj);
		return;
	}

	ASSERT(qphdl);
	uqp = (uverbs_uqp_uobj_t *)ibt_get_qp_private((ibt_qp_hdl_t)qphdl);
	ASSERT(uqp);
	if (uqp->uqp_free_state != SOL_UVERBS2UCMA_FREE_PENDING) {
		/*
		 * Enable free flag, so that close or userland free_qp
		 * call can free this in the future.
		 */
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "set_qp_free_state : uqp %p, setting Enable QP Free",
		    uqp);
		rw_enter(&(uqp->uobj.uo_lock), RW_WRITER);
		uqp->uqp_free_state = SOL_UVERBS2UCMA_ENABLE_QP_FREE;
		rw_exit(&(uqp->uobj.uo_lock));
	} else {
		/*
		 * uqp_free_state is set to FREE_PENDING, QP has been freed
		 * by userland. Call uverbs_uqp_free() to free this.
		 */
		SOL_OFS_DPRINTF_L5(sol_uverbs_dbg_str,
		    "set_qp_free_state : uqp %p calling uverbs_uqp_free()",
		    uqp);
		rw_enter(&(uqp->uobj.uo_lock), RW_WRITER);
		sol_ofs_uobj_ref(&uqp->uobj);
		if (uverbs_uqp_free(uqp, uqp->uctxt))
			SOL_OFS_DPRINTF_L2(sol_uverbs_dbg_str,
			    "set_qp_free_state : uverbs_uqp_free(%p) failed",
			    uqp);
	}
}

/*
 * Function:
 *	sol_uverbs_user_objects_init
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	None
 * Description:
 * 	Initializes all of the user object resource managment tables.
 */
static void sol_uverbs_user_objects_init()
{
	sol_ofs_uobj_tbl_init(&uverbs_uctxt_uo_tbl,
	    sizeof (uverbs_uctxt_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_upd_uo_tbl,
	    sizeof (uverbs_upd_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_umr_uo_tbl,
	    sizeof (uverbs_umr_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_ucq_uo_tbl,
	    sizeof (uverbs_ucq_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_usrq_uo_tbl,
	    sizeof (uverbs_usrq_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_uqp_uo_tbl,
	    sizeof (uverbs_uqp_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_uah_uo_tbl,
	    sizeof (uverbs_uah_uobj_t));
	sol_ofs_uobj_tbl_init(&uverbs_ufile_uo_tbl,
	    sizeof (uverbs_ufile_uobj_t));
}

/*
 * Function:
 *	sol_uverbs_user_objects_fini
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	None
 * Description:
 * 	Releases all of the user object resource managment tables.
 */
static void sol_uverbs_user_objects_fini()
{
	sol_ofs_uobj_tbl_fini(&uverbs_ufile_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_uah_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_uqp_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_usrq_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_ucq_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_umr_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_upd_uo_tbl);
	sol_ofs_uobj_tbl_fini(&uverbs_uctxt_uo_tbl);
}

/*
 * Function:
 *	sol_uverbs_ibt_to_kernel_status
 * Input:
 *	status	- An IBT status code.
 * Output:
 *	None
 * Returns:
 *	The "errno" based kernel error code the IBT status maps to.
 * Description:
 * 	Map an IBT status to the "errno" code that should be returned.
 */
int
sol_uverbs_ibt_to_kernel_status(ibt_status_t status)
{
	int err;

	switch (status) {
		case IBT_NOT_SUPPORTED:
			err = ENOTSUP;
			break;

		case IBT_ILLEGAL_OP:
		case IBT_INVALID_PARAM:
			err = EINVAL;
			break;

		case IBT_HCA_IN_USE:
		case IBT_HCA_BUSY_DETACHING:
		case IBT_HCA_BUSY_CLOSING:
		case IBT_CHAN_IN_USE:
		case IBT_CQ_BUSY:
		case IBT_MR_IN_USE:
		case IBT_PD_IN_USE:
		case IBT_SRQ_IN_USE:
			err = EBUSY;
			break;
		case	IBT_INSUFF_RESOURCE:
		case	IBT_INSUFF_KERNEL_RESOURCE:
		case	IBT_HCA_WR_EXCEEDED:
		case	IBT_HCA_SGL_EXCEEDED:
			err = ENOMEM;
			break;

		default:
			err = EINVAL;
	}
	return (err);
}

/* ARGSUSED */
uint32_t
sol_uverbs_ibt_to_of_device_cap_flags(ibt_hca_flags_t flags,
    ibt_hca_flags2_t flags2) {

	uint32_t of_flags = 0;

	if (flags & IBT_HCA_RESIZE_CHAN)
		of_flags |= IB_DEVICE_RESIZE_MAX_WR;

	if (flags & IBT_HCA_PKEY_CNTR)
		of_flags |= IB_DEVICE_BAD_PKEY_CNTR;

	if (flags & IBT_HCA_QKEY_CNTR)
		of_flags |= IB_DEVICE_BAD_QKEY_CNTR;

	if (flags & IBT_HCA_RAW_MULTICAST)
		of_flags |= IB_DEVICE_RAW_MULTI;

	if (flags & IBT_HCA_AUTO_PATH_MIG)
		of_flags |= IB_DEVICE_AUTO_PATH_MIG;

	if (flags & IBT_HCA_SQD_SQD_PORT)
		of_flags |= IB_DEVICE_CHANGE_PHY_PORT;

	if (flags & IBT_HCA_AH_PORT_CHECK)
		of_flags |= IB_DEVICE_UD_AV_PORT_ENFORCE;

	if (flags & IBT_HCA_CURRENT_QP_STATE)
		of_flags |= IB_DEVICE_CURR_QP_STATE_MOD;

	if (flags & IBT_HCA_SHUTDOWN_PORT)
		of_flags |= IB_DEVICE_SHUTDOWN_PORT;

	if (flags & IBT_HCA_INIT_TYPE)
		of_flags |= IB_DEVICE_INIT_TYPE;

	if (flags & IBT_HCA_PORT_UP)
		of_flags |= IB_DEVICE_PORT_ACTIVE_EVENT;

	if (flags & IBT_HCA_SI_GUID)
		of_flags |= IB_DEVICE_SYS_IMAGE_GUID;

	if (flags & IBT_HCA_RNR_NAK)
		of_flags |= IB_DEVICE_RC_RNR_NAK_GEN;

	if (flags & IBT_HCA_RESIZE_SRQ)
		of_flags |= IB_DEVICE_SRQ_RESIZE;

	if (flags & IBT_HCA_BASE_QUEUE_MGT)
		of_flags |= IB_DEVICE_N_NOTIFY_CQ;

	if (flags & IBT_HCA_ZERO_BASED_VA)
		of_flags |= IB_DEVICE_ZERO_STAG;

	if (flags & IBT_HCA_LOCAL_INVAL_FENCE)
		of_flags |= IB_DEVICE_SEND_W_INV;

	if (flags & IBT_HCA_MEM_WIN_TYPE_2B)
		of_flags |= IB_DEVICE_MEM_WINDOW;

	return (of_flags);
}

uint64_t
sol_uverbs_ibt_to_of_page_sz(ibt_page_sizes_t page_szs)
{

	uint64_t of_page_sz = 0;

	if (page_szs & IBT_PAGE_4K)
		of_page_sz |= 1LL << 12;

	if (page_szs & IBT_PAGE_8K)
		of_page_sz |= 1LL << 13;

	if (page_szs & IBT_PAGE_16K)
		of_page_sz |= 1LL << 14;

	if (page_szs & IBT_PAGE_32K)
		of_page_sz |= 1LL << 15;

	if (page_szs & IBT_PAGE_64K)
		of_page_sz |= 1LL << 16;

	if (page_szs & IBT_PAGE_128K)
		of_page_sz |= 1LL << 17;

	if (page_szs & IBT_PAGE_256K)
		of_page_sz |= 1LL << 18;

	if (page_szs & IBT_PAGE_512K)
		of_page_sz |= 1LL << 19;

	if (page_szs & IBT_PAGE_1M)
		of_page_sz |= 1LL << 20;

	if (page_szs & IBT_PAGE_2M)
		of_page_sz |= 1LL << 21;

	if (page_szs & IBT_PAGE_4M)
		of_page_sz |= 1LL << 22;

	if (page_szs & IBT_PAGE_8M)
		of_page_sz |= 1LL << 23;

	if (page_szs & IBT_PAGE_16M)
		of_page_sz |= 1LL << 24;

	if (page_szs & IBT_PAGE_32M)
		of_page_sz |= 1LL << 25;

	if (page_szs & IBT_PAGE_64M)
		of_page_sz |= 1LL << 26;

	if (page_szs & IBT_PAGE_128M)
		of_page_sz |= 1LL << 27;

	if (page_szs & IBT_PAGE_256M)
		of_page_sz |= 1LL << 28;

	if (page_szs & IBT_PAGE_512M)
		of_page_sz |= 1LL << 29;

	if (page_szs & IBT_PAGE_1G)
		of_page_sz |= 1LL << 30;

	if (page_szs & IBT_PAGE_2G)
		of_page_sz |= 1LL << 31;

	if (page_szs & IBT_PAGE_4G)
		of_page_sz |= 1LL << 32;

	if (page_szs & IBT_PAGE_8G)
		of_page_sz |= 1LL << 33;

	if (page_szs & IBT_PAGE_16G)
		of_page_sz |= 1LL << 34;

	return (of_page_sz);
}
