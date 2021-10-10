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

/* Solaris Open Fabric kernel verbs */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/ib/clients/of/rdma/ib_verbs.h>
#include <sys/ib/clients/of/rdma/ib_addr.h>
#include <sys/ib/clients/of/rdma/rdma_cm.h>
#include <sys/ib/clients/of/sol_ofs/sol_kverb_impl.h>
#include <sys/ib/clients/of/sol_ofs/sol_kverb2cma.h>

static void *statep;
char *sol_kverbs_dbg_str = "sol_kverbs";

static llist_head_t client_list = LLIST_HEAD_INIT(client_list);
kmutex_t clist_lock; /* mutex for client_list */

extern kmutex_t		sol_ofs_dev_mutex;
extern kcondvar_t	sol_ofs_dev_cv;

void kverbs_async_handler(void *, ibt_hca_hdl_t, ibt_async_code_t,
    ibt_async_event_t *);
extern void sol_ofs_async_hdlr(void *, ibt_hca_hdl_t, ibt_async_code_t,
    ibt_async_event_t *);
struct ib_device *sol_kverbs_init_ib_dev(sol_ofs_dev_t *, void *,
    ib_guid_t, int *);

/*
 * set ibt_client_t members. clnt->ib_client must be set before
 * this func is called.
 */
static int
alloc_ibt_client(ofs_client_t *clnt)
{
	int namelen;
	ASSERT(clnt->ib_client != NULL);

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "alloc_ibt_client: client: 0x%p", clnt);

	/*
	 * double-check the name string. if it's longer than MAXNAMELEN
	 * including the string terminator, assuming the name is invalid,
	 * return EINVAL.
	 */
	namelen = strlen(clnt->ib_client->name);
	if (namelen >= MAXNAMELEN) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "alloc_ibt_client: client: 0x%p => "
		    "namelen(%d) is larger than MAXNAMELEN", clnt, namelen);
		return (-EINVAL);
	}
	clnt->ibt_client.mi_clnt_name = kmem_zalloc(namelen + 1, KM_NOSLEEP);
	if (clnt->ibt_client.mi_clnt_name == NULL) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "alloc_ibt_client: client: 0x%p => "
		    "no sufficient memory", clnt);
		return (-ENOMEM);
	}
	bcopy(clnt->ib_client->name, clnt->ibt_client.mi_clnt_name, namelen);
	clnt->ibt_client.mi_ibt_version = IBTI_V_CURR;
	if (clnt->ib_client->dip) {
		if (strncmp(clnt->ibt_client.mi_clnt_name, "sol_uverbs",
		    namelen) == 0) {
			clnt->ibt_client.mi_clnt_class = IBT_USER;
		} else if (strncmp(clnt->ibt_client.mi_clnt_name,
		    "sol_umad", namelen) == 0) {
			clnt->ibt_client.mi_clnt_class = IBT_USER;
		} else {
			clnt->ibt_client.mi_clnt_class = IBT_GENERIC;
		}
	} else {
		clnt->ibt_client.mi_clnt_class = IBT_GENERIC_MISC;
	}
	clnt->ibt_client.mi_async_handler = sol_ofs_async_hdlr;

	return (0);
}

static void
free_ibt_client(ofs_client_t *clnt)
{
	int namelen = strlen(clnt->ib_client->name);
	ASSERT(namelen < MAXNAMELEN);

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "free_ibt_client: client: 0x%p", clnt);

	kmem_free(clnt->ibt_client.mi_clnt_name, namelen + 1);
	clnt->ibt_client.mi_clnt_name = NULL;
}

/*
 * get_device() returns a pointer to struct ib_devcie with
 * the same guid as one passed to the function.
 */
static ib_device_t *
get_device(ofs_client_t *ofs_client, ib_guid_t guid)
{
	ib_device_t *device;
	llist_head_t *entry;

	ASSERT(RW_LOCK_HELD(&ofs_client->lock));

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "get_device: client: 0x%p, guid:0x%p",
	    ofs_client, (void *)(uintptr_t)htonll(guid));

	list_for_each(entry, &ofs_client->device_list) {
		device = entry->ptr;
		SOL_OFS_DPRINTF_L5(sol_kverbs_dbg_str, "0x%p 0x%p 0x%p",
		    device,
		    (void *)(uintptr_t)(device->node_guid),
		    (void *)(uintptr_t)htonll(guid));
		if (device->node_guid == htonll(guid)) {
			ASSERT(device->node_type == RDMA_NODE_IB_CA);
			ASSERT(device->clnt_hdl == (ofs_client_p_t)ofs_client);
			return (device);
		}
	}

	SOL_OFS_DPRINTF_L5(sol_kverbs_dbg_str,
	    "get_device: client: 0x%p, guid:0x%p => no match guid",
	    ofs_client, (void *)(uintptr_t)htonll(guid));

	return (NULL);
}

static enum ib_event_type kverbs_ibt2ofed_evnt(ibt_async_code_t,
    boolean_t *, boolean_t *, boolean_t *);

/*
 * kverbs_async_handler() is a delegated function to handle asynchronous
 * events,  which dispatches each event to corresponding qp/cq handlers
 * registered with ib_create_qp() and/or ib_create_cq().
 */
void
kverbs_async_handler(void *clntp, ibt_hca_hdl_t hdl, ibt_async_code_t code,
    ibt_async_event_t *event)
{
	ofs_client_t 		*ofs_client = (ofs_client_t *)clntp;
	struct ib_event 	ib_event;
	struct ib_qp 		*qpp;
	struct ib_cq		*cqp;
	enum ib_event_type	evnt_type;
	boolean_t		qp_evnt, cq_evnt, srq_evnt;
	sol_ofs_dev_t		*ofs_devp;

	ASSERT(ofs_client != NULL);

	if (code == IBT_HCA_ATTACH_EVENT) {
		ib_device_t	*device;
		int		rtn;

		rw_enter(&ofs_client->lock, RW_WRITER);
		device = sol_kverbs_init_ib_dev(NULL, ofs_client,
		    event->ev_hca_guid, &rtn);
		if (device == NULL)
			SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
			    "HCA_ATTACH: ib_device init failed");
		rw_exit(&ofs_client->lock);
		return;
	} else if (code == IBT_HCA_DETACH_EVENT) {
		struct ib_device 	*device;
		ib_device_impl_t	*impl_devp;

		rw_enter(&ofs_client->lock, RW_WRITER);
		device = ibt_get_hca_private(hdl);
		ASSERT(device);
		impl_devp =
		    (ib_device_impl_t *)device->impl_data;
		ASSERT(impl_devp);
		if (device->reg_state == IB_DEV_OPEN) {
			ibt_status_t		rtn;

			mutex_enter(&impl_devp->dev_impl_mutex);
			impl_devp->dev_impl_evt_ref--;
			while (impl_devp->dev_impl_evt_ref) {
				cv_wait(&impl_devp->dev_impl_cv,
				    &impl_devp->dev_impl_mutex);
			}
			mutex_exit(&impl_devp->dev_impl_mutex);

			/* invoke client's callback */
			if (ofs_client->ib_client->remove) {
				ofs_client->ib_client->remove(device);
			}

			/*
			 * Decrement the sol_ofs cnt of clients for
			 * this HCA.
			 */
			ofs_devp =
			    (sol_ofs_dev_t *)impl_devp->dev_impl_ofs_devp;

			ASSERT(ofs_devp);
			ASSERT(ofs_devp->ofs_dev_client_cnt);
			mutex_enter(&sol_ofs_dev_mutex);
			SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
			    "kverbs_async_hdlr: ofs_devp %p, "
			    "client_cnt %x decr, client nm %s",
			    ofs_devp, ofs_devp->ofs_dev_client_cnt,
			    ofs_client->ib_client->name);

			ofs_devp->ofs_dev_client_cnt--;
			if (!ofs_devp->ofs_dev_client_cnt)
				cv_broadcast(&sol_ofs_dev_cv);
			mutex_exit(&sol_ofs_dev_mutex);

			if (device->connect_qp_cnt) {
				device->reg_state = IB_DEV_UNREGISTERED;
				rw_exit(&ofs_client->lock);
				return;
			}

			/* close this HCA */
			rtn = ibt_close_hca(device->hca_hdl);
			if (rtn != IBT_SUCCESS) {
				SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
				    "kverbs_async_handler() - "
				    "ibt_close_hca(%p) failed %x",
				    device->hca_hdl, rtn);
				device->close_failed_state = B_TRUE;
				rw_exit(&ofs_client->lock);
				return;
			}
			/* change the state only */
			device->reg_state = IB_DEV_CLOSE;
			device->close_failed_state = B_FALSE;
			ASSERT(rtn == IBT_SUCCESS);
			ofs_client->hca_open_num--;
		}
		rw_exit(&ofs_client->lock);
		return;
	}

	evnt_type = kverbs_ibt2ofed_evnt(code, &qp_evnt, &cq_evnt,
	    &srq_evnt);
	if (ofs_client->client_not_kverbs == B_TRUE) {
		struct ib_event	client_event;

		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ofs_async_handler: calling driver evnt hdlr "
		    " for event code 0x%x", code);
		client_event.device = NULL;
		client_event.element.sol_ib_evt.sol_evt_hca_hdl = hdl;
		client_event.element.sol_ib_evt.sol_evt_event =  event;
		client_event.event = evnt_type;
		if (ofs_client->clnt_async_hdlr)
			(*ofs_client->clnt_async_hdlr)(
			    &ofs_client->client_ib_event_hdlr,
			    &client_event);

		return;
	}

	cqp = event->ev_cq_hdl ? ibt_get_cq_private(event->ev_cq_hdl) : NULL;
	qpp = event->ev_chan_hdl ?
	    ibt_get_qp_private(event->ev_chan_hdl) : NULL;

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ofs_async_handler: client: 0x%p, hca_hdl: 0x%p, code:0x%x, "
	    "event->qp: 0x%p, event->cq: 0x%p, event->srq: 0x%p "
	    "event->guid: 0x%p, event->port: 0x%x",
	    clntp, hdl, code, qpp, cqp, event->ev_srq_hdl,
	    (void *)(uintptr_t)event->ev_hca_guid, event->ev_port);

	bzero(&ib_event, sizeof (struct ib_event));
	if (qp_evnt == B_TRUE) {
		FIRE_QP_EVENT(ofs_client, hdl, ib_event, qpp,
		    evnt_type);
		return;
	} else if (cq_evnt == B_TRUE) {
		FIRE_CQ_EVENT(ofs_client, hdl, ib_event, cqp,
		    evnt_type);
		return;
	} else if (srq_evnt == B_TRUE) {
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "sol_ofs does not support SRQ event(0x%x).\n"
		    "\t clntp=0x%p, hca_hdl=0x%p, code=%d, eventp=0x%p\n",
		    code, clntp, hdl, code, event);
		return;
	} else  if (ofs_client->clnt_async_hdlr) {
		struct ib_event	client_event;

		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ofs_async_handler: calling driver evnt hdlr "
		    " for event code 0x%x", code);
		if ((evnt_type == IB_EVENT_PORT_ERR) ||
		    (evnt_type == IB_EVENT_PORT_ACTIVE)) {
			rw_enter(&ofs_client->lock, RW_READER);
			client_event.device = get_device(ofs_client,
			    event->ev_hca_guid);
			rw_exit(&ofs_client->lock);
			client_event.element.port_num = event->ev_port;
		} else {
			client_event.device = NULL;
			client_event.element.sol_ib_evt.sol_evt_hca_hdl = hdl;
			client_event.element.sol_ib_evt.sol_evt_event = event;
		}

		client_event.event = evnt_type;
		(*ofs_client->clnt_async_hdlr)(
		    &ofs_client->client_ib_event_hdlr, &client_event);
		return;
	} else {
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "sol_ofs does not support this event(0x%x).\n"
		    "\t clntp=0x%p, hca_hdl=0x%p, code=%d, eventp=0x%p\n",
		    code, clntp, hdl, code, event);
		return;
	}
}

/*
 * ib_register_client - Register an IB client
 * @client:Client to register
 *
 * Upper level users of the IB drivers can use ib_register_client() to
 * register callbacks for IB device addition and removal.  When an IB
 * device is added, each registered client's add method will be called
 * (in the order the clients were registered), and when a device is
 * removed, each client's remove method will be called (in the reverse
 * order that clients were registered).  In addition, when
 * ib_register_client() is called, the client will receive an add
 * callback for all devices already registered.
 *
 * Note that struct ib_client should have a dip pointer to the client,
 * which is different from the Linux implementation.
 */
int
ib_register_client(struct ib_client *client)
{
	ofs_client_t	*ofs_client;
	llist_head_t	*entry, *tmp;
	ib_device_t	*device;
	int		rtn;
	ibt_status_t	status;
	sol_ofs_dev_t	*ofs_devp;

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_register_client: client: 0x%p", client);

	/* get the number of HCAs on this system */
	mutex_enter(&sol_ofs_dev_mutex);
	if (list_is_empty(&sol_ofs_dev_list)) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => no HCA", client);
		mutex_exit(&sol_ofs_dev_mutex);
		return (-ENXIO);
	}
	mutex_exit(&sol_ofs_dev_mutex);

	/* allocate a new sol_ofs_client structure */
	ofs_client = kmem_zalloc(sizeof (ofs_client_t), KM_NOSLEEP);
	if (ofs_client == NULL) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => "
		    "no sufficient memory for ofs_client", client);
		return (-ENOMEM);
	}

	/* set members */
	ofs_client->ib_client = client;
	if ((rtn = alloc_ibt_client(ofs_client)) != 0) {
		kmem_free(ofs_client, sizeof (ofs_client_t));
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => "
		    "alloc_ibt_client failed w/ 0x%x", client, rtn);
		return (rtn);
	}
	ofs_client->state = IB_OFS_CLNT_INITIALIZED;
	llist_head_init(&ofs_client->device_list, NULL);
	llist_head_init(&ofs_client->client_list, ofs_client);
	rw_init(&ofs_client->lock, NULL, RW_DEFAULT, NULL);
	if (strcmp(client->name, "sol_uverbs") == 0 ||
	    strcmp(client->name, "sol_umad") == 0)
		ofs_client->client_not_kverbs = B_TRUE;

	/* initialize IB client */
	rw_enter(&ofs_client->lock, RW_WRITER);
	if (client->state != IB_CLNT_UNINITIALIZED) {
		rw_exit(&ofs_client->lock);
		kmem_free(ofs_client, sizeof (ofs_client_t));
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => "
		    "invalid client state(%d)", client, client->state);
		return (-EPERM);
	}

	/* attach this client to IBTF */
	status = ibt_attach(&ofs_client->ibt_client, client->dip, ofs_client,
	    &ofs_client->ibt_hdl);
	if (status != IBT_SUCCESS) {
		rw_exit(&ofs_client->lock);
		free_ibt_client(ofs_client);
		kmem_free(ofs_client, sizeof (ofs_client_t));
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => "
		    "ibt_attach failed w/ 0x%x", client, status);
		return (-EINVAL);
	}
	client->clnt_hdl = (ofs_client_p_t)ofs_client;
	client->state = IB_CLNT_INITIALIZED;

	/* link this client */
	mutex_enter(&clist_lock);
	llist_add_tail(&ofs_client->client_list, &client_list);
	mutex_exit(&clist_lock);

	/* Open HCAs */
	mutex_enter(&sol_ofs_dev_mutex);
	for (ofs_devp = list_head(&sol_ofs_dev_list); ofs_devp;
	    ofs_devp = list_next(&sol_ofs_dev_list, ofs_devp)) {
		mutex_exit(&sol_ofs_dev_mutex);
		/* allocate the ib_device structure */
		device = sol_kverbs_init_ib_dev(ofs_devp, ofs_client,
		    ofs_devp->ofs_dev_guid, &rtn);
		mutex_enter(&sol_ofs_dev_mutex);
		if (device == NULL) {
			mutex_exit(&sol_ofs_dev_mutex);
			goto err;
		}
	}
	mutex_exit(&sol_ofs_dev_mutex);
	if (ofs_client->hca_open_num == 0) {
		rtn = -ENXIO;
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => "
		    "no available HCA", client);
		goto err;
	}
	rw_exit(&ofs_client->lock);

	return (0);

err:
	/* first close all open HCAs */
	list_for_each(entry, &ofs_client->device_list) {
		device = entry->ptr;
		/*
		 * If it's open already, close it after the remove
		 * callback.
		 */
		if (device->reg_state == IB_DEV_OPEN) {
			ibt_status_t rtn;
			/* invoke client's callback */
			if (client->remove) {
				client->remove(device);
			}
			rtn = ibt_close_hca(device->hca_hdl);
			if (rtn != IBT_SUCCESS) {
				SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
				    "ofs_async_handler() - "
				    "ibt_close_hca(%p) failed %x",
				    rtn);
				device->close_failed_state = B_TRUE;
				rw_exit(&ofs_client->lock);
				return (-EBUSY);
			}
			device->reg_state = IB_DEV_CLOSE;
			device->close_failed_state = B_FALSE;
			ofs_client->hca_open_num--;
		}
	}
	ASSERT(ofs_client->hca_open_num == 0);

	/* then free the devices */
	list_for_each_safe(entry, tmp, &ofs_client->device_list) {
		device = entry->ptr;
		/* de-link and free the device */
		llist_del(entry);
		if (device->impl_data) {
			kmem_free(device->impl_data,
			    sizeof (ib_device_impl_t));
			device->impl_data = NULL;
		}
		kmem_free(device, sizeof (ib_device_t));
		ofs_client->hca_num--;
	}
	ASSERT(ofs_client->hca_num == 0);

	/* delink this client */
	mutex_enter(&clist_lock);
	llist_del(&ofs_client->client_list);
	mutex_exit(&clist_lock);

	/* detach the client */
	client->clnt_hdl = NULL;
	client->state = IB_CLNT_UNINITIALIZED;
	if ((status = ibt_detach(ofs_client->ibt_hdl)) != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_client: client: 0x%p => "
		    "ibt_detach() failed with status %d!!",
		    client, status);
		rw_exit(&ofs_client->lock);
		return (-EIO);
	}
	rw_exit(&ofs_client->lock);

	/* free sol_ofs_client */
	free_ibt_client(ofs_client);
	kmem_free(ofs_client, sizeof (ofs_client_t));

	return (rtn);
}

/*
 * ib_unregister_client - Unregister an IB client
 * @client:Client to unregister
 *
 * Upper level users use ib_unregister_client() to remove their client
 * registration.  When ib_unregister_client() is called, the client
 * will receive a remove callback for each IB device still registered.
 */
void
ib_unregister_client(struct ib_client *client)
{
	ofs_client_t		*ofs_client;
	ib_device_t		*device;
	llist_head_t		*entry, *tmp;
	ibt_status_t		status;
	ib_device_impl_t	*impl_devp;
	sol_ofs_dev_t		*ofs_devp;

	ASSERT(client->state == IB_CLNT_INITIALIZED &&
	    client->clnt_hdl != NULL);

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_unregister_client: client: 0x%p", client);

	ofs_client = (ofs_client_t *)client->clnt_hdl;
	rw_enter(&ofs_client->lock, RW_WRITER);

	/* first close all open HCAs */
	list_for_each(entry, &ofs_client->device_list) {
		device = entry->ptr;
		impl_devp =
		    (ib_device_impl_t *)device->impl_data;

		SOL_OFS_DPRINTF_L5(sol_kverbs_dbg_str,
		    "ib_unregister_client: client %p, dev %p, state %x",
		    ofs_client, device, device->reg_state);

		/*
		 * If it's open already, close it after the remove
		 * callback.
		 */
		if (device->reg_state == IB_DEV_OPEN) {
			/* invoke client's callback */
			if (client->remove) {
				client->remove(device);
			}

			ofs_devp = impl_devp->dev_impl_ofs_devp;
			mutex_enter(&sol_ofs_dev_mutex);
			SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
			    "unreig: ofs_devp %p, "
			    "client_cnt %x decr, client nm %s",
			    ofs_devp, ofs_devp->ofs_dev_client_cnt,
			    client->name);
			ofs_devp->ofs_dev_client_cnt--;
			if (!ofs_devp->ofs_dev_client_cnt)
				cv_broadcast(&sol_ofs_dev_cv);
			mutex_exit(&sol_ofs_dev_mutex);

			status = ibt_close_hca(device->hca_hdl);
			if (status != IBT_SUCCESS) {
				SOL_OFS_DPRINTF_L2(
				    sol_kverbs_dbg_str,
				    "ib_unregister_client(%p) - "
				    "ibt_close_hca failed %d",
				    client, status);
				device->close_failed_state = B_TRUE;
				rw_exit(&ofs_client->lock);
				return;
			}
			device->reg_state = IB_DEV_CLOSE;
			device->close_failed_state = B_FALSE;

			ofs_client->hca_open_num--;
		}
	}
	ASSERT(ofs_client->hca_open_num == 0);

	/* then free the devices */
	list_for_each_safe(entry, tmp, &ofs_client->device_list) {
		device = entry->ptr;
		/* de-link and free the device */
		llist_del(entry);
		if (device->impl_data) {
			kmem_free(device->impl_data,
			    sizeof (ib_device_impl_t));
			device->impl_data = NULL;
		}
		kmem_free(device, sizeof (ib_device_t));
		ofs_client->hca_num--;
	}
	ASSERT(ofs_client->hca_num == 0);

	/* delink this client */
	mutex_enter(&clist_lock);
	llist_del(&ofs_client->client_list);
	mutex_exit(&clist_lock);

	/* detach the client */
	if ((status = ibt_detach(ofs_client->ibt_hdl)) != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_unregister_client: client: 0x%p => "
		    "ibt_detach() failed with status %d!!",
		    client, status);
		rw_exit(&ofs_client->lock);
		return;
	}
	client->clnt_hdl = NULL;
	client->state = IB_CLNT_UNINITIALIZED;
	rw_exit(&ofs_client->lock);

	/* free sol_ofs_client */
	free_ibt_client(ofs_client);
	kmem_free(ofs_client, sizeof (ofs_client_t));
}

/*
 * ofs_lock_enter() and ofs_lock_exit() are used to avoid the recursive
 * rwlock while the client callbacks are invoked.
 *
 * Note that the writer lock is used only in the client callback case,
 * so that the kverb functions wanting to acquire the reader lock can
 * safely ignore the reader lock if the writer lock is already held.
 * The writer lock shouldn't be used in no other plances.
 */
static inline void
ofs_lock_enter(krwlock_t *lock)
{
	if (!RW_WRITE_HELD(lock)) {
		rw_enter(lock, RW_READER);
	}
}

static inline void
ofs_lock_exit(krwlock_t *lock)
{
	if (!RW_WRITE_HELD(lock)) {
		rw_exit(lock);
	}
}

/*
 * ib_get_client_data - Get IB client context
 * @device:Device to get context for
 * @client:Client to get context for
 *
 * ib_get_client_data() returns client context set with
 * ib_set_client_data() and returns NULL if it's not found.
 */
void *ib_get_client_data(struct ib_device *device,
    struct ib_client *client)
{
	ofs_client_t		*ofs_client;
	struct ib_device	*ib_device;
	boolean_t		found = B_FALSE;
	llist_head_t		*entry;
	void			*data;

	ASSERT(device != 0 && client != 0);

	ofs_client = (ofs_client_t *)client->clnt_hdl;
	if (ofs_client == 0) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_get_client_data: device: 0x%p, client: 0x%p => "
		    "no ofs_client", device, client);
		return (NULL);
	}

	ofs_lock_enter(&ofs_client->lock);
	list_for_each(entry, &ofs_client->device_list) {
		ib_device = entry->ptr;
		if (ib_device->node_guid == device->node_guid) {
			found = B_TRUE;
			break;
		}
	}
	if (!found) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_get_client_data: device: 0x%p, client: 0x%p => "
		    "no ib_device found", device, client);
		return (NULL);
	}
	data = ib_device->data;
	ofs_lock_exit(&ofs_client->lock);

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_get_client_data: device: 0x%p, client: 0x%p",
	    device, client);

	return (data);
}

/*
 * ib_set_client_data - Set IB client context
 * @device:Device to set context for
 * @client:Client to set context for
 * @data:Context to set
 *
 * ib_set_client_data() sets client context that can be retrieved with
 * ib_get_client_data(). If the specified device is not found, the function
 * returns w/o any operations.
 */
void ib_set_client_data(struct ib_device *device, struct ib_client *client,
    void *data)
{
	ofs_client_t		*ofs_client;
	struct ib_device	*ib_device;
	boolean_t		found = B_FALSE;
	llist_head_t		*entry;

	ASSERT(device != 0 && client != 0);

	ofs_client = (ofs_client_t *)client->clnt_hdl;
	if (ofs_client == 0) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "No client context found for %s/%s",
		    device->name, client->name);
		return;
	}

	ofs_lock_enter(&ofs_client->lock);
	list_for_each(entry, &ofs_client->device_list) {
		ib_device = entry->ptr;
		if (ib_device->node_guid == device->node_guid) {
			found = B_TRUE;
			break;
		}
	}
	if (!found) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "No client context found for %s/%s",
		    device->name, client->name);
		ofs_lock_exit(&ofs_client->lock);
		return;
	}
	ib_device->data = data;
	ofs_lock_exit(&ofs_client->lock);

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_set_client_data: device: 0x%p, client: 0x%p, "
	    "data: 0x%p", device, client, data);
}

/*
 * ib_query_device - Query IB device attributes
 * @device:Device to query
 * @device_attr:Device attributes
 *
 * ib_query_device() returns the attributes of a device through the
 * @device_attr pointer.
 */
int
ib_query_device(struct ib_device *device, struct ib_device_attr *attr)
{
	ofs_client_t	*ofs_client = (ofs_client_t *)device->clnt_hdl;
	ibt_hca_attr_t	hattr;
	int		rtn;

	ofs_lock_enter(&ofs_client->lock);
	if (device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_query_device: device: 0x%p => "
		    "invalid device state (%d)", device, device->reg_state);
		return (-ENXIO);
	}
	if ((rtn = ibt_query_hca(device->hca_hdl, &hattr)) != IBT_SUCCESS) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_query_device: device: 0x%p => "
		    "ibt_query_hca failed w/ 0x%x", device, rtn);
		return (-EIO);
	}
	ofs_lock_exit(&ofs_client->lock);

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_query_device: device: 0x%p, attr: 0x%p, rtn: 0x%p",
	    device, attr, rtn);

	/* OF order is major.micro.minor, so keep it here */
	attr->fw_ver = (uint64_t)hattr.hca_fw_major_version << 32	|
	    hattr.hca_fw_micro_version << 16 & 0xFFFF0000		|
	    hattr.hca_fw_minor_version & 0xFFFF;

	attr->device_cap_flags    = IB_DEVICE_CHANGE_PHY_PORT		|
	    IB_DEVICE_PORT_ACTIVE_EVENT					|
	    IB_DEVICE_SYS_IMAGE_GUID					|
	    IB_DEVICE_RC_RNR_NAK_GEN;
	if (hattr.hca_flags & IBT_HCA_PKEY_CNTR) {
		attr->device_cap_flags |= IB_DEVICE_BAD_PKEY_CNTR;
	}
	if (hattr.hca_flags & IBT_HCA_QKEY_CNTR) {
		attr->device_cap_flags |= IB_DEVICE_BAD_QKEY_CNTR;
	}
	if (hattr.hca_flags & IBT_HCA_AUTO_PATH_MIG) {
		attr->device_cap_flags |= IB_DEVICE_AUTO_PATH_MIG;
	}
	if (hattr.hca_flags & IBT_HCA_AH_PORT_CHECK) {
		attr->device_cap_flags |= IB_DEVICE_UD_AV_PORT_ENFORCE;
	}

	attr->vendor_id		= hattr.hca_vendor_id;
	attr->vendor_part_id	= hattr.hca_device_id;
	attr->hw_ver		= hattr.hca_version_id;
	attr->sys_image_guid	= htonll(hattr.hca_si_guid);
	attr->max_mr_size	= ~0ull;
	attr->page_size_cap	= IBTF2OF_PGSZ(hattr.hca_page_sz);
	attr->max_qp		= hattr.hca_max_qp;
	attr->max_qp_wr		= hattr.hca_max_qp_sz;
	attr->max_sge		= hattr.hca_max_sgl;
	attr->max_sge_rd	= hattr.hca_max_rd_sgl;
	attr->max_cq		= hattr.hca_max_cq;
	attr->max_cqe		= hattr.hca_max_cq_sz;
	attr->max_mr		= hattr.hca_max_memr;
	attr->max_pd		= hattr.hca_max_pd;
	attr->max_qp_rd_atom	= hattr.hca_max_rdma_in_qp;
	attr->max_qp_init_rd_atom	= hattr.hca_max_rdma_in_qp;
	attr->max_ee_rd_atom	= hattr.hca_max_rdma_in_ee;
	attr->max_ee_init_rd_atom	= hattr.hca_max_rdma_in_ee;
	attr->max_res_rd_atom	= hattr.hca_max_rsc;
	attr->max_srq		= hattr.hca_max_srqs;
	attr->max_srq_wr	= hattr.hca_max_srqs_sz -1;
	attr->max_srq_sge	= hattr.hca_max_srq_sgl;
	attr->local_ca_ack_delay	= hattr.hca_local_ack_delay;
	attr->atomic_cap = hattr.hca_flags & IBT_HCA_ATOMICS_GLOBAL ?
	    IB_ATOMIC_GLOB : (hattr.hca_flags & IBT_HCA_ATOMICS_HCA ?
	    IB_ATOMIC_HCA : IB_ATOMIC_NONE);
	attr->max_ee		= hattr.hca_max_eec;
	attr->max_rdd		= hattr.hca_max_rdd;
	attr->max_mw		= hattr.hca_max_mem_win;
	attr->max_pkeys		= hattr.hca_max_port_pkey_tbl_sz;
	attr->max_raw_ipv6_qp	= hattr.hca_max_ipv6_qp;
	attr->max_raw_ethy_qp	= hattr.hca_max_ether_qp;
	attr->max_mcast_grp	= hattr.hca_max_mcg;
	attr->max_mcast_qp_attach	= hattr.hca_max_qp_per_mcg;
	attr->max_total_mcast_qp_attach = hattr.hca_max_mcg_qps;
	attr->max_ah		= hattr.hca_max_ah;
	attr->max_fmr		= hattr.hca_max_fmrs;
	attr->max_map_per_fmr	= hattr.hca_opaque9; /* hca_max_map_per_fmr */

	return (0);
}

/* Protection domains */
struct ib_pd *
ib_alloc_pd(struct ib_device *device)
{
	ofs_client_t	*ofs_client = (ofs_client_t *)device->clnt_hdl;
	struct ib_pd	*pd;
	int		rtn;

	if ((pd = kmem_alloc(sizeof (struct ib_pd), KM_NOSLEEP)) == NULL) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_alloc_pd: device: 0x%p => no sufficient memory",
		    device);
		return ((struct ib_pd *)-ENOMEM);
	}

	ofs_lock_enter(&ofs_client->lock);
	if (device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_alloc_pd: device: 0x%p => invalid device state (%d)",
		    device, device->reg_state);
		return ((struct ib_pd *)-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_alloc_pd: device: 0x%p", device);

	rtn = ibt_alloc_pd(device->hca_hdl, IBT_PD_NO_FLAGS, &pd->ibt_pd);
	ofs_lock_exit(&ofs_client->lock);

	if (rtn == IBT_SUCCESS) {
		pd->device = device;
		mutex_init(&pd->lock, NULL, MUTEX_DEFAULT, NULL);
		pd->connect_qp_cnt = 0;
		pd->free_called = B_FALSE;
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_alloc_pd: device: 0x%p, pd: 0x%p, ibt_pd: 0x%p, "
		    "rtn: 0x%x", device, pd, pd->ibt_pd, rtn);
		return (pd);
	}
	kmem_free(pd, sizeof (struct ib_pd));

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_alloc_pd: device: 0x%p, pd: 0x%p, ibt_pd: 0x%p => "
	    "ibt_alloc_pd failed w/ 0x%x", device, pd, pd->ibt_pd, rtn);

	switch (rtn) {
	case IBT_INSUFF_RESOURCE:
		return ((struct ib_pd *)-ENOMEM);
	case IBT_HCA_HDL_INVALID:
		return ((struct ib_pd *)-EFAULT);
	default:
		return ((struct ib_pd *)-EIO);
	}
}

int
ib_dealloc_pd(struct ib_pd *pd)
{
	ofs_client_t *ofs_client = (ofs_client_t *)pd->device->clnt_hdl;
	int rtn;

	mutex_enter(&pd->lock);
	if (pd->connect_qp_cnt) {
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_dealloc_pd: pd: 0x%p => connect_qp_cnt (%d)",
		    pd, pd->connect_qp_cnt);
		pd->free_called = B_TRUE;
		mutex_exit(&pd->lock);
		return (0);
	}
	mutex_exit(&pd->lock);

	ofs_lock_enter(&ofs_client->lock);
	if (pd->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_dealloc_pd: pd: 0x%p => invalid device state (%d)",
		    pd, pd->device->reg_state);
		return (-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_dealloc_pd: pd: 0x%p", pd);

	rtn = ibt_free_pd(pd->device->hca_hdl, pd->ibt_pd);
	ofs_lock_exit(&ofs_client->lock);

	if (rtn == IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_dealloc_pd: pd: 0x%p, device: 0x%p, ibt_pd: 0x%p, "
		    "rtn: 0x%x", pd, pd->device, pd->ibt_pd, rtn);
		kmem_free(pd, sizeof (struct ib_pd));
		return (0);
	}

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_dealloc_pd: pd: 0x%p => ibt_free_pd failed w/ 0x%x",
	    pd, rtn);

	switch (rtn) {
	case IBT_PD_IN_USE:
		return (-EBUSY);
	case IBT_HCA_HDL_INVALID:
		return (-EFAULT);
	default:
		return (-EIO);
	}
}

/*
 * ofs_cq_handler() is a delegated function to handle CQ events,
 * which dispatches them to corresponding cq handlers registered
 * with ib_create_cq().
 */
static void
ofs_cq_handler(ibt_cq_hdl_t ibt_cq, void *arg)
{
	struct ib_cq 		*cq;
	struct ib_device	*devicep;
	ib_device_impl_t	*impl_devp;

	cq = (struct ib_cq *)ibt_get_cq_private(ibt_cq);
	if (!cq) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ofs_cq_handler(%p, %p), NULL ib_cq", ibt_cq, arg);
		return;
	}
	devicep = cq->device;
	impl_devp = (ib_device_impl_t *)devicep->impl_data;

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ofs_cq_handler: ibt_cq: 0x%p, ib_cq: 0x%p, "
	    "comp_handler: 0x%p, arg: 0x%p", ibt_cq, cq,
	    cq->comp_handler, arg);

	/*
	 * Increment dev_impl_evt_ref to ensure (*remove) is not
	 * called when CQ event handler is called.
	 *
	 * Increment evt_ref_cnt to ensure that ib_destroy_cq()
	 * does not set comp_handler to NULL.
	 */
	mutex_enter(&impl_devp->dev_impl_mutex);
	mutex_enter(&cq->lock);
	cq->evt_ref_cnt++;
	impl_devp->dev_impl_evt_ref++;
	mutex_exit(&cq->lock);
	mutex_exit(&impl_devp->dev_impl_mutex);
	if (cq->comp_handler) {
		cq->comp_handler(cq, cq->cq_context);
	}

	/*
	 * Decrement dev_impl_evt_ref and wake up if 0, for the
	 * device removal to continue.
	 *
	 * Decrement evt_ref_cnt and wakeup if 0, for the CQ destroy
	 * to continue.
	 */
	mutex_enter(&impl_devp->dev_impl_mutex);
	mutex_enter(&cq->lock);
	cq->evt_ref_cnt--;
	impl_devp->dev_impl_evt_ref--;
	if (!cq->evt_ref_cnt)
		cv_broadcast(&cq->evt_cv);
	if (!impl_devp->dev_impl_evt_ref)
		cv_broadcast(&impl_devp->dev_impl_cv);
	mutex_exit(&cq->lock);
	mutex_exit(&impl_devp->dev_impl_mutex);
}

/*
 * ib_create_cq - Creates a CQ on the specified device.
 * @device: The device on which to create the CQ.
 * @comp_handler: A user-specified callback that is invoked when a
 *   completion event occurs on the CQ.
 * @event_handler: A user-specified callback that is invoked when an
 *   asynchronous event not associated with a completion occurs on the CQ.
 * @cq_context: Context associated with the CQ returned to the user via
 *   the associated completion and event handlers.
 * @cqe: The minimum size of the CQ.
 * @comp_vector - Completion queue sched handle.
 *
 * Users can examine the cq structure to determine the actual CQ size.
 *
 */
struct ib_cq *
ib_create_cq(struct ib_device *device, ib_comp_handler comp_handler,
    void (*event_handler)(struct ib_event *, void *), void *cq_context,
    int cqe, void *comp_vector)
{
	ofs_client_t	*ofs_client = (ofs_client_t *)device->clnt_hdl;
	ibt_cq_attr_t	cq_attr;
	uint32_t	real_size;
	struct ib_cq	*cq;
	int		rtn;

	if ((cq = kmem_alloc(sizeof (struct ib_cq), KM_NOSLEEP)) == NULL) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_create_cq: device: 0x%p, comp_handler: 0x%p, "
		    "event_handler: 0x%p, cq_context: 0x%p, cqe: 0x%x, "
		    "comp_vector: %p => no sufficient memory", device,
		    comp_handler, event_handler, cq_context, cqe, comp_vector);
		return ((struct ib_cq *)-ENOMEM);
	}

	ofs_lock_enter(&ofs_client->lock);
	if (device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_create_cq: device: 0x%p, comp_handler: 0x%p, "
		    "event_handler: 0x%p, cq_context: 0x%p, cqe: 0x%x, "
		    "comp_vector: %p => invalid device state (%d)", device,
		    comp_handler, event_handler, cq_context, cqe, comp_vector,
		    device->reg_state);
		return ((struct ib_cq *)-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_create_cq: device: 0x%p, comp_handler: 0x%p, "
	    "event_handler: 0x%p, cq_context: 0x%p, cqe: 0x%x, "
	    "comp_vector: %p", device, comp_handler, event_handler,
	    cq_context, cqe, comp_vector);

	cq_attr.cq_size = cqe;
	cq_attr.cq_sched = comp_vector;
	cq_attr.cq_flags = IBT_CQ_NO_FLAGS;
	rtn = ibt_alloc_cq(device->hca_hdl, &cq_attr, &cq->ibt_cq, &real_size);
	ofs_lock_exit(&ofs_client->lock);

	if (rtn == IBT_SUCCESS) {
		cq->device = device;
		cq->comp_handler = comp_handler;
		cq->event_handler = event_handler;
		cq->cq_context = cq_context;
		cq->cqe = real_size;
		ibt_set_cq_private(cq->ibt_cq, cq);
		ibt_set_cq_handler(cq->ibt_cq, ofs_cq_handler, cq_context);
		mutex_init(&cq->lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&cq->evt_cv, NULL, CV_DEFAULT, NULL);
		cq->evt_ref_cnt = 1;
		cq->connect_qp_cnt = 0;
		cq->free_called = B_FALSE;
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_create_cq: device: 0x%p, cqe: 0x%x, ibt_cq: 0x%p, "
		    "rtn: 0x%x", device, cqe, cq->ibt_cq, rtn);
		return (cq);
	}
	kmem_free(cq, sizeof (struct ib_cq));

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_create_cq: device: 0x%p, cqe: 0x%x, ibt_cq: 0x%p => "
	    "ibt_alloc_cq failed w/ 0x%x", device, cqe, cq->ibt_cq, rtn);

	switch (rtn) {
	case IBT_HCA_CQ_EXCEEDED:
	case IBT_INVALID_PARAM:
	case IBT_HCA_HDL_INVALID:
		return ((struct ib_cq *)-EINVAL);
	case IBT_INSUFF_RESOURCE:
		return ((struct ib_cq *)-ENOMEM);
	default:
		return ((struct ib_cq *)-EIO);
	}
}

int
ib_destroy_cq(struct ib_cq *cq)
{
	ofs_client_t	*ofs_client;
	int		rtn;

	ofs_client = (ofs_client_t *)cq->device->clnt_hdl;
	ofs_lock_enter(&ofs_client->lock);
	mutex_enter(&cq->lock);
	if (cq->connect_qp_cnt) {
		ibt_cq_hdl_t	ibt_cq;

		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_destroy_cq: cq: 0x%p => connect_qp_cnt (%d)",
		    cq, cq->connect_qp_cnt);
		ibt_cq = cq->ibt_cq;
		mutex_exit(&cq->lock);
		ibt_set_cq_handler(ibt_cq, NULL, NULL);
		mutex_enter(&cq->lock);
		cq->evt_ref_cnt--;
		while (cq->evt_ref_cnt) {
			cv_wait(&cq->evt_cv, &cq->lock);
		}
		cq->free_called = B_TRUE;
		cq->event_handler = NULL;
		cq->comp_handler = NULL;
		mutex_exit(&cq->lock);
		ofs_lock_exit(&ofs_client->lock);
		return (0);
	}
	mutex_exit(&cq->lock);

	if (cq->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_destroy_cq: cq: 0x%p => invalid device state (%d)",
		    cq, cq->device->reg_state);
		return (-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_destroy_cq: cq: 0x%p", cq);

	/*
	 * if IBTL_ASYNC_PENDING is set, ibt_qp is not freed
	 * at this moment, but yet alive for a while. Then
	 * there is a possibility that this qp is used even after
	 * ib_destroy_cq() is called. To distinguish this case from
	 * others, clear ibt_qp here.
	 */
	ibt_set_cq_private(cq->ibt_cq, NULL);

	rtn = ibt_free_cq(cq->ibt_cq);
	if (rtn == IBT_SUCCESS) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_destroy_cq: cq: 0x%p, rtn: 0x%x", cq, rtn);
		mutex_destroy(&cq->lock);
		cv_destroy(&cq->evt_cv);
		kmem_free(cq, sizeof (struct ib_cq));
		return (0);
	}
	ibt_set_cq_private(cq->ibt_cq, cq);
	ofs_lock_exit(&ofs_client->lock);

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_destroy_cq: cq: 0x%p => ibt_free_cq failed w/ 0x%x", cq, rtn);

	switch (rtn) {
	case IBT_CQ_BUSY:
		return (-EBUSY);
	case IBT_HCA_HDL_INVALID:
	case IBT_CQ_HDL_INVALID:
		return (-EINVAL);
	default:
		return (-EIO);
	}
}

struct ib_qp *
ib_create_qp(struct ib_pd *pd, struct ib_qp_init_attr *qp_init_attr)
{
	ofs_client_t		*ofs_client = pd->device->clnt_hdl;
	ibt_qp_alloc_attr_t	attrs;
	ibt_chan_sizes_t	sizes;
	ib_qpn_t		qpn;
	ibt_qp_hdl_t		ibt_qp;
	struct ib_qp		*qp;
	int			rtn;

	/* sanity check */
	if (!(qp_init_attr->send_cq && qp_init_attr->recv_cq)) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_create_qp: pd: 0x%p => invalid cqs "
		    "(send_cq=0x%p, recv_cq=0x%p)", pd,
		    qp_init_attr->send_cq, qp_init_attr->recv_cq);
		return ((struct ib_qp *)-EINVAL);
	}

	/* UC, Raw IPv6 and Raw Ethernet are not supported */
	if (qp_init_attr->qp_type == IB_QPT_UC ||
	    qp_init_attr->qp_type == IB_QPT_RAW_IPV6 ||
	    qp_init_attr->qp_type == IB_QPT_RAW_ETY) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_create_qp: pd: 0x%p => invalid qp_type",
		    pd, qp_init_attr->qp_type);
		return ((struct ib_qp *)-EINVAL);
	}

	if ((qp = kmem_alloc(sizeof (struct ib_qp), KM_NOSLEEP)) == NULL) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_create_qp: pd: 0x%p, init_attr: 0x%p => "
		    "no sufficient memory", pd, qp_init_attr);
		return ((struct ib_qp *)-ENOMEM);
	}

	ofs_lock_enter(&ofs_client->lock);
	if (pd->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		kmem_free(qp, sizeof (struct ib_qp));
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_create_qp: pd: 0x%p, init_attr: 0x%p => "
		    "invalid device state (%d)", pd, qp_init_attr,
		    pd->device->reg_state);
		return ((struct ib_qp *)-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_create_qp: pd: 0x%p, event_handler: 0x%p, qp_context: 0x%p, "
	    "send_cq: 0x%p, recv_cq: 0x%p, srq: 0x%p, max_send_wr: 0x%x, "
	    "max_recv_wr: 0x%x, max_send_sge: 0x%x, max_recv_sge: 0x%x, "
	    "max_inline_data: 0x%x, sq_sig_type: %d, qp_type: %d, "
	    "port_num: %d",
	    pd, qp_init_attr->event_handler, qp_init_attr->qp_context,
	    qp_init_attr->send_cq, qp_init_attr->recv_cq, qp_init_attr->srq,
	    qp_init_attr->cap.max_send_wr, qp_init_attr->cap.max_recv_wr,
	    qp_init_attr->cap.max_send_sge, qp_init_attr->cap.max_recv_sge,
	    qp_init_attr->cap.max_inline_data, qp_init_attr->sq_sig_type,
	    qp_init_attr->qp_type, qp_init_attr->port_num);

	attrs.qp_alloc_flags = IBT_QP_NO_FLAGS;
	if (qp_init_attr->srq) {
		attrs.qp_alloc_flags |= IBT_QP_USES_SRQ;
	}

	attrs.qp_flags = IBT_ALL_SIGNALED | IBT_FAST_REG_RES_LKEY;
	if (qp_init_attr->sq_sig_type == IB_SIGNAL_REQ_WR) {
		attrs.qp_flags |= IBT_WR_SIGNALED;
	}

	attrs.qp_scq_hdl = qp_init_attr->send_cq->ibt_cq;
	attrs.qp_rcq_hdl = qp_init_attr->recv_cq->ibt_cq;
	attrs.qp_pd_hdl = pd->ibt_pd;

	attrs.qp_sizes.cs_sq = qp_init_attr->cap.max_send_wr;
	attrs.qp_sizes.cs_rq = qp_init_attr->cap.max_recv_wr;
	attrs.qp_sizes.cs_sq_sgl = qp_init_attr->cap.max_send_sge;
	attrs.qp_sizes.cs_rq_sgl = qp_init_attr->cap.max_recv_sge;
	attrs.qp_sizes.cs_inline = qp_init_attr->cap.max_inline_data;
	if (attrs.qp_sizes.cs_inline != 0)
		attrs.qp_alloc_flags |= IBT_QP_USES_INLINE;

	switch (qp_init_attr->qp_type) {
	case IB_QPT_RC:
		rtn = ibt_alloc_qp(pd->device->hca_hdl, IBT_RC_RQP, &attrs,
		    &sizes, &qpn, &ibt_qp);
		break;
	case IB_QPT_UD:
		rtn = ibt_alloc_qp(pd->device->hca_hdl, IBT_UD_RQP, &attrs,
		    &sizes, &qpn, &ibt_qp);
		break;
	case IB_QPT_SMI:
		rtn = ibt_alloc_special_qp(pd->device->hca_hdl,
		    qp_init_attr->port_num, IBT_SMI_SQP, &attrs, &sizes,
		    &ibt_qp);
		break;
	case IB_QPT_GSI:
		rtn = ibt_alloc_special_qp(pd->device->hca_hdl,
		    qp_init_attr->port_num, IBT_GSI_SQP, &attrs, &sizes,
		    &ibt_qp);
		break;
	default:
		/* this should never happens */
		ofs_lock_exit(&ofs_client->lock);
		kmem_free(qp, sizeof (struct ib_qp));
		return ((struct ib_qp *)-EINVAL);
	}
	ofs_lock_exit(&ofs_client->lock);

	if (rtn == IBT_SUCCESS) {
		/* fill in ib_qp_cap w/ the real values */
		qp_init_attr->cap.max_send_wr = sizes.cs_sq;
		qp_init_attr->cap.max_recv_wr = sizes.cs_rq;
		qp_init_attr->cap.max_send_sge = sizes.cs_sq_sgl;
		qp_init_attr->cap.max_recv_sge = sizes.cs_rq_sgl;
		qp_init_attr->cap.max_inline_data = sizes.cs_inline;
		/* fill in ib_qp */
		qp->device = pd->device;
		qp->pd = pd;
		qp->send_cq = qp_init_attr->send_cq;
		qp->recv_cq = qp_init_attr->recv_cq;
		qp->srq = qp_init_attr->srq;
		qp->event_handler = qp_init_attr->event_handler;
		qp->qp_context = qp_init_attr->qp_context;
		qp->qp_num = qp_init_attr->qp_type == IB_QPT_SMI ? 0 :
		    qp_init_attr->qp_type == IB_QPT_GSI ? 1 : qpn;
		qp->qp_type = qp_init_attr->qp_type;
		qp->ibt_qp = ibt_qp;
		ibt_set_qp_private(qp->ibt_qp, qp);
		mutex_init(&qp->lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&qp->evt_cv, NULL, CV_DEFAULT, NULL);
		qp->evt_ref_cnt = 1;
		qp->cm_id = NULL;
		qp->connect_state = KVERBS_QP_DISCONNECT;
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_create_qp: device: 0x%p, pd: 0x%x, init_attr: 0x%p, "
		    "rtn: 0x%x", pd->device, pd, qp_init_attr, rtn);
		return (qp);
	}
	kmem_free(qp, sizeof (struct ib_qp));

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_create_qp: device: 0x%p, pd: 0x%x, init_attr: 0x%p => "
	    "ibt_alloc_(special)_qp failed w/ rtn: 0x%x", pd->device, pd,
	    qp_init_attr, rtn);

	switch (rtn) {
	case IBT_NOT_SUPPORTED:
	case IBT_QP_SRV_TYPE_INVALID:
	case IBT_CQ_HDL_INVALID:
	case IBT_HCA_HDL_INVALID:
	case IBT_INVALID_PARAM:
	case IBT_SRQ_HDL_INVALID:
	case IBT_PD_HDL_INVALID:
	case IBT_HCA_SGL_EXCEEDED:
	case IBT_HCA_WR_EXCEEDED:
		return ((struct ib_qp *)-EINVAL);
	case IBT_INSUFF_RESOURCE:
		return ((struct ib_qp *)-ENOMEM);
	default:
		return ((struct ib_qp *)-EIO);
	}
}

void
kverbs_map_qp_to_cmid(struct ib_qp *qp, void *cmid)
{
	ASSERT(qp);
	mutex_enter(&qp->lock);
	qp->cm_id = cmid;
	mutex_exit(&qp->lock);
}

int
kverbs_get_qp_connect_state(struct ib_qp *qp)
{
	int	connect_state;

	ASSERT(qp);
	ASSERT(qp->cm_id);
	mutex_enter(&qp->lock);
	connect_state = qp->connect_state;
	mutex_exit(&qp->lock);
	return (connect_state);
}

int
kverbs_set_free_state_for_connected(struct ib_qp *qp)
{
	int 		rc = -1;
	ofs_client_t	*ofs_client;

	ASSERT(qp);
	ASSERT(qp->cm_id);
	ofs_client = (ofs_client_t *)qp->device->clnt_hdl;

	ofs_lock_enter(&ofs_client->lock);
	mutex_enter(&qp->lock);
	if (qp->connect_state & KVERBS_QP_CONNECTED) {
		qp->connect_state |= KVERBS_QP_FREE_CALLED;
		qp->event_handler = NULL;
		rc = 0;
	}
	mutex_exit(&qp->lock);
	ofs_lock_exit(&ofs_client->lock);
	return (rc);
}

void
kverbs_notify_qp_connect_state(struct ib_qp *qp, int state)
{
	struct ib_cq		*send_cq, *recv_cq;
	struct ib_pd 		*pd;
	struct ib_srq		*srq;
	ofs_client_t 		*ofs_client;
	ibt_status_t		status;
	boolean_t		recv_cq_update_req = B_FALSE;
	struct ib_device	*devicep;


	ASSERT(qp);
	ASSERT(qp->cm_id);
	ASSERT(qp->pd);
	send_cq = qp->send_cq;
	recv_cq = qp->recv_cq;
	srq = qp->srq;
	pd = qp->pd;
	ofs_client = (ofs_client_t *)qp->device->clnt_hdl;
	devicep = qp->device;
	if (recv_cq && send_cq != recv_cq)
		recv_cq_update_req = B_TRUE;

	ofs_lock_enter(&ofs_client->lock);
	mutex_enter(&qp->lock);
	if (send_cq)
		mutex_enter(&send_cq->lock);
	if (recv_cq_update_req == B_TRUE)
		mutex_enter(&recv_cq->lock);
	if (srq)
		mutex_enter(&srq->lock);
	if (pd)
		mutex_enter(&pd->lock);

	switch (state) {
	case KVERBS_QP_CONNECTED :
		qp->connect_state |= KVERBS_QP_CONNECTED;
		if (send_cq)
			send_cq->connect_qp_cnt++;
		if (recv_cq_update_req == B_TRUE)
			recv_cq->connect_qp_cnt++;
		if (srq)
			srq->connect_qp_cnt++;
		if (pd)
			pd->connect_qp_cnt++;
		devicep->connect_qp_cnt++;
		break;
	case KVERBS_QP_DISCONNECT :
		qp->connect_state &= ~KVERBS_QP_CONNECTED;
		if ((qp->connect_state & KVERBS_QP_FREE_CALLED) == 0)
			break;
		if (qp->device->reg_state != IB_DEV_OPEN) {
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "kverbs_notify_qp_connect_state: qp: 0x%p"
			    " => invalid device state (%d)",
			    qp, qp->device->reg_state);
			ibt_set_qp_private(qp->ibt_qp, qp);
			goto qp_connect_out;
		}
		ibt_set_qp_private(qp->ibt_qp, NULL);
		status = ibt_free_qp(qp->ibt_qp);
		if (status != IBT_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "kverbs_notify_qp_connect_state: "
			    "qp: 0x%p, device: 0x%p, "
			    "ibt_cq: 0x%p, rtn: 0x%x", qp,
			    qp->device, qp->ibt_qp,
			    status);
			ibt_set_qp_private(qp->ibt_qp, qp);
			goto qp_connect_out;
		}
		qp->ibt_qp = NULL;
		if (send_cq) {
			send_cq->connect_qp_cnt--;
			if (send_cq->connect_qp_cnt &&
			    send_cq->free_called == B_TRUE) {
				if (send_cq->device->reg_state != IB_DEV_OPEN) {
					SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
					    "kverbs_notify_qp_connect_state:"
					    " send_cq: 0x%p"
					    " => invalid device state (%d)",
					    send_cq,
					    send_cq->device->reg_state);
					goto qp_connect_out;
				}
				status = ibt_free_cq(send_cq->ibt_cq);
				if (status != IBT_SUCCESS) {
					SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
					    "kverbs_notify_qp_connect_state: "
					    "send_cq: 0x%p, device: 0x%p, "
					    "ibt_cq: 0x%p, rtn: 0x%x", send_cq,
					    send_cq->device, send_cq->ibt_cq,
					    status);
					goto qp_connect_out;
				}
				mutex_exit(&send_cq->lock);
				kmem_free(send_cq, sizeof (struct ib_cq));
				send_cq = NULL;
			}
		}
		if (recv_cq_update_req == B_TRUE) {
			recv_cq->connect_qp_cnt--;
			if (recv_cq->connect_qp_cnt &&
			    recv_cq->free_called == B_TRUE) {
				if (recv_cq->device->reg_state != IB_DEV_OPEN) {
					SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
					    "kverbs_notify_qp_connect_state:"
					    " recv_cq: 0x%p"
					    " => invalid device state (%d)",
					    recv_cq,
					    recv_cq->device->reg_state);
					goto qp_connect_out;
				}
				status = ibt_free_cq(recv_cq->ibt_cq);
				if (status != IBT_SUCCESS) {
					SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
					    "kverbs_notify_qp_connect_state: "
					    "recv_cq: 0x%p, device: 0x%p, "
					    "ibt_cq: 0x%p, rtn: 0x%x", recv_cq,
					    recv_cq->device, recv_cq->ibt_cq,
					    status);
					goto qp_connect_out;
				}
				mutex_exit(&recv_cq->lock);
				kmem_free(recv_cq, sizeof (struct ib_cq));
				recv_cq = NULL;
			}
		}
		if (srq) {
			srq->connect_qp_cnt--;
			if (srq->connect_qp_cnt == 0 &&
			    srq->free_called == B_TRUE) {
				if (srq->device->reg_state != IB_DEV_OPEN) {
					SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
					    "kverbs_notify_qp_connect_state: "
					    "srq: 0x%p => invalid device "
					    "state (%d)", srq,
					    srq->device->reg_state);
					goto qp_connect_out;
				}
				status = ibt_free_srq(srq->ibt_srq);
				if (status != IBT_SUCCESS) {
					SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
					    "kverbs_notify_qp_connect_state: "
					    "srq: 0x%p, device: 0x%p, "
					    "ibt_srq: 0x%p, rtn: 0x%x", srq,
					    srq->device, srq->ibt_srq, status);
					goto qp_connect_out;
				}
				mutex_exit(&srq->lock);
				kmem_free(srq, sizeof (struct ib_srq));
				srq = NULL;
			}
		}
		if (pd) {
			pd->connect_qp_cnt--;
			if (pd->connect_qp_cnt == 0 &&
			    pd->free_called == B_TRUE) {
				if (pd->device->reg_state != IB_DEV_OPEN) {
					SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
					    "notify_qp_connect_state: pd: 0x%p"
					    " => invalid device state (%d)",
					    pd, pd->device->reg_state);
					goto qp_connect_out;
				}
				status = ibt_free_pd(pd->device->hca_hdl,
				    pd->ibt_pd);
				if (status != IBT_SUCCESS) {
					SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
					    "notify_qp_connect_state: pd: 0x%p,"
					    " device: 0x%p, ibt_pd: 0x%p, "
					    "rtn: 0x%x", pd, pd->device,
					    pd->ibt_pd, status);
					goto qp_connect_out;
				}
				mutex_exit(&pd->lock);
				kmem_free(pd, sizeof (struct ib_pd));
				pd = NULL;
			}
		}
		devicep->connect_qp_cnt--;
		if (devicep->connect_qp_cnt == 0 &&
		    devicep->reg_state == IB_DEV_UNREGISTERED) {
			ibt_status_t	rtn;

			/* close this HCA */
			rtn = ibt_close_hca(devicep->hca_hdl);
			if (rtn != IBT_SUCCESS) {
				SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
				    "Notify disconnect - "
				    "ibt_close_hca(%p) failed %x",
				    devicep->hca_hdl, rtn);
				devicep->close_failed_state = B_TRUE;
				rw_exit(&ofs_client->lock);
				return;
			}
			/* change the state only */
			devicep->reg_state = IB_DEV_CLOSE;
			devicep->close_failed_state = B_FALSE;
			ASSERT(rtn == IBT_SUCCESS);
			ofs_client->hca_open_num--;
		}

		if (qp->connect_state & KVERBS_QP_FREE_CALLED) {
			mutex_exit(&qp->lock);
			kmem_free(qp, sizeof (struct ib_qp));
			qp = NULL;
		}
		break;
	case KVERBS_QP_FREE_CALLED :
		qp->connect_state |= KVERBS_QP_FREE_CALLED;
		ibt_set_qp_private(qp->ibt_qp, NULL);
		break;
	}
qp_connect_out:
	if (qp)
		mutex_exit(&qp->lock);
	if (send_cq)
		mutex_exit(&send_cq->lock);
	if (recv_cq_update_req == B_TRUE)
		mutex_exit(&recv_cq->lock);
	if (srq)
		mutex_exit(&srq->lock);
	if (pd)
		mutex_exit(&pd->lock);
	ofs_lock_exit(&ofs_client->lock);
}

void *
kverbs_device2ibt_hdl(void *devp)
{
	ofs_client_t		*clientp;
	struct ib_device	*ib_devp;

	ASSERT(devp);
	ib_devp = (struct ib_device *)devp;
	clientp = (ofs_client_t *)ib_devp->clnt_hdl;
	ASSERT(clientp);
	return (clientp->ibt_hdl);
}

int
ib_destroy_qp(struct ib_qp *qp)
{
	ofs_client_t	*ofs_client = (ofs_client_t *)qp->device->clnt_hdl;
	ibt_status_t	status;

	ofs_lock_enter(&ofs_client->lock);
	mutex_enter(&qp->lock);
	if (qp->cm_id && qp->connect_state & KVERBS_QP_CONNECTED) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_destroy_qp: destroying connected qp %p", qp);
		qp->event_handler = NULL;
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		return (-ENXIO);
	}
	mutex_exit(&qp->lock);

	if (qp->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_destroy_qp: qp: 0x%p => invalid device state (%d)",
		    qp, qp->device->reg_state);
		return (-ENXIO);
	}

	/*
	 * if IBTL_ASYNC_PENDING is set, ibt_qp is not freed
	 * at this moment, but yet alive for a while. Then
	 * there is a possibility that this qp is used even after
	 * ib_destroy_qp() is called. To distinguish this case from
	 * others, clear ibt_qp here.
	 */
	ibt_set_qp_private(qp->ibt_qp, NULL);

	status = ibt_free_qp(qp->ibt_qp);
	if (status == IBT_SUCCESS) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_destroy_qp: qp: 0x%p, rtn: 0x%x", qp, status);
		mutex_destroy(&qp->lock);
		cv_destroy(&qp->evt_cv);
		kmem_free(qp, sizeof (struct ib_qp));
		return (0);
	}
	ibt_set_qp_private(qp->ibt_qp, qp);
	ofs_lock_exit(&ofs_client->lock);

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_destroy_qp: qp: 0x%p => ibt_free_qp "
	    "failed w/ 0x%x", qp, status);

	switch (status) {
	case IBT_CHAN_STATE_INVALID:
	case IBT_HCA_HDL_INVALID:
	case IBT_QP_HDL_INVALID:
		return (-EINVAL);
	default:
		return (-EIO);
	}
}

/*
 * ib_req_notify_cq - Request completion notification on a CQ.
 * @cq: The CQ to generate an event for.
 * @flags:
 *   Must contain exactly one of %IB_CQ_SOLICITED or %IB_CQ_NEXT_COMP
 *   to request an event on the next solicited event or next work
 *   completion at any type, respectively. %IB_CQ_REPORT_MISSED_EVENTS
 *   may also be |ed in to request a hint about missed events, as
 *   described below.
 *
 * Return Value:
 *    < 0 means an error occurred while requesting notification
 *   == 0 means notification was requested successfully, and if
 *        IB_CQ_REPORT_MISSED_EVENTS was passed in, then no events
 *        were missed and it is safe to wait for another event.  In
 *        this case is it guaranteed that any work completions added
 *        to the CQ since the last CQ poll will trigger a completion
 *        notification event.
 *    > 0 is only returned if IB_CQ_REPORT_MISSED_EVENTS was passed
 *        in.  It means that the consumer must poll the CQ again to
 *        make sure it is empty to avoid missing an event because of a
 *        race between requesting notification and an entry being
 *        added to the CQ.  This return value means it is possible
 *        (but not guaranteed) that a work completion has been added
 *        to the CQ since the last poll without triggering a
 *        completion notification event.
 *
 * Note that IB_CQ_REPORT_MISSED_EVENTS is currently not supported.
 */
int
ib_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags)
{
	ibt_cq_notify_flags_t	notify_type;
	ibt_status_t		status;
	ofs_client_t		*ofs_client = cq->device->clnt_hdl;

	ofs_lock_enter(&ofs_client->lock);
	if (cq->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_req_notify_cq: cq: 0x%p, flag: 0x%x", cq, flags);
		return (-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_req_notify_cq: cq: 0x%p, flag: 0x%x", cq, flags);

	switch (flags & IB_CQ_SOLICITED_MASK) {
	case IB_CQ_SOLICITED:
		notify_type = IBT_NEXT_SOLICITED;
		break;
	case IB_CQ_NEXT_COMP:
		notify_type = IBT_NEXT_COMPLETION;
		break;
	default:
		/* Currently only two flags are supported */
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_req_notify_cq: cq: 0x%p, flag: 0x%x => invalid flag",
		    cq, flags);
		return (-EINVAL);
	}

	status = ibt_enable_cq_notify(cq->ibt_cq, notify_type);
	ofs_lock_exit(&ofs_client->lock);

	if (status == IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "ib_req_notify_cq: cq: 0x%p, flag: 0x%x rtn: 0x%x",
		    cq, flags, status);
		return (0);
	}

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_req_notify_cq: cq: 0x%p, flag: 0x%x => ibt_enable_cq_notify "
	    "failed w/ 0x%x", cq, flags, status);

	switch (status) {
	case IBT_HCA_HDL_INVALID:
	case IBT_CQ_HDL_INVALID:
	case IBT_CQ_NOTIFY_TYPE_INVALID:
		return (-EINVAL);
	default:
		return (-EIO);
	}
}

static const struct {
	int			valid;
	enum ib_qp_attr_mask	req_param[IB_QPT_RAW_ETY + 1];
	enum ib_qp_attr_mask	opt_param[IB_QPT_RAW_ETY + 1];
} qp_state_table[IB_QPS_ERR + 1][IB_QPS_ERR + 1] = {

	[IB_QPS_RESET] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_INIT]  = {
			.valid = 1,
			.req_param = {
				[IB_QPT_UD] = (IB_QP_PKEY_INDEX | IB_QP_PORT |
				    IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_PKEY_INDEX | IB_QP_PORT |
				    IB_QP_ACCESS_FLAGS),
				[IB_QPT_RC] = (IB_QP_PKEY_INDEX | IB_QP_PORT |
				    IB_QP_ACCESS_FLAGS),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
			}
		},
	},
	[IB_QPS_INIT]  = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_INIT]  = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_PKEY_INDEX | IB_QP_PORT |
				    IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_PKEY_INDEX | IB_QP_PORT |
				    IB_QP_ACCESS_FLAGS),
				[IB_QPT_RC] = (IB_QP_PKEY_INDEX | IB_QP_PORT |
				    IB_QP_ACCESS_FLAGS),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
			}
		},
		[IB_QPS_RTR]   = {
			.valid = 1,
			.req_param = {
				[IB_QPT_UC] = (IB_QP_AV | IB_QP_PATH_MTU |
				    IB_QP_DEST_QPN | IB_QP_RQ_PSN),
				[IB_QPT_RC] = (IB_QP_AV | IB_QP_PATH_MTU |
				    IB_QP_DEST_QPN | IB_QP_RQ_PSN |
				    IB_QP_MAX_DEST_RD_ATOMIC |
				    IB_QP_MIN_RNR_TIMER),
			},
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_ALT_PATH |
				    IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX),
				[IB_QPT_RC] = (IB_QP_ALT_PATH |
				    IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
			}
		}
	},
	[IB_QPS_RTR]   = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =   { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.req_param = {
				[IB_QPT_UD] = IB_QP_SQ_PSN,
				[IB_QPT_UC] = IB_QP_SQ_PSN,
				[IB_QPT_RC] = (IB_QP_TIMEOUT |
				    IB_QP_RETRY_CNT | IB_QP_RNR_RETRY |
				    IB_QP_SQ_PSN | IB_QP_MAX_QP_RD_ATOMIC),
				[IB_QPT_SMI] = IB_QP_SQ_PSN,
				[IB_QPT_GSI] = IB_QP_SQ_PSN,
			},
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_CUR_STATE |
				    IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
				    IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC] = (IB_QP_CUR_STATE |
				    IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS	|
				    IB_QP_MIN_RNR_TIMER | IB_QP_PATH_MIG_STATE),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE | IB_QP_QKEY),
			}
		}
	},
	[IB_QPS_RTS] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =  { .valid = 1 },
		[IB_QPS_RTS] = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_CUR_STATE	|
				    IB_QP_ACCESS_FLAGS | IB_QP_ALT_PATH |
				    IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC] = (IB_QP_CUR_STATE	|
				    IB_QP_ACCESS_FLAGS | IB_QP_ALT_PATH |
				    IB_QP_PATH_MIG_STATE | IB_QP_MIN_RNR_TIMER),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE	| IB_QP_QKEY),
			}
		},
		[IB_QPS_SQD] = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD] = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_UC] = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_RC] = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_SMI] = IB_QP_EN_SQD_ASYNC_NOTIFY,
				[IB_QPT_GSI] = IB_QP_EN_SQD_ASYNC_NOTIFY
			}
		},
	},
	[IB_QPS_SQD] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] = { .valid = 1 },
		[IB_QPS_RTS] = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_CUR_STATE |
				    IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
				    IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC] = (IB_QP_CUR_STATE |
				    IB_QP_ALT_PATH | IB_QP_ACCESS_FLAGS |
				    IB_QP_MIN_RNR_TIMER	| IB_QP_PATH_MIG_STATE),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE	| IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE	| IB_QP_QKEY),
			}
		},
		[IB_QPS_SQD] = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_PKEY_INDEX	| IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_AV | IB_QP_ALT_PATH |
				    IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX |
				    IB_QP_PATH_MIG_STATE),
				[IB_QPT_RC] = (IB_QP_PORT | IB_QP_AV |
				    IB_QP_TIMEOUT | IB_QP_RETRY_CNT |
				    IB_QP_RNR_RETRY | IB_QP_MAX_QP_RD_ATOMIC |
				    IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_ALT_PATH |
				    IB_QP_ACCESS_FLAGS | IB_QP_PKEY_INDEX |
				    IB_QP_MIN_RNR_TIMER	| IB_QP_PATH_MIG_STATE),
				[IB_QPT_SMI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_PKEY_INDEX | IB_QP_QKEY),
			}
		}
	},
	[IB_QPS_SQE]  = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] = { .valid = 1 },
		[IB_QPS_RTS] = {
			.valid = 1,
			.opt_param = {
				[IB_QPT_UD] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_UC] = (IB_QP_CUR_STATE |
				    IB_QP_ACCESS_FLAGS),
				[IB_QPT_SMI] = (IB_QP_CUR_STATE | IB_QP_QKEY),
				[IB_QPT_GSI] = (IB_QP_CUR_STATE	| IB_QP_QKEY),
			}
		}
	},
	[IB_QPS_ERR] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR] =  { .valid = 1 }
	}
};

static inline int
ib_modify_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state next_state,
    enum ib_qp_type type, enum ib_qp_attr_mask mask)
{
	enum ib_qp_attr_mask req_param, opt_param;

	if (cur_state  < 0 || cur_state  > IB_QPS_ERR ||
	    next_state < 0 || next_state > IB_QPS_ERR) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp_is_ok: cur_state: %d, next_state: %d, "
		    "qp_type: %d, attr_mask: 0x%x => invalid state(1)",
		    cur_state, next_state, type, mask);
		return (0);
	}

	if (mask & IB_QP_CUR_STATE &&
	    cur_state != IB_QPS_RTR && cur_state != IB_QPS_RTS &&
	    cur_state != IB_QPS_SQD && cur_state != IB_QPS_SQE) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp_is_ok: cur_state: %d, next_state: %d, "
		    "qp_type: %d, attr_mask: 0x%x => invalid state(2)",
		    cur_state, next_state, type, mask);
		return (0);
	}

	if (!qp_state_table[cur_state][next_state].valid) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp_is_ok: cur_state: %d, next_state: %d, "
		    "qp_type: %d, attr_mask: 0x%x => state is not valid",
		    cur_state, next_state, type, mask);
		return (0);
	}

	req_param = qp_state_table[cur_state][next_state].req_param[type];
	opt_param = qp_state_table[cur_state][next_state].opt_param[type];

	if ((mask & req_param) != req_param) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp_is_ok: cur_state: %d, next_state: %d, "
		    "qp_type: %d, attr_mask: 0x%x => "
		    "required param doesn't match. req_param = 0x%x",
		    cur_state, next_state, type, mask, req_param);
		return (0);
	}

	if (mask & ~(req_param | opt_param | IB_QP_STATE)) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp_is_ok: cur_state: %d, next_state: %d, "
		    "qp_type: %d, attr_mask: 0x%x => "
		    "unsupported options. req_param = 0x%x, opt_param = 0x%x",
		    cur_state, next_state, type, mask, req_param, opt_param);
		return (0);
	}

	return (1);
}

static inline enum ib_qp_state
qp_current_state(ibt_qp_query_attr_t *qp_attr)
{
	ASSERT(qp_attr->qp_info.qp_state != IBT_STATE_SQDRAIN);
	return (enum ib_qp_state)(qp_attr->qp_info.qp_state);
}

static inline ibt_tran_srv_t
of2ibtf_qp_type(enum ib_qp_type type)
{
	switch (type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	case IB_QPT_UD:
		return (IBT_UD_SRV);
	case IB_QPT_RC:
		return (IBT_RC_SRV);
	case IB_QPT_UC:
		return (IBT_UC_SRV);
	case IB_QPT_RAW_IPV6:
		return (IBT_RAWIP_SRV);
	case IB_QPT_RAW_ETY:
	default:
		ASSERT(type == IB_QPT_RAW_ETY);
		return (IBT_RAWETHER_SRV);
	}
}

static inline void
set_av(struct ib_ah_attr *attr, ibt_cep_path_t *pathp)
{
	ibt_adds_vect_t		*av = &pathp->cep_adds_vect;

	pathp->cep_hca_port_num = attr->port_num;
	av->av_srate = OF2IBTF_SRATE(attr->static_rate);
	av->av_srvl = attr->sl & 0xF;
	av->av_send_grh = attr->ah_flags & IB_AH_GRH ? 1 : 0;

	if (av->av_send_grh) {
		av->av_dgid.gid_prefix =
		    attr->grh.dgid.global.subnet_prefix;
		av->av_dgid.gid_guid =
		    attr->grh.dgid.global.interface_id;
		av->av_flow = attr->grh.flow_label & 0xFFFFF;
		av->av_tclass = attr->grh.traffic_class;
		av->av_hop = attr->grh.hop_limit;
		av->av_sgid_ix = attr->grh.sgid_index;
	}
	av->av_dlid = attr->dlid;
	av->av_src_path = attr->src_path_bits;
}

int
ib_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr, int attr_mask)
{
	enum ib_qp_state	cur_state, new_state;
	ibt_hca_attr_t		hattr;
	ibt_qp_query_attr_t	qp_attr;
	ibt_qp_info_t		modify_attr;
	ibt_cep_modify_flags_t	flags;
	ibt_status_t		status;
	ofs_client_t		*ofs_client = qp->device->clnt_hdl;

	ofs_lock_enter(&ofs_client->lock);
	if (qp->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p => invalid device state (%d)",
		    qp, qp->device->reg_state);
		return (-ENXIO);
	}

	status = ibt_query_hca(qp->device->hca_hdl, &hattr);
	if (status != IBT_SUCCESS) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, hca_hdl: 0x%p => "
		    "ibt_query_hca() failed w/ %d",
		    qp, qp->device->hca_hdl, status);
		return (-EIO);
	}

	/* only one thread per qp is allowed during the qp modification */
	mutex_enter(&qp->lock);

	/* Get the current QP attributes first */
	bzero(&qp_attr, sizeof (ibt_qp_query_attr_t));
	if ((status = ibt_query_qp(qp->ibt_qp, &qp_attr)) != IBT_SUCCESS) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "ibt_query_qp failed w/ 0x%x", qp, attr, attr_mask, status);
		return (-EIO);
	}

	/* Get the current and new state for this QP */
	cur_state = attr_mask & IB_QP_CUR_STATE ?  attr->cur_qp_state :
	    qp_current_state(&qp_attr);
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state :
	    cur_state;

	/* Sanity check of the current/new states */
	if (cur_state == new_state && cur_state == IB_QPS_RESET) {
		/* Linux OF returns 0 in this case */
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "invalid state (both of current/new states are RESET)",
		    qp, attr, attr_mask);
		return (0);
	}

	/*
	 * Check if this modification request is supported with the new
	 * and/or current state.
	 */
	if (!ib_modify_qp_is_ok(cur_state, new_state, qp->qp_type, attr_mask)) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "invalid arguments",
		    qp, attr, attr_mask);
		return (-EINVAL);
	}

	/* Sanity checks */
	if (attr_mask & IB_QP_PORT && (attr->port_num == 0 ||
	    attr->port_num > hattr.hca_nports)) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "invalid attr->port_num(%d), max_nports(%d)",
		    qp, attr, attr_mask, attr->port_num, hattr.hca_nports);
		return (-EINVAL);
	}

	if (attr_mask & IB_QP_PKEY_INDEX &&
	    attr->pkey_index >= hattr.hca_max_port_pkey_tbl_sz) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "invalid attr->pkey_index(%d), max_pkey_index(%d)",
		    qp, attr, attr_mask, attr->pkey_index,
		    hattr.hca_max_port_pkey_tbl_sz);
		return (-EINVAL);
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > hattr.hca_max_rdma_out_qp) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "invalid attr->max_rd_atomic(0x%x), max_rdma_out_qp(0x%x)",
		    qp, attr, attr_mask, attr->max_rd_atomic,
		    hattr.hca_max_rdma_out_qp);
		return (-EINVAL);
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > hattr.hca_max_rdma_in_qp) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
		    "invalid attr->max_dest_rd_atomic(0x%x), "
		    "max_rdma_in_qp(0x%x)", qp, attr, attr_mask,
		    attr->max_dest_rd_atomic, hattr.hca_max_rdma_in_qp);
		return (-EINVAL);
	}

	/* copy the current setting */
	modify_attr = qp_attr.qp_info;

	/*
	 * Since it's already checked if the modification request matches
	 * the new and/or current states, just assign both of states to
	 * modify_attr here. The current state is required if qp_state
	 * is RTR, but it's harmelss otherwise, so it's set always.
	 */
	modify_attr.qp_current_state = OF2IBTF_STATE(cur_state);
	modify_attr.qp_state = OF2IBTF_STATE(new_state);
	modify_attr.qp_trans = of2ibtf_qp_type(qp->qp_type);

	/* Convert OF modification requests into IBTF ones */
	flags = IBT_CEP_SET_STATE;	/* IBTF needs IBT_CEP_SET_STATE */
	if (cur_state == IB_QPS_RESET &&
	    new_state == IB_QPS_INIT) {
		flags |= IBT_CEP_SET_RESET_INIT;
	} else if (cur_state == IB_QPS_INIT &&
	    new_state == IB_QPS_RTR) {
		flags |= IBT_CEP_SET_INIT_RTR;
	} else if (cur_state == IB_QPS_RTR &&
	    new_state == IB_QPS_RTS) {
		flags |= IBT_CEP_SET_RTR_RTS;
	}
	if (attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY) {
		flags |= IBT_CEP_SET_SQD_EVENT;
	}
	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		modify_attr.qp_flags &= ~(IBT_CEP_RDMA_RD | IBT_CEP_RDMA_WR |
		    IBT_CEP_ATOMIC);
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_READ) {
			flags |= IBT_CEP_SET_RDMA_R;
			modify_attr.qp_flags |= IBT_CEP_RDMA_RD;
		}
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE) {
			flags |= IBT_CEP_SET_RDMA_W;
			modify_attr.qp_flags |= IBT_CEP_RDMA_WR;
		}
		if (attr->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC) {
			flags |= IBT_CEP_SET_ATOMIC;
			modify_attr.qp_flags |= IBT_CEP_ATOMIC;
		}
	}
	if (attr_mask & IB_QP_PKEY_INDEX) {
		flags |= IBT_CEP_SET_PKEY_IX;
		switch (qp->qp_type)  {
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
			modify_attr.qp_transport.ud.ud_pkey_ix =
			    attr->pkey_index;
			break;
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_path.cep_pkey_ix =
			    attr->pkey_index;
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_path.cep_pkey_ix =
			    attr->pkey_index;
			break;
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_PKEY_INDEX): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}
	if (attr_mask & IB_QP_PORT) {
		flags |= IBT_CEP_SET_PORT;
		switch (qp->qp_type) {
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
			modify_attr.qp_transport.ud.ud_port = attr->port_num;
			break;
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_path.cep_hca_port_num =
			    attr->port_num;
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_path.cep_hca_port_num =
			    attr->port_num;
			break;
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_PORT): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}
	if (attr_mask & IB_QP_QKEY) {
		ASSERT(qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_SMI ||
		    qp->qp_type == IB_QPT_GSI);
		flags |= IBT_CEP_SET_QKEY;
		modify_attr.qp_transport.ud.ud_qkey = attr->qkey;
	}
	if (attr_mask & IB_QP_AV) {
		flags |= IBT_CEP_SET_ADDS_VECT;
		switch (qp->qp_type) {
		case IB_QPT_RC:
			set_av(&attr->ah_attr,
			    &modify_attr.qp_transport.rc.rc_path);
			break;
		case IB_QPT_UC:
			set_av(&attr->ah_attr,
			    &modify_attr.qp_transport.uc.uc_path);
			break;
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_AV): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}
	if (attr_mask & IB_QP_PATH_MTU) {
		switch (qp->qp_type) {
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_path_mtu =
			    OF2IBTF_PATH_MTU(attr->path_mtu);
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_path_mtu =
			    OF2IBTF_PATH_MTU(attr->path_mtu);
			break;
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
		default:
			/* nothing to do */
			break;
		}
	}
	if (attr_mask & IB_QP_TIMEOUT && qp->qp_type == IB_QPT_RC) {
		flags |= IBT_CEP_SET_TIMEOUT;
		modify_attr.qp_transport.rc.rc_path.cep_timeout =
		    attr->timeout;
	}
	if (attr_mask & IB_QP_RETRY_CNT && qp->qp_type == IB_QPT_RC) {
		flags |= IBT_CEP_SET_RETRY;
		modify_attr.qp_transport.rc.rc_retry_cnt =
		    attr->retry_cnt & 0x7;
	}
	if (attr_mask & IB_QP_RNR_RETRY && qp->qp_type == IB_QPT_RC) {
		flags |= IBT_CEP_SET_RNR_NAK_RETRY;
		modify_attr.qp_transport.rc.rc_rnr_retry_cnt =
		    attr->rnr_retry & 0x7;
	}
	if (attr_mask & IB_QP_RQ_PSN) {
		switch (qp->qp_type) {
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_rq_psn =
			    attr->rq_psn & 0xFFFFFF;
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_rq_psn =
			    attr->rq_psn & 0xFFFFFF;
			break;
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
		default:
			/* nothing to do */
			break;
		}
	}
	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC && qp->qp_type == IB_QPT_RC) {
		if (attr->max_rd_atomic) {
			flags |= IBT_CEP_SET_RDMARA_OUT;
			modify_attr.qp_transport.rc.rc_rdma_ra_out =
			    attr->max_rd_atomic;
		}
	}
	if (attr_mask & IB_QP_ALT_PATH) {
		/* Sanity checks */
		if (attr->alt_port_num == 0 ||
		    attr->alt_port_num > hattr.hca_nports) {
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp: qp: 0x%p, attr: 0x%p, "
			    "attr_mask: 0x%x => invalid attr->alt_port_num"
			    "(%d), max_nports(%d)",
			    qp, attr, attr_mask, attr->alt_port_num,
			    hattr.hca_nports);
			return (-EINVAL);
		}
		if (attr->alt_pkey_index >= hattr.hca_max_port_pkey_tbl_sz) {
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp: qp: 0x%p, attr: 0x%p, "
			    "attr_mask: 0x%x => invalid attr->alt_pkey_index"
			    "(%d), max_port_key_index(%d)",
			    qp, attr, attr_mask, attr->alt_pkey_index,
			    hattr.hca_max_port_pkey_tbl_sz);
			return (-EINVAL);
		}
		flags |= IBT_CEP_SET_ALT_PATH;
		switch (qp->qp_type) {
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_alt_path.
			    cep_pkey_ix = attr->alt_pkey_index;
			modify_attr.qp_transport.rc.rc_alt_path.
			    cep_hca_port_num = attr->alt_port_num;
			set_av(&attr->alt_ah_attr,
			    &modify_attr.qp_transport.rc.rc_alt_path);
			modify_attr.qp_transport.rc.rc_alt_path.
			    cep_timeout = attr->alt_timeout;
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_alt_path.
			    cep_pkey_ix = attr->alt_pkey_index;
			modify_attr.qp_transport.uc.uc_alt_path.
			    cep_hca_port_num = attr->alt_port_num;
			set_av(&attr->alt_ah_attr,
			    &modify_attr.qp_transport.uc.uc_alt_path);
			modify_attr.qp_transport.uc.uc_alt_path.
			    cep_timeout = attr->alt_timeout;
			break;
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_ALT_PATH): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}
	if (attr_mask & IB_QP_MIN_RNR_TIMER && qp->qp_type == IB_QPT_RC) {
		flags |= IBT_CEP_SET_MIN_RNR_NAK;
		modify_attr.qp_transport.rc.rc_min_rnr_nak =
		    attr->min_rnr_timer & 0x1F;
	}
	if (attr_mask & IB_QP_SQ_PSN) {
		switch (qp->qp_type)  {
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
			modify_attr.qp_transport.ud.ud_sq_psn =
			    attr->sq_psn;
			break;
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_sq_psn =
			    attr->sq_psn;
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_sq_psn =
			    attr->sq_psn;
			break;
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_SQ_PSN): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}
	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC && qp->qp_type == IB_QPT_RC) {
		/* Linux OF sets the value if max_dest_rd_atomic is not zero */
		if (attr->max_dest_rd_atomic) {
			flags |= IBT_CEP_SET_RDMARA_IN;
			modify_attr.qp_transport.rc.rc_rdma_ra_in =
			    attr->max_dest_rd_atomic;
		}
	}
	if (attr_mask & IB_QP_PATH_MIG_STATE) {
		flags |= IBT_CEP_SET_MIG;
		switch (qp->qp_type)  {
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_mig_state =
			    OF2IBTF_PATH_MIG_STATE(attr->path_mig_state);
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_mig_state =
			    OF2IBTF_PATH_MIG_STATE(attr->path_mig_state);
			break;
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_PATH_MIG_STATE): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}
	if (attr_mask & IB_QP_CAP) {
		/* IB_QP_CAP is not supported */
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_qp: qp: 0x%p, attr: 0x%p, "
		    "attr_mask: 0x%x => IB_QP_CAP is not supported",
		    qp, attr, attr_mask);
		return (-EINVAL);
	}
	if (attr_mask & IB_QP_DEST_QPN) {
		switch (qp->qp_type)  {
		case IB_QPT_RC:
			modify_attr.qp_transport.rc.rc_dst_qpn =
			    attr->dest_qp_num;
			break;
		case IB_QPT_UC:
			modify_attr.qp_transport.uc.uc_dst_qpn =
			    attr->dest_qp_num;
			break;
		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
		default:
			/* This should never happen */
			mutex_exit(&qp->lock);
			ofs_lock_exit(&ofs_client->lock);
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_modify_qp(IB_QP_DEST_PSN): qp: 0x%p, "
			    "attr: 0x%p, attr_mask: 0x%x => "
			    "invalid qp->qp_type(%d)",
			    qp, attr, attr_mask, qp->qp_type);
			return (-EINVAL);
		}
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x, "
	    "flags: 0x%x, modify_attr: 0x%p",
	    qp, attr, attr_mask, flags, &modify_attr);

	/* Modify the QP attributes */
	status = ibt_modify_qp(qp->ibt_qp, flags, &modify_attr, NULL);
	if (status == IBT_SUCCESS) {
		mutex_exit(&qp->lock);
		ofs_lock_exit(&ofs_client->lock);
		return (0);
	}
	mutex_exit(&qp->lock);
	ofs_lock_exit(&ofs_client->lock);

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_modify_qp: qp: 0x%p, attr: 0x%p, attr_mask: 0x%x => "
	    "ibt_modify_qp failed w/ %d, flags: 0x%x",
	    qp, attr, attr_mask, status, flags);

	switch (status) {
	case IBT_HCA_HDL_INVALID:
	case IBT_QP_HDL_INVALID:
	case IBT_QP_SRV_TYPE_INVALID:
	case IBT_QP_STATE_INVALID:
	case IBT_HCA_PORT_INVALID:
	case IBT_PKEY_IX_ILLEGAL:
		return (-EINVAL);
	default:
		return (-EIO);
	}
}

static inline enum ib_wc_status
ibt2of_wc_status(ibt_wc_status_t status)
{
	switch (status) {
	case IBT_WC_LOCAL_LEN_ERR:
		return (IB_WC_LOC_LEN_ERR);
	case IBT_WC_LOCAL_CHAN_OP_ERR:
		return (IB_WC_LOC_QP_OP_ERR);
	case IBT_WC_LOCAL_PROTECT_ERR:
		return (IB_WC_LOC_PROT_ERR);
	case IBT_WC_WR_FLUSHED_ERR:
		return (IB_WC_WR_FLUSH_ERR);
	case IBT_WC_MEM_WIN_BIND_ERR:
		return (IB_WC_MW_BIND_ERR);
	case IBT_WC_BAD_RESPONSE_ERR:
		return (IB_WC_BAD_RESP_ERR);
	case IBT_WC_LOCAL_ACCESS_ERR:
		return (IB_WC_LOC_ACCESS_ERR);
	case IBT_WC_REMOTE_INVALID_REQ_ERR:
		return (IB_WC_REM_INV_REQ_ERR);
	case IBT_WC_REMOTE_ACCESS_ERR:
		return (IB_WC_REM_ACCESS_ERR);
	case IBT_WC_REMOTE_OP_ERR:
		return (IB_WC_REM_OP_ERR);
	case IBT_WC_TRANS_TIMEOUT_ERR:
		return (IB_WC_RETRY_EXC_ERR);
	case IBT_WC_RNR_NAK_TIMEOUT_ERR:
		return (IB_WC_RNR_RETRY_EXC_ERR);
	case IBT_WC_SUCCESS:
	default:
		/* Hermon doesn't support EEC yet */
		ASSERT(status == IBT_WC_SUCCESS);
		return (IB_WC_SUCCESS);
	}
}

static inline enum ib_wc_opcode
ibt2of_wc_opcode(ibt_wrc_opcode_t wc_type)
{
	switch (wc_type) {
	case IBT_WRC_SEND:
		return (IB_WC_SEND);
	case IBT_WRC_RDMAR:
		return (IB_WC_RDMA_READ);
	case IBT_WRC_RDMAW:
		return (IB_WC_RDMA_WRITE);
	case IBT_WRC_CSWAP:
		return (IB_WC_COMP_SWAP);
	case IBT_WRC_FADD:
		return (IB_WC_FETCH_ADD);
	case IBT_WRC_BIND:
		return (IB_WC_BIND_MW);
	case IBT_WRC_RECV:
		return (IB_WC_RECV);
	case IBT_WRC_RECV_RDMAWI:
	default:
		ASSERT(wc_type == IBT_WRC_RECV_RDMAWI);
		return (IB_WC_RECV_RDMA_WITH_IMM);
	}
}

static inline int
ibt2of_wc_flags(ibt_wc_flags_t wc_flags)
{
	return (wc_flags & ~IBT_WC_CKSUM_OK);
}

static inline void
set_wc(ibt_wc_t *ibt_wc, struct ib_wc *wc)
{
	wc->wr_id = ibt_wc->wc_id;
	wc->status = ibt2of_wc_status(ibt_wc->wc_status);
	/* opcode can be undefined if status is not success */
	if (wc->status == IB_WC_SUCCESS) {
		wc->opcode = ibt2of_wc_opcode(ibt_wc->wc_type);
	}
	wc->vendor_err = 0;			/* not supported */
	wc->byte_len = ibt_wc->wc_bytes_xfer;
	wc->qp = NULL;				/* not supported */
	wc->imm_data = htonl(ibt_wc->wc_immed_data);
	wc->src_qp = ibt_wc->wc_qpn;
	wc->wc_flags = ibt2of_wc_flags(ibt_wc->wc_flags);
	wc->pkey_index = ibt_wc->wc_pkey_ix;
	wc->slid = ibt_wc->wc_slid;
	wc->sl = ibt_wc->wc_sl;
	wc->dlid_path_bits = ibt_wc->wc_path_bits;
	wc->port_num = 0;			/* not supported */
}

/*
 * ib_poll_cq - poll a CQ for completion(s)
 * @cq:the CQ being polled
 * @num_entries:maximum number of completions to return
 * @wc:array of at least @num_entries &struct ib_wc where completions
 *   will be returned
 *
 * Poll a CQ for (possibly multiple) completions.  If the return value
 * is < 0, an error occurred.  If the return value is >= 0, it is the
 * number of completions returned.  If the return value is
 * non-negative and < num_entries, then the CQ was emptied.
 *
 * Note that three following memebers in struct ib_wc are not supported
 * currently, and the values are always either 0 or NULL.
 *	u32			vendor_err;
 *	struct ib_qp		*qp;
 *	u8			port_num;
 */
int
ib_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	ibt_wc_t	ibt_wc;
	int		npolled;
	ibt_status_t	status;
	ofs_client_t	*ofs_client = (ofs_client_t *)cq->device->clnt_hdl;

	ofs_lock_enter(&ofs_client->lock);
	if (cq->device->reg_state != IB_DEV_OPEN) {
		ofs_lock_exit(&ofs_client->lock);
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_poll_cq: cq: 0x%p => invalid device state (%d)",
		    cq, cq->device->reg_state);
		return (-ENXIO);
	}

	SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
	    "ib_poll_cq: cq: 0x%p, num_entries: %d, wc: 0x%p, "
	    "ibt_cq: 0x%p, ibt_wc: 0x%p",
	    cq, num_entries, wc, cq->ibt_cq, &ibt_wc);

	/* only one thread per cq is allowed during ibt_poll_cq() */
	mutex_enter(&cq->lock);
	for (npolled = 0; npolled < num_entries; ++npolled) {
		bzero(&ibt_wc, sizeof (ibt_wc_t));
		status = ibt_poll_cq(cq->ibt_cq, &ibt_wc, 1, NULL);
		if (status != IBT_SUCCESS) {
			break;
		}
		/* save this result to struct ib_wc */
		set_wc(&ibt_wc, wc + npolled);
	}
	mutex_exit(&cq->lock);
	ofs_lock_exit(&ofs_client->lock);

	if (status == IBT_SUCCESS || status == IBT_CQ_EMPTY) {
		return (npolled);
	}

	SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
	    "ib_poll_cq: cq: 0x%p, num_entries: %d, wc: 0x%p => "
	    "ibt_poll_cq failed w/ %d, npolled = %d",
	    cq, num_entries, wc, status, npolled);

	switch (status) {
	case IBT_HCA_HDL_INVALID:
	case IBT_CQ_HDL_INVALID:
	case IBT_INVALID_PARAM:
		return (-EINVAL);
	default:
		return (-EIO);
	}
}

ibt_hca_hdl_t
ib_get_ibt_hca_hdl(struct ib_device *device)
{
	return (device->hca_hdl);
}

ibt_channel_hdl_t
ib_get_ibt_channel_hdl(struct rdma_cm_id *cm)
{
	return (cm->qp == NULL ? NULL : cm->qp->ibt_qp);
}

int
ib_register_event_handler(struct ib_event_handler *evt_hdlr)
{
	struct ib_device	*devicep;
	ofs_client_t		*clientp;

	SOL_OFS_DPRINTF_L5(sol_kverbs_dbg_str,
	    "ib_register_event_handler(%p)", evt_hdlr);

	if (evt_hdlr == NULL)
		return (EINVAL);
	devicep = evt_hdlr->device;
	if (!devicep) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_event_handler: NULL device");
		return (EINVAL);
	}
	clientp = (ofs_client_t *)devicep->clnt_hdl;
	ASSERT(clientp);
	if (!clientp->clnt_async_hdlr) {
		clientp->client_ib_event_hdlr.handler =
		    evt_hdlr->handler;
		clientp->client_ib_event_hdlr.device =
		    evt_hdlr->device;
		clientp->clnt_async_hdlr = evt_hdlr->handler;
		clientp->clnt_async_hdlr_cnt = 1;
	} else if (clientp->clnt_async_hdlr == evt_hdlr->handler) {
		clientp->clnt_async_hdlr_cnt++;
	} else {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_register_event_handler: different event handlers");
		return (EINVAL);
	}
	return (0);
}

int
ib_unregister_event_handler(struct ib_event_handler *evt_hdlr)
{
	struct ib_device	*devicep;
	ofs_client_t		*clientp;

	SOL_OFS_DPRINTF_L5(sol_kverbs_dbg_str,
	    "ib_unregister_event_handler(%p)", evt_hdlr);

	if (evt_hdlr == NULL)
		return (EINVAL);
	devicep = evt_hdlr->device;
	if (!devicep) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_unregister_event_handler: NULL device");
		return (EINVAL);
	}
	clientp = (ofs_client_t *)devicep->clnt_hdl;
	ASSERT(clientp);
	if (clientp->clnt_async_hdlr) {
		clientp->clnt_async_hdlr_cnt--;
		if (!clientp->clnt_async_hdlr_cnt)
			clientp->clnt_async_hdlr = NULL;
	} else {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_unregister_event_handler: NULL event handler");
		return (EINVAL);
	}
	return (0);
}

int
ib_index2pkey(struct ib_device *devp, uint8_t port_num,
    uint16_t pkey_ix, ib_pkey_t *pkey)
{
	ibt_status_t	status;

	ASSERT(devp);
	status = ibt_index2pkey(devp->hca_hdl, port_num,
	    pkey_ix, pkey);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_index2pkey: ibt_index2pkey(%p, %x, %x) "
		    "failed : %x", devp->hca_hdl, port_num,
		    pkey_ix, status);
		return (EINVAL);
	}
	return (0);
}

int
ib_modify_port(struct ib_device *devp, uint8_t port_num,
    boolean_t set_op)
{
	ibt_status_t	status;

	ASSERT(devp);
	status = ibt_modify_port(devp->hca_hdl, port_num,
	    set_op ? IBT_PORT_SET_SM : IBT_PORT_RESET_SM, 0);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_modify_port: ibt_modify_port(%p, %x, %x) "
		    "failed status %x", devp->hca_hdl, port_num,
		    set_op ? IBT_PORT_SET_SM : IBT_PORT_RESET_SM,
		    status);
		return (EINVAL);
	}
	return (0);
}

int
ib_get_hca_max_chans(struct ib_device *devp, uint_t *max_chans)
{
	ibt_status_t	status;
	ibt_hca_attr_t	hca_attr;

	ASSERT(devp);
	status = ibt_query_hca(devp->hca_hdl, &hca_attr);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_get_hca_max_chans: ibt_query_hca() failed %d",
		    status);
		return (EINVAL);
	}

	*max_chans = hca_attr.hca_max_chans;
	return (0);
}

int
ib_get_hca_srq_szs(struct ib_device *devp, uint_t *max_srq_sz,
    uint_t *max_srq_sgl)
{
	ibt_status_t	status;
	ibt_hca_attr_t	hca_attr;

	ASSERT(devp);
	status = ibt_query_hca(devp->hca_hdl, &hca_attr);
	if (status != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
		    "ib_get_hca_max_srq_szs: ibt_query_hca() failed %d",
		    status);
		return (EINVAL);
	}

	*max_srq_sz = hca_attr.hca_max_srqs_sz;
	*max_srq_sgl = hca_attr.hca_max_srq_sgl;
	return (0);
}

struct ib_device *
sol_kverbs_init_ib_dev(sol_ofs_dev_t *ofs_devp, void *clientp,
    ib_guid_t hca_guid, int *err)
{
	struct ib_device	*devicep = NULL;
	ofs_client_t		*ofs_client;
	ibt_status_t		status;
	ib_device_impl_t	*impl_devp;

	ofs_client = (ofs_client_t *)clientp;
	if (err)
		*err = 0;


	/* re-use the device once it was created */
	if (clientp)
		devicep = get_device(ofs_client, hca_guid);

	SOL_OFS_DPRINTF_L5(sol_kverbs_dbg_str,
	    "sol_kverbs_init_ib_dev(%p, %p, %llx), devp %p",
	    ofs_devp, clientp, hca_guid, devicep);

	if (devicep == NULL) {
		devicep = kmem_zalloc(sizeof (*devicep), KM_NOSLEEP);
		if (!devicep) {
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "init_ib_dev: client: 0x%p => "
			    "no sufficient memory for ib_device", clientp);
			if (err)
				*err = ENOMEM;
			return (NULL);
		}
		devicep->node_type = RDMA_NODE_IB_CA;
		devicep->reg_state = IB_DEV_CLOSE;
		devicep->clnt_hdl = (ofs_client_p_t)ofs_client;
		devicep->node_guid = htonll(hca_guid);
		devicep->data = NULL;
		llist_head_init(&devicep->list, devicep);
		if (clientp) {
			llist_add_tail(&devicep->list,
			    &ofs_client->device_list);
			ofs_client->hca_num++;
		}

		devicep->impl_data = impl_devp = kmem_zalloc(
		    sizeof (ib_device_impl_t), KM_SLEEP);
		mutex_init(&impl_devp->dev_impl_mutex,
		    NULL, MUTEX_DEFAULT, NULL);
		cv_init(&impl_devp->dev_impl_cv,
		    NULL, CV_DEFAULT, NULL);
	}

	/*
	 * If clientp is NULL, this is for sol_ofs internal use only.
	 */
	if (clientp == NULL) {
		mutex_enter(&ofs_devp->ofs_dev_mutex);
		devicep->hca_hdl = ofs_devp->ofs_dev_hca_hdl;
		(void) strncpy(devicep->name, "sol_ofs", 64);
		/* local_dma_lkey uninitialized. unused sol_ofs */
		devicep->phys_port_cnt = ofs_devp->ofs_dev_nports;
		devicep->reg_state = IB_DEV_OPEN;
		mutex_exit(&ofs_devp->ofs_dev_mutex);
		return (devicep);
	}

	if (devicep->reg_state == IB_DEV_OPEN) {
		if (!ofs_devp) {
			impl_devp =
			    (ib_device_impl_t *)devicep->impl_data;
			ofs_devp = impl_devp->dev_impl_ofs_devp;
		}
		ASSERT(ofs_devp);
		mutex_enter(&sol_ofs_dev_mutex);
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "init_ib_dev 1: ofs_devp %p, "
		    "client_cnt %x incr, client nm %s",
		    ofs_devp, ofs_devp->ofs_dev_client_cnt,
		    ofs_client->ib_client->name);
		if (devicep->close_failed_state == B_TRUE) {
			ofs_devp->ofs_dev_client_cnt++;
			devicep->close_failed_state = B_FALSE;
		}
		mutex_exit(&sol_ofs_dev_mutex);

		/*
		 * Any reused ib_device because of HCA DETACH &
		 * ATTACH should have dev_impl_evt_ref as 0.
		 *
		 * Set dev_impl_evt_ref to 1. HCA DETACH will
		 * decrement this and if zero, no CQ events are
		 * been notified.
		 */
		mutex_enter(&impl_devp->dev_impl_mutex);
		impl_devp->dev_impl_evt_ref = 1;
		mutex_exit(&impl_devp->dev_impl_mutex);
		return (devicep);
	}

	status = ibt_open_hca(ofs_client->ibt_hdl, hca_guid,
	    &devicep->hca_hdl);
	if (status == IBT_SUCCESS) {
		ibt_hca_attr_t hattr;

		ofs_client->hca_open_num++;
		devicep->reg_state = IB_DEV_OPEN;

		status = ibt_query_hca(devicep->hca_hdl, &hattr);
		if (status != IBT_SUCCESS) {
			if (err)
				*err = -EIO;
			SOL_OFS_DPRINTF_L2(sol_kverbs_dbg_str,
			    "ib_register_client: client: 0x%p,"
			    "hca_hdl: 0x%p ==> "
			    "ibt_query_hca() failed w/ %d",
			    clientp, devicep->hca_hdl, status);
			(void) ibt_close_hca(devicep->hca_hdl);
			devicep->reg_state = IB_DEV_CLOSE;
			devicep->close_failed_state = B_FALSE;
			return (NULL);
		}

		(void) sprintf(devicep->name, "%x:%x:%x",
		    hattr.hca_vendor_id, hattr.hca_device_id,
		    hattr.hca_version_id);
		devicep->local_dma_lkey = hattr.hca_reserved_lkey;
		devicep->phys_port_cnt = hattr.hca_nports;

		ibt_set_hca_private(devicep->hca_hdl, devicep);

		if (!ofs_devp)
			ofs_devp = sol_ofs_find_dev(hattr.hca_dip,
			    hattr.hca_nports, B_FALSE);
		ASSERT(ofs_devp);

		(void) strncpy(devicep->ofusr_name,
		    ofs_devp->ofs_dev_ofusr_name,
		    MAXNAMELEN);
		devicep->ofusr_hca_idx =
		    ofs_devp->ofs_dev_ofusr_hca_idx;
		devicep->ofusr_port_idx =
		    ofs_devp->ofs_dev_ofusr_port_index;
		(void) strncpy(devicep->ofusr_hca_drv_name,
		    ofs_devp->ofs_dev_hca_drv_name,
		    MAXNAMELEN);
		devicep->ofusr_hca_drv_inst =
		    ofs_devp->ofs_dev_drvr_inst;
		devicep->ofusr_hca_vendorid = hattr.hca_vendor_id;
		devicep->ofusr_hca_deviceid = hattr.hca_device_id;

		impl_devp = devicep->impl_data;
		mutex_enter(&impl_devp->dev_impl_mutex);
		impl_devp->dev_impl_ofs_devp = (void *)ofs_devp;
		ASSERT(impl_devp->dev_impl_evt_ref == 0);
		impl_devp->dev_impl_evt_ref = 1;
		mutex_exit(&impl_devp->dev_impl_mutex);

		mutex_enter(&sol_ofs_dev_mutex);
		SOL_OFS_DPRINTF_L3(sol_kverbs_dbg_str,
		    "init_ib_dev 2: ofs_devp %p, "
		    "client_cnt %x incr, client nm %s",
		    ofs_devp, ofs_devp->ofs_dev_client_cnt,
		    ofs_client->ib_client->name);
		ofs_devp->ofs_dev_client_cnt++;
		mutex_exit(&sol_ofs_dev_mutex);

		/* invoke client's callback */
		if (ofs_client->ib_client->add) {
			ofs_client->ib_client->add(devicep);
		}
	}
	return (devicep);
}

/*
 * Map the IBTF error code to the OFED error code. Also
 * set a flag to indicate if this is a QP/CQ/SRQ related
 * event.
 */
static enum ib_event_type
kverbs_ibt2ofed_evnt(ibt_async_code_t ibt_code,
    boolean_t *qp_evt, boolean_t *cq_evt,
    boolean_t *srq_evt)
{
	enum ib_event_type    ofa_code;

	*qp_evt = *cq_evt = *srq_evt = B_FALSE;
	switch (ibt_code) {
	case IBT_EVENT_PATH_MIGRATED:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_PATH_MIG;
		break;

	case IBT_EVENT_SQD:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_SQ_DRAINED;
		break;

	case IBT_EVENT_COM_EST:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_COMM_EST;
		break;

	case IBT_ERROR_CATASTROPHIC_CHAN:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_QP_FATAL;
		break;

	case IBT_ERROR_INVALID_REQUEST_CHAN:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_QP_REQ_ERR;
		break;

	case IBT_ERROR_ACCESS_VIOLATION_CHAN:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_QP_ACCESS_ERR;
		break;

	case IBT_ERROR_PATH_MIGRATE_REQ:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_PATH_MIG_ERR;
		break;

	case IBT_ERROR_CQ:
		*cq_evt = B_TRUE;
		ofa_code = IB_EVENT_CQ_ERR;
		break;

	case IBT_EVENT_PORT_UP:
		ofa_code = IB_EVENT_PORT_ACTIVE;
		break;

	case IBT_ERROR_PORT_DOWN:
		ofa_code = IB_EVENT_PORT_ERR;
		break;

	case IBT_ERROR_LOCAL_CATASTROPHIC:
		/*
		 * FIRE_QP_ does not notify this
		 * event currently. Double check.
		 */
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_QP_FATAL;
		break;

	/*
	 * No mapping for IBT_HCA_ATTACH_EVENT and
	 * IBT_HCA_DETACH_EVENT, notified by the
	 * add & remove callbacks.
	 */
	case IBT_HCA_ATTACH_EVENT:
		/* FALLTHRU */
	case IBT_HCA_DETACH_EVENT:
		ofa_code = -1;
		break;

	/* No mapping for EEC events. */
	case IBT_ASYNC_OPAQUE1:
		/* FALLTHRU */
	case IBT_ASYNC_OPAQUE2:
		/* FALLTHRU */
	case IBT_ASYNC_OPAQUE3:
		/* FALLTHRU */
	case IBT_ASYNC_OPAQUE4:
		ofa_code = -1;
		break;

	case IBT_EVENT_LIMIT_REACHED_SRQ:
		*srq_evt = B_TRUE;
		ofa_code = IB_EVENT_SRQ_LIMIT_REACHED;
		break;

	case IBT_EVENT_EMPTY_CHAN:
		*qp_evt = B_TRUE;
		ofa_code = IB_EVENT_QP_LAST_WQE_REACHED;
		break;

	case IBT_ERROR_CATASTROPHIC_SRQ:
		*srq_evt = B_TRUE;
		ofa_code = IB_EVENT_SRQ_ERR;
		break;

	case IBT_PORT_CHANGE_EVENT:
		ofa_code = IB_EVENT_CLIENT_REREGISTER;
		break;

	case IBT_CLNT_REREG_EVENT:
		ofa_code = IB_EVENT_CLIENT_REREGISTER;
		break;

	case IBT_FEXCH_ERROR:
		/* TBD - Verify if this is appropriate mapping */
		ofa_code = IB_EVENT_QP_FATAL;
		*qp_evt = B_TRUE;
		break;

	default:
		ofa_code = -1;
		break;
	}

	return (ofa_code);
}
