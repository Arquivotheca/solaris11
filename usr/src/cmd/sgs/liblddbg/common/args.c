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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include	<debug.h>
#include	"_debug.h"
#include	"msg.h"

void
Dbg_args_option(Lm_list *lml, int ndx, int c, char *optarg)
{
	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	if (optarg)
		dbg_print(lml, MSG_INTL(MSG_ARG_OPTARG), ndx, c, optarg,
		    MSG_ORIG(MSG_STR_EMPTY));
	else
		dbg_print(lml, MSG_INTL(MSG_ARG_OPTION), ndx, c);
}

/*
 * This is the same thing as Dbg_args_option(), except specific to -l
 * options. Therefore the optarg (libname) is required to be non-NULL,
 * and the ignore_stub option is present.
 */
void
Dbg_args_lib_option(Lm_list *lml, int ndx, int c, char *libname,
    Boolean ignore_stub)
{
	const char	*stub_ignore;

	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	stub_ignore = ignore_stub ?
	    MSG_INTL(MSG_ARG_IGN_STUB) : MSG_ORIG(MSG_STR_EMPTY);
	dbg_print(lml, MSG_INTL(MSG_ARG_OPTARG), ndx, c, libname,
	    stub_ignore);
}

void
Dbg_args_str2chr(Lm_list *lml, int ndx, const char *opt, int c)
{
	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	dbg_print(lml, MSG_INTL(MSG_ARG_STR2CHR), ndx, opt, c);
}

void
Dbg_args_Wldel(Lm_list *lml, int ndx, const char *opt)
{
	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	dbg_print(lml, MSG_INTL(MSG_ARG_WLDEL), ndx, opt);
}

void
Dbg_args_file(Lm_list *lml, int ndx, char *file, Boolean ignore_stub)
{
	const char	*stub_ignore;

	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	stub_ignore = ignore_stub ?
	    MSG_INTL(MSG_ARG_IGN_STUB) : MSG_ORIG(MSG_STR_EMPTY);
	dbg_print(lml, MSG_INTL(MSG_ARG_FILE), ndx, file, stub_ignore);
}


/*
 * Report unrecognized item provided to '-z guidance' or '-z strip-class'
 * option.
 */
void
Dbg_args_unknown(Lm_list *lml, const char *item, int type)
{
	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	if (type == DBG_ARGS_GUIDANCE)
		dbg_print(lml, MSG_INTL(MSG_ARG_GUIDE_UNKNOWN), item);
	else
		dbg_print(lml, MSG_INTL(MSG_ARG_STRIP_UNKNOWN), item);
}

/*
 * Report unused '-z assert-deflib=libname' options.
 */
void
Dbg_args_unused_assert_deflib(Lm_list *lml, const char *item)
{
	if (DBG_NOTCLASS(DBG_C_ARGS))
		return;

	dbg_print(lml, MSG_INTL(MSG_ARG_UNUSED_ASDLIB), item);
}
