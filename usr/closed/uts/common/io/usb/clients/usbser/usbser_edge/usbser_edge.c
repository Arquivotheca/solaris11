/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */

/*
 *
 * Edgeport driver glue code
 *
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#define	USBDRV_MAJOR_VER	2
#define	USBDRV_MINOR_VER	0

#include <sys/usb/usba.h>

#include <sys/usb/clients/usbser/usbser.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_var.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_subr.h>


/* configuration entry points */
static int	usbser_edge_attach(dev_info_t *, ddi_attach_cmd_t);
static int	usbser_edge_detach(dev_info_t *, ddi_detach_cmd_t);
static int	usbser_edge_getinfo(dev_info_t *, ddi_info_cmd_t, void *,
		void **);
static int	usbser_edge_open(queue_t *, dev_t *, int, int, cred_t *);

/* TI boot mode */
static int	edgeti_boot_attach(dev_info_t *, ddi_attach_cmd_t, void *);
static int	edgeti_boot_detach(dev_info_t *, ddi_detach_cmd_t, void *);


static void    *usbser_edge_statep;	/* soft state */

extern ds_ops_t edge_ds_ops;		/* DSD operations */

/*
 * STREAMS structures
 */
struct module_info usbser_edge_modinfo = {
	0,			/* module id */
	"usbser_edge",		/* module name */
	USBSER_MIN_PKTSZ,	/* min pkt size */
	USBSER_MAX_PKTSZ,	/* max pkt size */
	USBSER_HIWAT,		/* hi watermark */
	USBSER_LOWAT		/* low watermark */
};

static struct qinit usbser_edge_rinit = {
	putq,
	usbser_rsrv,
	usbser_edge_open,
	usbser_close,
	NULL,
	&usbser_edge_modinfo,
	NULL
};

static struct qinit usbser_edge_winit = {
	usbser_wput,
	usbser_wsrv,
	NULL,
	NULL,
	NULL,
	&usbser_edge_modinfo,
	NULL
};

struct streamtab usbser_edge_str_info = {
	&usbser_edge_rinit, &usbser_edge_winit, NULL, NULL
};

static struct cb_ops usbser_edge_cb_ops = {
	nodev,			/* cb_open */
	nodev,			/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	&usbser_edge_str_info,	/* cb_stream */
	(int)(D_64BIT | D_NEW | D_MP | D_HOTPLUG)	/* cb_flag */
};

/*
 * auto configuration ops
 */
struct dev_ops usbser_edge_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	usbser_edge_getinfo,	/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	usbser_edge_attach,	/* devo_attach */
	usbser_edge_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	&usbser_edge_cb_ops,	/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	usbser_power,		/* devo_power */
	ddi_quiesce_not_needed,	/* devo_quiesce */
};

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* type of module - driver */
	"USB Digi Edgeport driver",
	&usbser_edge_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, 0
};


/*
 * configuration entry points
 * --------------------------
 */
int
_init(void)
{
	int    error;

	if ((error = mod_install(&modlinkage)) == 0) {
		error = ddi_soft_state_init(&usbser_edge_statep,
		    max(usbser_soft_state_size(), sizeof (edgeti_boot_t)), 1);
	}

	return (error);
}


int
_fini(void)
{
	int    error;

	if ((error = mod_remove(&modlinkage)) == 0) {
		ddi_soft_state_fini(&usbser_edge_statep);
	}

	return (error);
}


int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*ARGSUSED*/
int
usbser_edge_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result)
{
	return (usbser_getinfo(dip, infocmd, arg, result, usbser_edge_statep));
}


static int
usbser_edge_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int	rval;

	rval = edgeti_boot_attach(dip, cmd, usbser_edge_statep);
	if (rval == DDI_ECONTEXT) {

		return (usbser_attach(dip, cmd, usbser_edge_statep,
		    &edge_ds_ops));
	} else {

		return (rval);
	}
}


static int
usbser_edge_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (ddi_get_driver_private(dip) == NULL) {

		return (edgeti_boot_detach(dip, cmd, usbser_edge_statep));
	} else {

		return (usbser_detach(dip, cmd, usbser_edge_statep));
	}
}


static int
usbser_edge_open(queue_t *rq, dev_t *dev, int flag, int sflag, cred_t *cr)
{
	return (usbser_open(rq, dev, flag, sflag, cr, usbser_edge_statep));
}


/*
 * TI boot mode
 * ------------
 */
static int
edgeti_boot_attach(dev_info_t *dip, ddi_attach_cmd_t cmd, void *statep)
{
	int		instance;
	edgeti_boot_t	*etp;
	usb_client_dev_data_t *dev_data;

	instance = ddi_get_instance(dip);

	switch (cmd) {
	case DDI_ATTACH:

		break;
	case DDI_RESUME:

		return (DDI_SUCCESS);
	default:

		return (DDI_FAILURE);
	}

	/* attach driver to USBA */
	if (usb_client_attach(dip, USBDRV_VERSION, 0) != USB_SUCCESS) {

		return (DDI_FAILURE);
	}
	if (usb_get_dev_data(dip, &dev_data, USB_PARSE_LVL_IF, 0) !=
	    USB_SUCCESS) {

		return (DDI_FAILURE);
	}

	/* if not TI or not in boot mode, do normal attach */
	if (!edgeti_is_ti(dev_data) || !edgeti_is_boot(dev_data)) {
		usb_client_detach(dip, dev_data);

		return (DDI_ECONTEXT);
	}

	/* TI Edgeport in boot mode */

	/* allocate and get soft state */
	if (ddi_soft_state_zalloc(statep, instance) != DDI_SUCCESS) {
		usb_client_detach(dip, dev_data);

		return (DDI_FAILURE);
	}
	if ((etp = ddi_get_soft_state(statep, instance)) == NULL) {

		goto fail_unreg;
	}

	etp->et_dip = dip;
	etp->et_instance = instance;
	etp->et_dev_data = dev_data;

	/* open bulk pipe for firmware download */
	if (edgeti_boot_open_pipes(etp) != USB_SUCCESS) {

		goto fail_state;
	}

	edgeti_boot_determine_i2c_type(etp);

	/* read mfg descriptor */
	if (edgeti_read_mfg_descr(&etp->et_def_pipe, &etp->et_mfg_descr) !=
	    USB_SUCCESS) {

		goto fail_pipes;
	} else {
		edgeti_manuf_descriptor_t *desc = &etp->et_mfg_descr;

		USB_DPRINTF_L4(DPRINT_DEF_PIPE, etp->et_lh,
		    "edgeti_read_mfg_descr: %x %x %x %x %x %x\n",
		    desc->IonConfig, desc->Version, desc->CpuRev_BoardRev,
		    desc->NumPorts, desc->NumVirtualPorts, desc->TotalPorts);
	}

	/* download operational code */
	if (edgeti_download_code(etp->et_dip, etp->et_lh,
	    etp->et_bulk_pipe.pipe_handle) != USB_SUCCESS) {

		goto fail_pipes;
	}

	/* TI Edgeport boot mode done. */
	return (DDI_SUCCESS);

fail_pipes:
	edgeti_boot_close_pipes(etp);
fail_state:
	ddi_soft_state_free(statep, instance);
fail_unreg:
	usb_client_detach(dip, dev_data);

	return (DDI_FAILURE);
}


static int
edgeti_boot_detach(dev_info_t *dip, ddi_detach_cmd_t cmd, void *statep)
{
	int		instance = ddi_get_instance(dip);
	edgeti_boot_t	*etp;

	etp = ddi_get_soft_state(statep, instance);

	switch (cmd) {
	case DDI_DETACH:

		break;
	case DDI_SUSPEND:

		return (DDI_SUCCESS);
	default:

		return (DDI_FAILURE);
	}

	edgeti_boot_close_pipes(etp);
	usb_client_detach(dip, etp->et_dev_data);
	ddi_soft_state_free(statep, instance);

	return (DDI_SUCCESS);
}
