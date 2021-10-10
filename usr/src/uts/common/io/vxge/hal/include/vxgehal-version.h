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
 * FileName :   vxgehal-version.h
 *
 * Description:  hal versioning file
 *
 * Created:       27 December 2006
 */

#ifndef	VXGE_HAL_VERSION_H
#define	VXGE_HAL_VERSION_H

#include "build-version.h"


/*
 * VXGE_HAL_VERSION_MAJOR - HAL major version
 */
#define	VXGE_HAL_VERSION_MAJOR		0

/*
 * VXGE_HAL_VERSION_MINOR - HAL minor version
 */
#define	VXGE_HAL_VERSION_MINOR		0

/*
 * VXGE_HAL_VERSION_FIX - HAL version fix
 */
#define	VXGE_HAL_VERSION_FIX		0

/*
 * VXGE_HAL_VERSION_BUILD - HAL build version
 */
#define	VXGE_HAL_VERSION_BUILD	GENERATED_BUILD_VERSION

/*
 * VXGE_HAL_VERSION - HAL version
 */
#define	VXGE_HAL_VERSION "VXGE_HAL_VERSION_MAJOR.VXGE_HAL_VERSION_MINOR.\
			VXGE_HAL_VERSION_FIX.VXGE_HAL_VERSION_BUILD"

/*
 * VXGE_HAL_DESC - HAL Description
 */
#define	VXGE_HAL_DESC	VXGE_DRIVER_NAME" v."VXGE_HAL_VERSION

/* Link Layer versioning */
#include "vxge_version.h"

#endif /* VXGE_HAL_VERSION_H */
