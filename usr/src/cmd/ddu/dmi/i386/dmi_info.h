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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_DMI_INFO_H
#define	_DMI_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dmi_info options
 * OPT_SYS: display SMBIOS system information
 * OPT_BIOS: display SMIBIOS bios information
 * OPT_MB: display SMBIOS motherboard information
 * OPT_PRO: display SMBIOS processor package informaion
 * OPT_CPU: display CPU information
 * OPT_MEM: display SMBIOS memory information
 * OPT_DMI: display SMBIOS system, bios, motherboard
 *          processor package, memory and cpu information
 */
#define	OPT_SYS		0x01
#define	OPT_BIOS	0x02
#define	OPT_MB		0x04
#define	OPT_PRO		0x08
#define	OPT_CPU		0x10
#define	OPT_MEM		0x20
#define	OPT_DMI		OPT_SYS | OPT_BIOS | OPT_MB |\
			OPT_PRO | OPT_MEM | OPT_CPU

#ifdef __cplusplus
}
#endif

#endif /* _DMI_INFO_H */
