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

#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/iommu.h>
#include <sys/pci.h>
#include <sys/smbios.h>
#include <sys/sysmacros.h>
#include <util/sscanf.h>

static dev_info_t *iommu_legacy_pci_parent(dev_info_t *dip);
static int iommu_bridgechild_info(dev_info_t *dip, void *arg);
static void iommu_dom_info(dev_info_t *dip, iommu_t *iommu,
    iommu_pci_info_t *pip, int *type, int *isunity, struct memlist **unitypp,
    struct memlist **resvpp);
static void iommu_late_quirks(dev_info_t *dip, iommu_pci_info_t *pinfo,
    int *isunity, struct memlist **mlpp);
static int iommu_mapping_prop(dev_info_t *dip);
static void iommu_set_prealloc(dev_info_t *dip, iommu_devi_t *iommu_devi,
    iommu_pci_info_t *pinfo);
static void iommu_set_root_table(dev_info_t *dip, iommu_t *iommu,
    iommu_pci_info_t *pinfo, iommu_domain_t *domain);

extern list_t iommu_list;
extern kmutex_t iommu_list_lock;

extern int pseudo_isa;

static struct modlmisc modlmisc = {
	&mod_miscops, "IOMMU DDI entry points module"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init(void)
{
	mutex_init(&iommu_list_lock, NULL, MUTEX_DEFAULT, NULL);

	list_create(&iommu_list, sizeof (iommu_t),
	    offsetof(iommu_t, iommu_node));

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	/* sanity (or paranoia if you will) */
	if (!list_is_empty(&iommu_list))
		return (EBUSY);

	mutex_destroy(&iommu_list_lock);
	list_destroy(&iommu_list);

	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Set up the IOMMU structures for a device. This includes finding
 * the IOMMU controlling its DMA, and setting up a translation
 * domain.
 *
 * Note that this function is called with a hold on the parent
 * (from initchild) so the children and siblings of the device can
 * safely be examined.
 *
 * IOMMU information is pointed to by a field in the dev_info structure,
 * devi_iommu. For both 1:1 mappings and the case of IOMMUs not being
 * active, this field will be NULL. Since the default initialization
 * of an IOMMU sets all devices up for passthrough / 1:1, this means
 * that setup can be aborted safely if an error occurs for some reason
 * (e.g. through BIOS bugs). Leaving devi_iommu at NULL will simply
 * default to 1:1 and will defer to the rootnex DMA code, allowing
 * the device to work.
 *
 * Legacy PCI/PCI-X devices behind a PCI-PCI bridge will share a domain
 * and may have to settle for a 1:1 mapping if either the bridge or
 * one of its downstream devices needs a 1:1 mapping. That's OK,
 * our main target here is modern systems with PCI-Express devices.
 */
void
iommu_setup(dev_info_t *dip, int where)
{
	dev_info_t *idip;
	iommu_pci_info_t pinfo;
	iommu_t *iommu;
	iommu_devi_t *iommu_devi;
	iommu_domain_t *domain;
	int domtype, devmode, iommumode;
	int isunity;
	struct memlist *unityp, *resvp;

	DEVI(dip)->devi_iommu = NULL;

	if (iommu_ops == NULL)
		return;

	devmode = iommu_mapping_prop(dip);

	/*
	 * For ISA, the domain information gathered
	 * here is that of the PCI-ISA bridge. So, use
	 * that (the parent dev_info) for an ISA device.
	 *
	 * In the pseudo_isa case, all ISA devices will
	 * have a 1:1 mapping, so there's nothing to do.
	 */
	if (where == IOMMU_SETUP_ISA) {
		if (pseudo_isa)
			return;
		idip = ddi_get_parent(dip);
	} else
		idip = dip;

	if (iommulib_pci_info(idip, &pinfo) < 0)
		return;

	iommu = iommu_ops->ioo_find_unit(idip, &pinfo);
	if (iommu == NULL)
		return;

	if (!(iommu->iommu_flags & IOMMU_FLAGS_DVMA_ENABLE))
		return;

	iommumode = iommu_mapping_prop(iommu->iommu_dip);
	isunity = 0;
	unityp = resvp = NULL;

	switch (where) {
	case IOMMU_SETUP_PCIEX_BRIDGE:
		idip = iommu_legacy_pci_parent(dip);
		if (idip != NULL) {
			iommu_devi = IOMMU_DEVI(idip);
			goto pcibridge;
		}
		/*FALLTHROUGH*/
	case IOMMU_SETUP_PCIEX_ROOT:
	case IOMMU_SETUP_PCI_ROOT:
		/*
		 * If there is an explicit 1:1 setting for this device,
		 * or if the default mode is 1:1 and the device does
		 * not override it, there's nothing to do here.
		 */
		if ((iommumode == IOMMU_DVMA_UNITY &&
		    devmode == IOMMU_DVMA_DEFAULT) ||
		    devmode == IOMMU_DVMA_UNITY)
			return;
		/*
		 * PCI-Express device or device on a PCI root bus. These get
		 * their own domain. If the device is a PCIE-PCI bridge, the
		 * domain will be shared with its downstream devices.
		 */
		iommu_dom_info(dip, iommu, &pinfo, &domtype, &isunity,
		    &unityp, &resvp);
		if (isunity)
			return;
		domain = iommu_domain_alloc(dip, iommu, domtype, unityp, resvp);
		if (domain == NULL)
			goto freelists;
		break;
	case IOMMU_SETUP_PCI_BRIDGE:
		/*
		 * Device under a PCI bridge. If the PCI bridge has a 1:1
		 * mapping, everything downstream from it gets a 1:1
		 * mapping. Else, share the domain of the parent, unless
		 * the ACPI info says that they have a different iommu than
		 * the parent.
		 */
		iommu_devi = DEVI(ddi_get_parent(dip))->devi_iommu;
pcibridge:
		if (iommu_devi == NULL) {
			/*
			 * Honoring the mode setting is not possible for
			 * devices behing a PCI bridge. If you're lucky,
			 * it matches what the bridge already has. If not,
			 * print a warning.
			 */
			if (devmode == IOMMU_DVMA_XLATE) {
				ddi_err(DER_NOTE, dip,
				    "IOMMU dvma setting for device behind "
				    "a PCI bridge ignored");
			}
			return;
		}
		if (iommu_devi->id_iommu != iommu) {
			iommu_dom_info(dip, iommu, &pinfo, &domtype, &isunity,
			    &unityp, &resvp);
			if (isunity)
				return;
			domain = iommu_domain_alloc(dip, iommu, domtype,
			    unityp, resvp);
			if (domain == NULL)
				goto freelists;
		} else {
			if (devmode == IOMMU_DVMA_UNITY) {
				ddi_err(DER_NOTE, dip,
				    "IOMMU dvma setting for device behind "
				    "a PCI bridge ignored");
			}
			domain = iommu_domain_dup(iommu_devi->id_domain);
			if (domain == NULL)
				return;
		}
		break;
	case IOMMU_SETUP_ISA:
		pinfo.pi_type = IOMMU_DEV_ISA;
		if (devmode != IOMMU_DVMA_DEFAULT) {
			ddi_err(DER_LOG, dip, "IOMMU dvma setting for ISA "
			    "device ignored");
		}
		iommu_devi = DEVI(ddi_get_parent(dip))->devi_iommu;
		if (iommu_devi == NULL)
			return;
		domain = iommu_domain_dup(iommu_devi->id_domain);
		if (domain == NULL)
			return;
		break;
	}

	iommu_devi = kmem_zalloc(sizeof (iommu_devi_t), KM_SLEEP);

	if (pinfo.pi_type != IOMMU_DEV_ISA)
		iommulib_domhash_insert(iommulib_bdf_to_sid(pinfo.pi_seg,
		    pinfo.pi_bus, pinfo.pi_dev, pinfo.pi_func), domain);

	iommu_devi->id_iommu = iommu;
	iommu_devi->id_domain = domain;
	iommu_devi->id_pci = pinfo;

	iommu_set_prealloc(dip, iommu_devi, &pinfo);

	iommu_set_root_table(dip, iommu, &pinfo, domain);

	DEVI(dip)->devi_iommu = iommu_devi;

freelists:
	iommu_domain_freelist(unityp);
	iommu_domain_freelist(resvp);
}

void
iommu_teardown(dev_info_t *dip)
{
	iommu_devi_t *iommu_devi;
	iommu_t *iommu;
	iommu_pci_info_t *pinfop;

	if (iommu_ops == NULL)
		return;

	iommu_devi = IOMMU_DEVI(dip);
	if (iommu_devi == NULL)
		return;

	iommu = iommu_devi->id_iommu;

	pinfop = &iommu_devi->id_pci;
	if (pinfop->pi_type != IOMMU_DEV_ISA)
		iommulib_domhash_remove(iommulib_bdf_to_sid(pinfop->pi_seg,
		    pinfop->pi_bus, pinfop->pi_dev, pinfop->pi_func));

	iommu_set_root_table(dip, iommu, pinfop, iommu->iommu_unity_domain);

	iommu_domain_free(dip, iommu_devi->id_domain);

	kmem_free(iommu_devi, sizeof (iommu_devi_t));
	DEVI(dip)->devi_iommu = NULL;
}

static void
iommu_set_root_table(dev_info_t *dip, iommu_t *iommu, iommu_pci_info_t *pinfo,
    iommu_domain_t *domain)
{
	switch (pinfo->pi_type) {
	case IOMMU_DEV_PCI:
	case IOMMU_DEV_PCIE:
	case IOMMU_DEV_PCI_PCI:
	case IOMMU_DEV_PCI_ISA:
		iommu_ops->ioo_set_root_table(dip, iommu, pinfo->pi_bus,
		    pinfo->pi_dev, pinfo->pi_func, domain);
		break;
	case IOMMU_DEV_PCIE_PCI:
		/*
		 * DMA transactions from behind a PCIE-PCI bridge can
		 * have a source id of either the bridge itself, or
		 * that of the secondary bus, with dev and func set to 0.
		 * Set both in the context table.
		 */
		iommu_ops->ioo_set_root_table(dip, iommu, pinfo->pi_bus,
		    pinfo->pi_dev, pinfo->pi_func, domain);

		iommu_ops->ioo_set_root_table(dip, iommu, pinfo->pi_secbus,
		    0, 0, domain);

		break;
	case IOMMU_DEV_ISA:
		/* PCI-ISA bridge has already done this. */
		break;
	case IOMMU_DEV_PCI_PCIE:
	case IOMMU_DEV_PCIE_PCIE:
		break;
	}
}

/*
 * Return the mapping type for a device, or the global default,
 * if dip refers to the IOMMU device itself.
 */
static int
iommu_mapping_prop(dev_info_t *dip)
{
	char *modestr;
	int ret;

	ret = IOMMU_DVMA_DEFAULT;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "iommu-dvma-mode", &modestr) == DDI_PROP_SUCCESS) {
		if (strcmp(modestr, "unity") == 0)
			ret = IOMMU_DVMA_UNITY;
		else if (strcmp(modestr, "xlate") == 0)
			ret = IOMMU_DVMA_XLATE;
		ddi_prop_free(modestr);
	}

	return (ret);
}

/*
 * There is a, mostly theoretical, possibility of a PCI-Express bus
 * appearing under a legacy PCI tree through a PCI-PCIE bridge
 * (a reversed PCIE-PCI bridge).
 *
 * No such systems supported by Solaris are currently known. The
 * normal layout is to have PCIE root buses, with possible legacy
 * PCI supported through a PCIE-PCI bridge, not the other way around.
 *
 * To check for the reverse case, walk up the tree, checking the
 * device_type property for each bridge. If a type of "pci" is
 * found, this means the device is under a pci subtree. In
 * that case, use the domain of this pci bridge, unless one
 * was specifically specified for the device (see iommu_setup()).
 */
static dev_info_t *
iommu_legacy_pci_parent(dev_info_t *dip)
{
	dev_info_t *tdip, *bdip;
	char *devtype;

	bdip = NULL;

	ndi_hold_devi(dip);

	for (tdip = ddi_get_parent(dip); tdip != ddi_root_node();
	    tdip = ddi_get_parent(tdip)) {
		if (ddi_prop_lookup_string(DDI_DEV_T_ANY, tdip,
		    DDI_PROP_DONTPASS, "device_type", &devtype)
		    != DDI_PROP_SUCCESS)
			continue;
		if (strcmp(devtype, "pci") == 0)
			bdip = tdip;
		ddi_prop_free((void *)devtype);
	}

	ndi_rele_devi(dip);

	return (bdip);
}

struct iommu_walk_args {
	iommu_t *w_iommu;
	int *w_isunity;
	struct memlist **w_unitypp;
	struct memlist **w_resvpp;
};

static void
iommu_dom_info(dev_info_t *dip, iommu_t *iommu, iommu_pci_info_t *pip,
    int *dtype, int *isunity, struct memlist **unitypp, struct memlist **resvpp)
{
	struct iommu_walk_args iw;
	int n;

	/*
	 * For PCIE bridges, we're done.
	 */
	if (pip->pi_type == IOMMU_DEV_PCI_PCIE ||
	    pip->pi_type == IOMMU_DEV_PCIE_PCIE) {
		*isunity = 1;
		return;
	}

	/*
	 * Look up any special handling. If this means a 1:1 mapping,
	 * we're done.
	 */
	iommu_late_quirks(dip, pip, isunity, unitypp);

	if (*isunity)
		return;

	iommu_ops->ioo_dev_reserved(dip, pip, resvpp, unitypp);

	/*
	 * Check if this an endpoint device, no further info is needed.
	 * We got here to allocate a new domain; inherited domains
	 * were already taken care of by the caller. So, endpoint
	 * devices will be ones that get their own exclusive domain.
	 */
	if (pip->pi_type == IOMMU_DEV_PCIE || pip->pi_type == IOMMU_DEV_PCI) {
		*dtype = IOMMU_DOMTYPE_EXCL;
		return;
	}

	/*
	 * That leaves PCI bridges. For these, child devices that use
	 * the same IOMMU must be walked to see if any of them needs a
	 * unity mapping. If so, all devices behind the bridge must use
	 * a 1:1 mapping. Also, any reserved ranges must be merged.
	 *
	 * ISA bridges need no further handling. In the pseudo_isa
	 * case, give them a unity mapping (which will be used by
	 * all ISA devices).
	 */

	*dtype = IOMMU_DOMTYPE_SHARED;

	if (pip->pi_type == IOMMU_DEV_PCI_ISA) {
		if (pseudo_isa)
			*isunity = 1;
		return;
	}

	iw.w_iommu = iommu;
	iw.w_isunity = isunity;
	iw.w_unitypp = unitypp;
	iw.w_resvpp = resvpp;

	ndi_devi_enter(dip, &n);
	ddi_walk_devs(ddi_get_child(dip), iommu_bridgechild_info, &iw);
	ndi_devi_exit(dip, n);

	*isunity = *iw.w_isunity;
	if (*isunity) {
		iommu_domain_freelist(*unitypp);
		iommu_domain_freelist(*resvpp);
	}
}

static int
iommu_bridgechild_info(dev_info_t *dip, void *arg)
{
	struct iommu_walk_args *iwp;
	iommu_t *iommu;
	iommu_pci_info_t pinfo;
	struct memlist *unityp, *resvp;

	iwp = arg;

	/*
	 * If a device that can't be dealt with in some way
	 * is encountered, play it safe and use 1:1.
	 */
	if (iommulib_pci_base_info(dip, &pinfo) < 0) {
		*iwp->w_isunity = 1;
		return (DDI_WALK_TERMINATE);
	}

	iommu = iommu_ops->ioo_find_unit(dip, &pinfo);
	if (iommu == NULL) {
		*iwp->w_isunity = 1;
		return (DDI_WALK_TERMINATE);
	}

	/*
	 * If the device is wired to a different IOMMU unit,
	 * skip it.
	 */
	if (iommu != iwp->w_iommu)
		return (DDI_WALK_CONTINUE);

	unityp = resvp = NULL;

	iommu_late_quirks(dip, &pinfo, iwp->w_isunity, &unityp);
	if (*iwp->w_isunity)
		return (DDI_WALK_TERMINATE);

	iommu_ops->ioo_dev_reserved(dip, &pinfo, &resvp, &unityp);

	/*
	 * If these fail (allocation failure), reset to 1:1
	 * and stop the walk.
	 */
	if (iommu_domain_mergelist(iwp->w_resvpp, resvp) < 0 ||
	    iommu_domain_mergelist(iwp->w_unitypp, unityp) < 0)
		*iwp->w_isunity = 1;

	iommu_domain_freelist(resvp);
	iommu_domain_freelist(unityp);

	return (*iwp->w_isunity ? DDI_WALK_TERMINATE : DDI_WALK_CONTINUE);
}

/*
 * Set default preallocated DVMA space sizes. See sys/iommu.h for
 * information on how this is implemented.
 */
static void
iommu_set_prealloc(dev_info_t *dip, iommu_devi_t *iommu_devi,
    iommu_pci_info_t *pinfop)
{
	int presize;

	presize = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "iommu-dvma-presize", -1);
	if (presize != -1) {
		if (presize >= MMU_PAGESIZE && presize <= IOMMU_PRESIZE_MAX &&
		    ISP2(presize)) {
			iommu_devi->id_presize = presize;
			iommu_devi->id_npreptes = presize / MMU_PAGESIZE;
			return;
		}
	}

	switch (pinfop->pi_class) {
	case PCI_CLASS_MASS:
	case PCI_CLASS_MEM:
		iommu_devi->id_presize = IOMMU_PRESIZE_STORAGE;
		iommu_devi->id_npreptes = IOMMU_NPREPTES_STORAGE;
		break;
	case PCI_CLASS_NET:
		if (pinfop->pi_subclass == PCI_NET_ENET) {
			iommu_devi->id_presize = IOMMU_PRESIZE_ETHERNET;
			iommu_devi->id_npreptes = IOMMU_NPREPTES_ETHERNET;
			break;
		}
		/*FALLTHROUGH*/
	default:
		iommu_devi->id_presize = IOMMU_PRESIZE_DEFAULT;
		iommu_devi->id_npreptes = IOMMU_NPREPTES_DEFAULT;
		break;
	}
}

/*
 * Quirk handling for devices. Called "late" quirk handling, because
 * the AMD/Intel specific modules may do some early quirk handling if
 * they need the information during setup.
 */

struct iommu_late_quirk_args {
	int *lqa_isunity;
	struct memlist **lqa_unitypp;
};

/*
 * Used for quirk entries that simply specify a 1:1 mapping
 * for a device. In our case, used for display devices.
 */
/*ARGSUSED*/
static void
iommu_late_quirk_unity(dev_info_t *dip, void *arg)
{
	struct iommu_late_quirk_args *lqa;

	lqa = arg;

	*lqa->lqa_isunity = 1;
}

/*
 * Some high-end desktop boards with Intel Tylersburg CPUs have
 * a BIOS that does not properly initialize the IOMMU that
 * serves the audio device. This causes the audio to lock up
 * the first time it tries to do DMA. Work around this by
 * using a 1:1 mapping (passthrough) for the audio device
 * on these boards.
 *
 * These boards were mostly produced in late 2008/2009, and
 * BIOSes were fixed later. So, check the BIOS date, and
 * assume it's buggy if it's from before 2010. This may
 * catch a few systems that don't need the workaround,
 * but that's not a big deal (they'll work fine). It's
 * better to err on the side of caution here, since this
 * bug causes the system to lock up during boot.
 */
static void
iommu_late_quirk_tylersburg(dev_info_t *dip, void *arg)
{
	smbios_bios_t sb;
	int month, day, year;
	struct iommu_late_quirk_args *lqa;

	lqa = arg;

	if (ksmbios == NULL || smbios_info_bios(ksmbios, &sb) == SMB_ERR)
		return;

	if (strncmp(sb.smbb_vendor, "American Megatrends", 19))
		return;

	if (sscanf(sb.smbb_reldate, "%d/%d/%d", &month, &day, &year) != 3)
		return;

	if (year < 2010) {
		ddi_err(DER_LOG, dip, "AMI Tylersburg bug: "
		    "using 1:1 IOMMU mapping for audiohd");
		*lqa->lqa_isunity = 1;
	}
}

/*
 * USB drivers may be quirky, especially with keyboards in legacy mode.
 * Use a somewhat heavy-handed approach to work around this, by giving
 * them a 1:1 mapping for all BIOS areas, and page 0.
 */
/*ARGSUSED*/
static void
iommu_late_quirk_hci(dev_info_t *dip, void *arg)
{
	struct iommu_late_quirk_args *lqa;
	struct memlist *mp, *new;

	lqa = arg;

	/*
	 * First, add page 0.
	 */
	new = kmem_zalloc(sizeof (*new), KM_SLEEP);
	new->ml_address = 0;
	new->ml_size = MMU_PAGESIZE;
	new->ml_next = *lqa->lqa_unitypp;
	*lqa->lqa_unitypp = new;

	mp = bios_rsvd;
	while (mp != NULL) {
		new = kmem_zalloc(sizeof (*new), KM_SLEEP);
		new->ml_address = mp->ml_address & MMU_PAGEMASK;
		new->ml_size =
		    ((mp->ml_address + mp->ml_size + MMU_PAGEOFFSET)
		    & MMU_PAGEMASK) - new->ml_address;

		new->ml_next = *lqa->lqa_unitypp;
		*lqa->lqa_unitypp = new;

		mp = mp->ml_next;
	}
}

/*
 * The default is to have fully translated mappings.
 */
/*ARGSUSED*/
static void
iommu_late_quirk_default(dev_info_t *dip, void *arg)
{
	struct iommu_late_quirk_args *lqa;

	lqa = arg;

	*lqa->lqa_isunity = 0;
}

static iommu_quirk_t iommu_late_quirk_table[] = {
	/*
	 * USB host controllers can be quirky and require
	 * 1:1 access to BIOS areas, especially when set
	 * to legacy mode for keyboard access during boot.
	 */
	{ IQMATCH_DNAME, "uhci", 0, 0, 0, 0, iommu_late_quirk_hci },

	{ IQMATCH_DNAME, "ehci", 0, 0, 0, 0, iommu_late_quirk_hci },

	{ IQMATCH_DNAME, "ohci", 0, 0, 0, 0, iommu_late_quirk_hci },

	/*
	 * Same for FireWire host controllers.
	 */

	{ IQMATCH_DNAME, "hci1394", 0, 0, 0, 0, iommu_late_quirk_hci },

	/*
	 * AMD IOMMU units appear as PCI devices, but obviously
	 * they are not subject to remapping.
	 */
	{ IQMATCH_DNAME, "amd_iommu", 0, 0, 0, 0, iommu_late_quirk_unity },

	/*
	 * Display devices get a 1:1 mapping. Any AGP handling
	 * also gets a 1:1 mapping.
	 */
	{ IQMATCH_CLASS, NULL, 0, 0, PCI_CLASS_DISPLAY, (uint_t)~0,
	    iommu_late_quirk_unity },

	{ IQMATCH_DNAME, "agpgart", 0, 0, 0, 0,
	    iommu_late_quirk_unity },

	{ IQMATCH_DNAME, "agptarget", 0, 0, 0, 0,
	    iommu_late_quirk_unity },

	/*
	 * 0x8086 = Intel
	 * 0x3a3e = HD audio ("Azalia")
	 *
	 * If this device is present, this may be an earlier
	 * high-end Intel desktop board, which can have a buggy
	 * AMI BIOS.
	 */
	{ IQMATCH_PCIID, NULL, 0x8086, 0x3a3e, 0, 0,
	    iommu_late_quirk_tylersburg },

	{ IQMATCH_DEFAULT, NULL, 0, 0, (uint_t)~0, (uint_t)~0,
	    iommu_late_quirk_default },
};

static void
iommu_late_quirks(dev_info_t *dip, iommu_pci_info_t *pinfo, int *isunity,
    struct memlist **mlpp)
{
	struct iommu_late_quirk_args lq;

	lq.lqa_isunity = isunity;
	lq.lqa_unitypp = mlpp;

	iommulib_quirk_match(dip, pinfo, &lq, iommu_late_quirk_table);
}
