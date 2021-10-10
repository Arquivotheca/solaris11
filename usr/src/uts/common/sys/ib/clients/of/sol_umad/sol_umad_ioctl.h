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

#ifndef _SYS_IB_CLIENTS_OF_SOL_UMAD_SOL_UMAD_IOCTL_H
#define	_SYS_IB_CLIENTS_OF_SOL_UMAD_SOL_UMAD_IOCTL_H

#include <sys/cred.h>

/*
 * sol_umad_ioctl.h
 *    Contains all of the prototypes, #defines, and structures necessary
 *    for all ioctl access into the driver.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	UMAD_IOCTL		('m' << 8)

#define	IB_USER_MAD_SOLARIS_ABI_VERSION	1

typedef enum {
	IB_USER_MAD_GET_PORT_INFO	= UMAD_IOCTL | 0x01
} umad_ioctl_enum_t;

typedef struct sol_umad_ioctl_port_info_s {
	char		umad_port_ibdev_name[MAXNAMELEN];
	uint32_t	umad_port_num;
	uint16_t	umad_port_idx;
	uint8_t		umad_port_pad1[2];
} sol_umad_ioctl_port_info_t;

typedef struct sol_umad_ioctl_info_s {
	int32_t				umad_abi_version;
	int32_t				umad_solaris_abi_version;
	int16_t				umad_port_cnt;
	int8_t				umad_pad1[6];    /* alignment padding */
	sol_umad_ioctl_port_info_t	umad_port_info[];
} sol_umad_ioctl_info_t;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IB_CLIENTS_OF_SOL_UMAD_SOL_UMAD_IOCTL_H */
