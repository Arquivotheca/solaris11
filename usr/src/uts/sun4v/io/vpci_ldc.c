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

#include <sys/conf.h>
#include <sys/sunndi.h>
#include <sys/archsystm.h>
#include <sys/note.h>
#include <sys/pcie_impl.h>
#include <sys/sysmacros.h>

#include <sys/mdeg.h>
#include <sys/ldoms.h>
#include <sys/ldc.h>
#include <sys/vio_mailbox.h>
#include <sys/vpci_var.h>

/* Global variables definitions */
uint64_t	vpci_min_timeout_ldc = 1 * MILLISEC;
uint64_t	vpci_max_timeout_ldc = 100 * MILLISEC;

extern uint64_t	vpci_tx_check_interval;

/* Defined for thread_create apis */
extern pri_t minclsyspri;

/*
 * Supported vpci protocol version pairs.
 *
 * The first array entry is the latest and preferred version.
 */
static const vio_ver_t	vpci_version[] = {{VPCI_VER_MAJOR, VPCI_VER_MINOR}};

/* Taskq routines */
static void	vpci_msg_thread(void *arg);

/*
 * Return true if the "type", "subtype", and "env" fields of the "tag" first
 * argument match the corresponding remaining arguments; otherwise, return false
 */
#define	VPCI_CHECK_MSGTYPE_SUBTYPE(_tag, _type, _subtype, _env)	\
	    (((_tag.vio_msgtype == _type) &&			\
	    (_tag.vio_subtype == _subtype) &&			\
	    (_tag.vio_subtype_env == _env)) ? B_TRUE : B_FALSE)

/*
 * Wrappers for LDC channel APIs
 *
 * These wrapper function could be reused by any other driver functions
 * to deal with LDC channel stuffs.
 */

/*
 * Function:
 *	vpci_terminate_ldc()
 *
 * Description:
 * 	Teardown LDC link for given vport
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code: N/A
 */
void
vpci_terminate_ldc(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;

	if (vport->ldc_init_flag & VPCI_LDC_OPEN) {
		DMSGX(0, "[%d] ldc_close()\n", vpci->instance);
		(void) ldc_close(vport->ldc_handle);
	}
	if (vport->ldc_init_flag & VPCI_LDC_CB) {
		DMSGX(0, "[%d] ldc_unreg_callback()\n", vpci->instance);
		(void) ldc_unreg_callback(vport->ldc_handle);
	}
	if (vport->ldc_init_flag & VPCI_LDC_INIT) {
		DMSGX(0, "[%d] ldc_fini()\n", vpci->instance);
		(void) ldc_fini(vport->ldc_handle);
		vport->ldc_handle = NULL;
	}

	vport->ldc_init_flag &= ~(VPCI_LDC_INIT | VPCI_LDC_CB | VPCI_LDC_OPEN);
}


/*
 * Function:
 *	vpci_do_ldc_init()
 *
 * Description:
 *	Initialize LDC link for given vport
 *
 * Arguments:
 *	vport		- Virtual pci port pointer
 *	vpci_ldc_cb	- LDC interrupt callback pointer
 *
 * Return Code:
 * 	0	- Success
 * 	EINVAL	- Invalid inputs
 * 	EIO	- I/O error
 */
int
vpci_do_ldc_init(vpci_port_t *vport, vpci_ldc_cb_t vpci_ldc_cb)
{
	vpci_t			*vpci = vport->vpci;
	int			rv = 0;
	ldc_attr_t		ldc_attr;

	ldc_attr.devclass = LDC_DEV_PCIV;
	ldc_attr.instance = vpci->instance;
	ldc_attr.mode = LDC_MODE_UNRELIABLE;	/* unreliable transport */
	ldc_attr.mtu = VPCI_LDC_MTU;

	rv = ldc_init(vport->ldc_id, &ldc_attr,
	    &vport->ldc_handle);
	if (rv != 0) {
		DMSGX(0, "[%d] ldc_init(chan %ld) returned %d\n",
		    vpci->instance, vport->ldc_id, rv);
		return (rv);
	}
	vport->ldc_init_flag |= VPCI_LDC_INIT;

	rv = ldc_reg_callback(vport->ldc_handle, vpci_ldc_cb,
	    (caddr_t)vport);
	if (rv != 0) {
		DMSGX(0, "[%d] LDC callback reg. failed (%d)\n",
		    vpci->instance, rv);
		goto init_exit;
	}
	vport->ldc_init_flag |= VPCI_LDC_CB;

	/*
	 * At this stage we have initialised LDC, we will now try and open
	 * the connection.
	 */
	rv = ldc_open(vport->ldc_handle);
	if (rv != 0) {
		DMSGX(0, "[%d] ldc_open(chan %ld) returned %d\n",
		    vpci->instance, vport->ldc_id, rv);
		goto init_exit;
	}
	vport->ldc_init_flag |= VPCI_LDC_OPEN;

init_exit:
	if (rv) {
		vpci_terminate_ldc(vport);
	}

	return (rv);
}


/*
 * Function:
 *	vpci_get_ldc_status()
 *
 * Description:
 * 	Query and update LDC link status for given vport
 *
 * Arguments:
 *	vport		- Virtual pci port pointer
 *	ldc_state	- LDC link status pointer
 *
 * Return Code:
 * 	0	- Success
 * 	EINVAL	- Invalid inputs
 */
int
vpci_get_ldc_status(vpci_port_t *vport, ldc_status_t *ldc_state)
{
	vpci_t	*vpci = vport->vpci;
	int	rv;

	rv = ldc_status(vport->ldc_handle, ldc_state);
	if (rv != 0)
		DMSGX(0, "[%d] Cannot discover LDC status [err=%d]",
		    vpci->instance, rv);

	return (rv);
}

/*
 * Function:
 *	vpci_do_ldc_up()
 *
 * Description:
 *	Bring the LDC link up for given vport
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 *	0		- Success.
 *	EINVAL		- Input is not valid
 *	ECONNREFUSED	- Other end is not listening
 */
int
vpci_do_ldc_up(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;
	int	rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	DMSG(vpci, 0, "[%d] Bringing up channel %lx\n",
	    vpci->instance, vport->ldc_id);

	if ((rv = ldc_up(vport->ldc_handle)) != 0) {

		switch (rv) {
		case ECONNREFUSED:	/* listener not ready at other end */
			DMSG(vpci, 0, "[%d] ldc_up(%lx,...) return %d\n",
			    vpci->instance, vport->ldc_id, rv);
			break;
		default:
			DMSG(vpci, 0, "[%d] Failed to bring up LDC: "
			    "channel=%ld, err=%d", vpci->instance,
			    vport->ldc_id, rv);
			break;
		}
	}

	/* reset session state */
	vpci_reset_session(vport);

	return (rv);
}

/*
 * Function:
 *	vpci_do_ldc_down()
 *
 * Description:
 *	Bring the LDC link down for given vport
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 *	0		- Success.
 *	EINVAL		- Input is not valid
 */
int
vpci_do_ldc_down(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;
	int	rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	rv = ldc_down(vport->ldc_handle);

	DMSG(vpci, 0, "ldc_down() = %d\n", rv);

	return (rv);
}

/*
 * Function:
 *	vpci_ldc_cb_disable()
 *
 * Description:
 * 	Disable LDC callback
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 *	0	- Success.
 *	<0	- Failed
 */
int
vpci_ldc_cb_disable(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;
	int	rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	rv = ldc_set_cb_mode(vport->ldc_handle, LDC_CB_DISABLE);

	DMSG(vpci, 0, "ldc_set_cb_mode() = %d\n", rv);

	return (rv);
}

/*
 * Function:
 *	vpci_ldc_cb_enable()
 *
 * Description:
 * 	Enable LDC callback
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 *	0	- Success.
 *	<0	- Failed
 */
int
vpci_ldc_cb_enable(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;
	int	rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	rv = ldc_set_cb_mode(vport->ldc_handle, LDC_CB_ENABLE);

	DMSG(vpci, 0, "ldc_set_cb_mode() = %d\n", rv);

	return (rv);
}

/*
 * Function:
 *	vpci_get_domain_id()
 *
 * Description:
 *	Get Domain id by using virtual deivce port id
 *
 * Arguments:
 *	vport_id	- Virutal device port id
 *
 * Return Code:
 *	>0		- A valid Domain id
 *	0		- An invalid Domain id
 */
dom_id_t
vpci_get_domain_id(uint64_t vport_id)
{
	/*
	 * A zero value of Domain id indicates that it
	 * is overflowed. Since vport_id is uint64_t,
	 * it should not happen in the real world.
	 */
	ASSERT(vport_id + 1  != 0);
	return (vport_id + 1);
}

/*
 * Function:
 *	vpci_do_parse_dev_md()
 *
 * Description:
 * 	Get device assignment from "iov-device" nodes and notify PCIE framework
 *
 * Arguments:
 *	md		- Machine description handle pointer
 *	vpci_node	- Machine description element cookie
 *	iov_dev		- mde_cookie_t pointer for "iov-device" node
 *	vport_id	- Virutal device port id
 *	action		- Action against each MD nodes
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
static int
vpci_do_parse_dev_md(md_t *md, mde_cookie_t vpci_node,
    mde_cookie_t *iov_dev, uint64_t vport_id, vpci_md_action_t action)
{
	int		num_dev;
	dom_id_t	domain_id, prev_domain_id;
	pcie_fn_type_t	type, prev_type;
	char		*devpath = NULL;
	uint64_t	numvfs, vfid;
	int		rv;

	/* Look for iov_device child(ren) of the vpci port MD node */
	if ((num_dev = md_scan_dag(md, vpci_node,
	    md_find_name(md, VPCI_MD_IOV_DEV_NAME),
	    md_find_name(md, "fwd"), iov_dev)) <= 0) {
		/* It's possible no iov_device MD nodes */
		cmn_err(CE_NOTE,
		    "No \"%s\" found for vport%lu\n",
		    VPCI_MD_IOV_DEV_NAME, vport_id);
		return (DDI_SUCCESS);
	}

	domain_id = vpci_get_domain_id(vport_id);

	for (int i = 0; i < num_dev; i ++) {

		/* Get the "path" value for the first iov_dev node */
		if (md_get_prop_str(md, iov_dev[i], VPCI_MD_IOV_DEV_PATH,
		    &devpath) != 0) {
			cmn_err(CE_WARN,
			    "vpci: No \"%s\" property found for \"%s\" "
			    "of vport %lu\n",
			    VPCI_MD_IOV_DEV_PATH, VPCI_MD_IOV_DEV_NAME,
			    vport_id);
			return (DDI_FAILURE);
		}

		/* Check function type by using "numvfs" and "vf-id" props */
		if (md_get_prop_val(md, iov_dev[i], VPCI_MD_IOV_DEV_NUMVFS,
		    &numvfs) == 0) {
			cmn_err(CE_WARN,
			    "vpci: Can not support PF(numvfs=%lu), vport=%lu\n",
			    numvfs, vport_id);
			continue;
		} else if (md_get_prop_val(md, iov_dev[i], VPCI_MD_IOV_DEV_VFID,
		    &vfid) == 0) {
			type = FUNC_TYPE_VF;
			DMSGX(0, "vpci: Found a VF(vfid=%lu), vport=%lu\n",
			    vfid, vport_id);
		} else {
			type = FUNC_TYPE_REGULAR;
		}

		switch (action) {
		case VPCI_MD_ADD:
			/*
			 * Store the device assignment info on the device tree,
			 * the code can work for both IO and Root Domain.
			 */
			if (pcie_assign_device(devpath, type, domain_id)
			    != DDI_SUCCESS) {
				cmn_err(CE_WARN,
				    "vpci_do_parse_dev_md: pcie_assign_device "
				    "failed for %s, domain id %lu\n",
				    devpath, domain_id);
				return (DDI_FAILURE);
			} else {
				rv = DDI_SUCCESS;
			}
			break;
		case VPCI_MD_CHANGE_CHK:
			/*
			 * Check whether the device assignment info needs to
			 * be updated.
			 */
			if (pcie_get_assigned_dev_props_by_devpath(devpath,
			    &prev_type, &prev_domain_id) != DDI_SUCCESS)
				return (DDI_FAILURE);

			if (prev_domain_id != domain_id || prev_type != type) {
				cmn_err(CE_WARN, "vpci_do_parse_dev_md: "
				    "Need update MD for %s, "
				    "domain id %lu, function type = %d\n",
				    devpath, domain_id, type);
				return (DDI_SUCCESS);
			} else {
				/* No MD changes */
				rv = DDI_FAILURE;
			}
			break;
		}
	}

	return (rv);
}

/*
 * Function:
 *	vpci_parse_dev_md()
 *
 * Description:
 * 	Device assignment parsing for given vport MD nodes
 *
 * Arguments:
 *	md		- Machine description handle pointer
 *	vpci_node	- Machine description element cookie
 *	vport_id	- Virutal device port id
 *	action		- Action against each MD nodes
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
int
vpci_parse_dev_md(md_t *md, mde_cookie_t vpci_node, uint64_t vport_id,
    vpci_md_action_t action)
{
	int		num_nodes, rv;
	size_t		size;
	mde_cookie_t	*iov_dev;

	if ((num_nodes = md_node_count(md)) <= 0) {
		cmn_err(CE_WARN,
		    "Invalid node count in Machine Description subtree");
		return (DDI_FAILURE);
	}
	size = num_nodes*(sizeof (*iov_dev));
	iov_dev = kmem_zalloc(size, KM_SLEEP);
	rv = vpci_do_parse_dev_md(md, vpci_node, iov_dev, vport_id, action);

	kmem_free(iov_dev, size);

	return (rv);
}

/*
 * Function:
 *	vpci_do_get_ldc_id()
 *
 * Description:
 * 	Get LDC channel id from "channel-endpoint" MD nodes
 *
 * Arguments:
 *	md		- Machine description handle pointer
 *	vpci_node	- Machine description element cookie
 *	iov_dev		- mde_cookie_t pointer for "channel-endpoint" node
 *	ldc_id		- LDC channel id
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
static int
vpci_do_get_ldc_id(md_t *md, mde_cookie_t vpci_node, mde_cookie_t *channel,
    uint64_t *ldc_id)
{
	int	num_channels;

	/* Look for channel endpoint child(ren) of the vpciisk MD node */
	if ((num_channels = md_scan_dag(md, vpci_node,
	    md_find_name(md, VPCI_MD_CHAN_NAME),
	    md_find_name(md, "fwd"), channel)) <= 0) {
		cmn_err(CE_WARN,
		    "No \"%s\" found for vport port ", VPCI_MD_CHAN_NAME);
		return (DDI_FAILURE);
	}

	/* Get the "id" value for the first channel endpoint node */
	if (md_get_prop_val(md, channel[0], VPCI_MD_ID, ldc_id) != 0) {
		cmn_err(CE_WARN,
		    "No \"%s\" property found for \"%s\" of vport port",
		    VPCI_MD_ID, VPCI_MD_CHAN_NAME);
		return (DDI_FAILURE);
	}

	if (num_channels > 1) {
		cmn_err(CE_WARN,
		    "Using the first channel for this vport port");
	}

	return (DDI_SUCCESS);
}

/*
 * Function:
 *	vpci_get_ldc_id()
 *
 * Description:
 * 	LDC channel node parsing for given vport MD nodes
 *
 * Arguments:
 *	md		- Machine description handle pointer
 *	vpci_node	- Machine description element cookie
 *	ldc_id		- LDC channel id
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
int
vpci_get_ldc_id(md_t *md, mde_cookie_t vpci_node, uint64_t *ldc_id)
{
	int		num_nodes, rv;
	size_t		size;
	mde_cookie_t	*channel;


	if ((num_nodes = md_node_count(md)) <= 0) {
		cmn_err(CE_WARN,
		    "Invalid node count in Machine Description subtree");
		return (DDI_FAILURE);
	}
	size = num_nodes*(sizeof (*channel));
	channel = kmem_zalloc(size, KM_SLEEP);
	rv = vpci_do_get_ldc_id(md, vpci_node, channel, ldc_id);

	kmem_free(channel, size);

	return (rv);
}

/*
 * Function:
 *	vpci_reset_taskq()
 *
 * Description:
 * 	The msg taskq thread which processes LDC reset requests, it
 * 	will initiate negotation process after the reset is done.
 *
 * Arguments:
 *	arg	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_reset_taskq(void *arg)
{
	vpci_port_t	*vport = (vpci_port_t *)arg;
	vpci_t		*vpci = vport->vpci;
	timeout_id_t	hshake_tid, tx_tid;

	rw_enter(&vport->hshake_lock, RW_WRITER);

	DMSG(vpci, 0, "[%d] Initiating channel reset.\n", vpci->instance);
	if (vport->reset_ldc) {
		/* Bring LDC down and disable callback */
		(void) vpci_do_ldc_down(vport);
		(void) vpci_ldc_cb_disable(vport);
	}

	/*
	 * Need to wake up any readers so they will detect
	 * that a reset has occurred. The msg taskq will exit.
	 */
	if (vport->msg_thr != NULL) {

		mutex_enter(&vport->read_lock);
		vport->read_state = VPCI_READ_RESET;
		cv_signal(&vport->read_cv);
		mutex_exit(&vport->read_lock);

		rw_exit(&vport->hshake_lock);

		DMSGX(1, "[%lu] Waiting thread exit and join\n", vport->id);
		thread_join(vport->msg_thr->t_did);
		vport->msg_thr = NULL;

		rw_enter(&vport->hshake_lock, RW_WRITER);

		/* Reset read state for msg taskq */
		vport->read_state = VPCI_READ_IDLE;
	}

	/*
	 * Turn off the timers, need exit hshake_lock to avoid
	 * deadlock before calling untimeout.
	 */
	if (vport->hshake_tid != 0) {
		hshake_tid = vport->hshake_tid;
		vport->hshake_tid = 0;
	}

	if (vport->tx_tid != 0) {
		tx_tid = vport->tx_tid;
		vport->tx_tid = 0;
	}

	rw_exit(&vport->hshake_lock);

	if (hshake_tid)
		(void) untimeout(hshake_tid);

	if (tx_tid)
		(void) untimeout(tx_tid);

	rw_enter(&vport->hshake_lock, RW_WRITER);

	/*
	 * Tx threads will be blocked until hand shake
	 * success again.
	 */
	vport->hshake_state = VPCI_NEGOTIATE_INIT;
	vpci_reset_session(vport);

	/*
	 * Cleanup the old drings.
	 */
	vpci_drain_fini_tx_rings(vport);
	vpci_fini_rx_rings(vport);

	/* Unset PCIv version */
	pciv_unset_version(vport->pciv_handle);

	/* Exit directly if hshake is disabled */
	if (vport->hshake_disable) {
		rw_exit(&vport->hshake_lock);
		return;
	}

	/*
	 * Clear reset request flag, the tx and hshake threads
	 * will be enabled.
	 */
	vport->reset_req = B_FALSE;

	if (vport->reset_ldc) {
		/* Bring LDC up and enable callback */
		(void) vpci_ldc_cb_enable(vport);
		(void) vpci_do_ldc_up(vport);
		vport->reset_ldc = B_FALSE;
	}

	(void) vpci_get_ldc_status(vport, &vport->ldc_state);
	DMSG(vpci, 1, "[%d] LDC state is %d\n",
	    vpci->instance, vport->ldc_state);

	rw_exit(&vport->hshake_lock);

	/* Reset taskq exit... */
	DMSG(vpci, 0, "[%d] Reset task done..\n", vpci->instance);
}

/*
 * Function:
 *	vpci_request_reset()
 *
 * Description:
 *	Trigger a vport reset, it might bring the LDC link down and restart
 *	handshake process for given vport
 *
 * Arguments:
 *	vport		- Virtual pci port pointer
 *	reset_ldc	- Indicate to bring the LDC link down
 *
 * Return Code:
 * 	N/A
 */
void
vpci_request_reset(vpci_port_t *vport, boolean_t reset_ldc)
{
	vpci_t	*vpci = vport->vpci;

	ASSERT(RW_LOCK_HELD(&vport->hshake_lock));

	/* Check hshake retry times */
	if (++ vport->hshake_cnt > VPCI_HSHAKE_MAX_RETRIES) {
		cmn_err(CE_NOTE, "vpci%d:Hand shake failed, "
		    "exceed the maximum times\n",
		    vpci->instance);

		/* After many times failures, ignore reset */
		if (vport->hshake_cnt > 2 * VPCI_HSHAKE_MAX_RETRIES)
			return;

		mutex_enter(&vport->io_lock);
		cv_broadcast(&vport->io_cv);
		mutex_exit(&vport->io_lock);
	}

	/* It should be OK that we change below fields with read lock here. */
	if (!vport->reset_ldc)
		vport->reset_ldc = reset_ldc;
	vport->reset_req = B_TRUE;

	if ((ddi_taskq_dispatch(vport->reset_taskq, vpci_reset_taskq,
	    (void *)vport, DDI_NOSLEEP)) != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "vpci%d:Failed to dispatch reset thread\n",
		    vpci->instance);
	}
}

/*
 * Function:
 *	vpci_decode_tag()
 *
 * Description:
 * 	Decode a standard LDC vio message, debug release only
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	msg	- vio message pointer
 *
 * Return Code:
 * 	N/A
 */
#ifdef DEBUG
static void
vpci_decode_tag(vpci_port_t *vport, vio_msg_t *msg)
{
	vpci_t	*vpci = vport->vpci;
	char	*ms, *ss, *ses;

	switch (msg->tag.vio_msgtype) {
#define	Q(_s)	case _s : ms = #_s; break;
	Q(VIO_TYPE_CTRL)
	Q(VIO_TYPE_DATA)
	Q(VIO_TYPE_ERR)
#undef Q
	default: ms = "unknown"; break;
	}

	switch (msg->tag.vio_subtype) {
#define	Q(_s)	case _s : ss = #_s; break;
	Q(VIO_SUBTYPE_INFO)
	Q(VIO_SUBTYPE_ACK)
	Q(VIO_SUBTYPE_NACK)
#undef Q
	default: ss = "unknown"; break;
	}

	switch (msg->tag.vio_subtype_env) {
#define	Q(_s)	case _s : ses = #_s; break;
	Q(VIO_VER_INFO)
	Q(VIO_ATTR_INFO)
	Q(VIO_DRING_REG)
	Q(VIO_DRING_UNREG)
	Q(VIO_RDX)
	Q(VIO_PKT_DATA)
	Q(VIO_DESC_DATA)
	Q(VIO_DRING_DATA)
#undef Q
	default: ses = "unknown"; break;
	}

	DMSG(vpci, 3, "(%x/%x/%x) message : (%s/%s/%s)\n",
	    msg->tag.vio_msgtype, msg->tag.vio_subtype,
	    msg->tag.vio_subtype_env, ms, ss, ses);
}
#endif

/*
 * Function:
 *	vpci_send()
 *
 * Description:
 *	The function encapsulates the call to write a message using LDC.
 *	If LDC indicates that the call failed due to the queue being full,
 *	we retry the ldc_write(), otherwise we return the error returned by LDC.
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	pkt	- Address of LDC message to be sent
 *	msglen	- The size of the message being sent
 *
 * Return Code:
 *	0		- Success.
 *	EINVAL		- Pkt or msglen were NULL
 *	ECONNRESET	- The connection was not up.
 *	EWOULDBLOCK	- LDC queue is full
 *	EBADMSG		- The actual write size less than request size
 *	xxx		- Other error codes returned by ldc_write
 */
int
vpci_send(vpci_port_t *vport, caddr_t pkt, size_t msglen)
{
	vpci_t		*vpci = vport->vpci;
	uint64_t	delay_time;
	size_t		size = 0;
	int		rv = 0;

#ifdef DEBUG
	vpci_decode_tag(vport, (vio_msg_t *)(uintptr_t)pkt);
#endif
	/*
	 * Wait indefinitely to send if channel is busy, but bail out
	 * if we succeed or if the channel closes or is reset.
	 */
	delay_time = vpci_min_timeout_ldc;
	do {
		size = msglen;
		rv = ldc_write(vport->ldc_handle, pkt, &size);
		if (rv == EWOULDBLOCK) {
			drv_usecwait(delay_time);
			/* geometric backoff */
			delay_time *= 2;
			if (delay_time >= vpci_max_timeout_ldc)
				delay_time = vpci_max_timeout_ldc;
		}
	} while (rv == EWOULDBLOCK);

	switch (rv) {
	case 0:
		if (size < msglen) {
			DMSG(vpci, 0, "[%d]performed only partial write",
			    vpci->instance);
			vpci_request_reset(vport, B_TRUE);
			return (EBADMSG);
		} else {
			DMSG(vpci, 0,
			    "[%d]vpci_send success: size=0x%"PRIu64"\n",
			    vpci->instance, msglen);
		}
		break;
	case EIO:
	case ECONNRESET:
		DMSG(vpci, 0, "[%d]Failed to send, rv=%d, do a soft reset",
		    vpci->instance, rv);
		vpci_request_reset(vport, B_FALSE);
		return (ECONNRESET);
	default:
		DMSG(vpci, 0, "[%d]Failed to send, rv=%d, do a full reset",
		    vpci->instance, rv);
		vpci_request_reset(vport, B_TRUE);
		break;
	}

	return (rv);
}

/*
 * Function:
 *	vpci_read()
 *
 * Description:
 *	The function encapsulates the call to read a message using LDC.
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	msg	- Pointer of LDC vio message will be returned
 *	nbytes	- Size of the message will be read and returned
 *
 * Return Code:
 *	0		- Success.
 *	EINVAL		- Invalid input
 *	ECONNRESET	- The connection was not up.
 *	ENOBUFS		- Incoming buffer is more than size
 *	xxx		- Other error codes returned by ldc_read
 */
static int
vpci_read(vpci_port_t *vport, vio_msg_t *msg, size_t *nbytes)
{
	int		rv;
	uint64_t	delay_time;
	size_t		len;

	/*
	 * Until we get a blocking ldc read we have to retry until the entire
	 * LDC message has arrived before ldc_read() will return that message.
	 */
	delay_time = vpci_min_timeout_ldc;

	for (;;) {
		len = *nbytes;
		/*
		 * vport->ldc_handle is protected by vport->hshake_lock but to
		 * avoid contentions we don't take the lock here. It should be
		 * safe since after handshake completing there is only one
		 * thread who can read msg by using this handle. And before
		 * that only handshake thread can use this handle.
		 */
		rv = ldc_read(vport->ldc_handle, (caddr_t)msg, &len);
		if (rv == EAGAIN) {
			delay_time *= 2;
			if (delay_time >= vpci_max_timeout_ldc)
				delay_time = vpci_max_timeout_ldc;
			drv_usecwait(delay_time);
			continue;
		}

		/* Set return length */
		*nbytes = len;
		break;
	}

	return (rv);
}

/*
 * Function:
 *	vpci_verify_session_id()
 *
 * Description:
 * 	Verify the session id when receiving a LDC VIO message
 *
 * Arguments:
 *	vport		- Virtual pci port pointer
 *	msg		- Pointer of LDC vio message will be returned
 *
 * Return Code:
 *	0	- Success
 *	EBADMSG	- VIO message is bad
 */
static int
vpci_verify_session_id(vpci_port_t *vport, vio_msg_t *msg)
{
	vpci_t		*vpci = vport->vpci;

	/* Verify the Session ID of the message */
	if ((vport->sid != 0) && (msg->tag.vio_sid != vport->sid)) {
		DMSG(vpci, 0, "[%d] Invalid SID: received 0x%x,expected 0x%lx",
		    vpci->instance, msg->tag.vio_sid, vport->sid);
		return (EBADMSG);
	}

	return (0);
}

/*
 * Funtions initiate negotiation process to peer side.
 *
 * A virtual pci port of the vpci driver initiate negotiation process to its
 * peer side by issues the negotiation messages. Since the peer side might ACK
 * or NACK these negotiation messages, the initiator can receive and verify the
 * ACK or NACK negotiation messages, to determine that negotiation process is
 * successful or not.
 */

/*
 * Function:
 *	vpci_init_ver_negotiation()
 *
 * Description:
 * 	Initialized vio version message and sent it out
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	ver	- LDC vio version message
 *
 * Return Code:
 *	0		- Success
 *	ECONNRESET	- The connection was not up.
 */
static int
vpci_init_ver_negotiation(vpci_port_t *vport, vio_ver_t ver)
{
	vpci_t		*vpci = vport->vpci;
	vio_ver_msg_t	pkt;
	size_t		msglen = sizeof (pkt);
	int		rv = -1;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	DMSG(vpci, 0, "[%d] Entered.\n", vpci->instance);

	/* Set the Session ID to a unique value */
	if (vport->sid == 0)
		vport->sid = ddi_get_lbolt();
	pkt.tag.vio_msgtype = VIO_TYPE_CTRL;
	pkt.tag.vio_subtype = VIO_SUBTYPE_INFO;
	pkt.tag.vio_subtype_env = VIO_VER_INFO;
	pkt.tag.vio_sid = vport->sid;
	pkt.dev_class = vport->dev_class;
	pkt.ver_major = ver.major;
	pkt.ver_minor = ver.minor;

	rv = vpci_send(vport, (caddr_t)&pkt, msglen);
	DMSG(vpci, 3, "[%d] Ver info sent (rv = %d)\n",
	    vpci->instance, rv);
	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to send Ver negotiation info: "
		    "id(%lx) rv(%d) size(%ld)", vpci->instance,
		    vport->ldc_handle, rv, msglen);
	}

	return (rv);
}

/*
 * Function:
 *	vpci_is_supported_version()
 *
 * Description:
 *	This routine checks if the major/minor version numbers specified in
 *	'ver_msg' are supported. If not it finds the next version that is
 *	in the supported version list 'vpci_version[]' and sets the fields in
 *	'ver_msg' to those values
 *
 * Arguments:
 *	ver_msg	- LDC vio version message pointer sent by peer driver
 *
 * Return Code:
 *	B_TRUE	- Success
 *	B_FALSE	- Version not supported
 */
static boolean_t
vpci_is_supported_version(vio_ver_msg_t *ver_msg)
{
	int vpci_num_versions = sizeof (vpci_version) /
	    sizeof (vpci_version[0]);

	for (int i = 0; i < vpci_num_versions; i++) {
		ASSERT(vpci_version[i].major > 0);
		ASSERT((i == 0) ||
		    (vpci_version[i].major < vpci_version[i-1].major));

		/*
		 * If the major versions match, adjust the minor version, if
		 * necessary, down to the highest value supported by this
		 * client. The server should support all minor versions lower
		 * than the value it sent
		 */
		if (ver_msg->ver_major == vpci_version[i].major) {
			if (ver_msg->ver_minor > vpci_version[i].minor) {
				DMSGX(0,
				    "Adjusting minor version from %u to %u",
				    ver_msg->ver_minor, vpci_version[i].minor);
				ver_msg->ver_minor = vpci_version[i].minor;
			}
			return (B_TRUE);
		}

		/*
		 * If the message contains a higher major version number, set
		 * the message's major/minor versions to the current values
		 * and return false, so this message will get resent with
		 * these values, and the server will potentially try again
		 * with the same or a lower version
		 */
		if (ver_msg->ver_major > vpci_version[i].major) {
			ver_msg->ver_major = vpci_version[i].major;
			ver_msg->ver_minor = vpci_version[i].minor;
			DMSGX(0, "Suggesting major/minor (0x%x/0x%x)\n",
			    ver_msg->ver_major, ver_msg->ver_minor);

			return (B_FALSE);
		}

		/*
		 * Otherwise, the message's major version is less than the
		 * current major version, so continue the loop to the next
		 * (lower) supported version
		 */
	}

	/*
	 * No common version was found; "ground" the version pair in the
	 * message to terminate negotiation
	 */
	ver_msg->ver_major = 0;
	ver_msg->ver_minor = 0;

	return (B_FALSE);
}

/*
 * Function:
 *	vpci_handle_ver_msg()
 *
 * Description:
 *	ACK/NACK vio version message sent by initiator, or
 *	process ACK/NACK messages responsed by peer side.
 *
 * Arguments:
 *	vport	- Virtual pci port
 *	ver_msg	- LDC vio version message sent by peer side
 *
 * Return Code:
 *	0	- Success
 *	EPROTO	- Protocol error
 *	EINVAL	- Invalid input
 *	ENOTSUP	- Not support
 */
static int
vpci_handle_ver_msg(vpci_port_t *vport, vio_ver_msg_t *ver_msg)
{
	vpci_t	*vpci = vport->vpci;
	int	rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	switch (ver_msg->tag.vio_subtype) {
	case VIO_SUBTYPE_INFO:
		/* Verify dev_class, local dev is same with initiator */
		if (ver_msg->dev_class != vport->dev_class) {
			DMSG(vpci, 0, "Expected device class %u; received %u",
			    vport->dev_class, ver_msg->dev_class);
			return (EBADMSG);
		}
		/* Set our device class for "ACK/NACK" back to initiator */
		ver_msg->dev_class = vport->dev_class;

		/*
		 * Check whether the (valid) version message specifies a
		 * version supported by local side. If the version is not
		 * supported, return EBADMSG so the message will get NACK'ed;
		 * The vpci_is_supported_version() will have updated the
		 * message with a supported version for the initiator to
		 * consider.
		 */
		if (!vpci_is_supported_version(ver_msg))
			return (EBADMSG);

		/*
		 * A version has been agreed upon; use the peer SID for
		 * communication on this channel now
		 */
		ASSERT(vport->sid == 0);
		vport->sid = ver_msg->tag.vio_sid;

		/*
		 * Store the negotiated major and minor version values in the
		 * "vport" data structure so that we can check if certain
		 * operations are supported by driver.
		 */
		vport->hshake_ver.major = ver_msg->ver_major;
		vport->hshake_ver.minor = ver_msg->ver_minor;

		DMSG(vpci, 1, "Using major version %u, minor version %u\n",
		    ver_msg->ver_major, ver_msg->ver_minor);

		/* Move to next hshake state */
		(void) vpci_next_hshake_state(vport);
		break;
	case VIO_SUBTYPE_ACK:
		/*
		 * We check to see if the version returned is indeed supported
		 * (The server may have also adjusted the minor number downwards
		 * and if so 'ver_msg' will contain the actual version agreed)
		 */
		if (vpci_is_supported_version(ver_msg)) {
			vport->hshake_ver.major = ver_msg->ver_major;
			vport->hshake_ver.minor = ver_msg->ver_minor;
			ASSERT(vport->hshake_ver.major > 0);

			/* Start new stage hand shake */
			if (vpci_next_hshake_state(vport))
				(void) vpci_hshake(vport);
		} else {
			rv = EPROTO;
		}
		break;
	case VIO_SUBTYPE_NACK:
		/*
		 * call vpci_is_supported_version() which will return the next
		 * supported version (if any) in 'ver_msg'
		 */
		(void) vpci_is_supported_version(ver_msg);
		if (ver_msg->ver_major > 0) {
			size_t len = sizeof (*ver_msg);

			ASSERT(vport->hshake_ver.major > 0);

			/* reset the necessary fields and resend */
			ver_msg->tag.vio_subtype = VIO_SUBTYPE_INFO;
			ASSERT(vport->sid != 0);
			ver_msg->tag.vio_sid = vport->sid;
			ver_msg->dev_class = vport->dev_class;

			rv = vpci_send(vport, (caddr_t)ver_msg, len);
			DMSG(vpci, 3,
			    "[%d] Resend VER info (LDC rv = %d)\n",
			    vpci->instance, rv);
		} else {
			DMSG(vpci, 0,
			    "[%d] No common version with vPCI server",
			    vpci->instance);
			rv = ENOTSUP;
		}
		break;
	default:
		rv = EINVAL;
		break;
	}

	return (rv);
}

/*
 * Function:
 *	vpci_init_attr_negotiation()
 *
 * Description:
 * 	Initialized vio attribute message and sent it out
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 *	0		- Success
 *	ECONNRESET	- The connection was not up.
 */
static int
vpci_init_attr_negotiation(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;
	vpci_attr_msg_t	pkt;
	size_t		msglen = sizeof (pkt);
	int		rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	DMSG(vpci, 0, "[%d] entered\n", vpci->instance);

	/* fill in tag */
	pkt.tag.vio_msgtype = VIO_TYPE_CTRL;
	pkt.tag.vio_subtype = VIO_SUBTYPE_INFO;
	pkt.tag.vio_subtype_env = VIO_ATTR_INFO;
	pkt.tag.vio_sid = vport->sid;
	/* fill in payload */
	pkt.xfer_mode = VIO_DESC_MODE;
	pkt.max_xfer_sz = vport->max_xfer_sz;

	rv = vpci_send(vport, (caddr_t)&pkt, msglen);
	DMSG(vpci, 0, "Attr info sent (rv = %d)\n", rv);

	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to send Attr negotiation info: "
		    "id(%lx) rv(%d) size(%ld)", vpci->instance,
		    vport->ldc_handle, rv, msglen);
	}

	return (rv);
}

/*
 * Function:
 *	vpci_handle_attr_msg()
 *
 * Description:
 *	ACK/NACK vio attribute message sent by initiator, or
 *	process ACK/NACK messages responsed by peer side.
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	attr_msg	- LDC vio attribute message sent by peer side
 *
 * Return Code:
 *	0	- Success
 *	EPROTO	- Protocol error
 *	EINVAL	- Invalid input
 *	ENOTSUP	- Not support
 */
static int
vpci_handle_attr_msg(vpci_port_t *vport, vpci_attr_msg_t *attr_msg)
{
	vpci_t	*vpci = vport->vpci;
	int	rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	switch (attr_msg->tag.vio_subtype) {
	case VIO_SUBTYPE_INFO:
		/*
		 * Verify the attributes sent by initiator.
		 */
		if (attr_msg->xfer_mode != VIO_DESC_MODE) {
			DMSG(vpci, 0, "[%d]xfer_mode(%d) is not supported",
			    vpci->instance, attr_msg->xfer_mode);
			return (EINVAL);
		}

		if (attr_msg->max_xfer_sz > vport->max_xfer_sz) {
			DMSG(vpci, 0, "[%d]max_xfer_sz=%"PRIu64" is not valid",
			    vpci->instance, attr_msg->max_xfer_sz);
			return (EINVAL);
		}

		/* Move to next hshake state */
		(void) vpci_next_hshake_state(vport);
		break;
	case VIO_SUBTYPE_ACK:
		/*
		 * We now verify the attributes agreed by peer side.
		 */
		if (attr_msg->xfer_mode != VIO_DESC_MODE) {
			DMSG(vpci, 0, "[%d] Invalid cap from peer side",
			    vpci->instance);
			rv = EINVAL;
			break;
		}

		/* Start new stage hand shake */
		if (vpci_next_hshake_state(vport))
			(void) vpci_hshake(vport);
		break;

	case VIO_SUBTYPE_NACK:
		/*
		 * Peer side could not handle the attributes we sent so
		 * we stop negotiating.
		 */
		rv = EPROTO;
		break;

	default:
		rv = ENOTSUP;
		break;
	}

	return (rv);
}

/*
 * Function:
 *	vpci_init_dring_negotiation()
 *
 * Description:
 * 	Initialized and send out vio dring message
 *
 * Arguments:
 *	vport - pointer to virtual pci port
 *
 * Return Code:
 *	0		- Success
 *	ECONNRESET	- The connection was not up
 */
static int
vpci_init_dring_negotiation(vpci_port_t *vport)
{
	vpci_dring_t		*dring = &vport->tx_dring;
	vpci_t			*vpci = vport->vpci;
	vio_dring_reg_msg_t	pkt;
	size_t			msglen = sizeof (pkt);
	int			nretries = 3;
	int			rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	for (int retry = 0; retry < nretries; retry++) {
		rv = vpci_init_tx_rings(vport);
		if (rv != EAGAIN)
			break;
		drv_usecwait(vpci_min_timeout_ldc);
	}

	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to init DRing (rv = %d)\n",
		    vpci->instance, rv);
		return (rv);
	}

	DMSG(vpci, 0, "[%d] Init of descriptor ring completed (rv = %d)\n",
	    vpci->instance, rv);

	/* fill in tag */
	pkt.tag.vio_msgtype = VIO_TYPE_CTRL;
	pkt.tag.vio_subtype = VIO_SUBTYPE_INFO;
	pkt.tag.vio_subtype_env = VIO_DRING_REG;
	pkt.tag.vio_sid = vport->sid;
	/* fill in payload */
	pkt.dring_ident = dring->ident;
	pkt.num_descriptors = dring->nentry;
	pkt.descriptor_size = dring->entrysize;
	pkt.options = (VIO_TX_DRING | VIO_RX_DRING);
	pkt.ncookies = dring->ncookie;
	/* Only support one cookie */
	pkt.cookie[0] = dring->cookie[0];

	rv = vpci_send(vport, (caddr_t)&pkt, msglen);
	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to register DRing err=%d\n",
		    vpci->instance, rv);
	}

	return (rv);
}

/*
 * Function:
 *	vpci_handle_dring_reg_msg()
 *
 * Description:
 *	ACK/NACK vio dring_reg message sent by initiator, or
 *	process ACK/NACK messages responsed by peer side.
 *
 * Arguments:
 *	tx_ring		- Tx ring pointer
 *	dreg_msg	- LDC vio dring_reg message sent by peer side
 *
 * Return Code:
 *	0	- Success
 *	EPROTO	- Protocol error
 *	EINVAL	- Invalid input
 *	ENOTSUP	- Not support
 */
static int
vpci_handle_dring_reg_msg(vpci_port_t *vport,
    vio_dring_reg_msg_t *dreg_msg)
{
	vpci_dring_t	*rx_dring = &vport->rx_dring;
	vpci_dring_t	*tx_dring = &vport->tx_dring;
	vpci_t		*vpci = vport->vpci;
	int		rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	switch (dreg_msg->tag.vio_subtype) {
	case VIO_SUBTYPE_INFO:
		if (dreg_msg->num_descriptors > INT32_MAX) {
			DMSG(vpci, 0,
			    "[%d]num_descriptors = %u; must be <= %u",
			    vpci->instance, dreg_msg->ncookies, INT32_MAX);
			return (EBADMSG);
		}

		if (dreg_msg->ncookies != 1) {
			/* Multiple cookies are not supported */
			DMSG(vpci, 0, "[%d]ncookies = %u != 1", vpci->instance,
			    dreg_msg->ncookies);
			return (EBADMSG);
		}

		rx_dring->ident = dreg_msg->dring_ident + 1;
		rx_dring->nentry = dreg_msg->num_descriptors;
		rx_dring->entrysize = dreg_msg->descriptor_size;
		rx_dring->cookie = dreg_msg->cookie;
		rx_dring->ncookie = dreg_msg->ncookies;

		/* Map tx_dring into local side  */
		rv = vpci_init_rx_rings(vport);

		/* Will ACK msg with RX dring's new ident */
		if (rv == 0)
			dreg_msg->dring_ident = rx_dring->ident;

		/* Start new stage hand shake */
		if (vpci_next_hshake_state(vport))
			(void) vpci_hshake(vport);
		break;
	case VIO_SUBTYPE_ACK:
		/* ACK message from peer side will add 1 against dring ident */
		if (tx_dring->ident + 1 != dreg_msg->dring_ident) {
			DMSG(vpci, 0, "[%d]Received wrong dring ident=0x%lx\n",
			    vpci->instance, dreg_msg->dring_ident);
			rv = EPROTO;
		}

		/* Start new stage hand shake */
		if (vpci_next_hshake_state(vport))
			(void) vpci_hshake(vport);
		break;
	case VIO_SUBTYPE_NACK:
		/*
		 * Peer side could not handle the DRing info we sent so we
		 * stop negotiating.
		 */
		DMSG(vpci, 0, "[%d] server could not register DRing\n",
		    vpci->instance);
		rv = EPROTO;
		break;

	default:
		rv = ENOTSUP;
	}

	return (rv);
}

/*
 * Function:
 *	vpci_send_rdx()
 *
 * Description:
 * 	Initialized vio rdx message and sent it out
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 *	0		- Success
 *	ECONNRESET	- The connection was not up.
 */
static int
vpci_send_rdx(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;
	vio_msg_t	msg;
	size_t		msglen = sizeof (vio_msg_t);
	int		rv;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));
	/*
	 * Send an RDX message to vpcis to indicate we are ready
	 * to send data
	 */
	msg.tag.vio_msgtype = VIO_TYPE_CTRL;
	msg.tag.vio_subtype = VIO_SUBTYPE_INFO;
	msg.tag.vio_subtype_env = VIO_RDX;
	msg.tag.vio_sid = vport->sid;
	rv = vpci_send(vport, (caddr_t)&msg, msglen);
	if (rv != 0) {
		DMSG(vpci, 0, "[%d] Failed to send RDX message (%d)",
		    vpci->instance, rv);
	}

	return (rv);
}

/*
 * Function:
 *	vpci_handle_rdx()
 *
 * Description:
 *	Verify vio rdx message ACK'ed by peer driver
 *
 * Arguments:
 *	vport	- Virtual pci port
 *	msg	- LDC vio rdx message sent by peer side
 *
 * Return Code:
 *	0	- Success
 *	EPROTO	- Protocol error
 *	EINVAL	- Invalid input
 *	ENOTSUP	- Not support
 */
static int
vpci_handle_rdx(vpci_port_t *vport, vio_rdx_msg_t *msg)
{
	vpci_t		*vpci = vport->vpci;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	DMSG(vpci, 1, "[%d] Got an RDX msg\n", vpci->instance);

	switch (msg->tag.vio_subtype) {
	case VIO_SUBTYPE_INFO:
	case VIO_SUBTYPE_ACK:
		/* Start new stage hand shake */
		if (vpci_next_hshake_state(vport))
			(void) vpci_hshake(vport);
		break;
	case VIO_SUBTYPE_NACK:
		break;
	}

	return (0);
}

/*
 * Function:
 *	vpci_reset_session()
 *
 * Description:
 * 	Reset the session id and sequence number
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	N/A
 */
void
vpci_reset_session(vpci_port_t *vport)
{
	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	/* initialized the seq num and session id */
	vport->sid = 0;
	vport->local_seq = 1;
	vport->local_seq_ack = 0;
	vport->peer_seq = 0;
}

/*
 * Function:
 *	vpci_decode_state()
 *
 * Description:
 * 	Decode the negotiation state
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	VPCI_NEGOTIATE_xxx	- Valid state
 *	unknown			- Unknown state
 */
static char *
vpci_decode_state(int state)
{
	char *str;

#define	CASE_NEGOTIATE(_s)	case _s: str = #_s; break;

	switch (state) {
	CASE_NEGOTIATE(VPCI_NEGOTIATE_INIT)
	CASE_NEGOTIATE(VPCI_NEGOTIATE_VER)
	CASE_NEGOTIATE(VPCI_NEGOTIATE_ATTR)
	CASE_NEGOTIATE(VPCI_NEGOTIATE_DRING)
	CASE_NEGOTIATE(VPCI_NEGOTIATE_RDX)
	CASE_NEGOTIATE(VPCI_NEGOTIATE_FINI)
	default: str = "unknown"; break;
	}

#undef CASE_NEGOTIATE

	return (str);
}

/*
 * Function:
 *	vpci_hshake_check()
 *
 * Description:
 * 	Handshake status checking, caller will block on io_cv if hshake
 * 	is ongoing or reset request is asserted. After hand shake process
 * 	is completed, the caller will return propper status code according
 * 	to handshake result.
 *
 * Arguments:
 *	vport	- vpci port pointer
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
int
vpci_hshake_check(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;

	ASSERT(RW_READ_HELD(&vport->hshake_lock));

	while (vport->hshake_state != VPCI_NEGOTIATE_FINI ||
	    vport->reset_req) {

		if (vport->hshake_cnt > VPCI_HSHAKE_MAX_RETRIES ||
		    vport->hshake_disable) {
			DMSG(vpci, 0, "[%d]Hand shake check failed, returned\n",
			    vpci->instance);
			return (DDI_FAILURE);
		}

		rw_exit(&vport->hshake_lock);

		mutex_enter(&vport->io_lock);
		/*
		 * Don't need hold hshake_lock here. If hshake_disable is
		 * already true, it will not be enabled again. If
		 * hshake_disable is false, io_lock will ensure the cv_wait
		 * could be notified later.
		 */
		if (!vport->hshake_disable)
			cv_wait(&vport->io_cv, &vport->io_lock);
		mutex_exit(&vport->io_lock);

		rw_enter(&vport->hshake_lock, RW_READER);

	}

	return (DDI_SUCCESS);
}

/*
 * Function:
 *	vpci_next_hshake_state()
 *
 * Description:
 * 	Move to next hand shake state
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	B_TRUE	- State is moved
 * 	B_FALSE	- State is not to be moved
 */
boolean_t
vpci_next_hshake_state(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	/*
	 * We can not move to next state if current state is not done,
	 * or the handshake is completed.
	 */
	if (vport->hshake_state < VPCI_NEGOTIATE_FINI) {
		vport->hshake_state ++;
		DMSG(vpci, 1, "Move to state(%s)\n",
		    vpci_decode_state(vport->hshake_state));
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Function:
 *	vpci_hshake_timer()
 *
 * Description:
 * 	Handshake watchdog timeout handler.
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_hshake_timer(void *arg)
{
	vpci_port_t	*vport = (vpci_port_t *)arg;
	vpci_t		*vpci = vport->vpci;

	rw_enter(&vport->hshake_lock, RW_WRITER);

	DMSG(vpci, 1, "handshake timeout: state(%s), hshake_cnt=%d\n",
	    vpci_decode_state(vport->hshake_state), vport->hshake_cnt);

	if (vport->hshake_tid == 0) {
		/* Might be cancelled by vpci_reset_taskq */
		rw_exit(&vport->hshake_lock);
		return;
	}
	vport->hshake_tid = 0;

	/*
	 * Something is wrong; handshake with the peer seems to be hung. We now
	 * go ahead and reset the channel to break out of this condition.
	 */
	vpci_request_reset(vport, B_TRUE);
	rw_exit(&vport->hshake_lock);
}

/*
 * Function:
 *	vpci_hshake()
 *
 * Description:
 * 	Initiate hand shake process
 *
 * Arguments:
 *	vport	- Virtual pci port
 *
 * Return Code:
 *	0		- Success
 *	ECONNRESET	- The connection was not up or reset req is asserted
 *	EPROTO		- Protocol error
 *	EINVAL		- Invalid input
 *	ENOTSUP		- Not support
 */
int
vpci_hshake(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;
	timeout_id_t	hshake_tid;
	int		rv = 0;

	ASSERT(RW_WRITE_HELD(&vport->hshake_lock));

	/* Ignore it if the reset request has been asserted */
	if (vport->reset_req)
		return (ECONNRESET);

	DMSG(vpci, 1, "Begin: Current state(%s)\n",
	    vpci_decode_state(vport->hshake_state));

	switch (vport->hshake_state) {
	case VPCI_NEGOTIATE_VER:
		/*
		 * Start a timer for handshake process, turn off the timer
		 * off if handshake state is VPCI_NEGOTIATE_END or there is
		 * ldc channel reset.
		 */
		ASSERT(vport->hshake_tid == 0);
		vport->hshake_tid = timeout(vpci_hshake_timer, (caddr_t)vport,
		    drv_usectohz(vpci_max_timeout_ldc*1000));
		(void) vpci_init_ver_negotiation(vport, vpci_version[0]);
		break;
	case VPCI_NEGOTIATE_ATTR:
		(void) vpci_init_attr_negotiation(vport);
		break;
	case VPCI_NEGOTIATE_DRING:
		(void) vpci_init_dring_negotiation(vport);
		break;
	case VPCI_NEGOTIATE_RDX:
		if (vpci_send_rdx(vport) != 0)
			break;

		/* Create a thread who is responsible for recv msgs */
		vport->read_state = VPCI_READ_IDLE;
		vport->msg_thr = thread_create(NULL, 0, vpci_msg_thread,
		    vport, 0, &p0, TS_RUN, minclsyspri);
		if (vport->msg_thr == NULL) {
			cmn_err(CE_NOTE,
			    "vpci%d:Failed to create vpci_msg_thread\n",
			    vpci->instance);
			rv = ENOMEM;
			break;
		}

		/* Set up a watchdog to detect tx hang */
		vport->tx_tid =
		    timeout((void (*)(void *))vpci_tx_watchdog_timer,
		    (caddr_t)vport, drv_usectohz(vpci_tx_check_interval*1000));

		break;
	case VPCI_NEGOTIATE_FINI:
		DMSG(vpci, 1, "[%d] Handshake Last state.\n", vpci->instance);

		/* Setup PCIv version here */
		if ((pciv_set_version(vport->pciv_handle,
		    vport->hshake_ver.major, vport->hshake_ver.minor)) != 0) {
			rv = EPROTO;
			break;
		}

		/* Reset hand shake counter */
		vport->hshake_cnt = 0;
		/* Signal anyone waiting for the connection to come on line. */
		mutex_enter(&vport->io_lock);
		cv_broadcast(&vport->io_cv);
		mutex_exit(&vport->io_lock);

		/* Turn off the hshake timer */
		if (vport->hshake_tid != 0) {
			hshake_tid = vport->hshake_tid;
			vport->hshake_tid = 0;
			rw_exit(&vport->hshake_lock);
			(void) untimeout(hshake_tid);
			rw_enter(&vport->hshake_lock, RW_WRITER);
		}

		DMSG(vpci, 1, "[%d] Hshake Done, enter running state\n",
		    vpci->instance);

		break;
	default:
		DMSG(vpci, 0, "[%d] Invalid hshake state.\n", vpci->instance);
		rv = EINVAL;
		break;
	}

	if (rv != 0) {
		DMSG(vpci, 1, "hshake failed: state(%s), rv=%d\n",
		    vpci_decode_state(vport->hshake_state), rv);

		/* handle the errors which can not be handled by vpci_send */
		vpci_request_reset(vport, B_TRUE);
	} else {
		DMSG(vpci, 1, "End: Current state(%s)\n",
		    vpci_decode_state(vport->hshake_state));
	}

	return (rv);
}

/*
 * Function:
 *	vpci_ack_msg()
 *
 * Description:
 * 	ACK/NACK messages
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	msg	- LDC vio message pointer
 *
 * Return Code:
 * 	N/A
 */
void
vpci_ack_msg(vpci_port_t *vport, vio_msg_t *msg, boolean_t ack)
{
	vpci_t	*vpci = vport->vpci;
	size_t	msglen = sizeof (vio_msg_t);

	/* Set session ID */
	msg->tag.vio_sid = vport->sid;
	if (ack) {
		/* ACK valid, successfully-processed messages */
		msg->tag.vio_subtype = VIO_SUBTYPE_ACK;
	} else {
		DMSG(vpci, 0, "[%d]Received unexpected message\n",
		    vpci->instance);
		/* NACK invalid messages */
		msg->tag.vio_subtype = VIO_SUBTYPE_NACK;
	}
	DMSG(vpci, 3, "Sending %s\n",
	    (msg->tag.vio_subtype == VIO_SUBTYPE_ACK) ? "ACK" : "NACK");

	/*
	 * Send the ACK or NACK back to peer, if sending message via LDC
	 * message via LDC fails, vpci_send will reset channel.
	 */
	if (vpci_send(vport, (caddr_t)msg, msglen) != 0) {
		DMSG(vpci, 0, "[%d]Can not ACK/NACK message\n",
		    vpci->instance);
	}
}

/*
 * Function:
 *	vpci_process_negotiation_msg()
 *
 * Description:
 * 	Common negotiation message process routine, maintainis the
 * 	negotiation state, and ACK/NACK negotiation messages
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	msg	- LDC vio message pointer
 *
 * Return Code:
 *	0	- Success
 *	EBADMSG	- Bad message format
 *	EINVAL	- Invalid message
 *	ENOTSUP	- Not support
 *	EPROTO	- Protocol error
 */
static int
vpci_process_negotiation_msg(vpci_port_t *vport, vio_msg_t *msg)
{
	int		rv = 0;

	rw_enter(&vport->hshake_lock, RW_WRITER);


	switch (msg->tag.vio_subtype_env) {
	case VIO_VER_INFO:
		rv = vpci_handle_ver_msg(vport, (vio_ver_msg_t *)msg);
		break;
	case VIO_ATTR_INFO:
		rv = vpci_handle_attr_msg(vport, (vpci_attr_msg_t *)msg);
		break;
	case VIO_DRING_REG:
		rv = vpci_handle_dring_reg_msg(vport,
		    (vio_dring_reg_msg_t *)msg);
		break;
	case VIO_RDX:
		rv = vpci_handle_rdx(vport, (vio_rdx_msg_t *)msg);
		break;
	}

	/* ACK/NACK messages ? */
	if (msg->tag.vio_subtype == VIO_SUBTYPE_INFO)
		vpci_ack_msg(vport, msg, (rv == 0));

	rw_exit(&vport->hshake_lock);

	return (rv);
}

/*
 * Function:
 *	vpci_process_ctrl_msg()
 *
 * Description:
 *	Process control messages
 *
 * Arguments:
 *	vport	- Virtual pci port
 *	msg 	- The VIO message pointer
 *
 * Return Code:
 *	0	- Success
 *	EBADMSG	- Failure
 */
static int
vpci_process_ctrl_msg(vpci_port_t *vport, vio_msg_t *msg)
{
	int	rv = 0;

	switch (msg->tag.vio_subtype_env) {
	case VIO_VER_INFO:
	case VIO_ATTR_INFO:
	case VIO_DRING_REG:
	case VIO_RDX:
		/* Process negotiation msgs here. */
		rv = vpci_process_negotiation_msg(vport, msg);
		break;
	default:
		/*
		 * Process other control msgs here. Currently, vpci
		 * driver doesn't support other control msg types.
		 */
		return (EBADMSG);
	}

	return (rv);
}

/*
 * Function:
 *	vpci_check_rx_seq_num()
 *
 * Description:
 * 	Check rx dring messages serial number
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	dring_msg	- Dring message pointer
 *
 * Return Code:
 *	0	- Success
 *	EBADMSG	- Failure
 */
static int
vpci_check_rx_seq_num(vpci_port_t *vport, vio_dring_msg_t *dring_msg)
{
	vpci_t	*vpci = vport->vpci;

	if ((vport->peer_seq != 0) &&
	    (dring_msg->seq_num != vport->peer_seq + 1)) {
		DMSG(vpci, 0, "[%d]Received seq_num %lu; expected %lu",
		    vpci->instance, dring_msg->seq_num, (vport->peer_seq + 1));
		return (EBADMSG);
	}

	vport->peer_seq = dring_msg->seq_num;
	return (0);
}

/*
 * Function:
 *	vpci_check_tx_seq_num()
 *
 * Description:
 * 	Check received tx dring messages serial number
 *
 * Arguments:
 *	vport		- Virtual pci port
 *	dring_msg	- LDC dring message pointer
 *
 * Return Code:
 *	0	- Success
 *	EBADMSG	- Right message type but bad message format
 */
static int
vpci_check_tx_seq_num(vpci_port_t *vport, vio_dring_msg_t *dring_msg)
{
	vpci_t		*vpci = vport->vpci;

	/*
	 * Check to see if the messages were responded to in the correct
	 * order by vpci.
	 */
	if ((dring_msg->seq_num < vport->local_seq_ack) ||
	    (dring_msg->seq_num >= vport->local_seq)) {
		DMSG(vpci, 0, "?[%d] Bogus sequence_number %lu: "
		    "expected value should >= %lu and <%lu\n",
		    vpci->instance, dring_msg->seq_num,
		    vport->local_seq_ack, vport->local_seq);
		return (EBADMSG);
	}
	vport->local_seq_ack = dring_msg->seq_num;

	return (0);
}

/*
 * Function:
 *	vpci_process_dring_msg()
 *
 * Description:
 * 	Verify a dring message, which is used for interrupt notification
 * 	after the negotiation process completed
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *	msg	- LDC vio message pointer, actually point to dring message
 *
 * Return Code:
 *	0	- Success
 *	EBADMSG	- Right message type but bad message format
 *	EIO	- I/O error
 */
static int
vpci_process_dring_msg(vpci_port_t *vport, vio_msg_t *msg)
{
	vpci_t		*vpci = vport->vpci;
	vpci_rx_ring_t	*rx_ring = NULL;
	vpci_tx_ring_t	*tx_ring = NULL;
	vio_dring_msg_t	*dring_msg = (vio_dring_msg_t *)msg;
	boolean_t	is_rx = VPCI_IS_RX_DRING_MSG(dring_msg);
	vio_dring_msg_t *new_dmsg = NULL;
	int		rv = 0;

	DMSG(vpci, 2, "[%d]Processing desc ident=%"PRIu64", start=%u, end=%u\n",
	    vpci->instance, dring_msg->dring_ident,
	    dring_msg->start_idx, dring_msg->end_idx);

	if (is_rx) {
		if (!VPCI_CHECK_MSGTYPE_SUBTYPE(msg->tag, VIO_TYPE_DATA,
		    VIO_SUBTYPE_INFO, VIO_DRING_DATA)) {
			DMSG(vpci, 0,
			    "[%d]Message subtype:%d/%d is not VIO_SUBTYPE_INFO",
			    vpci->instance, msg->tag.vio_subtype,
			    msg->tag.vio_subtype_env);
			goto nack;
		}

		if (vpci_check_rx_seq_num(vport, dring_msg) != 0)
			goto nack;

		/* Get local RX ring */
		if ((rx_ring = vpci_rx_ring_get_by_ident(vport,
		    dring_msg->dring_ident + 1)) == NULL) {
			DMSG(vpci, 0, "[%d]Can't get rx_ring, local_ident=%lu",
			    vpci->instance, dring_msg->dring_ident + 1);
			goto nack;
		}

		if (dring_msg->end_idx - dring_msg->start_idx
		    > rx_ring->nentry) {
			DMSG(vpci, 0,
			    "[%d]invalid rx idx total:%u start:%u end:%u",
			    vpci->instance, rx_ring->nentry,
			    dring_msg->start_idx, dring_msg->end_idx);
			goto nack;
		}

	} else {
		if (VPCI_CHECK_MSGTYPE_SUBTYPE(msg->tag, VIO_TYPE_DATA,
		    VIO_SUBTYPE_INFO, VIO_DRING_DATA)) {
			DMSG(vpci, 0,
			    "[%d]Message:(%d/%d) is not an ACK/NACK message",
			    vpci->instance, msg->tag.vio_subtype,
			    msg->tag.vio_subtype_env);
			return (EBADMSG);
		}

		if (vpci_check_tx_seq_num(vport, dring_msg) != 0)
			return (EBADMSG);

		if (dring_msg->tag.vio_subtype == VIO_SUBTYPE_NACK) {
			DMSG(vpci, 0, "[%d] DATA NACK\n", vpci->instance);
			VPCI_DUMP_DRING_MSG(dring_msg);
			return (EIO);
		}

		/* Get local TX ring */
		if ((tx_ring = vpci_tx_ring_get_by_ident(vport,
		    dring_msg->dring_ident - 1)) == NULL) {
			DMSG(vpci, 0, "[%d]Can't get tx_ring, local_ident=%lu",
			    vpci->instance, dring_msg->dring_ident - 1);
			return (EBADMSG);
		}

		if (dring_msg->end_idx - dring_msg->start_idx
		    > tx_ring->nentry) {
			DMSG(vpci, 0,
			    "[%d]invalid tx idx total:%u start:%u end:%u",
			    vpci->instance, rx_ring->nentry,
			    dring_msg->start_idx, dring_msg->end_idx);
			return (EBADMSG);
		}
	}

	/* New allocate msg will be freed after the IO is done */
	new_dmsg = kmem_alloc(sizeof (vio_dring_msg_t), KM_SLEEP);
	bcopy(dring_msg, new_dmsg, sizeof (vio_dring_msg_t));

	/* Process rx_ring or tx_ring */
	rv = is_rx ? vpci_dring_msg_rx(rx_ring, new_dmsg):
	    vpci_dring_msg_tx(tx_ring, new_dmsg);

	return (rv);

nack:
	/* Received a bad msg just NACK the msg and return error */
	dring_msg->dring_ident ++;
	vpci_ack_msg(vport, msg, B_FALSE);
	return (EBADMSG);
}

/*
 * Function:
 *	vpci_recv_msgs()
 *
 * Description:
 * 	Receive a msg and handle the msg according to msg type, exit loop if
 * 	there is no new msgs on LDC queue.
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
void
vpci_recv_msgs(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;
	vio_msg_tag_t	*tagp;
	vio_msg_t	msg;
	size_t		msglen;
	int		rv;

	for (;;) {
		/*
		 * The msglen need to be initialized as it could
		 * be changed after vpci_read return.
		 */
		msglen = sizeof (vio_msg_t);
		rv = vpci_read(vport, &msg, &msglen);
		DMSG(vpci, 3, "vpci_read() done.. rv=%d size=%"PRIu64"\n",
		    rv, msglen);
		if (rv != 0)
			goto reset;

		if (msglen == 0) {
			DMSG(vpci, 3, "vpci%d vpci_read NODATA\n",
			    vpci->instance);
			break;
		}

		if (msglen < sizeof (vio_msg_tag_t)) {
			DMSG(vpci, 0, "vpci%d Expect %lu bytes; recv'd %lu\n",
			    vpci->instance, sizeof (vio_msg_tag_t), msglen);
			goto reset;
		}
#ifdef DEBUG
		vpci_decode_tag(vport, &msg);
#endif
		/* Verify the session ID */
		if ((rv = vpci_verify_session_id(vport, &msg)) != 0)
			goto reset;

		tagp = (vio_msg_tag_t *)&msg;

		switch (tagp->vio_msgtype) {
		case VIO_TYPE_CTRL:
			rv = vpci_process_ctrl_msg(vport, &msg);
			break;

		case VIO_TYPE_DATA:
			rv = vpci_process_dring_msg(vport, &msg);
			break;

		case VIO_TYPE_ERR:
		default:
			rv = EBADMSG;
			break;
		}
	}

	if (rv != 0)
		goto reset;

	return;
reset:
	DMSG(vpci, 0, "vpci%d vpci_recv_msg request reset err=%d\n",
	    vpci->instance, rv);

	rw_enter(&vport->hshake_lock, RW_WRITER);
	if (rv == ECONNRESET)
		vpci_request_reset(vport, B_FALSE);
	else
		vpci_request_reset(vport, B_TRUE);
	rw_exit(&vport->hshake_lock);
}

/*
 * Function:
 *	vpci_msg_thread()
 *
 * Description:
 * 	The msg process thread which receives LDC messages, it is only
 * 	created after the handshake is completed. Before the handshake
 * 	is done, the LDC messages will be handled by the LDC callback
 * 	directly. The thread will be destroied if a LDC reset request
 * 	is asserted.
 *
 * Arguments:
 *	arg	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_msg_thread(void *arg)
{
	vpci_port_t	*vport = (vpci_port_t *)arg;
	vpci_t		*vpci = vport->vpci;

	for (;;) {

		/* Read msgs from LDC queue */
		vpci_recv_msgs(vport);

		mutex_enter(&vport->read_lock);

		while (vport->read_state != VPCI_READ_PENDING) {
			/* detect if the connection has been reset */
			if (vport->read_state == VPCI_READ_RESET) {
				mutex_exit(&vport->read_lock);
				goto exit;
			}
			vport->read_state = VPCI_READ_WAITING;
			cv_wait(&vport->read_cv, &vport->read_lock);
		}
		vport->read_state = VPCI_READ_IDLE;

		mutex_exit(&vport->read_lock);
	}

exit:
	DMSG(vpci, 3, "vpci%d vpci_msg_thread exit\n", vpci->instance);
	thread_exit();
}
