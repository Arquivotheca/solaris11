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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SHADOWTEST_H
#define	_SHADOWTEST_H

#include <sys/vfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	ST_IOC_GETFID = 1890,		/* get FID for a file descriptor */
	ST_IOC_SUSPEND,			/* suspend automatic rotation */
	ST_IOC_RESUME,			/* resume automatic rotation */
	ST_IOC_ROTATE,			/* rotate pending log */
	ST_IOC_SPIN,			/* spin during migration */
	ST_IOC_CRED_SET,		/* set file credentials to curproc */
	ST_IOC_CRED_CLEAR,		/* reset file credentials to kcred */
	ST_IOC_KTHREAD			/* migrate file from kernel thread */
} st_ioc_t;

typedef struct st_cmd {
	int		stc_fd;
	fid32_t		stc_fid;
	boolean_t	stc_op;
} st_cmd_t;

#define	SHADOWTEST_DEV	"/devices/pseudo/shadowtest@0:shadowtest"

#ifdef	__cplusplus
}
#endif

#endif	/* _SHADOWTEST_H */
