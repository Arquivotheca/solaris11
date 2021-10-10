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

#ifndef _SYS_IB_CLIENTS_OF_SOL_UVERBS_SOL_UVERBS_IOCTL_H
#define	_SYS_IB_CLIENTS_OF_SOL_UVERBS_SOL_UVERBS_IOCTL_H

#include <sys/cred.h>

/*
 * sol_uverbs_ioctl.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for all ioctl access into the driver.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	UVERBS_IOCTL		('v' << 8)

#define	IB_USER_VERBS_SOLARIS_ABI_VERSION	1

typedef enum {
	UVERBS_IOCTL_GET_HCA_INFO	= UVERBS_IOCTL | 0x01
} uverbs_ioctl_enum_t;

typedef struct sol_uverbs_hca_info_s {
	char		uverbs_hca_psid_string[MAXNAMELEN];
	char		uverbs_hca_ibdev_name[MAXNAMELEN];
	char		uverbs_hca_driver_name[MAXNAMELEN];
	uint32_t	uverbs_hca_driver_instance;
	uint32_t	uverbs_hca_vendorid;
	uint16_t	uverbs_hca_deviceid;
	uint8_t		uverbs_hca_devidx;
	uint8_t		uverbs_hca_pad1[5];
} sol_uverbs_hca_info_t;

typedef struct sol_uverbs_info_s {
	int32_t			uverbs_abi_version;
	int32_t			uverbs_solaris_abi_version;
	int16_t			uverbs_hca_cnt;
	int8_t			uverbs_pad1[6];    /* Padding for alignment */
	sol_uverbs_hca_info_t	uverbs_hca_info[];
} sol_uverbs_info_t;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IB_CLIENTS_OF_SOL_UVERBS_SOL_UVERBS_IOCTL_H */
