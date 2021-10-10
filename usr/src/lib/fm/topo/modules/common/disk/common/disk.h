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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _DISK_H
#define	_DISK_H

#include <fm/topo_mod.h>
#include <libdevinfo.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Topo plugin version */
#define	DISK_VERSION			TOPO_VERSION

/*
 * device node information.
 */
typedef struct dev_di_node {
	topo_list_t	ddn_list;	/* list of devices */

	/* the following two fields are always defined */
	uchar_t		ddn_dtype;	/* scsi inquiry device type. */
	char		*ddn_devid;	/* devid of device */

	char		*ddn_mfg;	/* misc information about device */
	char		*ddn_model;
	char		*ddn_part;
	char		*ddn_serial;
	char		*ddn_firm;
	char		*ddn_cap;

	int		ddn_dpaths_n;	/* devinfo paths */
	char		**ddn_dpaths;	/* path to devinfo of an lpath */

	int		ddn_lpaths_n;	/* logical names of /dev paths */
	char		**ddn_lpaths;	/* i.e. c#t#d# component of /dev path */
					/* (no slice, no dsk/rdsk) */

	int		ddn_ppaths_n;	/* phci paths (devinfo or pathinfo */
	char		**ddn_ppaths;

	int		ddn_target_ports_n;	/* target-ports */
	char		**ddn_target_ports;

	int		ddn_attached_ports_n;	/* attached-ports */
	char		**ddn_attached_ports;

	int		ddn_bridge_ports_n;	/* bridge-ports */
	char		**ddn_bridge_ports;
} dev_di_node_t;

struct topo_list;

/* Methods shared with the ses module (disk_common.c) */
extern int dev_list_gather(topo_mod_t *, struct topo_list *);
extern void dev_list_free(topo_mod_t *, struct topo_list *);

extern int disk_declare_path(topo_mod_t *, tnode_t *,
    struct topo_list *, const char *);
extern int disk_declare_addr(topo_mod_t *, tnode_t *,
    struct topo_list *, const char *, tnode_t **);
extern int disk_declare_non_enumerated(topo_mod_t *, tnode_t *, tnode_t **);
extern int disk_declare_empty(topo_mod_t *, tnode_t *);

extern char *disk_auth_clean(topo_mod_t *, const char *);

#ifdef __cplusplus
}
#endif

#endif /* _DISK_H */
