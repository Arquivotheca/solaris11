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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_DEVMGMT_H
#define	_DEVMGMT_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * devmgmt.h
 *
 * This header provides definitions for the (now defunct) OAM Device
 * Management subsystem.  A few funtions from this subsystem have lived
 * on as compatibility stubs.  These are defined here.
 *
 * Contents:
 *    -	Device Management definitions,
 *    -	getvol() definitions
 */

/*
 * Standard field names in the device table
 */
#define	DTAB_ALIAS			"alias"
#define	DTAB_CDEVICE			"cdevice"
#define	DTAB_BDEVICE			"bdevice"
#define	DTAB_PATHNAME			"pathname"

/*
 * Device Management Functions:
 *
 *	devattr()	Returns a device's attribute
 *	listdev()	List attributes defined for a device
 */

extern char *devattr(char *, char *);
extern char **listdev(char *);

/*
 * getvol() definitions
 */

#define	DM_BATCH	0x0001
#define	DM_ELABEL	0x0002
#define	DM_FORMAT	0x0004
#define	DM_FORMFS	0x0008
#define	DM_WLABEL	0x0010
#define	DM_OLABEL	0x0020

extern int getvol(char *, char *, int, char *);
extern int _getvol(char *, char *, int, char *, char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DEVMGMT_H */
