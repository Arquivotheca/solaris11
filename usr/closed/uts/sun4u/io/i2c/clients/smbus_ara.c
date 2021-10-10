/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/stat.h>		/* ddi_create_minor_node S_IFCHR */
#include <sys/modctl.h>		/* for modldrv */
#include <sys/open.h>		/* for open params.	 */
#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/conf.h>		/* req. by dev_ops flags */
#include <sys/ddi.h>
#include <sys/file.h>
#include <sys/note.h>
#include <sys/i2c/clients/i2c_client.h>

struct smbus_ara_unit {
	kmutex_t		mutex;
	int			oflag;
	i2c_client_hdl_t	hdl;
};

#ifdef DEBUG
static int smbus_ara_debug = 0;
#define	DPRINTF(ARGS) if (smbus_ara_debug & 0x1) cmn_err ARGS;
#else  /* DEBUG */
#define	DPRINTF(ARGS)
#endif /* DEBUG */


static void *soft_statep;

static int smbus_ara_get(struct smbus_ara_unit *, uchar_t *);

/*
 * cb ops
 */
static int smbus_ara_open(dev_t *, int, int, cred_t *);
static int smbus_ara_close(dev_t, int, int, cred_t *);
static int smbus_ara_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);

static struct cb_ops smbus_ara_cbops = {
	smbus_ara_open,			/* open  */
	smbus_ara_close,		/* close */
	nodev,				/* strategy */
	nodev,				/* print */
	nodev,				/* dump */
	nodev,				/* read */
	nodev,				/* write */
	smbus_ara_ioctl,		/* ioctl */
	nodev,				/* devmap */
	nodev,				/* mmap */
	nodev,				/* segmap */
	nochpoll,			/* poll */
	ddi_prop_op,			/* cb_prop_op */
	NULL,				/* streamtab */
	D_NEW | D_MP | D_HOTPLUG,	/* Driver compatibility flag */
	CB_REV,				/* rev */
	nodev,				/* int (*cb_aread)() */
	nodev				/* int (*cb_awrite)() */
};

/*
 * dev ops
 */
static int smbus_ara_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int smbus_ara_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

static struct dev_ops smbus_ara_ops = {
	DEVO_REV,
	0,
	ddi_getinfo_1to1,
	nulldev,
	nulldev,
	smbus_ara_attach,
	smbus_ara_detach,
	nodev,
	&smbus_ara_cbops,
	NULL,
	NULL,
	ddi_quiesce_not_needed,		/* quiesce */
};

extern struct mod_ops mod_driverops;

static struct modldrv smbus_ara_modldrv = {
	&mod_driverops,			/* type of module - driver */
	"SMBUS_ARA i2c device driver",
	&smbus_ara_ops
};

static struct modlinkage smbus_ara_modlinkage = {
	MODREV_1,
	&smbus_ara_modldrv,
	0
};

int
_init(void)
{
	int error;

	error = mod_install(&smbus_ara_modlinkage);
	if (!error)
		(void) ddi_soft_state_init(&soft_statep,
		    sizeof (struct smbus_ara_unit), 1);
	return (error);
}

int
_fini(void)
{
	int error;

	error = mod_remove(&smbus_ara_modlinkage);
	if (!error)
		ddi_soft_state_fini(&soft_statep);
	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&smbus_ara_modlinkage, modinfop));
}

static int
smbus_ara_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct smbus_ara_unit *unitp;
	int instance;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);

	if (ddi_soft_state_zalloc(soft_statep, instance) != 0)
		return (DDI_FAILURE);
	unitp = ddi_get_soft_state(soft_statep, instance);
	if (ddi_create_minor_node(dip, "ara", S_IFCHR, instance,
	    "ddi_i2c:ioexp", NULL) == DDI_FAILURE) {
		goto err;
	}
	if (i2c_client_register(dip, &unitp->hdl) != I2C_SUCCESS) {
		goto err;
	}

	mutex_init(&unitp->mutex, NULL, MUTEX_DRIVER, NULL);
	return (DDI_SUCCESS);

err:
	ddi_remove_minor_node(dip, NULL);
	ddi_soft_state_free(soft_statep, instance);
	return (DDI_FAILURE);
}

static int
smbus_ara_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct smbus_ara_unit *unitp;
	int instance;

	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	unitp = ddi_get_soft_state(soft_statep, instance);

	i2c_client_unregister(unitp->hdl);
	ddi_remove_minor_node(dip, NULL);
	mutex_destroy(&unitp->mutex);
	ddi_soft_state_free(soft_statep, instance);

	return (DDI_SUCCESS);
}

static int
smbus_ara_open(dev_t *devp, int flags, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(credp))

	struct smbus_ara_unit *unitp;
	int instance;
	int error = 0;

	instance = getminor(*devp);
	unitp = (struct smbus_ara_unit *)
	    ddi_get_soft_state(soft_statep, instance);
	if (unitp == NULL)
		return (ENXIO);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	mutex_enter(&unitp->mutex);

	if (flags & FEXCL) {
		if (unitp->oflag != 0) {
			error = EBUSY;
		} else {
			unitp->oflag = FEXCL;
		}
	} else {
		if (unitp->oflag == FEXCL) {
			error = EBUSY;
		} else {
			unitp->oflag = FOPEN;
		}
	}

	mutex_exit(&unitp->mutex);
	return (error);
}

static int
smbus_ara_close(dev_t dev, int flags, int otyp, cred_t *credp)
{
	_NOTE(ARGUNUSED(flags, otyp, credp))

	struct smbus_ara_unit *unitp;
	int instance;

	instance = getminor(dev);
	unitp = (struct smbus_ara_unit *)
	    ddi_get_soft_state(soft_statep, instance);
	if (unitp == NULL)
		return (ENXIO);

	mutex_enter(&unitp->mutex);
	unitp->oflag = 0;
	mutex_exit(&unitp->mutex);
	return (DDI_SUCCESS);
}

static int
smbus_ara_ioctl(dev_t dev, int cmd, intptr_t arg, int mode,
		cred_t *credp, int *rvalp)
{
	_NOTE(ARGUNUSED(credp, rvalp))

	struct smbus_ara_unit *unitp;
	int instance;
	int err = 0;
	i2c_port_t ioctl_port;
	uchar_t byte;

	instance = getminor(dev);
	unitp = (struct smbus_ara_unit *)
	    ddi_get_soft_state(soft_statep, instance);
	if (unitp == NULL)
		return (ENXIO);

	switch (cmd) {
	case I2C_GET_PORT:
		err = smbus_ara_get(unitp, &byte);
		if (err) {
			DPRINTF((CE_NOTE, "smbus_ara_get error: %d", err));
			break;
		}

		ioctl_port.value = byte;
		/*
		 * We silently allow a NULL arg, because the user may just
		 * want to clear an ARA and doesn't care about the value.
		 */
		if (arg != NULL && ddi_copyout(&ioctl_port, (void *)arg,
		    sizeof (i2c_port_t), mode) != DDI_SUCCESS) {
			DPRINTF((CE_WARN, "Failed in I2C_GET_PORT "
			    "ddi_copyout routine"));
				err = EFAULT;
		}

		DPRINTF((CE_NOTE, "contains %x", byte));
		break;

	default:
		DPRINTF((CE_WARN, "Invalid IOCTL cmd: %x", cmd));
		err = EINVAL;
	}

	return (err);
}

static int
smbus_ara_get(struct smbus_ara_unit *unitp, uchar_t *byte) {
	i2c_transfer_t *i2c_tran_pointer;
	int err = 0;

	(void) i2c_transfer_alloc(unitp->hdl, &i2c_tran_pointer,
					0, 1, I2C_SLEEP);
	if (i2c_tran_pointer == NULL)
		return (ENOMEM);

	i2c_tran_pointer->i2c_flags = I2C_RD;
	i2c_tran_pointer->i2c_rlen = 1;
	err = i2c_transfer(unitp->hdl, i2c_tran_pointer);
	if (err == 0) {
		*byte = i2c_tran_pointer->i2c_rbuf[0];
	} else if (err == I2C_FAILURE) {
		/*
		 * i2c_transfer() returns an errno value sometimes and
		 * I2C_FAILURE other times.  Make it consistent.
		 */
		err = EIO;
	}

	i2c_transfer_free(unitp->hdl, i2c_tran_pointer);
	return (err);
}
