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
 * LDoms virtual pci device driver
 *
 * This driver runs on an IO Domain which owns a virtual PCIE fabric, and
 * communicates with another virtual pci driver running on the another IO
 * Domain which owns the physical PCIE fabric.
 *
 * The driver can be divided into five sections:
 *
 * 1) Generic device driver housekeeping (vpci_main.c)
 *	_init, _fini, attach, detach, ops structures, etc.
 *	Machine Descriptor parsing and data structures creating
 *	Setup a communications link with its peer driver over the LDC channel
 *	Transmission and receiving entry points
 *
 * 2) LDC channel interfaces (vpci_ldc.c)
 * 	LDC channel setup and teardown
 * 	LDC packets sending and receiving
 *
 * 3) Rx rings and Tx rings interfaces (vpci_rings.c)
 *	Initialise the descriptor rings which allows LDC clients to transfer
 *	data via memory mappings.
 *
 * 4) Transmitting supports for pciv_send etc (vpci_tx.c)
 *	a) The upper layers call into vpci via tx entry point. Driver gets
 *	buffer from a pciv packet, and binds the buffer into multiple share
 *	memory cookies.
 *	b) Driver accquires a tx ring and populates LDC share memory cookies
 *	into tx descriptors on the ring.
 *	c) After fill the tx ring, it will send a LDC packet which contains
 *	start and end descriptor index to the peer side driver to trigger an
 *	new interrupt.
 *	d) The peer side driver will get the LDC packet ACKed by peer side
 *	to recyle the descriptors to an available status. After that, the
 *	sleeping sending thread will be awoke and return the pciv packet to
 *	the upper layer.
 *
 * 5) Receiving supports for ddi_cb_register etc (vpci_rx.c)
 *	a) A new interrupt is triggered by peer side driver. Driver reads the
 *	LDC packet, get the start and end descriptor index number.
 *	b) Driver processes all of descriptors which are in the range of start
 *	and end descriptor index, and maps shared memory to create the pciv
 *	packets by using share memory cookies from the ring descriptors.
 *	c) New pciv packet chain will be passed up to upper layer, which will
 *	delivery packets to the driver who register the receive callback by
 *	calling ddi_cb_register.
 *	d) When driver callback returns with return code, it will invoke proxy
 *	driver callback to process related descriptors on the ring. The LDC
 *	packet read previously will be ACKed and send back to peer side.
 */

#include <sys/conf.h>
#include <sys/sunndi.h>
#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/note.h>
#include <sys/pciev_impl.h>

#include <sys/mdeg.h>
#include <sys/ldoms.h>
#include <sys/ldc.h>
#include <sys/vio_mailbox.h>
#include <sys/vpci_var.h>

/*
 * Function prototypes
 */

/* Standard driver functions */
static int	vpci_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int	vpci_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

/* LDC callback routine */
static uint_t	vpci_handle_cb(uint64_t event, caddr_t arg);

/* Dev status check entry point */
int vpci_dev_check(void *arg);

/* Transimit entry point */
static void	vpci_tx(void *arg, pciv_pkt_t *pkt);

/*
 * Module variables
 */

/*
 * Tunable variables to control how long vpci waits before timing out on
 * various operations
 */
extern uint64_t	vpci_min_timeout_ldc;
extern uint64_t	vpci_max_timeout_ldc;

/* Soft state pointer */
static void	*vpci_state;

/*
 * Controlling the verbosity of the error/debug messages
 *
 * vpci_msglevel - controls level of messages
 * vpci_matchinst - 64-bit variable where each bit corresponds
 *                 to the vpci instance the vpci_msglevel applies.
 */
int		vpci_msglevel = 0x0;
uint64_t	vpci_matchinst = 0x0;

/*
 * Specification of an MD node passed to the MDEG to filter any
 * 'vport' nodes that do not belong to the specified node. This
 * template is copied for each vpci instance and filled in with
 * the appropriate 'cfg-handle' value before being passed to the MDEG.
 */
static mdeg_prop_spec_t	vpci_prop_template[] = {
	{ MDET_PROP_STR,	"name",		VPCI_MD_PCIV_VDEV_NAME },
	{ MDET_PROP_VAL,	"cfg-handle",	NULL },
	{ MDET_LIST_END,	NULL, 		NULL }
};

/*
 * Matching criteria passed to the MDEG to register interest
 * in changes to 'virtual-device-port' nodes identified by their
 * 'id' property.
 */
static md_prop_match_t	vpci_prop_match[] = {
	{ MDET_PROP_VAL,	VPCI_MD_ID},
	{ MDET_LIST_END,	NULL }
};

static mdeg_node_match_t vpci_match = {VPCI_MD_PORT_NAME,
				    vpci_prop_match};

/*
 * Device Operations Structure
 */
static struct dev_ops vpci_ops = {
	DEVO_REV,	/* devo_rev */
	0,		/* devo_refcnt */
	nulldev,	/* devo_getinfo */
	nulldev,	/* devo_identify */
	nulldev,	/* devo_probe */
	vpci_attach,	/* devo_attach */
	vpci_detach,	/* devo_detach */
	nodev,		/* devo_reset */
	NULL,		/* devo_cb_ops */
	NULL,		/* devo_bus_ops */
	nulldev,	/* devo_power */
	ddi_quiesce_not_needed,	/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"virtual pci client",
	&vpci_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};


/*
 * Machine Descriptors process functions
 *
 * Virtual PCI driver compatible name is "SUNW,sun4v-pciv-comm", which is
 * acutally defined by a virtual-device Machine Descriptors node, whose
 * name is "pciv-communication".
 *
 * When a new IO Domain is created, a new virtual-device-port associated
 * with this virtual-device node will be created. One virtual-device-port
 * node has its own LDC channel id which is defined by channel-endpoint node.
 * All of IO devices which are assigned to this IO domain also have their
 * own corresponding iov-device nodes which are associated with this new
 * virtual-device-port node.
 *
 * A virtual pci port is the driver software data structure created for a
 * Machine Descriptors virtual-device-port node. It represnts an instance
 * of communication channel between local IO Domain and remote IO Domain.
 * While upper layer wants to communicate with another IO Domain, it will
 * have to select a virtual pci port as a communication handle.
 *
 * It is possible that one IO domain has multiple virtual pci ports, each
 * of ports communicates with a virtual or physical PCIE farbic of other
 * IO Domains.
 */

/*
 * Function:
 *	vpci_init_port()
 *
 * Description:
 * 	Initialize an virtual pci port for an IO domain
 *
 * Arguments:
 *	vpci		- Soft state pointer
 *	id		- Virtual pci port id
 *	ldc_id		- LDC channel id
 *	vpci_ldc_cb_t	- Function pointer of LDC interrupt callback
 *
 * Return Code:
 *	DDI_SUCCESS	- Success
 *	DDI_FAILURE	- Failure
 */
static int
vpci_init_port(vpci_t *vpci, uint64_t id, uint64_t ldc_id,
    vpci_ldc_cb_t vpci_ldc_cb)
{
	vpci_port_t		*vport = NULL;
	dom_id_t		domain_id;
	char			tq_name[VPCI_TASKQ_NAMELEN];
	pciv_proxy_reg_t	pciv_proxy_reg;
	mod_hash_val_t		val;

	DMSGX(1, "[%lu] Entered vpci_init_port\n", id);

	domain_id = vpci_get_domain_id(id);

	if ((vport = kmem_zalloc(sizeof (vpci_port_t), KM_NOSLEEP)) == NULL) {
		cmn_err(CE_WARN, "No memory for virtual pci port");
		goto fail1;
	}
	vport->vpci = vpci;
	vport->id = id;
	vport->domain_id = domain_id;
	vport->dev_class = VDEV_PCI;
	vport->ldc_id = ldc_id;
	vport->max_xfer_sz = PCIV_MAX_BUF_SIZE;

	/* Initialize locking */
	rw_init(&vport->hshake_lock, NULL, RW_DRIVER, NULL);
	mutex_init(&vport->io_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&vport->read_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&vport->io_cv, NULL, CV_DRIVER, NULL);
	cv_init(&vport->read_cv, NULL, CV_DRIVER, NULL);

	if ((vpci_alloc_rings(vport)) != DDI_SUCCESS)
		goto fail1;

	if (vpci_do_ldc_init(vport, vpci_ldc_cb) != 0)
		goto fail1;

	/* Add the successfully-initialized vport to the vport table */
	if (mod_hash_insert(vpci->vport_tbl, (mod_hash_key_t)id, vport) != 0) {
		DMSGX(0, "Error adding vport ID %lu to table", id);
		goto fail2;
	}

	/* Create reset taskq thread */
	vport->hshake_state = VPCI_NEGOTIATE_INIT;
	vport->reset_req = B_FALSE;
	vport->reset_ldc = B_FALSE;
	vport->hshake_disable = B_FALSE;
	(void) snprintf(tq_name, sizeof (tq_name), "reset_taskq%lu",
	    vport->id);
	DMSGX(1, "tq_name = %s\n", tq_name);
	if ((vport->reset_taskq = ddi_taskq_create(vpci->dip, tq_name, 1,
	    TASKQ_DEFAULTPRI, 0)) == NULL) {
		DMSGX(0, "Could not create task queue %s", tq_name);
		goto fail2;
	}

	/* Register the proxy to upper layer */
	pciv_proxy_reg.dip = vpci->dip;
	pciv_proxy_reg.state = (void *)vport;
	pciv_proxy_reg.domid = vport->domain_id;
	pciv_proxy_reg.check = vpci_dev_check;
	pciv_proxy_reg.tx = vpci_tx;

	if (pciv_proxy_register(&pciv_proxy_reg, &vport->pciv_handle)
	    != DDI_SUCCESS) {
		cmn_err(CE_NOTE,
		    "vpci%d: pciv_proxy_register failed, domain id=%lu",
		    vpci->instance, vport->domain_id);
		(void) vpci_do_ldc_down(vport);
		goto fail2;
	}

	/* Bring up LDC connection */
	rw_enter(&vport->hshake_lock, RW_WRITER);

	(void) vpci_do_ldc_up(vport);
	(void) vpci_get_ldc_status(vport, &vport->ldc_state);
	DMSG(vpci, 1, "[%d] LDC state is %d\n",
	    vpci->instance, vport->ldc_state);

	rw_exit(&vport->hshake_lock);

	DMSGX(1, "[%lu] Created vpci port\n", vport->id);

	return (DDI_SUCCESS);
fail2:
	if (vport->reset_taskq != NULL) {
		DMSGX(1, "[%lu] Waiting reset taskq\n", vport->id);
		ddi_taskq_wait(vport->reset_taskq);
		ddi_taskq_destroy(vport->reset_taskq);
		vport->reset_taskq = NULL;
	}

	/* Remove the virtual pci port from hash table */
	if (mod_hash_remove(vpci->vport_tbl, (mod_hash_key_t)id, &val) != 0)
		DMSGX(0, "Failed to delete entry %lu", id);
fail1:
	vpci_terminate_ldc(vport);
	vpci_free_rings(vport);
	kmem_free(vport, sizeof (vpci_port_t));

	return (DDI_FAILURE);
}

/*
 * Function:
 *	vpci_fini_port()
 *
 * Description:
 * 	Finish an virtual pci port for an IO domain
 *
 * Arguments:
 *	arg	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_fini_port(void *arg)
{
	vpci_port_t	*vport = (vpci_port_t *)arg;

	ASSERT(vport != NULL);

	DMSGX(1, "[%lu] Entering vpci_fini_port\n", vport->id);

	/* Perform reset and disable ldc callback */
	if (vport->reset_taskq != NULL) {
		rw_enter(&vport->hshake_lock, RW_WRITER);

		/* Disable hshake before fini the port */
		vport->hshake_disable = B_TRUE;
		vpci_request_reset(vport, B_TRUE);
		rw_exit(&vport->hshake_lock);

		DMSGX(1, "[%lu] Waiting reset taskq\n", vport->id);
		ddi_taskq_wait(vport->reset_taskq);
		ddi_taskq_destroy(vport->reset_taskq);
		vport->reset_taskq = NULL;
	}

	/* Signal pending IO return */
	mutex_enter(&vport->io_lock);
	cv_broadcast(&vport->io_cv);
	mutex_exit(&vport->io_lock);

	/* Channel are not available, unregister from upper layer. */
	if (vport->pciv_handle) {
		pciv_proxy_unregister(vport->pciv_handle);
		vport->pciv_handle = NULL;
	}
	(void) pcie_unassign_devices(vport->domain_id);

	vpci_fini_tx_rings(vport);
	vpci_fini_rx_rings(vport);

	vpci_terminate_ldc(vport);

	vpci_free_rings(vport);

	rw_destroy(&vport->hshake_lock);
	mutex_destroy(&vport->io_lock);
	mutex_destroy(&vport->read_lock);
	cv_destroy(&vport->io_cv);
	cv_destroy(&vport->read_cv);

	DMSGX(1, "[%lu] Destroyed vpci port\n", vport->id);

	kmem_free(vport, sizeof (vpci_port_t));
}

/*
 * Function:
 *	vpci_remove_port()
 *
 * Description:
 * 	Handles a virtual device port MD node remove
 *
 * Arguments:
 *	vpci		- Virtual pci port pointer
 *	md		- Machine description handle pointer
 *	vpci_node	- Machine description element cookie
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_remove_port(vpci_t *vpci, md_t *md, mde_cookie_t vpci_node)
{
	uint64_t	id = 0;

	/* Get virtual device port id */
	if (md_get_prop_val(md, vpci_node, VPCI_MD_ID, &id) != 0) {
		DMSGX(0, "Unable to get \"%s\" property from vpci's MD node",
		    VPCI_MD_ID);
		return;
	}

	/*
	 * Remove this virtual pci port record from the hash table and
	 * vpci_fini_port will be called inside the function.
	 */
	if (mod_hash_destroy(vpci->vport_tbl, (mod_hash_key_t)id) != 0)
		DMSGX(0, "Failed to delete entry %lu", id);

	DMSGX(1, "Removing vpci ID %lu\n", id);
}

/*
 * Function:
 *	vpci_change_port()
 *
 * Description:
 * 	Handles a virtual device port MD node change
 *
 * Arguments:
 *	vpci		- Virtual pci port pointer
 *	prev_md		- Old machine description handle pointer
 *	prev_vpci_node	- Old machine description element cookie
 *	curr_md		- New machine description handle pointer
 *	curr_vpci_node	- New machine description element cookie
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_change_port(vpci_t *vpci, md_t *prev_md, mde_cookie_t prev_vpci_node,
    md_t *curr_md, mde_cookie_t curr_vpci_node)
{
	uint64_t	curr_id = 0, curr_ldc_id = 0;
	uint64_t	prev_id = 0, prev_ldc_id = 0;
	char		*node_name = NULL;

	/* Get previous virtual device port id */
	if (md_get_prop_val(prev_md, prev_vpci_node, VPCI_MD_ID,
	    &prev_id) != 0) {
		DMSGX(0, "Error getting previous vpci \"%s\" property",
		    VPCI_MD_ID);
		return;
	}
	/* Get current virtual device port id */
	if (md_get_prop_val(curr_md, curr_vpci_node, VPCI_MD_ID,
	    &curr_id) != 0) {
		DMSGX(0, "Error getting current vpci \"%s\" property",
		    VPCI_MD_ID);
		return;
	}
	/* Virtual device port id can not be changed */
	if (curr_id != prev_id) {
		DMSGX(0, "Not changing vpci:  ID changed from %lu to %lu",
		    prev_id, curr_id);
		return;
	}

	/* Get previous ldc channel id */
	if (vpci_get_ldc_id(prev_md, prev_vpci_node, &prev_ldc_id) != 0) {
		DMSGX(0, "Error getting LDC ID for vpci %lu", prev_id);
		return;
	}
	/* Get current ldc channel id */
	if (vpci_get_ldc_id(curr_md, curr_vpci_node, &curr_ldc_id) != 0) {
		DMSGX(0, "Error getting LDC ID for vpci %lu", curr_id);
		return;
	}
	/* LDC channel id can not be changed */
	if (curr_ldc_id != prev_ldc_id) {
		DMSGX(0, "Not changing vpci:"
		    "LDC ID changed from %lu to %lu", prev_ldc_id, curr_ldc_id);
		return;
	}

	/* Validate node name */
	if (md_get_prop_str(curr_md, curr_vpci_node, "name", &node_name) != 0) {
		DMSGX(0, "Error getting node name for vpci %lu", curr_id);
		return;
	}

	/* Is this a virtual device port which is watched by vpci driver */
	if (strcmp(node_name, VPCI_MD_PCIV_PORT_NAME) != 0) {
		DMSGX(0, "The node name is %s, we expect it is %s, port id %lu",
		    node_name, VPCI_MD_PCIV_PORT_NAME, curr_id);
		return;
	}

	DMSGX(1, "Changing virtual port ID %lu\n", prev_id);

	/*
	 * Check whether it's a fake change notification.
	 */
	if (vpci_parse_dev_md(curr_md, curr_vpci_node, curr_id,
	    VPCI_MD_CHANGE_CHK) != DDI_SUCCESS) {
		DMSGX(1, "No MD changes, virtual port ID %lu\n",
		    curr_id);
		return;
	}

	/*
	 * Remove this virtual pci port record from the hash table and
	 * vpci_fini_port will be called inside the function.
	 */
	if (mod_hash_destroy(vpci->vport_tbl, (mod_hash_key_t)prev_id) != 0) {
		DMSGX(0, "Failed to delete entry %lu", prev_id);
		return;
	}

	/*
	 * The Old vport is destroyed, parsing assigned PCIE device for new
	 * virtual pci port.
	 */
	if (vpci_parse_dev_md(curr_md, curr_vpci_node, curr_id, VPCI_MD_ADD)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "Can't assign iov devices to vpci port %lu\n", curr_id);
		return;
	}

	/* Create a new virtual pci port in driver software layer */
	if (vpci_init_port(vpci, curr_id, curr_ldc_id, vpci_handle_cb)
	    != DDI_SUCCESS)
		cmn_err(CE_WARN, "Failed to init vpci ID %lu", curr_id);
}

/*
 * Function:
 *	vpci_add_port()
 *
 * Description:
 * 	Handles a virtual device port MD node add
 *
 * Arguments:
 *	vpci		- Virtual pci port pointer
 *	md		- Machine description handle pointer
 *	vpci_node	- Machine description element cookie
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_add_port(vpci_t *vpci, md_t *md, mde_cookie_t vpci_node)
{
	uint64_t	id = 0, ldc_id = 0;

	/* Get virtual device port id */
	if (md_get_prop_val(md, vpci_node, VPCI_MD_ID, &id) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "Error getting vpci port \"%s\"\n", VPCI_MD_ID);
		return;
	}

	DMSGX(1, "Adding vitual pci port ID %lu\n", id);

	/* Get ldc channel id */
	if (vpci_get_ldc_id(md, vpci_node, &ldc_id) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "Error getting LDC ID for vpci port %lu\n", id);
		return;
	}
	/* Parsing assigned PCIE device for this virtual pci port */
	if (vpci_parse_dev_md(md, vpci_node, id, VPCI_MD_ADD) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "Can't assign iov devices to vpci port %lu\n", id);
		return;
	}

	/* Create a new virtual pci port in driver software layer */
	if (vpci_init_port(vpci, id, ldc_id, vpci_handle_cb) != DDI_SUCCESS)
		cmn_err(CE_WARN, "Failed to init vpci ID %lu\n", id);
}

/*
 * Function:
 *	vpci_md_cb()
 *
 * Description:
 * 	Machine Descriptor callback registered by vpci driver
 *
 * Arguments:
 *	arg		- Driver soft state pointer
 *	md		- MD update result pointer
 *
 * Return Code:
 * 	MDEG_SUCCESS	- Success
 * 	MDEG_FAILURE	- Failure
 */
static int
vpci_md_cb(void *arg, mdeg_result_t *md)
{
	int	i;
	vpci_t	*vpci = arg;


	if (md == NULL)
		return (MDEG_FAILURE);
	ASSERT(vpci != NULL);

	for (i = 0; i < md->removed.nelem; i++)
		vpci_remove_port(vpci, md->removed.mdp, md->removed.mdep[i]);
	for (i = 0; i < md->match_curr.nelem; i++)
		vpci_change_port(vpci, md->match_prev.mdp,
		    md->match_prev.mdep[i], md->match_curr.mdp,
		    md->match_curr.mdep[i]);
	for (i = 0; i < md->added.nelem; i++)
		vpci_add_port(vpci, md->added.mdp, md->added.mdep[i]);

	return (MDEG_SUCCESS);
}

/*
 * Interrupt handlers for messages from LDC
 *
 * LDC Interrupt handler is a per virtual pci port interrupt callback, which is
 * registered by ldc_reg_callback while the virtual pci port is initialzed.
 */

/*
 * Function:
 *	vpci_handle_evt_up()
 *
 * Description:
 * 	Handle LDC_EVT_UP
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_handle_evt_up(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;

	rw_enter(&vport->hshake_lock, RW_WRITER);

	/* Reset handshake counter */
	vport->hshake_cnt = 0;

	if (vport->hshake_state > VPCI_NEGOTIATE_INIT) {
		/* Casue a soft reset */
		DMSG(vpci, 0, "[%d] request a reset",
		    vpci->instance);
		vpci_request_reset(vport, B_TRUE);
	} else {
		/* Initiate handshake negotiation with peer side. */
		DMSG(vpci, 1, "[%d] initiate hand shake process\n",
		    vpci->instance);
		if (vpci_next_hshake_state(vport))
			(void) vpci_hshake(vport);
	}

	rw_exit(&vport->hshake_lock);
}

/*
 * Function:
 *	vpci_handle_evt_reset()
 *
 * Description:
 * 	Handle LDC_EVT_RESET and LDC_EVT_DOWN
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_handle_evt_reset(vpci_port_t *vport)
{
	vpci_t		*vpci = vport->vpci;

	rw_enter(&vport->hshake_lock, RW_WRITER);

	if (vport->hshake_state > VPCI_NEGOTIATE_INIT) {
		DMSG(vpci, 0, "[%d] request a soft reset",
		    vpci->instance);
		/* soft reset */
		vpci_request_reset(vport, B_FALSE);
	} else {
		DMSG(vpci, 1,
		    "[%d] LDC RESET has been done, ignore...\n",
		    vpci->instance);

		(void) vpci_do_ldc_up(vport);
	}

	rw_exit(&vport->hshake_lock);
}

/*
 * Function:
 *	vpci_handle_evt_read()
 *
 * Description:
 * 	Handle LDC_EVT_READ
 *
 * Arguments:
 *	vport	- Virtual pci port pointer
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_handle_evt_read(vpci_port_t *vport)
{
	vpci_t	*vpci = vport->vpci;

	rw_enter(&vport->hshake_lock, RW_READER);
	if (vport->hshake_state < VPCI_NEGOTIATE_FINI) {
		rw_exit(&vport->hshake_lock);
		/* Receive the msg from the callback */
		DMSG(vpci, 3, "[%d] received in vpci_handle_evt_read\n",
		    vpci->instance);
		vpci_recv_msgs(vport);
		return;
	}
	rw_exit(&vport->hshake_lock);

	/*
	 * If the receive taskq is enabled, then wakeup the taskq
	 * to process the LDC messages.
	 */
	ASSERT(vport->msg_thr != NULL);
	DMSG(vpci, 3, "[%d] received in vpci_msg_thread\n", vpci->instance);
	mutex_enter(&vport->read_lock);
	vport->read_state = VPCI_READ_PENDING;
	cv_signal(&vport->read_cv);
	mutex_exit(&vport->read_lock);
}

/*
 * Function:
 *	vpci_handle_cb()
 *
 * Description:
 * 	Per port LDC Interrupt callback
 *
 * Arguments:
 *	event	- Type of event (LDC_EVT_xxx) that triggered the callback
 *	arg	- Virtual pci port pointer
 *
 * Return Code:
 *	LDC_SUCCESS	- Success
 */
static uint_t
vpci_handle_cb(uint64_t event, caddr_t arg)
{
	vpci_port_t	*vport = (vpci_port_t *)(void *)arg;
	vpci_t		*vpci = vport->vpci;

	ASSERT(vport != NULL);

	/*
	 * Depending on the type of event that triggered this
	 * callback, we modify the handshake state or read the data.
	 *
	 * NOTE: not done as a switch() as event could be triggered by
	 * a state change and a read request. Also the ordering	of the
	 * check for the event types is deliberate.
	 */
	if (event & LDC_EVT_UP) {
		DMSGX(0, "[%d] Received LDC_EVT_UP\n", vpci->instance);
		vpci_handle_evt_up(vport);
	}

	if (event & (LDC_EVT_RESET|LDC_EVT_DOWN)) {

		DMSG(vpci, 0, "[%d] Received LDC RESET event\n",
		    vpci->instance);
		vpci_handle_evt_reset(vport);
	}

	if (event & LDC_EVT_READ) {
		DMSG(vpci, 1, "[%d] Received LDC_EVT_READ\n", vpci->instance);
		vpci_handle_evt_read(vport);
	}

	if (event & ~(LDC_EVT_UP | LDC_EVT_RESET | LDC_EVT_DOWN | LDC_EVT_READ))
		DMSGX(0, "![%d] Unexpected LDC event (%lx) received",
		    vpci->instance, event);

	return (LDC_SUCCESS);
}

/*
 * Dev status check entry point
 *
 * This entry point allow upper layer checking underlying device status.
 * If check return failure, that means the device is not available. Caller
 * might be blocked on this interface.
 */

/*
 * Function:
 *	vpci_dev_check()
 *
 * Description:
 * 	Device status checking entry point for upper layer
 *
 * Arguments:
 *	arg	- Virtual pci port pointer
 *
 * Return Code:
 * 	DDI_SUCCESS	- Success
 * 	DDI_FAILURE	- Failure
 */
int
vpci_dev_check(void *arg)
{
	vpci_port_t	*vport = (vpci_port_t *)arg;
	int		rv;

	ASSERT(vport != NULL);

	rw_enter(&vport->hshake_lock, RW_READER);
	rv = vpci_hshake_check(vport);
	rw_exit(&vport->hshake_lock);

	return (rv);
}

/*
 * Transimit entry point
 *
 * This tx entry point is a per virtual pci port interface. When virtual pci
 * port is created successfully, the tx entry point is registered to the upper
 * layer. The upper layer framework could select a proper virtual pci port
 * tx entry point according to the address information in pciv packet header.
 */

/*
 * Function:
 *	vpci_tx()
 *
 * Description:
 * 	Transimit entry point for a virtual pci port
 *
 * Arguments:
 *	arg	- Virtual pci port pointer
 *	pkt	- Virtual pci packet
 *
 * Return Code:
 * 	N/A
 */
static void
vpci_tx(void *arg, pciv_pkt_t *pkt)
{
	vpci_port_t	*vport = (vpci_port_t *)arg;
	vpci_t		*vpci = (vpci_t *)vport->vpci;
	vpci_tx_ring_t	*tx_ring = NULL;

	if ((tx_ring = vpci_tx_ring_get(vport, pkt)) != NULL) {
		vpci_ring_tx(tx_ring, pkt);
	} else {
		pkt->io_flag |= PCIV_ERROR;
		pkt->io_err = DDI_FAILURE;
		DMSG(vpci, 0, "[%d]vpci_tx_ring_get failed, pkt type =%d\n",
		    vpci->instance, pkt->hdr.type);
	}
}

/*
 * Device Driver housekeeping and setup
 */

int
_init(void)
{
	int	rv;

	if ((rv = ddi_soft_state_init(&vpci_state, sizeof (vpci_t), 1))
	    != 0)
		return (rv);
	if ((rv = mod_install(&modlinkage)) != 0)
		ddi_soft_state_fini(&vpci_state);
	return (rv);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int	rv;

	if ((rv = mod_remove(&modlinkage)) != 0)
		return (rv);
	ddi_soft_state_fini(&vpci_state);
	return (0);
}

static int
vpci_do_attach(dev_info_t *dip)
{
	vpci_t			*vpci = NULL;
	int			size;
	int			cfg_handle;
	int			instance;
	mdeg_prop_spec_t	*pspecp;
	mdeg_node_spec_t	*ispecp;

	instance = ddi_get_instance(dip);
	DMSGX(1, "[%d] Entered\n", instance);


	if (ddi_soft_state_zalloc(vpci_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "[%d] Couldn't alloc state structure",
		    vpci->instance);
		return (DDI_FAILURE);
	}

	if ((vpci = ddi_get_soft_state(vpci_state, instance)) == NULL) {
		cmn_err(CE_NOTE, "[%d] Couldn't get state structure",
		    vpci->instance);
		return (DDI_FAILURE);
	}

	vpci->dip = dip;
	vpci->instance = instance;

	/*
	 * Create a hash table for holding the virtual device port state
	 * instances. vpci_fini_port is the record destroy callback.
	 */
	vpci->vport_tbl = mod_hash_create_ptrhash("vpci_port_tbl", VPCI_NCHAINS,
	    vpci_fini_port, sizeof (void *));

	/*
	 * We assign the value to init_progress in this case to zero out the
	 * variable and then set bits in it to indicate what has been done
	 */
	vpci->init_progress = VPCI_SOFT_STATE;

	/*
	 * Get the OBP instance number for comparison with the MD instance
	 *
	 * The "cfg-handle" property of a vpci node in an MD contains the MD's
	 * notion of "instance", or unique identifier, for that node; OBP
	 * stores the value of the "cfg-handle" MD property as the value of
	 * the "reg" property on the node in the device tree it builds from
	 * the MD and passes to Solaris.  Thus, we look up the devinfo node's
	 * "reg" property value to uniquely identify this device instance.
	 * If the "reg" property cannot be found, the device tree state is
	 * presumably so broken that there is no point in continuing.
	 */
	if (!ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, OBP_REG)) {
		cmn_err(CE_WARN, "'%s' property does not exist", OBP_REG);
		return (ENOENT);
	}
	cfg_handle = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    OBP_REG, -1);
	DMSGX(1, "[%d] OBP inst=%d\n", vpci->instance, cfg_handle);

	/* Initialize a template structure */
	size = sizeof (vpci_prop_template);
	pspecp = kmem_alloc(size, KM_SLEEP);
	bcopy(vpci_prop_template, pspecp, size);

	VPCI_SET_MDEG_PROP_INST(pspecp, cfg_handle);

	/* Initialize the MD node spec structure */
	ispecp = kmem_zalloc(sizeof (mdeg_node_spec_t), KM_SLEEP);
	ispecp->namep = VPCI_MD_VDEV_NAME;
	ispecp->specp = pspecp;

	/*
	 * Register a callback for MD updates. This callback may be called
	 * immediately in attach routine if the MD node changes already
	 * happened before the registration.
	 */
	if (mdeg_register(ispecp, &vpci_match, vpci_md_cb, vpci,
	    &vpci->mdeg) != MDEG_SUCCESS) {
		cmn_err(CE_NOTE, "vpci%d:Unable to register for MD updates",
		    vpci->instance);
		kmem_free(ispecp, sizeof (mdeg_node_spec_t));
		kmem_free(pspecp, size);
		return (DDI_FAILURE);
	}

	vpci->ispecp = ispecp;

	vpci->init_progress |= VPCI_MDEG;

	ddi_report_dev(dip);

	DMSGX(0, "[%d] Attach completed\n", vpci->instance);
	return (DDI_SUCCESS);
}

static int
vpci_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int	rv;

	switch (cmd) {
	case DDI_ATTACH:
		if ((rv = vpci_do_attach(dip)) != 0)
			(void) vpci_detach(dip, DDI_DETACH);
		return (rv);
	case DDI_RESUME:
		/* Nothing to do for this non-device */
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

/*
 * Call back for hash table walking, it will be called for each record
 * in the hash table.
 */
static uint_t
vpci_vport_check_cb(mod_hash_key_t key, mod_hash_val_t *val, void *arg)
{
	_NOTE(ARGUNUSED(key, val))

	/*
	 * If this routine is called, that means at least there is one
	 * record in the hash table. We don't care the value of arg,
	 * just make sure that it is a non-zero value by increasing it.
	 */
	(*((uint_t *)arg))++;
	return (MH_WALK_TERMINATE);
}

static int
vpci_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int	instance;
	vpci_t	*vpci = NULL;
	uint_t	vport_present = 0;

	switch (cmd) {
	case DDI_DETACH:
		/* The real work happens below */
		break;
	case DDI_SUSPEND:
		/* Nothing to do for this non-device */
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	ASSERT(cmd == DDI_DETACH);
	instance = ddi_get_instance(dip);
	DMSGX(1, "[%d] Entered\n", instance);

	if ((vpci = ddi_get_soft_state(vpci_state, instance)) == NULL) {
		cmn_err(CE_NOTE, "[%d] Couldn't get state structure",
		    vpci->instance);
		return (DDI_FAILURE);
	}

	/* Walk the hash table, and do no detach when serving any vports */
	mod_hash_walk(vpci->vport_tbl, vpci_vport_check_cb, &vport_present);

	if (vport_present) {
		DMSGX(0, "[%d] Not detaching because serving vports",
		    vpci->instance);
		return (DDI_FAILURE);
	}

	DMSGX(0, "[%d] proceeding...\n", vpci->instance);

	if (vpci->init_progress & VPCI_MDEG) {
		(void) mdeg_unregister(vpci->mdeg);
		kmem_free(vpci->ispecp->specp, sizeof (vpci_prop_template));
		kmem_free(vpci->ispecp, sizeof (mdeg_node_spec_t));
		vpci->ispecp = NULL;
		vpci->mdeg = NULL;
	}

	/* All virtual pci ports are removed, now destroy the hash table */
	mod_hash_destroy_hash(vpci->vport_tbl);

	DMSGX(0, "[%d] End %p", vpci->instance, (void *)vpci);

	if (vpci->init_progress & VPCI_SOFT_STATE)
		ddi_soft_state_free(vpci_state, vpci->instance);

	return (DDI_SUCCESS);
}
