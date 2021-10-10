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

#ifndef _SYS_PCI_PARAM_H
#define	_SYS_PCI_PARAM_H

#include <sys/dditypes.h>
#include <sys/ddipropdefs.h>
#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	PCI_PARAM_DATA_TYPE_UNKNOWN		DATA_TYPE_UNKNOWN
#define	PCI_PARAM_DATA_TYPE_BOOLEAN		DATA_TYPE_BOOLEAN
#define	PCI_PARAM_DATA_TYPE_BYTE		DATA_TYPE_BYTE
#define	PCI_PARAM_DATA_TYPE_INT8		DATA_TYPE_INT8
#define	PCI_PARAM_DATA_TYPE_UINT8		DATA_TYPE_UINT8
#define	PCI_PARAM_DATA_TYPE_INT16		DATA_TYPE_INT16
#define	PCI_PARAM_DATA_TYPE_UINT16		DATA_TYPE_UINT16
#define	PCI_PARAM_DATA_TYPE_INT32		DATA_TYPE_INT32
#define	PCI_PARAM_DATA_TYPE_UINT32		DATA_TYPE_UINT32
#define	PCI_PARAM_DATA_TYPE_INT64		DATA_TYPE_INT64
#define	PCI_PARAM_DATA_TYPE_UINT64		DATA_TYPE_UINT64
#define	PCI_PARAM_DATA_TYPE_STRING		DATA_TYPE_STRING
#define	PCI_PARAM_DATA_TYPE_PLIST		DATA_TYPE_NVLIST
#define	PCI_PARAM_DATA_TYPE_BYTE_ARRAY		DATA_TYPE_BYTE_ARRAY
#define	PCI_PARAM_DATA_TYPE_INT8_ARRAY		DATA_TYPE_INT8_ARRAY
#define	PCI_PARAM_DATA_TYPE_UINT8_ARRAY		DATA_TYPE_UINT8_ARRAY
#define	PCI_PARAM_DATA_TYPE_INT16_ARRAY		DATA_TYPE_INT16_ARRAY
#define	PCI_PARAM_DATA_TYPE_UINT16_ARRAY	DATA_TYPE_UINT16_ARRAY
#define	PCI_PARAM_DATA_TYPE_INT32_ARRAY		DATA_TYPE_INT32_ARRAY
#define	PCI_PARAM_DATA_TYPE_UINT32_ARRAY	DATA_TYPE_UINT32_ARRAY
#define	PCI_PARAM_DATA_TYPE_INT64_ARRAY		DATA_TYPE_INT64_ARRAY
#define	PCI_PARAM_DATA_TYPE_UINT64_ARRAY	DATA_TYPE_UINT64_ARRAY
#define	PCI_PARAM_DATA_TYPE_STRING_ARRAY	DATA_TYPE_STRING_ARRAY
#define	PCI_PARAM_DATA_TYPE_PLIST_ARRAY		DATA_TYPE_NVLIST_ARRAY


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCI_PARAM_H */
