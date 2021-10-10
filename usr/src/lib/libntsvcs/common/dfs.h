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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_DFS_H
#define	_DFS_H

#include <priv.h>
#include <smbsrv/smb_dfs.h>
#include <smbsrv/libsmb.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Status returned by dfs_link_stat()
 */
#define	DFS_STAT_UNKNOWN	0
#define	DFS_STAT_NOTFOUND	1
#define	DFS_STAT_NOTLINK	2
#define	DFS_STAT_ISREPARSE	3
#define	DFS_STAT_ISDFS		4

typedef struct dfs_path {
	char		p_fspath[DFS_PATH_MAX];
	smb_unc_t	p_unc;
	uint32_t	p_type;
} dfs_path_t;

typedef struct dfs_node {
	avl_node_t	dn_hook;
	char		dn_uncpath[DFS_PATH_MAX];
	char		dn_fspath[DFS_PATH_MAX];
	uint32_t	dn_type;
} dfs_node_t;

uint32_t dfs_ns_count(void);
uint32_t dfs_ns_create(const char *, const char *);
uint32_t dfs_ns_destroy(const char *);
uint32_t dfs_ns_getflavor(const char *);
void dfs_ns_hold(const char *);
void dfs_ns_rele(const char *);
uint32_t dfs_ns_addlink(const char *, dfs_path_t *,
    const char *, const char *, const char *, uint32_t);
uint32_t dfs_ns_removelink(const char *, dfs_path_t *,
    const char *, const char *);
uint32_t dfs_ns_numlink(const char *);
dfs_node_t *dfs_ns_firstlink(const char *);
dfs_node_t *dfs_ns_nextlink(const char *, dfs_node_t *);

uint32_t dfs_root_getinfo(const char *, dfs_info_t *, uint32_t);
uint32_t dfs_root_setinfo(const char *, dfs_info_t *, uint32_t);

uint32_t dfs_link_add(const char *, const char *, const char *,
    const char *, uint32_t, boolean_t *);
uint32_t dfs_link_remove(const char *, const char *, const char *);
uint32_t dfs_link_stat(const char *, uint32_t *);
uint32_t dfs_link_getinfo(const char *, dfs_info_t *, uint32_t);
uint32_t dfs_link_setinfo(const char *, dfs_info_t *, uint32_t);

uint32_t dfs_path_parse(dfs_path_t *, const char *, uint32_t);
void dfs_path_free(dfs_path_t *);

uint32_t dfs_getinfo(dfs_node_t *, dfs_info_t *, uint32_t);

void dfs_init(void);
void dfs_fini(void);
void dfs_setpriv(priv_op_t);

void dfs_info_trace(const char *, dfs_info_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DFS_H */
