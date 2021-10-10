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
 * Copyright (c) 2004, 2005, Oracle and/or its affiliates. All rights reserved.
 *
 * Interfaces internal to the i86pc PCI nexus driver.
 */

#ifndef	_SYS_PCI_CFGSPACE_H
#define	_SYS_PCI_CFGSPACE_H

#ifdef	__cplusplus
extern "C" {
#endif

extern void pci_cfgspace_init(void);

/*
 * These used to be set by, and live in, pci_autoconfig; now they are set
 * by pci_cfgspace_init(), and live in the base kernel.
 */

extern uint8_t (*pci_getb_func)(int bus, int dev, int func, int reg);
extern uint16_t (*pci_getw_func)(int bus, int dev, int func, int reg);
extern uint32_t (*pci_getl_func)(int bus, int dev, int func, int reg);
extern void (*pci_putb_func)(int bus, int dev, int func, int reg, uint8_t val);
extern void (*pci_putw_func)(int bus, int dev, int func, int reg, uint16_t val);
extern void (*pci_putl_func)(int bus, int dev, int func, int reg, uint32_t val);
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCI_CFGSPACE_H */
