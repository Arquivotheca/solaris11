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

#ifndef _BAY_IMPL_H
#define	_BAY_IMPL_H

#include <fm/topo_mod.h>
#include <libdevinfo.h>
#include <fm/topo_list.h>
#include <fm/topo_method.h>
#include <bay.h>


#ifdef __cplusplus
extern "C" {
#endif

/* Topo plugin version */
#define	BAY_VERSION		TOPO_VERSION

/* facility defines */
#define	BAY_PROP_IDENT		"bay_locate"
#define	BAY_PROP_FAULT		"bay_fail"
#define	BAY_PROP_OK2RM		"bay_ok2rm"

#define	BAY_INDICATOR_GET	0x0
#define	BAY_INDICATOR_SET	0x1

/* bay_subr.c prototypes */
int		 create_l0ids(topo_mod_t *, tnode_t *, bay_t *);
di_minor_t	 find_minor_ap(topo_mod_t *, di_node_t);
int		 find_child(topo_mod_t *, di_node_t, di_node_t *,
    di_path_t *, int);
char		*gen_ap(topo_mod_t *, di_minor_t);
char		*gen_oc(topo_mod_t *, di_node_t, di_path_t);
boolean_t	 internal_ch(bay_t *);
char		*get_devid(topo_mod_t *, char *, bay_t *);
int		 get_prod(topo_mod_t *, tnode_t *, char *, char *);
int		 get_num_phys(di_node_t);
int		 read_config(topo_mod_t *, di_node_t, char *, bay_t *, int *);
int		sort_hba_nodes(topo_mod_t *, tnode_t *, di_node_t *);

/* bay_common.c prototypes */
int		 bay_set_label(topo_mod_t *, bay_t *, tnode_t *);
int		 bay_set_auth(topo_mod_t *, tnode_t *, tnode_t *);
int		 bay_set_system(topo_mod_t *, tnode_t *);
int		 bay_create_tnode(topo_mod_t *, tnode_t *, tnode_t **, bay_t *);

/* facility nodes */
int		 bay_enum_facility(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
int		 bay_led_ctl(topo_mod_t *, tnode_t *, char *, uint32_t, int);

#ifdef __cplusplus
}
#endif

#endif /* _BAY_IMPL_H */
