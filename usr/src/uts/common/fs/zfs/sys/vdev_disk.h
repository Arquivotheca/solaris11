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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#ifndef _SYS_VDEV_DISK_H
#define	_SYS_VDEV_DISK_H

#include <sys/vdev.h>
#ifdef _KERNEL
#include <sys/buf.h>
#include <sys/ddi.h>
#include <sys/sunldi.h>
#include <sys/sunddi.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct vdev_disk {
	ddi_devid_t	vd_devid;
	char		*vd_minor;
	ldi_handle_t	vd_lh;
	uint_t		vd_maxxfer;
	int		vd_mode;
	boolean_t	vd_wholedisk;
} vdev_disk_t;

#ifdef _KERNEL
extern int vdev_disk_dump(vdev_disk_t *, caddr_t, uint64_t, uint64_t);
extern int vdev_disk_physio(ldi_handle_t, caddr_t, size_t, uint64_t, int,
    uint_t);
#endif
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_DISK_H */
