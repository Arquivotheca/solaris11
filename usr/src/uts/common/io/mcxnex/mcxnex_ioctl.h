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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MCXNEX_IOCTL_H
#define	_MCXNEX_IOCTL_H

#include <sys/cred.h>

/*
 * mcxnex_ioctl.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for all ioctl access into the driver.  This includes everything
 *    necessary for updating firmware, accessing the mcxnex flash device,
 *    providing interfaces for VTS.
 *
 *	Note: we just include hermon_ioctl.h here so that to provide same
 *	IOCTL interface as hermon to consumers, such as fwflash too etc.
 *	Need to re-visit this when we add mcxib driver to replace hermon.
 */
#include <sys/ib/adapters/hermon/hermon_ioctl.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef	hermon_ioctl_enum_t		mcxnex_ioctl_enum_t;
typedef	hermon_fw_info_ioctl_t		mcxnex_fw_info_ioctl_t;
typedef	hermon_flash_ioctl_t		mcxnex_flash_ioctl_t;
typedef	hermon_flash_init_ioctl_t	mcxnex_flash_init_ioctl_t;
typedef	hermon_reg_ioctl_t		mcxnex_reg_ioctl_t;
typedef	hermon_stat_port_ioctl_t	mcxnex_stat_port_ioctl_t;
typedef	hermon_ports_ioctl_t		mcxnex_ports_ioctl_t;
typedef	hermon_loopback_error_t		mcxnex_loopback_error_t;
typedef	hermon_loopback_ioctl_t		mcxnex_loopback_ioctl_t;
typedef	hermon_info_ioctl_t		mcxnex_info_ioctl_t;

int mcxnex_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp);

#ifdef __cplusplus
}
#endif

#endif	/* _MCXNEX_IOCTL_H */
