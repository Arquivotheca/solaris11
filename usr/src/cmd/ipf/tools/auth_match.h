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

#ifndef	_AUTH_MATCH_H
#define	_AUTH_MATCH_H

#include <netinet/ip_auth.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Function auth_load_rules() loads rules from file referred by fname to
 * application.  It returns 0 on success, -1 on failure.
 */
extern int auth_load_rules(const char *fname);

/*
 * Function auth_check() processes auth request ar. It applies rules previously
 * loaded by auth_load_rules().
 *
 * returns 0 on success, -1 on failure.
 */
extern int auth_check(frauth_t *ar);

/*
 * Function auth_unload_rules() releases rules previously loaded by
 * auth_load_rules().
 */
extern void auth_unload_rules(void);

#ifdef __cplusplus
}
#endif

#endif	/* _AUTH_MATCH_H */
