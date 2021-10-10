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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#ifndef _DRYRUN_H_
#define	_DRYRUN_H_

#include "cfext.h"

/* The various types of status entry in the info file. */
#define	PARTIAL	1
#define	RUNLEVEL 2
#define	PKGFILES 3
#define	DEPEND 4
#define	SPACE 5
#define	CONFLICT 6
#define	SETUID 7
#define	PRIV 8
#define	PKGDIRS 9
#define	REQUESTEXITCODE 10
#define	CHECKEXITCODE 11
#define	EXITCODE 12
#define	DR_TYPE 13

#define	INSTALL_TYPE	1
#define	REMOVE_TYPE	0

extern void	set_dryrun_mode(void);
extern int	in_dryrun_mode(void);
extern void	set_continue_mode(void);
extern int	in_continue_mode(void);
extern void	init_contfile(char *cn_dir);
extern void	init_dryrunfile(char *dr_dir);
extern void	set_dr_info(int type, int value);
extern int	cmd_ln_respfile(void);
extern int	is_a_respfile(void);
extern void	write_dryrun_file(struct cfextra **extlist);
extern boolean_t	read_continuation(int *error);

#endif	/* _DRYRUN_H_ */
