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

#ifndef _SOLARIS_KSH_CMDLIST_H
#define	_SOLARIS_KSH_CMDLIST_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * List builtins for Solaris.
 */

#define	BINCMDLIST(f)	\
	{ "/bin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	USRBINCMDLIST(f)	\
	{ "/usr/bin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	SBINCMDLIST(f)	\
	{ "/sbin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	SUSRBINCMDLIST(f)	\
	{ "/usr/sbin/"	#f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
/* POSIX compatible commands */
#define	XPG6CMDLIST(f)	\
	{ "/usr/xpg6/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#define	XPG4CMDLIST(f)	\
	{ "/usr/xpg4/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#ifdef SHOPT_USR_GNU_BIN_BUILTINS
/* GNU coreutils compatible commands */
#define	GNUCMDLIST(f)	\
	{ "/usr/gnu/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },
#else
#define	GNUCMDLIST(f)
#endif

/*
 * Make all ksh93 builtins accessible when /usr/ast/bin was added to
 * /usr/xpg6/bin:/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:/bin:/opt/SUNWspro/bin
 */
#define	ASTCMDLIST(f)	\
	{ "/usr/ast/bin/" #f, NV_BLTIN|NV_BLTINOPT|NV_NOFREE, bltin(f) },

/* undo ast_map.h #defines to avoid collision */
#undef basename
#undef dirname
#undef mktemp

/* Same as stock ksh93 but prefixed with /usr/ast/bin instead of /opt/ast/bin */
ASTCMDLIST(basename)
ASTCMDLIST(cat)
ASTCMDLIST(chmod)
ASTCMDLIST(cmp)
ASTCMDLIST(cut)
ASTCMDLIST(dirname)
ASTCMDLIST(getconf)
ASTCMDLIST(head)
ASTCMDLIST(logname)
ASTCMDLIST(mkdir)
ASTCMDLIST(sync)
ASTCMDLIST(uname)
ASTCMDLIST(wc)

#ifdef	__cplusplus
}
#endif

#endif /* !_SOLARIS_KSH_CMDLIST_H */
