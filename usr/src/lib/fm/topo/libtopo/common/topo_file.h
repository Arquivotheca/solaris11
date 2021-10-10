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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _TOPO_FILE_H
#define	_TOPO_FILE_H

#include <topo_parse.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct topo_file {
	tf_info_t *tf_tmap;		/* topology map file info */
	char *tf_filenm;		/* topology file name */
	topo_mod_t *tf_mod;		/* scheme-specific builtin mod */
} topo_file_t;

extern int topo_file_load(topo_mod_t *, tnode_t *, const char *, const char *,
    boolean_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _TOPO_FILE_H */
