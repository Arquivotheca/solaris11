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

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include "msg.h"
#include "_libld.h"

/*
 * -z assert-deflib support.
 *
 * syntax:
 *	-z assert-deflib	(enable default lib assertions)
 *	-z assert-deflib=lib	(enable default lib assertions, register lib)
 *
 * where lib is the final path component of the library file. This
 * typically corresponds to the compilation symlink for a shared object
 * (e.g. libc.so).
 *
 * When processing a -l command line option, the link-editor first
 * examines the locations given by the user via -L options, and then
 * falls back to look at default locations that are supplied by the
 * link-editor (or via the -Y command line options).
 *
 * When assert-deflib is used, the link-editor checks that any library
 * found in the default locations has a corresponding assert-deflib
 * option to explicitly declare it. Otherwise, a warning is issued.
 *
 * This is a specialized option, primarily of interest in build environments
 * where multiple objects with the same name exist and tight control over
 * the library used is required. This feature is used to build the core
 * Solaris OS.
 */


typedef struct {
	avl_node_t	adl_avlnode;
	const char	*adl_name;	/* Library name */
	Boolean		adl_used;	/* TRUE if library is found */
} assert_deflib_t;

/*
 * AVL comparison function for assert_deflib_t items.
 *
 * entry:
 *	n1, n2 - pointers to nodes to be compared
 *
 * exit:
 *	Returns -1 if (n1 < n2), 0 if they are equal, and 1 if (n1 > n2)
 */
static int
assert_deflib_cmp(const void *n1, const void *n2)
{
	int		rc;

	rc = strcmp(((assert_deflib_t *)n1)->adl_name,
	    ((assert_deflib_t *)n2)->adl_name);

	if (rc > 0)
		return (1);
	if (rc < 0)
		return (-1);
	return (0);
}

/*
 * Parse a '-z assert-deflib' argument and update the ofl_asdeflib tree.
 *
 * entry:
 *	ofl - Output file descriptor
 *	optarg - option value to be processed. This is the string following the
 *              option 'assert-deflib', and is either a '\0' termination, or a
 *		'=' followed by guidance values.
 *
 * exit:
 *	Returns LD_TRUE if a new value is processed, or if a value already
 *	exists, LD_FALSE if the value if the path contains '/'.  Both of these
 *	are considered a success by the caller.  LD_ERROR is returned for a
 *	fatal, allocation, error
 */
Ld_ret
ld_assert_deflib_enter(Ofl_desc *ofl, const char *optarg)
{
	assert_deflib_t	*adlp, adl;
	avl_index_t	where;

	/* If this is the first -z assert-deflib option, create the AVL tree */
	if (ofl->ofl_asdeflib == NULL) {
		ofl->ofl_asdeflib =
		    libld_calloc(1, sizeof (*ofl->ofl_asdeflib));
		if (ofl->ofl_asdeflib == NULL)
			return (LD_ERROR);
		avl_create(ofl->ofl_asdeflib, assert_deflib_cmp,
		    sizeof (assert_deflib_t),
		    SGSOFFSETOF(assert_deflib_t, adl_avlnode));
	}

	/* If no lib is present, we're done */
	if (optarg[0] == '\0')
		return (0);

	optarg++;		/* skip '=' */

	/* library name should not include any path components (i.e. no '/') */
	if (strchr(optarg, '/') != NULL) {
		ld_eprintf(ofl, ERR_FATAL, MSG_INTL(MSG_ARG_BADASDEFLIB),
		    optarg);
		return (LD_FALSE);
	}

	/* Have we already entered this one? */
	adl.adl_name = optarg;
	if ((adlp = avl_find(ofl->ofl_asdeflib, &adl, &where)) != NULL)
		return (LD_TRUE);

	/* Allocate a new node */
	if ((adlp = libld_calloc(1, sizeof (*adlp))) == NULL)
		return (LD_ERROR);
	adlp->adl_name = optarg;
	adlp->adl_used = FALSE;

	/* Insert the new node */
	avl_insert(ofl->ofl_asdeflib, adlp, where);
	return (LD_TRUE);
}

/*
 * Test the given path against the assert-deflib AVL tree, and issue a
 * warning if the tree exists and the library does not have an assertion.
 *
 * entry:
 *	ofl - Output file descriptor
 *	path - Path of library file to test.
 *	defpath - TRUE if library was found via a default path, and
 *		FALSE if it was found on a non-default path.
 *
 * exit:
 *	On success, marks the assertion record so that ld_assert_deflib_finish()
 *	will see that the assertion was used, and returns quietly.
 *
 *	On failure, issues a warning.
 */
void
ld_assert_deflib_test(Ofl_desc *ofl, const char *path, Boolean defpath)
{
	assert_deflib_t	*adlp, adl;
	const char	*lib;

	/* If there's no AVL tree, the -z assert-deflib feature is not in use */
	if (ofl->ofl_asdeflib == NULL)
		return;

	/* The final path component is the library name */
	lib = strrchr(path, '/');
	if (lib == NULL)
		lib = path;	/* no path components so use as is */
	else
		lib++;		/* skip the '/' */

	/* Is there an assertion? */
	adl.adl_name = lib;
	if ((adlp = avl_find(ofl->ofl_asdeflib, &adl, NULL)) != NULL) {
		adlp->adl_used = TRUE;

		/* If library found on non-default path, issue warning */
		if (!defpath)
			ld_eprintf(ofl, ERR_WARNING,
			    MSG_INTL(MSG_FIL_ASDEFLIB_NDP), path, lib);

		return;
	}

	/* Complain about object from default location without assertion */
	if (defpath)
		ld_eprintf(ofl, ERR_WARNING, MSG_INTL(MSG_FIL_ASDEFLIB_DP),
		    path, lib);
}

/*
 * Ensure that every entry in the ofl_asdeflib avl tree was used.
 * Otherwise, provide debug output pointing it out.
 *
 * entry:
 *	ofl - Output file descriptor
 *
 * exit:
 *	Returns TRUE on success, and FALSE on failure.
 */
void
ld_assert_deflib_finish(Ofl_desc *ofl)
{
	assert_deflib_t	*adlp;

	if (!DBG_ENABLED|| (ofl->ofl_asdeflib == NULL) ||
	    (ofl->ofl_dtflags_1 & DF_1_STUB))
		return;

	for (adlp = avl_first(ofl->ofl_asdeflib); adlp != NULL;
	    adlp = AVL_NEXT(ofl->ofl_asdeflib, adlp))
		if (adlp->adl_used == FALSE)
			DBG_CALL(Dbg_args_unused_assert_deflib(ofl->ofl_lml,
			    adlp->adl_name));
}
