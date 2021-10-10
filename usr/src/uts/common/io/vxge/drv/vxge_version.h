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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :	vxge_version.h
 *
 * Description:  Version description
 *
 * Created:	  7 June 2004
 */

#ifndef VXGE_VERSION_H
#define	VXGE_VERSION_H


#define	VXGE_VERSION_MAJOR	1
#define	VXGE_VERSION_MINOR	0
#define	VXGE_VERSION_FIX	0

#define	VXGE_FW_MAJOR_VERSION 1
#define	VXGE_FW_MINOR_VERSION 6
#define	VXGE_FW_BUILD_NUMBER  0

#define	VXGE_VERSION_BUILD	GENERATED_BUILD_VERSION

/* Firmware array name */
#define	VXGE_FW_ARRAY_NAME	X3fw_ncf

#define	VXGE_FW_VER(major, minor, build) \
	(((major) << 16) + ((minor) << 8) + (build))

#define	VXGE_BASE_FW_MAJOR_VERSION 1
#define	VXGE_BASE_FW_MINOR_VERSION 4
#define	VXGE_BASE_FW_BUILD_NUMBER  4

#define	VXGE_BASE_FW_VER \
VXGE_FW_VER(VXGE_BASE_FW_MAJOR_VERSION, VXGE_BASE_FW_MINOR_VERSION, \
	VXGE_BASE_FW_BUILD_NUMBER)

#endif /* VXGE_VERSION_H */
