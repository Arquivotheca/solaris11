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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* Standard driver includes */
#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/time.h>

#include <sys/ib/clients/of/ofed_kernel.h>
#include <sys/ib/clients/of/sol_ofs/sol_ofs_common.h>
#include <sys/ib/clients/of/rdma/ib_verbs.h>

list_t		sol_ofs_dev_list;
kmutex_t	sol_ofs_dev_mutex;
kcondvar_t	sol_ofs_dev_cv;
ibt_clnt_hdl_t	sol_ofs_ibt_clnt;

char	*sol_ofs_dbg_str = "sol_ofs_mod";

void sol_ofs_async_hdlr(void *, ibt_hca_hdl_t, ibt_async_code_t,
    ibt_async_event_t *);
static void sol_ofs_init_dev_list();
static int  sol_ofs_fini_dev_list();

extern void sol_ofs_dprintf_init();
extern void sol_cma_init();
extern void sol_ofs_dprintf_fini();
extern int  sol_cma_fini();
extern void sol_cma_add_dev(struct ib_device *);
extern int  sol_cma_rem_dev(struct ib_device *);
extern struct ib_device *sol_kverbs_init_ib_dev(sol_ofs_dev_t *, void *,
    ib_guid_t, int *);
extern void kverbs_async_handler(void *, ibt_hca_hdl_t, ibt_async_code_t,
    ibt_async_event_t *);

/* Modload support */
static struct modlmisc sol_ofs_modmisc	= {
	&mod_miscops,
	"Solaris OFS Misc module"
};

struct modlinkage sol_ofs_modlinkage = {
	MODREV_1,
	(void *)&sol_ofs_modmisc,
	NULL
};

static ibt_clnt_modinfo_t ofs_ibt_clnt = {
	IBTI_V_CURR,		/* mi_ibt_version */
	IBT_GENERIC_MISC,	/* mi_clnt_class */
	sol_ofs_async_hdlr,	/* mi_async_handler */
	NULL,			/* mi_reserved */
	"sol_ofs"		/* mi_clnt_name */
};

#if !defined(offsetof)
#define	offsetof(s, m)		(size_t)(&(((s *)0)->m))
#endif

int
_init(void)
{
	ibt_status_t	status;

	sol_ofs_dprintf_init();
	SOL_OFS_DPRINTF_L5(sol_ofs_dbg_str, "_init()");
	mutex_init(&sol_ofs_dev_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&sol_ofs_dev_cv, NULL, CV_DEFAULT, NULL);
	list_create(&sol_ofs_dev_list, sizeof (sol_ofs_dev_t),
	    offsetof(sol_ofs_dev_t, ofs_dev_list));
	sol_cma_init();

	status = ibt_attach(&ofs_ibt_clnt, NULL, NULL, &sol_ofs_ibt_clnt);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "_init: ibt_attach() failed with status %d", status);
		if (sol_cma_fini() != 0) {
			SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
			    "_init: sol_cma_fini() failed");
		}
		list_destroy(&sol_ofs_dev_list);
		sol_ofs_dprintf_fini();
		return (EINVAL);
	}
	sol_ofs_init_dev_list();

	SOL_OFS_DPRINTF_L5(sol_ofs_dbg_str, "_init() - ret");
	return (0);
}

int
_fini(void)
{
	int		err;
	ibt_status_t	status;

	SOL_OFS_DPRINTF_L5(sol_ofs_dbg_str, "_fini()");

	err = sol_ofs_fini_dev_list();
	if (err != 0) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "_fini: device list close not successful");
		return (EBUSY);
	}

	status = ibt_detach(sol_ofs_ibt_clnt);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "_fini: ibt_detach() failed with status %d", status);
		sol_ofs_init_dev_list();
		return (EBUSY);
	}

	if ((err = sol_cma_fini()) != 0) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "_fini: sol_cma_fini failed");
		status = ibt_attach(&ofs_ibt_clnt, NULL, NULL,
		    &sol_ofs_ibt_clnt);
		ASSERT(status == IBT_SUCCESS);
		sol_ofs_init_dev_list();
		return (err);
	}

	if ((err = mod_remove(&sol_ofs_modlinkage)) != 0) {
		SOL_OFS_DPRINTF_L3(sol_ofs_dbg_str,
		    "_fini: mod_remove failed");
		sol_cma_init();
		status = ibt_attach(&ofs_ibt_clnt, NULL, NULL,
		    &sol_ofs_ibt_clnt);
		ASSERT(status == IBT_SUCCESS);
		sol_ofs_init_dev_list();
		return (err);
	}

	mutex_destroy(&sol_ofs_dev_mutex);
	cv_destroy(&sol_ofs_dev_cv);
	SOL_OFS_DPRINTF_L5(sol_ofs_dbg_str, "_fini() - ret");
	sol_ofs_dprintf_fini();
	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&sol_ofs_modlinkage, modinfop));
}

/*
 * Local functions.
 */
static void
init_ofusr_fields(sol_ofs_dev_t *ofs_devp, int num_ports)
{
	static	uint8_t		mthca_idx = 0, mlx4_idx = 0;
	static	uint8_t		hca_idx = 0;
	static	uint16_t	port_idx = 0;

	ofs_devp->ofs_dev_ofusr_hca_idx = hca_idx;
	ofs_devp->ofs_dev_ofusr_port_index = port_idx;
	if (ofs_devp->ofs_dev_drvr_nm == SOL_OFS_HCA_DRVR_TAVOR) {
		(void) snprintf(ofs_devp->ofs_dev_ofusr_name,
		    MAXNAMELEN, "mthca%d", mthca_idx);
		mthca_idx++;
	} else {
		(void) snprintf(ofs_devp->ofs_dev_ofusr_name,
		    MAXNAMELEN, "mlx4_%d", mlx4_idx);
		mlx4_idx++;
	}
	hca_idx++;
	port_idx += (2 * num_ports);
}

sol_ofs_dev_t *
sol_ofs_find_dev(dev_info_t *hca_dip, int num_ports, boolean_t do_alloc)
{
	sol_ofs_dev_t		*ofs_devp;
	const char		*dname;
	int			dinst;
	sol_ofs_hca_drvr_nm_t	drvr_name;

	dname = ddi_driver_name(hca_dip);
	dinst = ddi_get_instance(hca_dip);
	if (strcmp(dname, "tavor") == 0)
		drvr_name = SOL_OFS_HCA_DRVR_TAVOR;
	else if (strcmp(dname, "hermon") == 0)
		drvr_name = SOL_OFS_HCA_DRVR_HERMON;
	else {
		return (NULL);
	}

rescan_list:
	mutex_enter(&sol_ofs_dev_mutex);
	for (ofs_devp = list_head(&sol_ofs_dev_list); ofs_devp;
	    ofs_devp = list_next(&sol_ofs_dev_list, ofs_devp)) {
		if (ofs_devp->ofs_dev_drvr_nm == drvr_name &&
		    ofs_devp->ofs_dev_drvr_inst == dinst)
			break;
	}
	mutex_exit(&sol_ofs_dev_mutex);

	if (ofs_devp) {
		return (ofs_devp);
	} else if (!ofs_devp && do_alloc == B_TRUE) {
		/*
		 * sol_ofs has called for itself. Allocate
		 * a new ofs_devp.
		 */
		ofs_devp = kmem_zalloc(sizeof (sol_ofs_dev_t),
		    KM_SLEEP);
		mutex_init(&ofs_devp->ofs_dev_mutex, NULL,
		    MUTEX_DRIVER, NULL);

		mutex_enter(&sol_ofs_dev_mutex);
		mutex_enter(&ofs_devp->ofs_dev_mutex);
		ofs_devp->ofs_dev_state = SOL_OFS_DEV_ADDED;
		ofs_devp->ofs_dev_drvr_nm = drvr_name;
		(void) strncpy(ofs_devp->ofs_dev_hca_drv_name,
		    dname, strlen(dname));
		ofs_devp->ofs_dev_drvr_inst = dinst;
		init_ofusr_fields(ofs_devp, num_ports);
		mutex_exit(&ofs_devp->ofs_dev_mutex);
		list_insert_tail(&sol_ofs_dev_list, ofs_devp);
		cv_broadcast(&sol_ofs_dev_cv);
		mutex_exit(&sol_ofs_dev_mutex);
	} else {
		/*
		 * Called by sol_ofs client. ofs_devp not
		 * yet alloced for sol_ofs, for this HCA.
		 * wait and rescan.
		 */
		mutex_enter(&sol_ofs_dev_mutex);
		cv_wait(&sol_ofs_dev_cv, &sol_ofs_dev_mutex);
		mutex_exit(&sol_ofs_dev_mutex);
		goto rescan_list;
	}

	return (ofs_devp);
}

static sol_ofs_dev_t *
sol_ofs_add_hca(uint64_t guid)
{
	ibt_status_t		status;
	ibt_hca_hdl_t		hca_hdl;
	ibt_hca_attr_t		hca_attr;
	sol_ofs_dev_t		*ofs_devp;

	SOL_OFS_DPRINTF_L5(sol_ofs_dbg_str, "ofs_add_hca(0x%llx)",
	    guid);
	status = ibt_open_hca(sol_ofs_ibt_clnt, guid, &hca_hdl);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "ofs_add_hca: ibt_open_hca(%llx) failed %d",
		    guid, status);
		return (NULL);
	}
	status = ibt_query_hca(hca_hdl, &hca_attr);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "ofs_add_hca: ibt_query_hca failed %d",
		    status);
		status = ibt_close_hca(hca_hdl);
		if (status != IBT_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
			    "ofs_add_hca: ibt_close_hca failed %d",
			    status);
		}
		return (NULL);
	}
	ofs_devp = sol_ofs_find_dev(hca_attr.hca_dip,
	    hca_attr.hca_nports, B_TRUE);
	if (!ofs_devp) {
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "ofs_add_hca: ofs_devp ret NULL");
		status = ibt_close_hca(hca_hdl);
		if (status != IBT_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
			    "ofs_add_hca: ibt_close_hca failed %d",
			    status);
		}
		return (NULL);
	}

	mutex_enter(&ofs_devp->ofs_dev_mutex);
	ofs_devp->ofs_dev_hca_hdl = hca_hdl;
	ofs_devp->ofs_dev_nports = hca_attr.hca_nports;
	ofs_devp->ofs_dev_guid = guid;
	mutex_exit(&ofs_devp->ofs_dev_mutex);
	ibt_set_hca_private(hca_hdl, ofs_devp);
	return (ofs_devp);
}

static void
sol_ofs_init_dev_list()
{
	uint_t			i, nhcas;
	ib_guid_t		*guidp;
	sol_ofs_dev_t		*ofs_devp;
	struct ib_device	*ib_devp;

	nhcas = ibt_get_hca_list(&guidp);
	for (i = 0; i < nhcas; i++) {
		ofs_devp = sol_ofs_add_hca(guidp[i]);
		if (!ofs_devp)
			continue;
		ib_devp = sol_kverbs_init_ib_dev(ofs_devp, NULL,
		    guidp[i], NULL);
		mutex_enter(&ofs_devp->ofs_dev_mutex);
		ofs_devp->ofs_dev_ib_device = (void *)ib_devp;
		if (ib_devp)
			sol_cma_add_dev(ib_devp);
		mutex_exit(&ofs_devp->ofs_dev_mutex);
	}
	ibt_free_hca_list(guidp, nhcas);
}

/*
 * This is the common async handler for both sol_ofs clients
 * and for sol_ofs itself. sol_ofs calls ibt_attach passing
 * NULL as the arg for async handler. For sol_ofs clients,
 * the ofs_client * is the arg passed to ibt_attach().
 *
 * If arg != NULL, this is for sol_ofs client, call the kverbs
 * async handler. If not, this is for sol_ofs. handle the event.
 */
void
sol_ofs_async_hdlr(void *arg, ibt_hca_hdl_t hca_hdl,
    ibt_async_code_t code, ibt_async_event_t *eventp)
{
	sol_ofs_dev_t		*ofs_devp;
	struct ib_device	*ib_devp;
	ibt_status_t		status;
	int			rc;

	SOL_OFS_DPRINTF_L5(sol_ofs_dbg_str,
	    "ofs_async_hdlr(%p, %p, %x, %p)",
	    arg, hca_hdl, code, eventp);

	if (arg != NULL) {
		kverbs_async_handler(arg, hca_hdl, code, eventp);
		return;
	}

	switch (code) {
	case IBT_HCA_ATTACH_EVENT:
		ofs_devp = sol_ofs_add_hca(eventp->ev_hca_guid);
		if (!ofs_devp)
			return;
		ib_devp = sol_kverbs_init_ib_dev(ofs_devp, NULL,
		    eventp->ev_hca_guid, NULL);
		mutex_enter(&ofs_devp->ofs_dev_mutex);
		ofs_devp->ofs_dev_ib_device = (void *)ib_devp;
		if (ib_devp)
			sol_cma_add_dev(ib_devp);
		mutex_exit(&ofs_devp->ofs_dev_mutex);
		break;
	case IBT_HCA_DETACH_EVENT:
		ofs_devp = ibt_get_hca_private(hca_hdl);
		ASSERT(ofs_devp);

		/*
		 * Wait for (*remove) callbacks to the
		 * sol_ofs clients to be invoked.
		 */
		mutex_enter(&sol_ofs_dev_mutex);
		while (ofs_devp->ofs_dev_client_cnt) {
			SOL_OFS_DPRINTF_L3(sol_ofs_dbg_str,
			    "ofs_async_hdlr: ofs_devp %p, "
			    "waiting client cnt %x",
			    ofs_devp, ofs_devp->ofs_dev_client_cnt);
			cv_wait(&sol_ofs_dev_cv, &sol_ofs_dev_mutex);
		}
		mutex_exit(&sol_ofs_dev_mutex);

		mutex_enter(&ofs_devp->ofs_dev_mutex);
		ib_devp =
		    (struct ib_device *)ofs_devp->ofs_dev_ib_device;
		if (ib_devp) {
			rc = sol_cma_rem_dev(ib_devp);
			if (rc) {
				SOL_OFS_DPRINTF_L3(sol_ofs_dbg_str,
				    "ofs_async_hdlr : HCA_DETACH "
				    "ib_devp %p, cma_busy", ib_devp);
				mutex_exit(&ofs_devp->ofs_dev_mutex);
				return;
			}
			ofs_devp->ofs_dev_ib_device = NULL;
			kmem_free(ib_devp->impl_data,
			    sizeof (ib_device_impl_t));
			ib_devp->impl_data = NULL;
			kmem_free(ib_devp, sizeof (struct ib_device));
		}
		if (ofs_devp->ofs_dev_hca_hdl) {
			status = ibt_close_hca(
			    ofs_devp->ofs_dev_hca_hdl);
			if (status != IBT_SUCCESS)  {
				SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
				    "ofs_async_hdlr: ibt_close_hca:"
				    " failed with status %d", status);
				mutex_exit(&ofs_devp->ofs_dev_mutex);
				return;
			}
		}
		ofs_devp->ofs_dev_hca_hdl = NULL;
		ofs_devp->ofs_dev_state = SOL_OFS_DEV_REMOVED;
		ofs_devp->ofs_dev_ib_device = NULL;
		mutex_exit(&ofs_devp->ofs_dev_mutex);

		break;
	case IBT_ERROR_PORT_DOWN:
		/* FALLTHRU */
	case IBT_EVENT_PORT_UP:
		/* FALLTHRU */
	case IBT_PORT_CHANGE_EVENT:
		/* FALLTHRU */
	case IBT_CLNT_REREG_EVENT:
		/* Ignore these events for sol_ofs. */
		break;
	default:
		SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
		    "ofs_async_hdlr: Unexpected event %x", code);
		break;
	}
}

static int
sol_ofs_fini_dev_list()
{
	sol_ofs_dev_t		*ofs_devp;
	struct ib_device	*ib_devp;
	ibt_status_t		status;
	int			rc;

	mutex_enter(&sol_ofs_dev_mutex);
	while (ofs_devp = list_head(&sol_ofs_dev_list)) {
		mutex_enter(&ofs_devp->ofs_dev_mutex);
		ib_devp =
		    (struct ib_device *)ofs_devp->ofs_dev_ib_device;
		if (ib_devp) {
			rc = sol_cma_rem_dev(ib_devp);
			if (rc) {
				SOL_OFS_DPRINTF_L3(sol_ofs_dbg_str,
				    "ofs_fini_dev_list: ib_device %p"
				    " cma busy", ib_devp);
				mutex_exit(&ofs_devp->ofs_dev_mutex);
				mutex_exit(&sol_ofs_dev_mutex);
				return (EBUSY);
			}
			ofs_devp->ofs_dev_ib_device = NULL;
			kmem_free(ib_devp->impl_data,
			    sizeof (ib_device_impl_t));
			ib_devp->impl_data = NULL;
			kmem_free(ib_devp, sizeof (struct ib_device));
		}
		if (ofs_devp->ofs_dev_hca_hdl) {
			status = ibt_close_hca(
			    ofs_devp->ofs_dev_hca_hdl);
			if (status != IBT_SUCCESS)  {
				SOL_OFS_DPRINTF_L2(sol_ofs_dbg_str,
				    "ofs_async_hdlr: ibt_close_hca:"
				    " failed with status %d", status);
				mutex_exit(&ofs_devp->ofs_dev_mutex);
				mutex_exit(&sol_ofs_dev_mutex);
				return (EBUSY);
			}
		}
		ofs_devp->ofs_dev_hca_hdl = NULL;
		ofs_devp->ofs_dev_state = SOL_OFS_DEV_REMOVED;
		ofs_devp->ofs_dev_ib_device = NULL;
		mutex_exit(&ofs_devp->ofs_dev_mutex);
		(void) list_remove_head(&sol_ofs_dev_list);
	}
	mutex_exit(&sol_ofs_dev_mutex);
	return (0);
}
