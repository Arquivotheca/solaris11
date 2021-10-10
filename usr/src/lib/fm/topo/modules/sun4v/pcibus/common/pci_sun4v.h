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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _PCI_SUN4V_H
#define	_PCI_SUN4V_H

#include "pcibus_labels.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Data for label lookup based on physical slot number.
 *
 * Platforms may need entries here if the PCIe slot labels
 * provided by firmware are missing or are incorrect.
 */

physnm_t t200_pnms[] = {
	/* Slot #, Label */
	{ 224, "PCIE0" },
	{ 225, "PCIE1" },
	{ 226, "PCIE2" }
};

physnm_t t5120_pnms[] = {
	/* Slot #, Label */
	{   0, "MB/RISER0/PCIE0" },
	{   1, "MB/RISER1/PCIE1" },
	{   2, "MB/RISER2/PCIE2" }
};

physnm_t t5220_pnms[] = {
	/* Slot #, Label */
	{   0, "MB/RISER0/PCIE0" },
	{   1, "MB/RISER1/PCIE1" },
	{   2, "MB/RISER2/PCIE2" },
	{   3, "MB/RISER0/PCIE3" },
	{   4, "MB/RISER1/PCIE4" },
	{   5, "MB/RISER2/PCIE5" }
};

physnm_t usbrdt5240_pnms[] = {
	/* Slot #, Label */
	{   0, "MB/RISER0/EM0" },
	{   1, "MB/RISER0/EM1" },
	{   2, "MB/RISER1/EM2" },
	{   3, "MB/RISER1/EM3" }
};

physnm_t netra_t5220_pnms[] = {
	/* Slot #, Label */
	{   0, "MB/RISER0/PCIE0" },
	{   1, "MB/RISER1/PCIE1" },
	{   2, "MB/RISER2/PCIE2" },
	{   3, "MB/PCI_MEZZ/PCIX3" },
	{   4, "MB/PCI_MEZZ/PCIX4" },
	{   5, "MB/PCI_MEZZ/PCIE5" }
};

physnm_t netra_t5440_pnms[] = {
	/* Slot #, Label */
	{   0, "MB/PCI_AUX/PCIX0" },
	{   1, "MB/PCI_AUX/PCIX1" },
	{   2, "MB/PCI_AUX/PCIE2" },
	{   3, "MB/PCI_AUX/PCIE3" },
	{   4, "MB/PCI_MEZZ/XAUI4" },
	{   5, "MB/PCI_MEZZ/XAUI5" },
	{   6, "MB/PCI_MEZZ/PCIE6" },
	{   7, "MB/PCI_MEZZ/PCIE7" },
	{   8, "MB/PCI_MEZZ/PCIE8" },
	{   9, "MB/PCI_MEZZ/PCIE9" }
};

physnm_t t5440_pnms[] = {
	/* Slot #, Label */
	{   0, "MB/PCIE0" },
	{   1, "MB/PCIE1" },
	{   2, "MB/PCIE2" },
	{   3, "MB/PCIE3" },
	{   4, "MB/PCIE4" },
	{   5, "MB/PCIE5" },
	{   6, "MB/PCIE6" },
	{   7, "MB/PCIE7" }
};

physnm_t blade_t6340_pnms[] = {
	/* Slot #, Label */
	{   0, "SYS/EM0" },
	{   1, "SYS/EM1" }
};

pphysnm_t plat_pnames[] = {
	{ "Sun-Fire-T200",
	    sizeof (t200_pnms) / sizeof (physnm_t),
	    t200_pnms },
	{ "SPARC-Enterprise-T5120",
	    sizeof (t5120_pnms) / sizeof (physnm_t),
	    t5120_pnms },
	{ "SPARC-Enterprise-T5220",
	    sizeof (t5220_pnms) / sizeof (physnm_t),
	    t5220_pnms },
	/*
	 * T5140/T5240 uses the same chassis as T5120/T5220, hence
	 * the same PCI slot mappings
	 */
	{ "T5140",
	    sizeof (t5120_pnms) / sizeof (physnm_t),
	    t5120_pnms },
	{ "T5240",
	    sizeof (t5220_pnms) / sizeof (physnm_t),
	    t5220_pnms },
	{ "T5440",
	    sizeof (t5440_pnms) / sizeof (physnm_t),
	    t5440_pnms },
	{ "USBRDT-5240",
	    sizeof (usbrdt5240_pnms) / sizeof (physnm_t),
	    usbrdt5240_pnms },
	{ "Netra-T5220",
	    sizeof (netra_t5220_pnms) / sizeof (physnm_t),
	    netra_t5220_pnms },
	{ "Netra-T5440",
	    sizeof (netra_t5440_pnms) / sizeof (physnm_t),
	    netra_t5440_pnms },
	{ "Sun-Blade-T6340",
	    sizeof (blade_t6340_pnms) / sizeof (physnm_t),
	    blade_t6340_pnms }
};

physlot_names_t PhyslotNMs = {
	sizeof (plat_pnames) / sizeof (pphysnm_t),
	plat_pnames
};

/*
 * Data for label lookup based on device info.
 *
 * Platforms need entries here if there is no physical slot number
 * (i.e. pci), and slot labels provided by firmware are missing.
 */

devlab_t t200_missing[] = {
	/* board, bridge, root-complex, bus, dev, label, test func */
	{ 0, 0, 1 - TO_PCI, 6, 1, "PCIX1", NULL },
	{ 0, 0, 1 - TO_PCI, 6, 2, "PCIX0", NULL }
};

pdevlabs_t plats_missing[] = {
	{ "Sun-Fire-T200",
	    sizeof (t200_missing) / sizeof (devlab_t),
	    t200_missing },
};

missing_names_t Missing = {
	sizeof (plats_missing) / sizeof (pdevlabs_t),
	plats_missing
};

char *usT1_plats[] = {
	"Sun-Fire-T200",
	"Netra-T2000",
	"SPARC-Enterprise-T1000",
	"SPARC-Enterprise-T2000",
	"Sun-Fire-T1000",
	"Netra-CP3060",
	"Sun-Blade-T6300",
	NULL
};

slotnm_rewrite_t *Slot_Rewrites = NULL;
physlot_names_t *Physlot_Names = &PhyslotNMs;
missing_names_t *Missing_Names = &Missing;

#ifdef __cplusplus
}
#endif

#endif /* _PCI_SUN4V_H */
