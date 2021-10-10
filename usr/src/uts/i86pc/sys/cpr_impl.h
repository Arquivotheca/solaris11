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

#ifndef	_SYS_CPR_IMPL_H
#define	_SYS_CPR_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif


#ifndef _ASM

#include <sys/machparam.h>
#include <sys/vnode.h>
#include <sys/pte.h>

/*
 * This file contains machine dependent information for CPR
 */
#define	CPR_MACHTYPE_X86	0x5856		/* 'X'0t86 */
typedef intptr_t cpr_ptr;
typedef uint64_t cpr_ext;

/*
 * This structure describes the machine dependent data for x64 platforms.
 * This structure is still evolving, but needs to fill out the first disk
 * block of the written statefile.  So besides the known structure, there
 * is an arbitrary pad.  The size should be adjusted as elements are
 * added and removed.
 */
struct cpr_x86pc_machdep {
	cpr_ptr	thrp;
	cpr_ext	qsav_pc;
	cpr_ext	qsav_sp;
	int	test_mode;
	uchar_t	pad[500];
};

typedef	struct cpr_x86pc_machdep csu_md_t;

extern void i_cpr_machdep_setup(void);
extern void i_cpr_enable_intr(void);
extern void i_cpr_stop_intr(void);
extern void i_cpr_handle_xc(int);
extern int i_cpr_prom_pages(int);
extern int i_cpr_write_machdep(vnode_t *);

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CPR_IMPL_H */
