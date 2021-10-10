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
/*
 * This file defines the private interface used by libiov
 */

#ifndef _SYS_IOV_PARAM_H
#define	_SYS_IOV_PARAM_H

#include <sys/dditypes.h>
#include <sys/ddipropdefs.h>

#ifdef	__cplusplus
extern "C" {
#endif


#define	IOV_PARAM_DESC_VERSION	1

/*
 * The application first issues IOV_GET_VER_INFO IOCTL to get version
 * and num_params. Using this it computes the size of the buffer that
 * will be required in the IOV_GET_PARAM_INFO IOCTL
 */
typedef	struct iov_param_ver_info {
	uint32_t	version; /* set to #define PCI_PARAM_DESC_VERSION */
	uint32_t	num_params; /* used to compute bufsize required */
} iov_param_ver_info_t;

/*
 * This structure has been carefully designed such that it is the same
 * size in both 32 and 64 bit applications.
 * The Lengths of char arrays are a multiple of 8
 * char arrays also start at a boundary that is multiple of 8.
 * When making changes follows these rules.
 */
#define	MAX_PARAM_NAME_SIZE 80
#define	MAX_PARAM_DESC_SIZE 128
#define	MAX_PARAM_DEFAULT_STRING_SIZE 128
#define	MAX_REASON_LEN	80

typedef struct iov_param_desc {
	char		pd_name[MAX_PARAM_NAME_SIZE];
	char		pd_desc[MAX_PARAM_DESC_SIZE];
	int32_t		pd_flag;
	int32_t		pd_data_type;
	uint64_t	pd_default_value;
	uint64_t	pd_min64;
	uint64_t	pd_max64;
	char		pd_default_string[MAX_PARAM_DEFAULT_STRING_SIZE];
} iov_param_desc_t;

/*
 * param flags to indicate if it is applicable to PF(Physical Function)
 * or VF(virtual function) or both.
 * The values chosen are power of 2 so that these can be OR'd successfully.
 */
#define	PCIV_DEV_PF	0x1
#define	PCIV_DEV_VF	0x2
#define	PCIV_READ_ONLY	0x4

	/*
	 * The libiov encodes the params into the pv_buf[1] of the structure.
	 * The pv_buflen specifies the length of the encoded buffer.
	 * A interface pci_param_get_ioctl() is provided to obtain
	 * param handle pci_param_t from the structure below.
	 * The driver passes the ioctl arg to pci_param_ioctl and gets
	 * pci_param_t handle.
	 * The driver can use the reason buffer below to return
	 * a explanation for the failure.
	 *
	 */

typedef	struct iov_param_validate {
	/*
	 * on return from ioctl the reason array contains  an explanatory
	 * string for the failure.
	 */
	char	pv_reason[MAX_REASON_LEN + 1];
	int32_t	pv_buflen;
	/*
	 * Encoded buffer containing params to be validated.
	 */
	char 	pv_buf[1]; /* size of this buffer is pv_buflen */
} iov_param_validate_t;



#define	IOV_IOCTL		(('I' << 24) | ('O' << 16) | ('V' << 8))
#define	IOV_GET_PARAM_VER_INFO	(IOV_IOCTL | 0)
#define	IOV_GET_PARAM_INFO	(IOV_IOCTL | 1)
#define	IOV_VALIDATE_PARAM	(IOV_IOCTL | 2)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOV_PARAM_H */
