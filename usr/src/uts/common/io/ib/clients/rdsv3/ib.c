/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file ib.c
 * Oracle elects to have and use the contents of ib.c under and governed
 * by the OpenIB.org BSD license (see below for full license text). However,
 * the following notice accompanied the original version of this file:
 */

/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <sys/sysmacros.h>
#include <sys/rds.h>

#include <sys/ib/ibtl/ibti.h>
#include <sys/ib/clients/rdsv3/rdsv3.h>
#include <sys/ib/clients/rdsv3/ib.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

unsigned int rdsv3_ib_retry_count = RDSV3_IB_DEFAULT_RETRY_COUNT;

struct list	rdsv3_ib_devices;

/* NOTE: if also grabbing ibdev lock, grab this first */
kmutex_t ib_nodev_conns_lock;
list_t ib_nodev_conns;

uint_t		rdsv3_ib_hca_count;

extern int rdsv3_ib_frag_constructor(void *buf, void *arg, int kmflags);
extern int rdsv3_ib_send_constructor(void *buf, void *arg, int kmflags);
extern void rdsv3_ib_frag_destructor(void *buf, void *arg);

void
rdsv3_ib_handle_port_down(struct rdsv3_ib_device *rds_ibdev, uint8_t portnum)
{
	struct rdsv3_ib_connection *ic, *tmpp;

	ASSERT(rds_ibdev);

	mutex_enter(&rds_ibdev->spinlock);
	RDSV3_FOR_EACH_LIST_NODE_SAFE(ic, tmpp, &rds_ibdev->conn_list,
	    ib_node) {
		if (ic->i_cm_id->port_num == portnum) {
			RDSV3_DPRINTF2("rdsv3_ib_handle_port_down",
			    "Connection (%p) %u.%u.%u.%u <--> %u.%u.%u.%u "
			    "reset", ic->conn, NIPQUAD(ic->conn->c_laddr),
			    NIPQUAD(ic->conn->c_faddr));
			rdsv3_conn_drop(ic->conn);
		}
	}
	mutex_exit(&rds_ibdev->spinlock);
}

void
rdsv3_async_handler(struct ib_event_handler *hdlrp, struct ib_event *eventp)
{
	struct rdsv3_ib_device *rds_ibdev;


	RDSV3_DPRINTF2("rdsv3_async_handler",
	    "device: %p, type: %d", eventp->device, eventp->event);

	switch (eventp->event) {
	case IB_EVENT_PORT_ERR:
		ASSERT(eventp->device);
		rds_ibdev =
		    ib_get_client_data(eventp->device, &rdsv3_ib_client);
		RDSV3_DPRINTF2("rdsv3_async_handler",
		    "rds_ibdev: %p, port error: %d", rds_ibdev,
		    eventp->element.port_num);
		rdsv3_ib_handle_port_down(rds_ibdev,
		    eventp->element.port_num);
		break;
	case IB_EVENT_PORT_ACTIVE:
		break;
	default:
		break;
	}

	RDSV3_DPRINTF2("rdsv3_async_handler",
	    "Return: device: %p, type: %d", hdlrp->device, eventp->event);
}

void
rdsv3_ib_add_one(ib_device_t *device)
{
	struct rdsv3_ib_device *rds_ibdev;
	ibt_hca_attr_t *dev_attr;
	struct ib_event_handler async_hdlr;
	char name[64];

	RDSV3_DPRINTF2("rdsv3_ib_add_one", "device: %p", device);

	/* Only handle IB (no iWARP) devices */
	if (device->node_type != RDMA_NODE_IB_CA)
		return;

	dev_attr = (ibt_hca_attr_t *)kmem_alloc(sizeof (*dev_attr),
	    KM_NOSLEEP);
	if (!dev_attr)
		return;

	if (ibt_query_hca(ib_get_ibt_hca_hdl(device), dev_attr)) {
		RDSV3_DPRINTF2("rdsv3_ib_add_one",
		    "Query device failed for %s", device->name);
		goto free_attr;
	}

	/* We depend on Reserved Lkey */
	if (!(dev_attr->hca_flags2 & IBT_HCA2_RES_LKEY)) {
		RDSV3_DPRINTF2("rdsv3_ib_add_one",
		    "Reserved Lkey support is required: %s",
		    device->name);
		goto free_attr;
	}

	/* We depend on "inline" */
	if (!(dev_attr->hca_flags & IBT_HCA_WQE_SIZE_INFO) ||
	    (rdsv3_max_inline + sizeof (struct rdsv3_header) >
	    dev_attr->hca_conn_send_inline_sz)) {
		RDSV3_DPRINTF2("rdsv3_ib_add_one",
		    "'inline' support is required: %s",
		    device->name);
		goto free_attr;
	}

	rds_ibdev = kmem_zalloc(sizeof (*rds_ibdev), KM_NOSLEEP);
	if (!rds_ibdev)
		goto free_attr;

	rds_ibdev->ibt_hca_hdl = ib_get_ibt_hca_hdl(device);
	rds_ibdev->hca_attr =  *dev_attr;

	rw_init(&rds_ibdev->rwlock, NULL, RW_DRIVER, NULL);
	mutex_init(&rds_ibdev->spinlock, NULL, MUTEX_DRIVER, NULL);

	rds_ibdev->max_wrs = dev_attr->hca_max_chan_sz;
	rds_ibdev->max_sge = min(dev_attr->hca_max_sgl, RDSV3_IB_MAX_SGE);
	rds_ibdev->max_inline = rdsv3_max_inline + sizeof (struct rdsv3_header);

	rds_ibdev->max_initiator_depth = (uint_t)dev_attr->hca_max_rdma_in_qp;
	rds_ibdev->max_responder_resources =
	    (uint_t)dev_attr->hca_max_rdma_in_qp;

	rds_ibdev->dev = device;
	rds_ibdev->pd = ib_alloc_pd(device);
	if (IS_ERR(rds_ibdev->pd))
		goto free_dev;

	if (rdsv3_ib_create_mr_pool(rds_ibdev) != 0) {
		goto free_dev;
	}

	if (rdsv3_ib_create_inc_pool(rds_ibdev) != 0) {
		rdsv3_ib_destroy_mr_pool(rds_ibdev);
		goto free_dev;
	}

	/* rdsv3_ib_refill_fn is expecting i_max_recv_alloc set */
	rds_ibdev->ib_max_recv_alloc =
	    rdsv3_ib_sysctl_max_recv_allocation / rdsv3_ib_hca_count;
	rds_ibdev->ib_recv_alloc = 0;

	(void) snprintf(name, 64, "RDSV3_IB_FRAG_%llx",
	    (longlong_t)htonll(dev_attr->hca_node_guid));
	rds_ibdev->ib_frag_slab = kmem_cache_create(name,
	    sizeof (struct rdsv3_page_frag), 0, rdsv3_ib_frag_constructor,
	    rdsv3_ib_frag_destructor, NULL, (void *)rds_ibdev, NULL, 0);
	if (rds_ibdev->ib_frag_slab == NULL) {
		RDSV3_DPRINTF2("rdsv3_ib_add_one",
		    "kmem_cache_create for ib_frag_slab failed for device: %s",
		    device->name);
		rdsv3_ib_destroy_mr_pool(rds_ibdev);
		rdsv3_ib_destroy_inc_pool(rds_ibdev);
		goto free_dev;
	}

	(void) snprintf(name, 64, "RDSV3_IB_SEND_%llx",
	    (longlong_t)htonll(dev_attr->hca_node_guid));
	rds_ibdev->ib_send_slab = kmem_cache_create(name,
	    sizeof (struct rdsv3_page_frag), 0, rdsv3_ib_send_constructor,
	    rdsv3_ib_frag_destructor, NULL, (void *)rds_ibdev, NULL, 0);
	if (rds_ibdev->ib_send_slab == NULL) {
		RDSV3_DPRINTF2("rdsv3_ib_add_one",
		    "kmem_cache_create for ib_frag_slab failed for device: %s",
		    device->name);
		rdsv3_ib_destroy_mr_pool(rds_ibdev);
		rdsv3_ib_destroy_inc_pool(rds_ibdev);
		kmem_cache_destroy(rds_ibdev->ib_frag_slab);
		goto free_dev;
	}

	rds_ibdev->aft_hcagp = rdsv3_af_grp_create(rds_ibdev->ibt_hca_hdl,
	    (uint64_t)rds_ibdev->hca_attr.hca_node_guid);
	if (rds_ibdev->aft_hcagp == NULL) {
		rdsv3_ib_destroy_mr_pool(rds_ibdev);
		rdsv3_ib_destroy_inc_pool(rds_ibdev);
		kmem_cache_destroy(rds_ibdev->ib_frag_slab);
		kmem_cache_destroy(rds_ibdev->ib_send_slab);
		goto free_dev;
	}
	rds_ibdev->fmr_soft_cq = rdsv3_af_thr_create(rdsv3_ib_drain_mrlist_fn,
	    (void *)rds_ibdev->fmr_pool, SCQ_HCA_BIND_CPU,
	    rds_ibdev->aft_hcagp, NULL, "rdsv3_ib_drain_mrlist_fn");
	if (rds_ibdev->fmr_soft_cq == NULL) {
		rdsv3_af_grp_destroy(rds_ibdev->aft_hcagp);
		rdsv3_ib_destroy_mr_pool(rds_ibdev);
		rdsv3_ib_destroy_inc_pool(rds_ibdev);
		kmem_cache_destroy(rds_ibdev->ib_frag_slab);
		kmem_cache_destroy(rds_ibdev->ib_send_slab);
		goto free_dev;
	}

	rdsv3_ib_create_wkr_pool(rds_ibdev);
	rds_ibdev->wkr_soft_cq = rdsv3_af_thr_create(rdsv3_ib_drain_wkrlist,
	    (void *)rds_ibdev->wkr_pool, SCQ_HCA_BIND_CPU,
	    rds_ibdev->aft_hcagp, NULL, "rdsv3_ib_drain_wkrlist");
	if (rds_ibdev->wkr_soft_cq == NULL) {
		rdsv3_af_thr_destroy(rds_ibdev->fmr_soft_cq);
		rdsv3_af_grp_destroy(rds_ibdev->aft_hcagp);
		rdsv3_ib_destroy_mr_pool(rds_ibdev);
		rdsv3_ib_destroy_inc_pool(rds_ibdev);
		rdsv3_ib_destroy_wkr_pool(rds_ibdev);
		kmem_cache_destroy(rds_ibdev->ib_frag_slab);
		kmem_cache_destroy(rds_ibdev->ib_send_slab);
		goto free_dev;
	}

	list_create(&rds_ibdev->ipaddr_list, sizeof (struct rdsv3_ib_ipaddr),
	    offsetof(struct rdsv3_ib_ipaddr, list));
	list_create(&rds_ibdev->conn_list, sizeof (struct rdsv3_ib_connection),
	    offsetof(struct rdsv3_ib_connection, ib_node));

	list_insert_tail(&rdsv3_ib_devices, rds_ibdev);

	ib_set_client_data(device, &rdsv3_ib_client, rds_ibdev);

	/* async event handler */
	async_hdlr.device = device;
	async_hdlr.handler = rdsv3_async_handler;
	(void) ib_register_event_handler(&async_hdlr);

	RDSV3_DPRINTF2("rdsv3_ib_add_one", "Return: device: %p", device);

	goto free_attr;

err_pd:
	(void) ib_dealloc_pd(rds_ibdev->pd);
free_dev:
	mutex_destroy(&rds_ibdev->spinlock);
	rw_destroy(&rds_ibdev->rwlock);
	kmem_free(rds_ibdev, sizeof (*rds_ibdev));
free_attr:
	kmem_free(dev_attr, sizeof (*dev_attr));
}

void
rdsv3_ib_remove_one(struct ib_device *device)
{
	struct rdsv3_ib_device *rds_ibdev;
	struct rdsv3_ib_ipaddr *i_ipaddr, *i_next;
	struct ib_event_handler async_hdlr;

	RDSV3_DPRINTF2("rdsv3_ib_remove_one", "device: %p", device);

	/* async event handler */
	async_hdlr.device = device;
	async_hdlr.handler = NULL;
	(void) ib_unregister_event_handler(&async_hdlr);

	rds_ibdev = ib_get_client_data(device, &rdsv3_ib_client);
	if (!rds_ibdev)
		return;

#ifdef HCA_HOT_REMOVAL
	RDSV3_FOR_EACH_LIST_NODE_SAFE(i_ipaddr, i_next, &rds_ibdev->ipaddr_list,
	    list) {
		list_remove_node(&i_ipaddr->list);
		kmem_free(i_ipaddr, sizeof (*i_ipaddr));
	}

	rdsv3_ib_destroy_conns(rds_ibdev);

	if (rds_ibdev->wkr_soft_cq)
		rdsv3_af_thr_destroy(rds_ibdev->wkr_soft_cq);
	if (rds_ibdev->fmr_soft_cq)
		rdsv3_af_thr_destroy(rds_ibdev->fmr_soft_cq);

	rdsv3_ib_destroy_mr_pool(rds_ibdev);
	rdsv3_ib_destroy_inc_pool(rds_ibdev);
	rdsv3_ib_destroy_wkr_pool(rds_ibdev);

	kmem_cache_destroy(rds_ibdev->ib_frag_slab);

	rdsv3_af_grp_destroy(rds_ibdev->aft_hcagp);

#if 0
	while (ib_dealloc_pd(rds_ibdev->pd)) {
#ifndef __lock_lint
		RDSV3_DPRINTF5("rdsv3_ib_remove_one",
		    "%s-%d Failed to dealloc pd %p",
		    __func__, __LINE__, rds_ibdev->pd);
#endif
		delay(drv_usectohz(1000));
	}
#else
	if (ib_dealloc_pd(rds_ibdev->pd)) {
#ifndef __lock_lint
		RDSV3_DPRINTF2("rdsv3_ib_remove_one",
		    "Failed to dealloc pd %p\n", rds_ibdev->pd);
#endif
	}
#endif

	list_destroy(&rds_ibdev->ipaddr_list);
	list_destroy(&rds_ibdev->conn_list);
	list_remove_node(&rds_ibdev->list);
	mutex_destroy(&rds_ibdev->spinlock);
	rw_destroy(&rds_ibdev->rwlock);
	kmem_free(rds_ibdev, sizeof (*rds_ibdev));
#endif /* HCA_HOT_REMOVAL */

	RDSV3_DPRINTF2("rdsv3_ib_remove_one", "Return: device: %p", device);
}

#ifndef __lock_lint
struct ib_client rdsv3_ib_client = {
	.name		= "rdsv3_ib",
	.add		= rdsv3_ib_add_one,
	.remove		= rdsv3_ib_remove_one,
	.clnt_hdl	= NULL,
	.state		= IB_CLNT_UNINITIALIZED
};
#else
struct ib_client rdsv3_ib_client = {
	"rdsv3_ib",
	rdsv3_ib_add_one,
	rdsv3_ib_remove_one,
	NULL,
	NULL,
	IB_CLNT_UNINITIALIZED
};
#endif

static int
rds_ib_conn_info_visitor(struct rdsv3_connection *conn,
    void *buffer)
{
	struct rds_info_rdma_connection *iinfo = buffer;
	struct rdsv3_ib_connection *ic;

	RDSV3_DPRINTF4("rds_ib_conn_info_visitor", "conn: %p buffer: %p",
	    conn, buffer);

	/* We will only ever look at IB transports */
	if (conn->c_trans != &rdsv3_ib_transport)
		return (0);

	iinfo->src_addr = conn->c_laddr;
	iinfo->dst_addr = conn->c_faddr;

	(void) memset(&iinfo->src_gid, 0, sizeof (iinfo->src_gid));
	(void) memset(&iinfo->dst_gid, 0, sizeof (iinfo->dst_gid));
	if (rdsv3_conn_state(conn) == RDSV3_CONN_UP) {
		struct rdsv3_ib_device *rds_ibdev;
		struct rdma_dev_addr *dev_addr;

		ic = conn->c_transport_data;
		dev_addr = &ic->i_cm_id->route.addr.dev_addr;

		ib_addr_get_sgid(dev_addr, (union ib_gid *)&iinfo->src_gid);
		ib_addr_get_dgid(dev_addr, (union ib_gid *)&iinfo->dst_gid);

		rds_ibdev = ib_get_client_data(ic->i_cm_id->device,
		    &rdsv3_ib_client);
		iinfo->max_send_wr = ic->i_send_ring.w_nr;
		iinfo->max_recv_wr = ic->i_recv_ring.w_nr;
		iinfo->max_send_sge = rds_ibdev->max_sge;
	}

	RDSV3_DPRINTF4("rds_ib_conn_info_visitor", "conn: %p buffer: %p",
	    conn, buffer);
	return (1);
}

static void
rds_ib_ic_info(struct rsock *sock, unsigned int len,
    struct rdsv3_info_iterator *iter,
    struct rdsv3_info_lengths *lens)
{
	RDSV3_DPRINTF4("rds_ib_ic_info", "sk: %p iter: %p, lens: %p, len: %d",
	    sock, iter, lens, len);

	rdsv3_for_each_conn_info(sock, len, iter, lens,
	    rds_ib_conn_info_visitor,
	    sizeof (struct rds_info_rdma_connection));
}

/*
 * Early RDS/IB was built to only bind to an address if there is an IPoIB
 * device with that address set.
 *
 * If it were me, I'd advocate for something more flexible.  Sending and
 * receiving should be device-agnostic.  Transports would try and maintain
 * connections between peers who have messages queued.  Userspace would be
 * allowed to influence which paths have priority.  We could call userspace
 * asserting this policy "routing".
 */
static int
rds_ib_laddr_check(uint32_be_t addr, void **dev)
{
	int ret;
	struct rdma_cm_id *cm_id;
	struct sockaddr_in sin;

	RDSV3_DPRINTF4("rds_ib_laddr_check", "addr: %x", ntohl(addr));

	/*
	 * Create a CMA ID and try to bind it. This catches both
	 * IB and iWARP capable NICs.
	 */
	cm_id = rdma_create_id(NULL, NULL, RDMA_PS_TCP);
	if (!cm_id)
		return (-EADDRNOTAVAIL);

	(void) memset(&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = rdsv3_scaddr_to_ibaddr(addr);

	/* rdma_bind_addr will only succeed for IB & iWARP devices */
	ret = rdma_bind_addr(cm_id, (struct sockaddr *)&sin);
	/*
	 * due to this, we will claim to support iWARP devices unless we
	 * check node_type.
	 */
	if (ret || !cm_id->device ||
	    cm_id->device->node_type != RDMA_NODE_IB_CA) {
		RDSV3_DPRINTF2("rds_ib_laddr_check",
		    "addr %u.%u.%u.%u ret %d node type %d",
		    NIPQUAD(addr), ret,
		    cm_id->device ? cm_id->device->node_type : -1);
		rdma_destroy_id(cm_id);
		return (-EADDRNOTAVAIL);
	}

	if (dev != NULL) {
		*dev = ib_get_client_data(cm_id->device, &rdsv3_ib_client);
		if (*dev == NULL) {
			RDSV3_DPRINTF2("rds_ib_laddr_check",
			    "HCA not initialized for addr %u.%u.%u.%u",
			    NIPQUAD(addr));
			rdma_destroy_id(cm_id);
			return (-EADDRNOTAVAIL);
		}
	}

	rdma_destroy_id(cm_id);

	return (ret);
}

void
rdsv3_ib_exit(void)
{
	RDSV3_DPRINTF4("rds_ib_exit", "Enter");

	rdsv3_info_deregister_func(RDS_INFO_IB_CONNECTIONS, rds_ib_ic_info);
	rdsv3_ib_destroy_nodev_conns();
	ib_unregister_client(&rdsv3_ib_client);
	rdsv3_ib_sysctl_exit();
	rdsv3_ib_recv_exit();
	rdsv3_trans_unregister(&rdsv3_ib_transport);
	kmem_free(rdsv3_ib_stats,
	    nr_cpus * sizeof (struct rdsv3_ib_statistics));
	rdsv3_ib_stats = NULL;
	mutex_destroy(&ib_nodev_conns_lock);
	list_destroy(&ib_nodev_conns);
	list_destroy(&rdsv3_ib_devices);

	RDSV3_DPRINTF4("rds_ib_exit", "Return");
}

#ifndef __lock_lint
struct rdsv3_transport rdsv3_ib_transport = {
	.laddr_check		= rds_ib_laddr_check,
	.copy_from_user		= rdsv3_ib_message_copy_from_user,
	.message_purge		= rdsv3_ib_message_purge,
	.xmit_prepare		= NULL,
	.xmit_complete		= rdsv3_ib_xmit_complete,
	.xmit			= rdsv3_ib_xmit,
	.alloc_cong_map		= rdsv3_ib_alloc_cong_map,
	.xmit_cong_map		= NULL,
	.free_cong_map		= rdsv3_ib_free_cong_map,
	.xmit_rdma		= rdsv3_ib_xmit_rdma,
	.recv			= rdsv3_ib_recv,
	.conn_alloc		= rdsv3_ib_conn_alloc,
	.conn_free		= rdsv3_ib_conn_free,
	.conn_connect		= rdsv3_ib_conn_connect,
	.conn_shutdown		= rdsv3_ib_conn_shutdown,
	.inc_copy_to_user	= rdsv3_ib_inc_copy_to_user,
	.inc_free		= rdsv3_ib_inc_free,
	.cm_initiate_connect	= rdsv3_ib_cm_initiate_connect,
	.cm_handle_connect	= rdsv3_ib_cm_handle_connect,
	.cm_connect_complete	= rdsv3_ib_cm_connect_complete,
	.stats_info_copy	= rdsv3_ib_stats_info_copy,
	.exit			= rdsv3_ib_exit,
	.get_mr			= rdsv3_ib_get_mr,
	.sync_mr		= rdsv3_ib_sync_mr,
	.free_mr		= rdsv3_ib_free_mr,
	.flush_mrs		= rdsv3_ib_flush_mrs,
	.init_delayed_msg	= rdsv3_ib_init_delayed_msg,
	.queue_delayed_msg	= rdsv3_ib_queue_delayed_msg,
	.cancel_delayed_msg	= rdsv3_ib_cancel_delayed_msg,
	.t_name			= "infiniband",
	.t_type			= RDS_TRANS_IB
};
#else
struct rdsv3_transport rdsv3_ib_transport;
#endif

int
rdsv3_ib_init(void)
{
	int ret;

	RDSV3_DPRINTF4("rds_ib_init", "Enter");

	list_create(&rdsv3_ib_devices, sizeof (struct rdsv3_ib_device),
	    offsetof(struct rdsv3_ib_device, list));
	list_create(&ib_nodev_conns, sizeof (struct rdsv3_ib_connection),
	    offsetof(struct rdsv3_ib_connection, ib_node));
	mutex_init(&ib_nodev_conns_lock, NULL, MUTEX_DRIVER, NULL);

	/* allocate space for ib statistics */
	ASSERT(rdsv3_ib_stats == NULL);
	rdsv3_ib_stats = kmem_zalloc(nr_cpus *
	    sizeof (struct rdsv3_ib_statistics), KM_SLEEP);

	rdsv3_ib_hca_count = ibt_get_hca_list(NULL);
	if (rdsv3_ib_hca_count == 0)
		rdsv3_ib_hca_count = 1;

	ret = rdsv3_ib_sysctl_init();
	if (ret)
		goto out_ibreg;

	rdsv3_ib_client.dip = rdsv3_dev_info;
	ret = ib_register_client(&rdsv3_ib_client);
	if (ret)
		goto out;

	ret = rdsv3_ib_recv_init();
	if (ret)
		goto out_sysctl;

	ret = rdsv3_trans_register(&rdsv3_ib_transport);
	if (ret)
		goto out_recv;

	rdsv3_info_register_func(RDS_INFO_IB_CONNECTIONS, rds_ib_ic_info);

	RDSV3_DPRINTF4("rds_ib_init", "Return");

	return (0);

out_recv:
	rdsv3_ib_recv_exit();
out_sysctl:
	rdsv3_ib_sysctl_exit();
out_ibreg:
	ib_unregister_client(&rdsv3_ib_client);
out:
	kmem_free(rdsv3_ib_stats,
	    nr_cpus * sizeof (struct rdsv3_ib_statistics));
	rdsv3_ib_stats = NULL;
	mutex_destroy(&ib_nodev_conns_lock);
	list_destroy(&ib_nodev_conns);
	list_destroy(&rdsv3_ib_devices);
	return (ret);
}

void
rdsv3_ib_init_delayed_msg(rdsv3_delayed_work_t *dwp, void *dev)
{
	dwp->wq = ((struct rdsv3_ib_device *)dev)->wkr_pool;
}

static void
rdsv3_ib_q_work(rdsv3_delayed_work_t *dwp)
{
	rdsv3_workqueue_struct_t *wq = dwp->wq;
	rdsv3_work_t *wp = &dwp->work;
	struct rdsv3_ib_device *rds_ibdev = (struct rdsv3_ib_device *)
	    wq->wq_dev;
	rdsv3_af_thr_t *af_thr = rds_ibdev->wkr_soft_cq;

	RDSV3_DPRINTF4("rdsv3_ib_q_work", "Enter(ibdev: %p wq: %p dwp: %p)",
	    rds_ibdev, wq, dwp);

	dwp->timeid = 0;
	mutex_enter(&wq->wq_lock);
	if (list_link_active(&wp->work_item)) {
		mutex_exit(&wq->wq_lock);
		return;
	}
	list_insert_tail(&wq->wq_queue, wp);
	wq->wq_cnt++;
	mutex_exit(&wq->wq_lock);

	rdsv3_af_thr_fire(af_thr);
}

void
rdsv3_ib_queue_delayed_msg(rdsv3_delayed_work_t *dwp, uint_t delay)
{
	if (delay == 0) {
		rdsv3_ib_q_work(dwp);
		return;
	}
	mutex_enter(&dwp->lock);
	if (dwp->timeid == 0) {
		dwp->timeid = timeout((void (*) (void*))rdsv3_ib_q_work,
		    dwp, delay);
	}
	mutex_exit(&dwp->lock);
}

void
rdsv3_ib_cancel_delayed_msg(rdsv3_delayed_work_t *dwp)
{
	timeout_id_t tid;

	tid = dwp->timeid;
	if (tid != 0) {
		dwp->timeid = 0;
		(void) untimeout(tid);
	}
}

void
rdsv3_ib_drain_wkrlist(void *data)
{
	rdsv3_workqueue_struct_t *wq = data;
	kmutex_t *lockp = &wq->wq_lock;
	rdsv3_work_t *work;
	list_t wkrlist;
	list_t *wkrlistp = &wkrlist;

	list_create(wkrlistp, sizeof (struct rdsv3_work_s),
	    offsetof(struct rdsv3_work_s, work_item));
	for (;;) {
		mutex_enter(lockp);
		if (wq->wq_cnt == 0) {
			mutex_exit(lockp);
			list_destroy(wkrlistp);
			return;
		}
		list_move_tail(wkrlistp, &wq->wq_queue);
		wq->wq_cnt = 0;
		mutex_exit(lockp);

		for (;;) {
			work = list_remove_head(wkrlistp);
			if (!work)
				break;
			work->func(work);
		}
	}
}

void
rdsv3_ib_create_wkr_pool(struct rdsv3_ib_device *rds_ibdev)
{
	rdsv3_workqueue_struct_t *wq;

	RDSV3_DPRINTF2("rdsv3_ib_create_wkr_pool", "Enter(ibdev: %p)",
	    rds_ibdev);

	wq = &rds_ibdev->wkr_data;
	rds_ibdev->wkr_pool = wq;
	list_create(&wq->wq_queue, sizeof (struct rdsv3_work_s),
	    offsetof(struct rdsv3_work_s, work_item));
	mutex_init(&wq->wq_lock, NULL, MUTEX_DRIVER, NULL);
	wq->wq_dev = (void *)rds_ibdev;
	wq->wq_cnt = 0;

	RDSV3_DPRINTF2("rdsv3_ib_create_wkrq", "wq: %p", wq);
}

void
rdsv3_ib_destroy_wkr_pool(struct rdsv3_ib_device *rds_ibdev)
{
	rdsv3_workqueue_struct_t *wq;

	RDSV3_DPRINTF2("rdsv3_ib_destroy_wkr_pool", "Enter(ibdev: %p)",
	    rds_ibdev);

	wq = rds_ibdev->wkr_pool;
	list_destroy(&wq->wq_queue);
	mutex_destroy(&wq->wq_lock);
	wq->wq_dev = NULL;
	rds_ibdev->wkr_pool = NULL;
}
