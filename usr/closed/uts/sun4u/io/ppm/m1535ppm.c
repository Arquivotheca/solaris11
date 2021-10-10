/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Acer ALi1535D and 1535D+ PCI PMU device gpio reg driver
 *   attach to node with compatible "SUNW,smbus-ppm" or "ali1535d+-ppm"
 *
 * Register Description:
 * first register -
 *    Register Name:	Suspend LED
 *    Register Index:	B3h
 *    Default value:	00h
 *    Attribute:	Read/Write
 *
 *    Bit  2		power button override enable/disable
 *    Bit  1:0		00:  Pin SPLED drives low  (LED on )
 *      		01:  Pin SPLED drives high (LED off)
 *         		10:  Pin SPLED drives 1Hz square waveform output
 *         		11:  Pin SPLED drives 2Hz square waveform output
 *
 * second register -
 *    Register Name:	Data Output GPO[37:36] (GPO_BLK5)
 *    Register Index:	BAh
 *    Default value:	00h
 *    Attribute:	Read/Write
 *
 *    Bit 7-2		Reseved
 *    Bit 1		Data Output to GPO37
 *    Bit 0		Data Output to GPO36
 *
 * third register -
 *    Register Name:	Data Output to GPO[44,35,34,32] (GPO_BLK4[15:8])
 *    Register Index:   B9h
 *    Default value:    04h
 *    Attribute:        Read/Write
 *
 *    Bit 7-5		Reserved
 *    Bit 4		GPO44
 *    Bit 3		GPO35
 *    Bit 2		GPO34
 *    Bit 1		Reserved
 *    Bit 0		GPO32 (GPO_BLK4[8])
 *
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/stat.h>
#include <sys/open.h>
#include <sys/kmem.h>
#include <sys/file.h>
#include <sys/sunldi.h>
#include <sys/ppmvar.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>


/*
 * cb_ops
 */
static int	m1535ppm_open(dev_t *, int, int, cred_t *);
static int	m1535ppm_close(dev_t, int, int, cred_t *);
static int	m1535ppm_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);

static struct cb_ops m1535ppm_cbops = {
	m1535ppm_open,		/* open */
	m1535ppm_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	m1535ppm_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* chpoll */
	ddi_prop_op,		/* prop_op */
	NULL,			/* stream */
	D_MP | D_NEW,		/* flag */
	CB_REV,			/* rev */
	nodev,			/* aread */
	nodev,			/* awrite */
};


/*
 * dev_ops
 */
static int	m1535ppm_attach(dev_info_t *, ddi_attach_cmd_t);
static int	m1535ppm_detach(dev_info_t *, ddi_detach_cmd_t);
static int	m1535ppm_info(dev_info_t *, ddi_info_cmd_t, void *, void **);

static struct dev_ops m1535ppm_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	m1535ppm_info,		/* getinfo */
	nulldev,		/* identify */
	nulldev,		/* probe */
	m1535ppm_attach,	/* attach */
	m1535ppm_detach,	/* detach */
	nodev,			/* reset */
	&m1535ppm_cbops,	/* cb_ops */
	NULL,			/* bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

extern struct mod_ops mod_driverops;
static struct modldrv modldrv = {
	&mod_driverops,
	"ALi1535D PMU GPIO control",
	&m1535ppm_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

typedef struct m1535ppm_reg {
	ddi_acc_handle_t	hndl;
	uint8_t 		*reg;
	int			mask;
} m1535ppm_reg_t;

/* register mask */
#define	SPLED_MASK	(1<<0)
#define	GPO32_MASK	(1<<0)
#define	GPO36_MASK	(1<<0)
#define	GPO37_MASK	(1<<1)

/*
 * Private data structures
 */
struct m1535ppm_unit {
	dev_info_t		*dip;		/* dev_info pointer	*/
	m1535ppm_reg_t		spled;		/* SPLED reg		*/
	m1535ppm_reg_t		gpo_blk5;	/* GPO_BLK5 reg		*/
	m1535ppm_reg_t		gpo_blk4h;	/* GPO_BLK4[15:8] reg	*/
	kmutex_t		lock;		/* per unit mutex	*/
	int			lyropen;
};

#define	SPLED_SUBUNIT	0
#define	GPO32_SUBUNIT	1
#define	GPO36_SUBUNIT	2
#define	GPO37_SUBUNIT	3
#define	m1535_minor(instance, subu)	((instance) << 8 | (subu))
#define	m1535_minor_to_unit(minor)	((minor) & 0xff)
#define	m1535_minor_to_inst(minor)	((minor) >> 8)


/*
 * Global variables
 */
static struct m1535ppm_unit	m1535ppm_state;
int		m1535ppm_inst = -1;
uint64_t	*regmask = NULL;

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * attach(9e)
 */
static int
m1535ppm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	char	*str = "m1535ppm_attach";
	int		instance, minor;
	struct m1535ppm_unit *unitp = &m1535ppm_state;
	ddi_device_acc_attr_t	attr;
	int		rv = DDI_SUCCESS;
	int		regmask_size = 0;
	uint_t		rn = 0;

	switch (cmd) {
	case DDI_ATTACH:
		if (m1535ppm_inst != -1) {
			cmn_err(CE_NOTE, "%s: dev %s@%s already attached", str,
			    ddi_get_name(dip), ddi_get_name_addr(dip) ?
			    ddi_get_name_addr(dip) : " ");
			return (DDI_FAILURE);
		}
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		cmn_err(CE_WARN, "%s: unsupported command %d", str, cmd);
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	unitp->dip = dip;
	m1535ppm_inst = instance;

	/* indicate support LDI (Layered Driver Interface) */
	if ((rv = ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    DDI_KERNEL_IOCTL, NULL, 0)) != DDI_PROP_SUCCESS) {
		cmn_err(CE_NOTE, "%s: failed create property", str);
		goto doerr;
	}

	/* setup registers */
	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	if ((rv = ddi_regs_map_setup(dip, rn++, (caddr_t *)&unitp->spled.reg,
	    0, 1, &attr, &unitp->spled.hndl)) != DDI_SUCCESS) {
		cmn_err(CE_NOTE, "%s: can't map register set 0.", str);
		goto doerr;
	}

	/*
	 * Presently the "register-mask" is set for gpoblk5 register
	 * for register sharing, the first regmask entry (if present) is
	 * for gpo36/gpo37, so regmask should have at least one entry.
	 */
	rv = ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "register-mask", (caddr_t)&regmask, &regmask_size);
	if (rv != DDI_SUCCESS && rv != DDI_PROP_NOT_FOUND) {
		cmn_err(CE_NOTE, "%s: can't get mask for reg set %d", str, rn);
		goto doerr;
	}

	if ((rv == DDI_SUCCESS) && (regmask) && (regmask[0] != 0)) {
		if ((rv = ddi_regs_map_setup(dip, rn++,
		    (caddr_t *)&unitp->gpo_blk5.reg, 0, 1, &attr,
		    &unitp->gpo_blk5.hndl)) != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "%s: can't map reg set %d", str, rn);
			goto doerr;
		}
	}

	/* not all platforms have gpo_blk4h assigned to m1535ppm node */
	rv = ddi_regs_map_setup(dip, rn++, (caddr_t *)&unitp->gpo_blk4h.reg,
	    0, 1, &attr, &unitp->gpo_blk4h.hndl);
	if (rv != DDI_SUCCESS)
		unitp->gpo_blk4h.hndl = NULL;


	/* creates minor node per sub units */
	minor = m1535_minor(instance, SPLED_SUBUNIT);
	if ((rv = ddi_create_minor_node(dip, "spled", S_IFCHR,
	    minor, NULL, NULL)) != DDI_SUCCESS)
		goto doerr;

	if (unitp->gpo_blk5.hndl != NULL) {
		if (regmask[0] & GPO36_MASK) {
		minor = m1535_minor(instance, GPO36_SUBUNIT);
		if ((rv = ddi_create_minor_node(dip, "gpo36", S_IFCHR,
		    minor, NULL, NULL)) != DDI_SUCCESS)
			goto doerr;
		}

		if (regmask[0] & GPO37_MASK) {
		minor = m1535_minor(instance, GPO37_SUBUNIT);
		if ((rv = ddi_create_minor_node(dip, "gpo37", S_IFCHR,
		    minor, NULL, NULL)) != DDI_SUCCESS)
			goto doerr;
		}
	}

	if (unitp->gpo_blk4h.hndl != NULL) {
		minor = m1535_minor(instance, GPO32_SUBUNIT);
		if ((rv = ddi_create_minor_node(dip, "gpo32", S_IFCHR,
		    minor, NULL, NULL)) != DDI_SUCCESS)
			goto doerr;
	}

	mutex_init(&unitp->lock, NULL, MUTEX_DRIVER, NULL);
	unitp->lyropen = 0;
	ddi_report_dev(dip);
	return (rv);
	/* NOTREACHED */

doerr:
	if (unitp->spled.hndl != NULL)
		ddi_regs_map_free(&unitp->spled.hndl);

	if (unitp->gpo_blk5.hndl != NULL)
		ddi_regs_map_free(&unitp->gpo_blk5.hndl);

	if (unitp->gpo_blk4h.hndl != NULL)
		ddi_regs_map_free(&unitp->gpo_blk4h.hndl);

	if (regmask) {
		kmem_free(regmask, regmask_size);
		regmask = NULL;
	}

	if (ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_NOTPROM, DDI_KERNEL_IOCTL))
		ddi_prop_remove_all(dip);

	m1535ppm_inst = -1;
	return (rv);
}

/*
 * getinfo(9e)
 */
/* ARGSUSED */
static int
m1535ppm_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **result)
{
	dev_t	dev;
	int	instance;

	if (m1535ppm_inst == -1)
		return (DDI_FAILURE);

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *) m1535ppm_state.dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t)arg;
		instance = m1535_minor_to_inst(getminor(dev));
		*result = (void *)(uintptr_t)instance;
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}


/*
 * detach(9e)
 */
/* ARGSUSED */
static int
m1535ppm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		/*
		 * Upon CPR resume, OBP overwrites these registers according
		 * to their platform specific usages, therefore no need to
		 * save the register contents here.
		 */
		return (DDI_SUCCESS);
	default:
		cmn_err(CE_WARN, "m1535ppm_detach: unsupported cmd(%d)", cmd);
		return (DDI_FAILURE);
	}
}


/* ARGSUSED */
static int
m1535ppm_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	struct m1535ppm_unit	*unitp = &m1535ppm_state;
	int subu;

	if (drv_priv(cred_p) != 0)
		return (EPERM);

	/* LDI requirement: one open per layered interface per minor node */
	subu = m1535_minor_to_unit(getminor(*dev_p));
	switch (subu) {
	case SPLED_SUBUNIT:
		break;

	case GPO32_SUBUNIT:
		break;

	case GPO36_SUBUNIT:
		if (regmask == NULL || (regmask[0] & GPO36_MASK) == 0)
			return (EINVAL);
		break;

	case GPO37_SUBUNIT:
		if (regmask == NULL || (regmask[0] & GPO37_MASK) == 0)
			return (EINVAL);
		break;

	default:
		return (EINVAL);
	}

	mutex_enter(&unitp->lock);
	if ((unitp->lyropen & (1 << subu)) != 0) {
		mutex_exit(&unitp->lock);
		return (EBUSY);
	}
	unitp->lyropen |= (1 << subu);
	mutex_exit(&unitp->lock);

	return (DDI_SUCCESS);
}


/* ARGSUSED */
static int
m1535ppm_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	struct m1535ppm_unit	*unitp = &m1535ppm_state;
	int subu;

	subu = m1535_minor_to_unit(getminor(dev));
	mutex_enter(&unitp->lock);
	unitp->lyropen &= ~(1 << subu);
	mutex_exit(&unitp->lock);

	return (DDI_SUCCESS);
}

#define	M1535PPMIOC		('l' << 8)
#define	M1535PPMIOC_GET	(M1535PPMIOC | 1)
#define	M1535PPMIOC_SET	(M1535PPMIOC | 2)

/* ARGSUSED */
static int
m1535ppm_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred_p,
    int *rval_p)
{
	uint_t	val;
	struct m1535ppm_unit *unitp = &m1535ppm_state;
	int subu;
	m1535ppm_reg_t	*regp;
	uint8_t  data8;

	if (drv_priv(cred_p) != DDI_SUCCESS)
		return (EPERM);

	if (ddi_copyin((caddr_t)arg, &val, sizeof (val), mode) != 0)
		return (EFAULT);

	subu = m1535_minor_to_unit(getminor(dev));
	switch (subu) {
	case SPLED_SUBUNIT:
		regp = &unitp->spled;
		regp->mask = SPLED_MASK;
		break;
	case GPO32_SUBUNIT:
		regp = &unitp->gpo_blk4h;
		regp->mask = GPO32_MASK;
		break;
	case GPO36_SUBUNIT:
		regp = &unitp->gpo_blk5;
		regp->mask = GPO36_MASK;
		break;
	case GPO37_SUBUNIT:
		regp = &unitp->gpo_blk5;
		regp->mask = GPO37_MASK;
		break;
	default:
		return (EINVAL);
	}

	ASSERT(regp->hndl);
	mutex_enter(&unitp->lock);
	data8 = ddi_get8(regp->hndl, (uint8_t *)regp->reg);
	switch (cmd) {
	case M1535PPMIOC_GET:
		val = (uint_t)(data8 & regp->mask);
		if (ddi_copyout((const void *)&val, (void *)arg,
		    sizeof (val), mode) != 0)
			return (EFAULT);
		break;

	case M1535PPMIOC_SET:
		data8 = (data8 & ~regp->mask) | ((uint8_t)val & regp->mask);
		ddi_put8(regp->hndl, (uint8_t *)regp->reg, data8);
		(void) ddi_get8(regp->hndl, (uint8_t *)regp->reg);
		break;

	default:
		mutex_exit(&unitp->lock);
		return (EINVAL);
	}

	mutex_exit(&unitp->lock);

	return (0);
}
