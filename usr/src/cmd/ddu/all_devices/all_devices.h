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

#ifndef	_ALL_DEVICES_H
#define	_ALL_DEVICES_H


#ifdef __cplusplus
extern "C" {
#endif

/*
 * dev_id_name struct include pci device name information
 *
 * vname: pci device vendor name
 * dname: pci device device name
 * svname: pci device subvendor name
 * sysname: pci device subsystem name
 */
typedef struct {
	const char 	*vname;
	const char 	*dname;
	const char 	*svname;
	const char 	*sysname;
} dev_id_name;

#ifdef __cplusplus
}
#endif

#endif /* _ALL_DEVICES_H */
