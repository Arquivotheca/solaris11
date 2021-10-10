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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#include	<link.h>
#include	"msg.h"
#include	"_debug.h"
#include	"libld.h"

void
Dbg_libs_audit(Lm_list *lml, const char *opath, const char *npath)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	if (npath == opath)
		return;

	if (npath == NULL)
		dbg_print(lml, MSG_INTL(MSG_LIB_SKIP), opath);
	else
		dbg_print(lml, MSG_INTL(MSG_LIB_ALTER), npath);
}

void
Dbg_libs_find(Lm_list *lml, const char *name)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	Dbg_util_nl(lml, DBG_NL_STD);
	dbg_print(lml, MSG_INTL(MSG_LIB_FIND), name);
}

void
Dbg_libs_found(Lm_list *lml, const char *path, int alter)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_TRYING), path, alter ?
	    MSG_INTL(MSG_STR_ALTER) : MSG_ORIG(MSG_STR_EMPTY));
}

void
Dbg_libs_insecure(Lm_list *lml, const char *path, int usable)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	if (usable)
		dbg_print(lml, MSG_INTL(MSG_LIB_INUSE), path);
	else
		dbg_print(lml, MSG_INTL(MSG_LIB_IGNORE), path);
}

static void
Dbg_lib_dir_print(Lm_list *lml, APlist *libdir)
{
	Aliste	idx;
	char	*cp;

	for (APLIST_TRAVERSE(libdir, idx, cp))
		dbg_print(lml, MSG_ORIG(MSG_LIB_FILE), cp);
}

void
Dbg_libs_init(Lm_list *lml, APlist *ulibdir, APlist *dlibdir)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_INITPATH));
	Dbg_lib_dir_print(lml, ulibdir);
	Dbg_lib_dir_print(lml, dlibdir);
}

void
Dbg_libs_l(Lm_list *lml, const char *name, const char *path)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_LOPT), name, path);
}

void
Dbg_libs_path(Lm_list *lml, const char *path, uint_t orig, const char *obj)
{
	const char	*fmt;
	uint_t		search;

	if (path == NULL)
		return;
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	search = orig &
	    (LA_SER_LIBPATH | LA_SER_RUNPATH | LA_SER_DEFAULT | LA_SER_SECURE);

	switch (search) {
	case LA_SER_LIBPATH:
		if (orig & LA_SER_CONFIG)
			fmt = MSG_INTL(MSG_LIB_LDLIBPATHC);
		else
			fmt = MSG_INTL(MSG_LIB_LDLIBPATH);
		break;

	case LA_SER_RUNPATH:
		fmt = MSG_INTL(MSG_LIB_RUNPATH);
		break;

	case LA_SER_DEFAULT:
		if (orig & LA_SER_CONFIG)
			fmt = MSG_INTL(MSG_LIB_DEFAULTC);
		else
			fmt = MSG_INTL(MSG_LIB_DEFAULT);
		break;

	case LA_SER_SECURE:
		if (orig & LA_SER_CONFIG)
			fmt = MSG_INTL(MSG_LIB_TDEFAULTC);
		else
			fmt = MSG_INTL(MSG_LIB_TDEFAULT);
		break;

	default:
		return;
	}

	dbg_print(lml, fmt, path, obj);
}

void
Dbg_libs_req(Lm_list *lml, const char *so_name, const char *ref_file,
    const char *name)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_REQUIRED), so_name, name, ref_file);
}

void
Dbg_libs_update(Lm_list *lml, APlist *ulibdir, APlist *dlibdir)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_UPPATH));
	Dbg_lib_dir_print(lml, ulibdir);
	Dbg_lib_dir_print(lml, dlibdir);
}

void
Dbg_libs_yp(Lm_list *lml, const char *path)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_LIBPATH), path);
}

void
Dbg_libs_ylu(Lm_list *lml, const char *path, const char *orig, int index)
{
	if (DBG_NOTCLASS(DBG_C_LIBS))
		return;

	dbg_print(lml, MSG_INTL(MSG_LIB_YPATH), path, orig,
	    (index == YLDIR) ? 'L' : 'U');
}
