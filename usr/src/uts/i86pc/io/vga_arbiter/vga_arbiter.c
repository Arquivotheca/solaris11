/*
 * vgaarb.c: Implements the VGA arbitration. For details refer to
 * Documentation/vgaarbiter.txt
 *
 *
 * (C) Copyright 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * (C) Copyright 2007 Paulo R. Zanoni <przanoni@gmail.com>
 * (C) Copyright 2007, 2009 Tiago Vignatti <vignatti@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS
 * IN THE SOFTWARE.
 *
 */
/* Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved. */

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/ddi_impldefs.h>
#include <vm/seg_kmem.h>
#include <sys/vmsystm.h>
#include <sys/sysmacros.h>
#include <sys/ddidevmap.h>
#include <sys/cmn_err.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/pci.h>
#include <sys/stdbool.h>
#include <sys/vga_arbiter.h>

#define MAX_USER_CARDS		16

static int vga_arbiter_open(dev_t *devp, int flag, int otyp, cred_t *cred);
static int vga_arbiter_close(dev_t dev, int flag, int otyp, cred_t *cred);
static int vga_arbiter_read(dev_t dev, struct uio *uiop, cred_t *credp);
static int vga_arbiter_write(dev_t dev, struct uio *uiop, cred_t *credp);
static int vga_arbiter_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int vga_arbiter_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int vga_arbiter_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **result);
static bool vga_arbiter_add_pci_device(dev_info_t *dip);
static void vga_arbiter_notify_clients(void);

static 	struct cb_ops vga_arbiter_cb_ops = {
	vga_arbiter_open,	/* cb_open */
	vga_arbiter_close,	/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	vga_arbiter_read,	/* cb_read */
	vga_arbiter_write,	/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	NULL,			/* cb_mmap */
	NULL,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	NULL,			/* cb_stream */
	D_NEW | D_MP | D_64BIT,	/* cb_flag */
	CB_REV
};

static struct dev_ops vga_arbiter_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	vga_arbiter_getinfo,	/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	vga_arbiter_attach,	/* devo_attach */
	vga_arbiter_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	&vga_arbiter_cb_ops,	/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv vga_arbiter_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"vga_arbiter driver",	/* Name of the module. */
	&vga_arbiter_dev_ops,	/* driver ops */
};

static struct modlinkage vga_arbiter_modlinkage = {
	MODREV_1,
	(void *) &vga_arbiter_modldrv,
	NULL
};

struct vga_device {
	list_node_t link;
	dev_info_t *dip;
	unsigned int decodes;		/* what does it decodes */
	unsigned int owns;		/* what does it owns */
	unsigned int lega_locks;	/* what does it locks for legacy access */
	unsigned int norm_locks;	/* what does it locks for non-legacy (normal) access */
	unsigned int io_lega_lock_cnt;	/* legacy IO lock count */
	unsigned int mem_lega_lock_cnt;	/* legacy MEM lock count */
	unsigned int io_norm_lock_cnt;	/* normal IO lock count */
	unsigned int mem_norm_lock_cnt;	/* normal MEM lock count */
	unsigned int domain, bus, dev, func;

	/* allow IRQ enable/disable hook */
	void *cookie;
	void (*irq_set_state)(void *cookie, bool enable);
	unsigned int (*set_vga_decode)(void *cookie, bool decode);
};

/*
 * Each user has an array of these, tracking which cards have locks
 */
struct vga_arb_user_card {
	dev_info_t *dip;
	unsigned int mem_lega_cnt;
	unsigned int io_lega_cnt;
	unsigned int mem_norm_cnt;
	unsigned int io_norm_cnt;
};

struct vga_arbiter_private {
	list_node_t link;
	dev_info_t *target;	/* vga device currently being used */
	struct vga_arb_user_card cards[MAX_USER_CARDS];
};

static list_t 			vga_list;
static unsigned int 		vga_count, vga_decode_count;
static bool 			vga_arbiter_used;
/* mutex for access of vga device and arbitration code */
static kmutex_t 		vga_lock;
/* mutex for user open and close */
static kmutex_t			vga_user_lock;
/* mutex for operations on vga_wait_queue */
static kmutex_t			wait_lock;
static kcondvar_t 		vga_wait_queue;
static list_t			vga_user_list;
static struct vga_arbiter_private	*user_data[VGA_MAX_OPEN];

static int			debug;

/*
 * _init()
 *
 */
int
_init(void)
{
	int err;

	err = mod_install(&vga_arbiter_modlinkage);

	return (err);
}

/*
 * _info()
 *
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&vga_arbiter_modlinkage, modinfop));
}

/*
 * _fini()
 *
 */
int
_fini(void)
{
	int err;

	err = mod_remove(&vga_arbiter_modlinkage);

	return (err);
}

/*ARGSUSED*/
static int
find_and_add_vga_device(dev_info_t *dip, void *found)
{
	char *dev_type;

	if (dip == ddi_root_node())
		return (DDI_WALK_CONTINUE);

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device_type", &dev_type) != DDI_SUCCESS)
		return (DDI_WALK_PRUNECHILD);

	if (strcmp(dev_type, "display") == 0)
		(void) vga_arbiter_add_pci_device(dip);

	ddi_prop_free(dev_type);

	return (DDI_WALK_CONTINUE);
}

/*
 * vga_arbiter_attach()
 *
 */
static int
vga_arbiter_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int instance;
	unsigned int err;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);

	(void) memset(user_data, 0, VGA_MAX_OPEN * sizeof(struct vga_arbiter_private *));

	/* create the minor node (for the read/write) */
	err = ddi_create_minor_node(dip, ddi_get_name(dip), S_IFCHR, instance, 
		DDI_PSEUDO, 0);

	if (err != DDI_SUCCESS) {
		return (err);
	}

	mutex_init(&vga_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&vga_user_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&wait_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&vga_wait_queue, NULL, CV_DRIVER, NULL);

	list_create(&vga_list, sizeof(struct vga_device), 
		offsetof(struct vga_device, link));
	list_create(&vga_user_list, sizeof(struct vga_arbiter_private), 
		offsetof(struct vga_arbiter_private, link));

	/* create vga_list */
	ddi_walk_devs(ddi_root_node(), find_and_add_vga_device, NULL);

	cmn_err(CE_CONT, "!%d vga device(s) found", vga_count);

	/* Report that driver was loaded */
	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

/*
 * vga_arbiter_detach()
 *
 */
static int
vga_arbiter_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct vga_device *vgadev;
	struct vga_arbiter_private *priv;

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	ddi_remove_minor_node(dip, NULL);

	mutex_destroy(&vga_lock);
	mutex_destroy(&vga_user_lock);
	mutex_destroy(&wait_lock);
	cv_destroy(&vga_wait_queue);

	while (vgadev = list_head(&vga_list)) {
		list_remove(&vga_list, vgadev);
		kmem_free(vgadev, sizeof(struct vga_device));
	}
	list_destroy(&vga_list);

	while (priv = list_head(&vga_user_list)) {
		list_remove(&vga_user_list, priv);
		kmem_free(priv, sizeof(struct vga_arbiter_private));
	}
	list_destroy(&vga_user_list);

	return (DDI_SUCCESS);
}

/*
 * vga_arbiter_getinfo()
 *
 */
/*ARGSUSED*/
static int
vga_arbiter_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	minor_t minor;
	dev_t dev;
	int err;

	dev = (dev_t)arg;
	minor = getminor(dev);

	if (minor >= VGA_MAX_OPEN)
                return (DDI_FAILURE);

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (user_data[minor] == NULL) {
			err = DDI_FAILURE;
			break;
		}
		*result = user_data[minor]->target;
		err = DDI_SUCCESS;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)minor;
		err = DDI_SUCCESS;
		break;

	default:
		err = DDI_FAILURE;
		break;
	}

	return (err);
}

static char *vga_iostate_to_str[] = {
	"none",
	"io",
	"mem",
	"io+mem",
	"IO",
	"io+IO",
	"mem+IO",
	"io+mem+IO",
	"MEM",
	"io+MEM",
	"mem+MEM",
	"io+mem+MEM",
	"IO+MEM",
	"io+IO+MEM",
	"mem+IO+MEM",
	"io+mem+IO+MEM"
};

static bool vga_str_to_iostate(char *buf, unsigned int *io_state)
{
	unsigned int state = 0;
	char *bptr;

	/* we could in theory hand out locks on IO and mem
	 * separately to userspace but it can cause deadlocks */
	if (strncmp(buf, "none", 4) == 0) {
		*io_state = VGA_RSRC_NONE;
		return true;
	}

	for (bptr = buf; *bptr != 0; bptr++) {
		if (strncmp (bptr, "io", 2) == 0) {
			state |= VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
			bptr += 2;
		} else if (strncmp (bptr, "mem", 3) == 0) {
			state |= VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM;
			bptr += 3;
		} else if (strncmp (bptr, "IO", 2) == 0) {
			state |= VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
			bptr += 2;
		} else if (strncmp (bptr, "MEM", 3) == 0) {
			state |= VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
			bptr += 3;
		} else if (strncmp (bptr, "all", 3) == 0) {
			state |= VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
				VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
			break;
		} else
			break;

		if (*bptr != '+')
			break;
	};

	if (!state)
		return false;

	*io_state = state;

	return true;
}

#ifndef __ARCH_HAS_VGA_DEFAULT_DEVICE
/* this is only used a cookie - it should not be dereferenced */
static dev_info_t *vga_default;
#endif

static void vga_arb_device_card_gone(dev_info_t *dip);

/* Find somebody in our list */
static struct vga_device *vgadev_find(dev_info_t *dip, unsigned int bus,
	unsigned int devfn)
{
	struct vga_device *vgadev;

	if (dip) /* find using dip */ {
		for (vgadev = list_head(&vga_list); vgadev; 
			vgadev = list_next(&vga_list, vgadev)) {
			if (dip == vgadev->dip)
				return vgadev;
		}
	}
	else /* find using bus/devfn) */ {
		unsigned int i;

		for (vgadev = list_head(&vga_list); vgadev; 
			vgadev = list_next(&vga_list, vgadev)) {
			i = ((vgadev->dev & 0x1f) << 3) | (vgadev->func & 0x07);
			if ((bus == vgadev->bus) && (devfn == i))
				return vgadev;
		}
	}

	return NULL;
}

/* Returns the default VGA device (vgacon's babe) */
#ifndef __ARCH_HAS_VGA_DEFAULT_DEVICE
dev_info_t *vga_default_device(void)
{
	return vga_default;
}
#endif

static inline void vga_irq_set_state(struct vga_device *vgadev, int state)
{
	if (vgadev->irq_set_state)
		vgadev->irq_set_state(vgadev->cookie, state);
}


/* If we don't ever use VGA arb we should avoid
   turning off anything anywhere due to old X servers getting
   confused about the boot device not being VGA */
static void vga_check_first_use(void)
{
	/* we should inform all GPUs in the system that
	 * VGA arb has occured and to try and disable resources
	 * if they can */
	mutex_enter(&vga_lock);
	if (!vga_arbiter_used) {
		vga_arbiter_used = true;
		mutex_exit(&vga_lock);
		vga_arbiter_notify_clients();
	} else
		mutex_exit(&vga_lock);
	
}

static void pci_set_vga_state(dev_info_t *dip, bool decode,
	unsigned int command_bits, bool change_bridge)
{
	ddi_acc_handle_t pci_conf;
        uint16_t cmd;
	dev_info_t *pdip;

	if (i_ddi_attach_node_hierarchy(dip) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "vga_arbiter: attach_node_hierarchy failed");
		return;
	}

	if (pci_config_setup(dip, &pci_conf) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "vga_arbiter: pci_config_setup failed");
		return;
	}

        cmd = pci_config_get16(pci_conf, PCI_CONF_COMM);

        if (decode == true)
                cmd |= command_bits;
        else
                cmd &= ~command_bits;
        pci_config_put16(pci_conf, PCI_CONF_COMM, cmd);
	pci_config_teardown(&pci_conf);

        if (change_bridge == false)
                return;

	pdip = ddi_get_parent(dip);
	while (pdip) {
		char	*parent_type = NULL;

		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, pdip,
			DDI_PROP_DONTPASS, "device_type", &parent_type)
			!= DDI_SUCCESS)
			return;

		/* Verify still on the PCI/PCIEX parent tree */
		if (strcmp(parent_type, "pci") && strcmp(parent_type, "pciex")) {
			ddi_prop_free(parent_type);
			return;
		}

		ddi_prop_free(parent_type);
		parent_type = NULL;

		if (pci_config_setup(pdip, &pci_conf) != DDI_SUCCESS)
			return;

		cmd = pci_config_get16(pci_conf, PCI_BCNF_BCNTRL);
		if (decode == true)
			cmd |= PCI_BCNF_BCNTRL_VGA_ENABLE;
		else
			cmd &= ~PCI_BCNF_BCNTRL_VGA_ENABLE;
		pci_config_put16(pci_conf, PCI_BCNF_BCNTRL, cmd);
		pci_config_teardown(&pci_conf);

		pdip = ddi_get_parent(pdip);
	}
}

static struct vga_device *__vga_tryget(struct vga_device *vgadev,
					unsigned int rsrc)
{
	unsigned int wants, lega_wants, norm_wants, match;
	struct vga_device *conflict;
	unsigned int pci_bits;

	/* Check what resources we need to acquire  */
	wants = rsrc & ~vgadev->owns;

	/* We don't need to request a legacy resource, we just enable
	 * appropriate decoding and go
	 */
        lega_wants = rsrc & VGA_RSRC_LEGACY_MASK;

	/* already own everything: legal wants still need to grab other devices' owns */
	if ((wants == 0) && (lega_wants == 0))
		goto lock_them;

	/* wants not equal to 0 */
        norm_wants = wants & VGA_RSRC_NORMAL_MASK;

	/* Ok, we don't, let's find out how we need to kick off */
	for (conflict = list_head(&vga_list); conflict;
		conflict = list_next(&vga_list, conflict)) {
		unsigned int lwants = lega_wants;
		unsigned int nwants = norm_wants;
		unsigned int change_bridge = 0;

		/* Don't conflict with myself */
		if (vgadev == conflict)
			continue;

		/* Check if the architecture allows a conflict between those
		 * 2 devices or if they are on separate domains
		 */
		if (!vga_conflicts(vgadev->dip, conflict->dip))
			continue;

		/* We have a possible conflict. before we go further, we must
		 * check if we sit on the same bus as the conflicting device.
		 * if we don't, then we must tie both IO and MEM resources
		 * together since there is only a single bit controlling
		 * VGA forwarding on P2P bridges
		 */
		if (vgadev->bus != conflict->bus) {
			change_bridge = 1;
			if (lwants)
				lwants = VGA_RSRC_LEGACY_IO|VGA_RSRC_LEGACY_MEM;
			if (nwants)
				nwants = VGA_RSRC_NORMAL_IO|VGA_RSRC_NORMAL_MEM;
		}

		/* Check if the guy has a lock on the resource. If he does,
		 * return the conflicting entry
		 */
		/* legacy request: check conflict with legacy lock */
		if (lwants & conflict->lega_locks)
			return conflict;

		/* legacy request: check conflict with non-legacy lock */
		if (lwants & (conflict->norm_locks >> VGA_RSRC_LEGACY_TO_NORMAL_SHIFT))
			return conflict;

		/* non-legacy request: check conflict with legacy lock */
		if (nwants & (conflict->lega_locks << VGA_RSRC_LEGACY_TO_NORMAL_SHIFT))
			return conflict;

		/* Ok, now check if he owns the resource we want. We don't need
		 * to check "decodes" since it should be impossible to own
		 * legacy resources you don't decode unless I have a bug
		 * in this code...
		 */

		match = lwants & conflict->owns;
			 
		if (!match)
			continue;

		/* looks like he doesn't have a lock, we can steal
		 * them from him
		 */
		vga_irq_set_state(conflict, false);

		pci_bits = 0;
		if (lwants & VGA_RSRC_LEGACY_MEM) {
			pci_bits |= PCI_COMM_MAE;
			conflict->owns &= ~(VGA_RSRC_NORMAL_MEM|VGA_RSRC_LEGACY_MEM);
		}
		if (lwants & VGA_RSRC_LEGACY_IO) {
			pci_bits |= PCI_COMM_IO;
			conflict->owns &= ~(VGA_RSRC_NORMAL_IO|VGA_RSRC_LEGACY_IO);
		} 

		pci_set_vga_state(conflict->dip, false, pci_bits, change_bridge);
	}

	/* ok dude, we got it, everybody conflicting has been disabled, let's
	 * enable us. Make sure we don't mark a bit in "owns" that we don't
	 * also have in "decodes". We can lock resources we don't decode but
	 * not own them.
	 */
	if (wants) {
		pci_bits = 0;
		if (wants & (VGA_RSRC_LEGACY_MEM|VGA_RSRC_NORMAL_MEM)) {
			vgadev->owns |= VGA_RSRC_LEGACY_MEM|VGA_RSRC_NORMAL_MEM;
			pci_bits |= PCI_COMM_MAE;
		}
		if (wants & (VGA_RSRC_LEGACY_IO|VGA_RSRC_NORMAL_IO)) {
			vgadev->owns |= VGA_RSRC_LEGACY_IO|VGA_RSRC_NORMAL_IO;
			pci_bits |= PCI_COMM_IO;
		}
		pci_set_vga_state(vgadev->dip, true, pci_bits, !!(lega_wants));
	}

	vga_irq_set_state(vgadev, true);
lock_them:
	if (rsrc & VGA_RSRC_LEGACY_MASK) {
		vgadev->lega_locks |= rsrc & VGA_RSRC_LEGACY_MASK;
		if (rsrc & VGA_RSRC_LEGACY_IO)
			vgadev->io_lega_lock_cnt++;
		if (rsrc & VGA_RSRC_LEGACY_MEM)
			vgadev->mem_lega_lock_cnt++;
	}
	if (rsrc & VGA_RSRC_NORMAL_MASK) {
		vgadev->norm_locks |= rsrc & VGA_RSRC_NORMAL_MASK;
		if (rsrc & VGA_RSRC_NORMAL_IO)
			vgadev->io_norm_lock_cnt++;
		if (rsrc & VGA_RSRC_NORMAL_MEM)
			vgadev->mem_norm_lock_cnt++;
	}
	return NULL;
}

static void __vga_put(struct vga_device *vgadev, unsigned int rsrc)
{
	unsigned int old_lega_locks = vgadev->lega_locks;
	unsigned int old_norm_locks = vgadev->norm_locks;

	/* Update our counters, and account for equivalent legacy resources
	 * if we decode them
	 */
	if ((rsrc & VGA_RSRC_NORMAL_IO) && vgadev->io_norm_lock_cnt > 0)
		vgadev->io_norm_lock_cnt--;
	if ((rsrc & VGA_RSRC_NORMAL_MEM) && vgadev->mem_norm_lock_cnt > 0)
		vgadev->mem_norm_lock_cnt--;
	if ((rsrc & VGA_RSRC_LEGACY_IO) && vgadev->io_lega_lock_cnt > 0)
		vgadev->io_lega_lock_cnt--;
	if ((rsrc & VGA_RSRC_LEGACY_MEM) && vgadev->mem_lega_lock_cnt > 0)
		vgadev->mem_lega_lock_cnt--;

	/* Just clear lock bits, we do lazy operations so we don't really
	 * have to bother about anything else at this point
	 */
	if (vgadev->io_lega_lock_cnt == 0)
		vgadev->lega_locks &= ~VGA_RSRC_LEGACY_IO;
	if (vgadev->mem_lega_lock_cnt == 0)
		vgadev->lega_locks &= ~VGA_RSRC_LEGACY_MEM;
	if (vgadev->io_norm_lock_cnt == 0)
		vgadev->norm_locks &= ~VGA_RSRC_NORMAL_IO;
	if (vgadev->mem_norm_lock_cnt == 0)
		vgadev->norm_locks &= ~VGA_RSRC_NORMAL_MEM;

	/* Kick the wait queue in case somebody was waiting if we actually
	 * released something
	 */
	if ((old_lega_locks != vgadev->lega_locks) || (old_norm_locks != vgadev->norm_locks)) {
		mutex_enter(&wait_lock);
		cv_broadcast(&vga_wait_queue);
		mutex_exit(&wait_lock);
	}
}

int vga_get(dev_info_t *dip, unsigned int rsrc, unsigned int io_norm_cnt, 
		unsigned int mem_norm_cnt, int interruptible)
{
	struct vga_device *vgadev, *conflict;
	int rc = 0;
	bool released = false;

	vga_check_first_use();

	for (;;) {
		mutex_enter(&vga_lock);
		vgadev = vgadev_find(dip, 0, 0);
		if (vgadev == NULL) {
			mutex_exit(&vga_lock);
			rc = ENODEV;
			break;
		}
		conflict = __vga_tryget(vgadev, rsrc);

		if (conflict && (mem_norm_cnt || io_norm_cnt) && !released) {
			/* Release normal lock resources the same process has acquired
			   before going to sleep. Note here rsrc must have legacy requests.
			   If it doesn't (i.e. it has only normal requests), since it
			   already had normal locks, there wouldn't be any conflict  */
			if (!(vgadev->io_norm_lock_cnt -= io_norm_cnt))
				vgadev->norm_locks &= ~VGA_RSRC_NORMAL_IO;
			if (!(vgadev->mem_norm_lock_cnt -= mem_norm_cnt))
				vgadev->norm_locks &= ~VGA_RSRC_NORMAL_MEM;
			released = true;
		}
		if (!conflict && released) {
			/* Restore released normal lock resources before proceeding.
			   Note since legay requests are granted now, there shouldn't
			   be any problem of restoring normal lock resources without
			   arbitration  */
			if (vgadev->io_norm_lock_cnt += io_norm_cnt)
				vgadev->norm_locks |= VGA_RSRC_NORMAL_IO;
			if (vgadev->mem_norm_lock_cnt += mem_norm_cnt)
				vgadev->norm_locks |= VGA_RSRC_NORMAL_MEM;
		}
		mutex_exit(&vga_lock);

		if (conflict == NULL)
			break;


		/* We have a conflict, we wait until somebody kicks the
		 * work queue. Currently we have one work queue that we
		 * kick each time some resources are released, but it would
		 * be fairly easy to have a per device one so that we only
		 * need to attach to the conflicting device
		 */
		mutex_enter(&wait_lock);
		if (interruptible) {
			if (cv_wait_sig(&vga_wait_queue, &wait_lock) == 0) {
				mutex_exit(&wait_lock);
				return EINTR;
			}
		} else 
			cv_wait(&vga_wait_queue, &wait_lock);
		mutex_exit(&wait_lock);
	}

	return rc;
}

int vga_tryget(dev_info_t *dip, unsigned int rsrc)
{
	struct vga_device *vgadev;
	int rc = 0;

	vga_check_first_use();

	/* The one who calls us should check for this, but lets be sure... */
	if (dip == NULL) {
		dip = vga_default_device();
		if (dip == NULL)
			return 0;
	}

	mutex_enter(&vga_lock);
	vgadev = vgadev_find(dip, 0, 0);
	if (vgadev == NULL) {
		rc = ENODEV;
		goto bail;
	}
	if (__vga_tryget(vgadev, rsrc))
		rc = EBUSY;
bail:
	mutex_exit(&vga_lock);

	return rc;
}

void vga_put(dev_info_t *dip, unsigned int rsrc)
{
	struct vga_device *vgadev;

	/* The one who calls us should check for this, but lets be sure... */
	if (dip == NULL) {
		dip = vga_default_device();
		if (dip == NULL)
			return;
	}

	mutex_enter(&vga_lock);
	vgadev = vgadev_find(dip, 0, 0);
	if (vgadev == NULL)
		goto bail;
	__vga_put(vgadev, rsrc);
bail:
	mutex_exit(&vga_lock);
}

/*
 * Currently, we assume that the "initial" setup of the system is
 * not sane, that is we come up with conflicting devices and let
 * the arbiter's client decides if devices decodes or not legacy
 * things.
 */
static bool vga_arbiter_add_pci_device(dev_info_t *dip)
{
	struct vga_device *vgadev;
	ddi_acc_handle_t pci_conf;
	uint8_t base, sub;
	uint16_t cmd, busctrl;
	dev_info_t *pdip;
	pci_regspec_t *pcirp;
	char devname[32];
	uint_t n;

	/* Only deal with VGA class devices */
	if (i_ddi_attach_node_hierarchy(dip) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "vga_arbiter: attach_node_hierarchy failed");
		return false;
	}

	if (pci_config_setup(dip, &pci_conf) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "vga_arbiter: pci_config_setup failed");
		return false;
	}

	base = pci_config_get8(pci_conf, PCI_CONF_BASCLASS);
	sub = pci_config_get8(pci_conf, PCI_CONF_SUBCLASS);
	cmd = pci_config_get16(pci_conf, PCI_CONF_COMM);

	pci_config_teardown(&pci_conf);

	if ((base != PCI_CLASS_DISPLAY) || (sub != PCI_DISPLAY_VGA)) {
		cmn_err(CE_WARN, 
			"add_pci_device:  PCI_CLASS_DISPLAY|PCI_DISPLAY_VGA not match");
		return false;
	}

	/* Allocate structure */
	vgadev = kmem_alloc(sizeof(struct vga_device), KM_SLEEP);

	(void) memset(vgadev, 0, sizeof(struct vga_device));

	/* Take lock & check for duplicates */
	mutex_enter(&vga_lock);
	if (vgadev_find(dip, 0, 0) != NULL) {
		mutex_exit(&vga_lock);
		cmn_err(CE_WARN, "vga_arbiter: found duplicate pci device entry");
		goto fail;
	}
	vgadev->dip = dip;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip,
		DDI_PROP_DONTPASS, "reg", (int **)&pcirp, &n) != DDI_SUCCESS) {
		mutex_exit(&vga_lock);
		goto fail;
	}

	vgadev->domain = 0;  /* for the time being */
	vgadev->bus = PCI_REG_BUS_G(pcirp->pci_phys_hi);
	vgadev->dev = PCI_REG_DEV_G(pcirp->pci_phys_hi);
	vgadev->func = PCI_REG_FUNC_G(pcirp->pci_phys_hi);

	ddi_prop_free(pcirp);

	/* By default, assume we decode everything */
	vgadev->decodes = VGA_RSRC_LEGACY_IO| VGA_RSRC_LEGACY_MEM |
		  VGA_RSRC_NORMAL_IO| VGA_RSRC_NORMAL_MEM;

	/* Mark that we "own" resources based on our enables, we will
	 * clear that below if the bridge isn't forwarding
	 */
	if (cmd & PCI_COMM_IO)
		vgadev->owns |= VGA_RSRC_LEGACY_IO|VGA_RSRC_NORMAL_IO;
	if (cmd & PCI_COMM_MAE)
		vgadev->owns |= VGA_RSRC_LEGACY_MEM|VGA_RSRC_NORMAL_MEM;

	pdip = ddi_get_parent(dip);
	while (pdip) {
		char	*parent_type = NULL;

		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, pdip,
			DDI_PROP_DONTPASS, "device_type", &parent_type)
			!= DDI_SUCCESS)
			break;

		/* Verify still on the PCI/PCIEX parent tree */
		if (strcmp(parent_type, "pci") && strcmp(parent_type, "pciex")) {
			ddi_prop_free(parent_type);
			break;
		}

		ddi_prop_free(parent_type);
		parent_type = NULL;

		if (pci_config_setup(pdip, &pci_conf) != DDI_SUCCESS) {
			pdip = ddi_get_parent(pdip);
			continue;
		}

		busctrl = pci_config_get16(pci_conf, PCI_BCNF_BCNTRL);
		pci_config_teardown(&pci_conf);

		if (!(busctrl & PCI_BCNF_BCNTRL_VGA_ENABLE)) {
			vgadev->owns = 0;
			break;
		}

		pdip = ddi_get_parent(pdip);
	}

	/* by default mark it as decoding */
	vga_decode_count++;

	/* Deal with VGA default device. Use first enabled one
	 * by default if arch doesn't have it's own hook
	 */
#ifndef __ARCH_HAS_VGA_DEFAULT_DEVICE
	if (vga_default == NULL &&
	    ((vgadev->owns & VGA_RSRC_LEGACY_MASK) == VGA_RSRC_LEGACY_MASK))
		vga_default = dip;
#endif

	/* Add to the list */
	list_insert_head(&vga_list, vgadev);
	vga_count++;
	mutex_exit(&vga_lock);

	(void) snprintf(devname, 32, "%x:%x:%x.%x",
		vgadev->domain, vgadev->bus, vgadev->dev, vgadev->func);

	cmn_err(CE_CONT,
		"!vga_arbiter: device added: PCI:%s,decodes=%s,owns=%s,legalocks=%s,normlocks=%s\n",
		devname,
		vga_iostate_to_str[vgadev->decodes],
		vga_iostate_to_str[vgadev->owns],
		vga_iostate_to_str[vgadev->lega_locks],
		vga_iostate_to_str[vgadev->norm_locks]);

	return true;
fail:
	kmem_free(vgadev, sizeof(struct vga_device));
	return false;
}

#ifdef NOT_IN_USE
static bool vga_arbiter_del_pci_device(dev_info_t *dip)
{
	struct vga_device *vgadev;
	bool ret = true;

	mutex_enter(&vga_lock);

	vgadev = vgadev_find(dip, 0, 0);
	if (vgadev == NULL) {
		ret = false;
		goto bail;
	}

	if (vga_default == dip)
		vga_default = NULL;

	if (vgadev->decodes & (VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM))
		vga_decode_count--;

	/* Remove entry from list */
	list_remove(&vga_list, vgadev);
	vga_count--;
	
	/* Notify userland driver that the device is gone so it discards
	 * it's copies of the dip pointer
	 */
	vga_arb_device_card_gone(dip);

	/* Wake up all possible waiters */
	mutex_enter(&wait_lock);
	cv_broadcast(&vga_wait_queue);
	mutex_exit(&wait_lock);
bail:
	mutex_exit(&vga_lock);
	kmem_free(vgadev, sizeof(struct vga_device));
	return ret;
}
#endif

/* this is called with the lock */
static inline void vga_update_device_decodes(struct vga_device *vgadev,
					     int new_decodes)
{
	unsigned int old_decodes;
	struct vga_device *new_vgadev, *conflict;
	char devname[32], new_devname[32];

	old_decodes = vgadev->decodes;
	vgadev->decodes = new_decodes;

	(void) snprintf(devname, sizeof(devname), "%x:%x:%x.%x",
		vgadev->domain, vgadev->bus, vgadev->dev, vgadev->func);

	cmn_err(CE_CONT, 
		"!vga_arbiter: device changed decodes: "
		"PCI:%s,olddecodes=%s,decodes=%s:owns=%s\n",
		devname,
		vga_iostate_to_str[old_decodes],
		vga_iostate_to_str[vgadev->decodes],
		vga_iostate_to_str[vgadev->owns]);

	/* if we own the decodes we should move them along to
	   another card */
	if ((vgadev->owns & old_decodes) && (vga_count > 1)) {
		/* set us to own nothing */
		for (new_vgadev = list_head(&vga_list); new_vgadev; 
			new_vgadev = list_next(&vga_list, new_vgadev)) {
			if ((new_vgadev != vgadev) &&
				(new_vgadev->decodes & VGA_RSRC_LEGACY_MASK)) {
				(void) snprintf(new_devname, sizeof(devname), "%x:%x:%x.%x",
					new_vgadev->domain, new_vgadev->bus, 
					new_vgadev->dev, new_vgadev->func);
				cmn_err(CE_CONT, 
				    	"!vga_arbiter: transferring owner from "
					"from PCI:%s to PCI:%s\n",
					devname, new_devname);
				conflict = __vga_tryget(new_vgadev, VGA_RSRC_LEGACY_MASK);
				if (!conflict)
					__vga_put(new_vgadev, VGA_RSRC_LEGACY_MASK);
			}
		}
	}

	/* change decodes counter */
	/* Leave the code alone, since don't know what vga_decode_count really means */
	if (old_decodes != new_decodes) {
		if (new_decodes & (VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM))
			vga_decode_count++;
		else
			vga_decode_count--;
	}
}

void __vga_set_legacy_decoding(dev_info_t *dip, unsigned int decodes, bool userspace)
{
	struct vga_device *vgadev;

	mutex_enter(&vga_lock);
	vgadev = vgadev_find(dip, 0, 0);
	if (vgadev == NULL)
		goto bail;

	/* don't let userspace futz with kernel driver decodes */
	if (userspace && vgadev->set_vga_decode)
		goto bail;

	/* update the device decodes + counter */
	vga_update_device_decodes(vgadev, decodes);

	/* XXX if somebody is going from "doesn't decode" to "decodes" state
	 * here, additional care must be taken as we may have pending owner
	 * ship of non-legacy region ...
	 */
bail:
	mutex_exit(&vga_lock);
}

void vga_set_legacy_decoding(dev_info_t *dip, unsigned int decodes)
{
	__vga_set_legacy_decoding(dip, decodes, false);
}

#if defined(CONFIG_VGA_ARB)
/* call with NULL to unregister */
int vga_client_register(dev_info_t *dip, void *cookie,
			void (*irq_set_state)(void *cookie, bool state),
			unsigned int (*set_vga_decode)(void *cookie, bool decode))
{
	int ret = -1;
	struct vga_device *vgadev;

	mutex_enter(&vga_lock);
	vgadev = vgadev_find(dip, 0, 0);
	if (!vgadev)
		goto bail;

	vgadev->irq_set_state = irq_set_state;
	vgadev->set_vga_decode = set_vga_decode;
	vgadev->cookie = cookie;
	ret = 0;

bail:
	mutex_exit(&vga_lock);
	return ret;

}
#endif

/*
 * Char driver implementation
 *
 * Semantics is:
 *
 *  open       : open user instance of the arbitrer. by default, it's
 *                attached to the default VGA device of the system.
 *
 *  close      : close user instance, release locks
 *
 *  read       : return a string indicating the status of the target.
 *                an IO state string is of the form {io,mem,io+mem,none},
 *                mc and ic are respectively mem and io lock counts (for
 *                debugging/diagnostic only). "decodes" indicate what the
 *                card currently decodes, "owns" indicates what is currently
 *                enabled on it, and "locks" indicates what is locked by this
 *                card. If the card is unplugged, we get "invalid" then for
 *                card_ID and an ENODEV error is returned for any command
 *                until a new card is targeted
 *
 *   "<card_ID>,decodes=<io_state>,owns=<io_state>,locks=<io_state> (ic,mc)"
 *
 * write       : write a command to the arbiter. List of commands is:
 *
 *   target <card_ID>   : switch target to card <card_ID> (see below)
 *   lock <io_state>    : acquires locks on target ("none" is invalid io_state)
 *   trylock <io_state> : non-blocking acquire locks on target
 *   unlock <io_state>  : release locks on target
 *   unlock all         : release all locks on target held by this user
 *   decodes <io_state> : set the legacy decoding attributes for the card
 *
 * poll         : event if something change on any card (not just the target)
 *
 * card_ID is of the form "PCI:domain:bus:dev.fn". It can be set to "default"
 * to go back to the system default card (TODO: not implemented yet).
 * Currently, only PCI is supported as a prefix, but the userland API may
 * support other bus types in the future, even if the current kernel
 * implementation doesn't.
 *
 * Note about locks:
 *
 * The driver keeps track of which user has what locks on which card. It
 * supports stacking, like the kernel one. This complexifies the implementation
 * a bit, but makes the arbiter more tolerant to userspace problems and able
 * to properly cleanup in all cases when a process dies.
 * Currently, a max of 16 cards simultaneously can have locks issued from
 * userspace for a given user (file descriptor instance) of the arbiter.
 *
 * If the device is hot-unplugged, there is a hook inside the module to notify
 * they being added/removed in the system and automatically added/removed in
 * the arbiter.
 */

/*
 * This function gets a string in the format: "PCI:domain:bus:dev.fn" and
 * returns the respective values. If the string is not in this format,
 * it returns false.
 */
static bool vga_pci_str_to_vars(char *buf, unsigned int *domain,
	unsigned int *bus, unsigned int *devfn)
{
	long val[4];
	int i;
	char *ptr;

	if (strncmp(buf, "PCI:", 4) != 0)
		return false;

	ptr = buf + 3;
	for (i = 0; i < 4; i++) {
		if (*ptr && *(ptr + 1)) {
			if (ddi_strtol(ptr + 1, &ptr, 16, &val[i]))
				return false;
		} else
			return false;
	}

	*domain = (unsigned int) val[0];
	*bus =    (unsigned int) val[1];

	*devfn = ((val[2] & 0x1f) << 3) | (val[3] & 0x07);

	return true;
}

/*ARGSUSED*/
static int vga_arbiter_read(dev_t dev, struct uio *uiop, cred_t *credp)
{
	struct vga_arbiter_private *priv;
	struct vga_device *vgadev;
	dev_info_t *dip;
	size_t len;
	int rc; 
	minor_t minor;
	char *lbuf;
	char devname[32];;
#define BUFSIZE		1024

	minor = getminor(dev);

	if (minor >= VGA_MAX_OPEN)
                return (ENODEV);

	if ((priv = user_data[minor]) == NULL)
		return (ENODEV);

	lbuf = kmem_alloc(BUFSIZE, KM_SLEEP);

	/* Shields against vga_arb_device_card_gone (pci_dev going
	 * away), and allows access to vga list
	 */
	mutex_enter(&vga_lock);

	/* If we are targetting the default, use it */
	dip = priv->target;
	if (dip == NULL) {
		mutex_exit(&vga_lock);
		len = snprintf(lbuf, 9, "%s", "invalid\n");
		goto done;
	}

	/* Find card vgadev structure */
	vgadev = vgadev_find(dip, 0, 0);
	if (vgadev == NULL) {
		/* Wow, it's not in the list, that shouldn't happen,
		 * let's fix us up and return invalid card
		 */
		if (dip == priv->target)
			vga_arb_device_card_gone(dip);
		mutex_exit(&vga_lock);
		len = snprintf(lbuf, 9, "%s", "invalid\n");
		goto done;
	}
	mutex_exit(&vga_lock);

	/* Fill the buffer with infos */
	(void) snprintf(devname, sizeof(devname), "%x:%x:%x.%x",
		vgadev->domain, vgadev->bus, vgadev->dev, vgadev->func);

	len = snprintf(lbuf, BUFSIZE,
		"count:%d,PCI:%s,decodes=%s,owns=%s,legalocks=%s(%u:%u),normlocks=%s(%u:%u)\n",
		vga_decode_count, devname,
		vga_iostate_to_str[vgadev->decodes],
		vga_iostate_to_str[vgadev->owns],
		vga_iostate_to_str[vgadev->lega_locks],
		vgadev->io_lega_lock_cnt, vgadev->mem_lega_lock_cnt,
		vga_iostate_to_str[vgadev->norm_locks],
		vgadev->io_norm_lock_cnt, vgadev->mem_norm_lock_cnt);

done:
	/* Copy that to user */
	rc = uiomove(lbuf, len, UIO_READ, uiop);

	kmem_free(lbuf, BUFSIZE);

	return rc;
}


/*
 * TODO: To avoid parsing inside kernel and to improve the speed we may
 * consider use ioctl here
 */
/*ARGSUSED*/
static int vga_arbiter_write(dev_t dev, struct uio *uiop, cred_t *credp)
{
	struct vga_arbiter_private *priv; 
	struct vga_arb_user_card *uc = NULL, *card = NULL;
	dev_info_t *dip;
	unsigned int io_state;
	char *kbuf, *curr_pos;
	size_t remaining, count;
	int ret_val, i, rc;
	minor_t minor;

	minor = getminor(dev);

	if (minor >= VGA_MAX_OPEN)
                return (ENODEV);

	if ((priv = user_data[minor]) == NULL)
                return (ENODEV);

	remaining = count = uiop->uio_iov->iov_len;

	/* protection check */
	if (count > 1024) {
		cmn_err(CE_WARN, "vga_arbiter_write error: length %u", 
			(unsigned int) count);
		return (EINVAL);
	} 
	
	kbuf = kmem_alloc(count + 1, KM_SLEEP);

	rc = uiomove(kbuf, count + 1, UIO_WRITE, uiop);
	if (rc) { 
		cmn_err(CE_WARN, "vga_arbiter_write error: errno %d", rc);
		ret_val = rc;
		goto done;
	}

	curr_pos = kbuf;
	kbuf[count] = '\0';	/* Just to make sure... */

	if (strncmp(curr_pos, "lock ", 5) == 0) {
		curr_pos += 5;
		remaining -= 5;

		if (debug)
			cmn_err(CE_CONT, "client 0x%p called 'lock'", (void *) priv);

		if (!vga_str_to_iostate(curr_pos, &io_state)) {
			ret_val = EPROTO;
			goto done;
		}
		if (io_state == VGA_RSRC_NONE) {
			ret_val = EPROTO;
			goto done;
		}

		dip = priv->target;
		if (priv->target == NULL) {
			ret_val = ENODEV;
			goto done;
		}

		for (i = 0; i < MAX_USER_CARDS; i++) {
			if (priv->cards[i].dip == dip) {
				card = &priv->cards[i];
				break;
			}
		}

		/* card->io_norm_cnt and card->mem_norm_cnt are non-legacy access lock
		   resource the user has acquired. */
		(void) vga_get_uninterruptible(dip, io_state, card->io_norm_cnt, card->mem_norm_cnt);

		/* Update the client's locks lists... */
		if (io_state & VGA_RSRC_LEGACY_IO)
			card->io_lega_cnt++;
		if (io_state & VGA_RSRC_LEGACY_MEM)
			card->mem_lega_cnt++;
		if (io_state & VGA_RSRC_NORMAL_IO)
			card->io_norm_cnt++;
		if (io_state & VGA_RSRC_NORMAL_MEM)
			card->mem_norm_cnt++;

		ret_val = 0;
		goto done;
	} else if (strncmp(curr_pos, "unlock ", 7) == 0) {
		curr_pos += 7;
		remaining -= 7;

		if (debug)
			cmn_err(CE_CONT, "client 0x%p called 'unlock'", (void *) priv);

		if (strncmp(curr_pos, "all", 3) == 0)
			io_state = VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
				VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
		else {
			if (!vga_str_to_iostate
			    (curr_pos, &io_state)) {
				ret_val = EPROTO;
				goto done;
			}
			/* TODO: Add this?
			   if (io_state == VGA_RSRC_NONE) {
			   ret_val = EPROTO;
			   goto done;
			   }
			  */
		}

		if ((dip = priv->target) == NULL) {
			ret_val = ENODEV;
			goto done;
		}
		for (i = 0; i < MAX_USER_CARDS; i++) {
			if (priv->cards[i].dip == dip)
				uc = &priv->cards[i];
		}

		if (uc && debug) {
			cmn_err(CE_CONT, "uc->io_lega_cnt == %u, uc->mem_lega_cnt == %u, \
				uc->io_norm_cnt == %u, uc->mem_norm_cnt == %u",
				uc->io_lega_cnt, uc->mem_lega_cnt,
				uc->io_norm_cnt, uc->mem_norm_cnt);
		} else if (!uc) {
			cmn_err(CE_WARN, "no card found as target");
			ret_val = EINVAL;
			goto done;
		}

		if (io_state & VGA_RSRC_LEGACY_IO && uc->io_lega_cnt == 0) {
			ret_val = EINVAL;
			goto done;
		}

		if (io_state & VGA_RSRC_LEGACY_MEM && uc->mem_lega_cnt == 0) {
			ret_val = EINVAL;
			goto done;
		}

		if (io_state & VGA_RSRC_NORMAL_IO && uc->io_norm_cnt == 0) {
			ret_val = EINVAL;
			goto done;
		}

		if (io_state & VGA_RSRC_NORMAL_MEM && uc->mem_norm_cnt == 0) {
			ret_val = EINVAL;
			goto done;
		}

		vga_put(dip, io_state);

		if (io_state & VGA_RSRC_LEGACY_IO)
			uc->io_lega_cnt--;
		if (io_state & VGA_RSRC_LEGACY_MEM)
			uc->mem_lega_cnt--;
		if (io_state & VGA_RSRC_NORMAL_IO)
			uc->io_norm_cnt--;
		if (io_state & VGA_RSRC_NORMAL_MEM)
			uc->mem_norm_cnt--;

		ret_val = 0;
		goto done;
	} else if (strncmp(curr_pos, "trylock ", 8) == 0) {
		curr_pos += 8;
		remaining -= 8;
		
		if (debug)
			cmn_err(CE_CONT, "client 0x%p called 'trylock'", (void *) priv);

		if (!vga_str_to_iostate(curr_pos, &io_state)) {
			ret_val = EPROTO;
			goto done;
		}
		/* TODO: Add this?
		   if (io_state == VGA_RSRC_NONE) {
		   ret_val = EPROTO;
		   goto done;
		   }
		 */

		dip = priv->target;
		if (priv->target == NULL) {
			ret_val = ENODEV;
			goto done;
		}
		if (!vga_tryget(dip, io_state)) {
			/* Update the client's locks lists... */
			for (i = 0; i < MAX_USER_CARDS; i++) {
				if (priv->cards[i].dip == dip) {
					if (io_state & VGA_RSRC_LEGACY_IO)
						priv->cards[i].io_lega_cnt++;
					if (io_state & VGA_RSRC_LEGACY_MEM)
						priv->cards[i].mem_lega_cnt++;
					if (io_state & VGA_RSRC_NORMAL_IO)
						priv->cards[i].io_norm_cnt++;
					if (io_state & VGA_RSRC_NORMAL_MEM)
						priv->cards[i].mem_norm_cnt++;
					break;
				}
			}
			ret_val = 0;
			goto done;
		} else {
			ret_val = EBUSY;
			goto done;
		}

	} else if (strncmp(curr_pos, "target ", 7) == 0) {
		unsigned int domain, bus, devfn;
		struct vga_device *vgadev;

		curr_pos += 7;
		remaining -= 7;

		if (debug)
			cmn_err(CE_CONT, "client 0x%p called 'target'", (void *) priv);

		/* if target is default */
		if (!strncmp(curr_pos, "default", 7)) {
			dip = vga_default_device();
			mutex_enter(&vga_lock);
			vgadev = vgadev_find(dip, 0, 0);
			mutex_exit(&vga_lock);
		}
		else {
			if (!vga_pci_str_to_vars(curr_pos,
						 &domain, &bus, &devfn)) {
				ret_val = EPROTO;
				goto done;
			}

			mutex_enter(&vga_lock);
			vgadev = vgadev_find(NULL, bus, devfn);
			mutex_exit(&vga_lock);
		}

		if (debug)
			cmn_err(CE_CONT, "vga_arbiter: vgadev %p\n", (void *) vgadev);

		if (!vgadev) {
			cmn_err(CE_WARN, "vga_arbiter: invalid vga PCI address! bus %x devfn %x\n", bus, devfn);
			ret_val = ENODEV;
			goto done;
		}

		dip = vgadev->dip;
		priv->target = dip;

		for (i = 0; i < MAX_USER_CARDS; i++) {
			if (priv->cards[i].dip == dip)
				break;
			if (priv->cards[i].dip == NULL) {
				priv->cards[i].dip = dip;
				priv->cards[i].io_lega_cnt = 0;
				priv->cards[i].mem_lega_cnt = 0;
				priv->cards[i].io_norm_cnt = 0;
				priv->cards[i].mem_norm_cnt = 0;
				break;
			}
		}
		if (i == MAX_USER_CARDS) {
			cmn_err(CE_WARN, 
				"vga_arbiter: maximum user cards number reached!");
			
			/* XXX: which value to return? */
			ret_val =  ENOMEM;
			goto done;
		}

		ret_val = 0;
		goto done;


	} else if (strncmp(curr_pos, "decodes ", 8) == 0) {
		curr_pos += 8;
		remaining -= 8;

		if (debug)
			cmn_err(CE_CONT, "client 0x%p called 'decodes'", (void *) priv);

		if (!vga_str_to_iostate(curr_pos, &io_state)) {
			ret_val = EPROTO;
			goto done;
		}
		dip = priv->target;
		if (priv->target == NULL) {
			ret_val = ENODEV;
			goto done;
		}

		__vga_set_legacy_decoding(dip, io_state, true);
		ret_val = 0;
		goto done;
	}
	/* If we got here, the message written is not part of the protocol! */
	kmem_free(kbuf, count + 1);
	return EPROTO;

done:
	kmem_free(kbuf, count + 1);
	return ret_val;
}

/*ARGSUSED*/
static int vga_arbiter_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
	struct vga_arbiter_private *priv;
	int i;


	priv = kmem_alloc(sizeof(struct vga_arbiter_private), KM_SLEEP);

	(void) memset(priv, 0, sizeof(struct vga_arbiter_private));

	mutex_enter(&vga_user_lock);
	for (i=0; i < VGA_MAX_OPEN; i++) {
		if (user_data[i] == NULL)
			break;
	}

	if (i == VGA_MAX_OPEN) {
		mutex_exit(&vga_user_lock);
		cmn_err(CE_WARN, "vga_arbiter_open error: open exceeds %d", VGA_MAX_OPEN);
		kmem_free(priv, sizeof(struct vga_arbiter_private));
		return ENOMEM;
	}

	list_insert_head(&vga_user_list, priv);


	user_data[i] = priv;
	priv->target = vga_default_device(); /* Maybe this is still null! */ 
	priv->cards[0].dip = priv->target;
	mutex_exit(&vga_user_lock);

	cmn_err(CE_CONT, "!vga_arbiter_open succeeds: %d", i);
	*devp = makedevice(getmajor(*devp), i);

	return 0;
}

/*ARGSUSED*/
static int vga_arbiter_close(dev_t dev, int flag, int otyp, cred_t *cred)
{
	struct vga_arbiter_private *priv;
	struct vga_arb_user_card *uc;
	int i;
	minor_t minor;

	minor = getminor(dev);
	cmn_err(CE_CONT, "!vga_arbiter_close %d", minor);

	if (minor >= VGA_MAX_OPEN)
		return (ENODEV);
	
	if ((priv = user_data[minor]) == NULL)
		return (ENODEV);

	mutex_enter(&vga_user_lock);
	for (i = 0; i < MAX_USER_CARDS; i++) {
		uc = &priv->cards[i];
		if (uc->dip == NULL)
                        continue;

		while (uc->io_lega_cnt--)
			vga_put(uc->dip, VGA_RSRC_LEGACY_IO);
		while (uc->mem_lega_cnt--)
			vga_put(uc->dip, VGA_RSRC_LEGACY_MEM);
		while (uc->io_norm_cnt--)
			vga_put(uc->dip, VGA_RSRC_NORMAL_IO);
		while (uc->mem_norm_cnt--)
			vga_put(uc->dip, VGA_RSRC_NORMAL_MEM);
        }

	list_remove(&vga_user_list, priv);
	mutex_exit(&vga_user_lock);

	kmem_free(priv, sizeof(struct vga_arbiter_private));
	user_data[minor] = NULL;

	return (0);
}

/*ARGSUSED*/
static void vga_arb_device_card_gone(dev_info_t *dip)
{
}

/*
 * registered clients to let them know we have a
 * change in VGA cards
 */
static void vga_arbiter_notify_clients(void)
{
	struct vga_device *vgadev;
	uint32_t new_decodes;
	int new_state;

	if (!vga_arbiter_used)
		return;

	mutex_enter(&vga_lock);
	if (vga_count > 1)
		new_state = false;
	else
		new_state = true;
	for (vgadev = list_head(&vga_list); vgadev; 
		vgadev = list_next(&vga_list, vgadev)) {
		if (vgadev->set_vga_decode) {
			new_decodes = vgadev->set_vga_decode(vgadev->cookie, new_state);
			vga_update_device_decodes(vgadev, new_decodes);
		}
	}
	mutex_exit(&vga_lock);
}


