/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: sem_method.c,v $ $Revision: 1.1.4.2 $"
 *	" (OSF) $Date: 1992/10/06 15:21:11 $";
 * #endif
 */

/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */
#include <dlfcn.h>
#include "locdef.h"

/*
 * array containing all the libraries
 * specified in methods source file
 */
library_t	lib_array[LAST_METHOD + 1];
int user_specified_libc = FALSE;

static int	has_set_at_native_method = 0;

static char *
grab(int *n, int want, char **previous, const char *def)
/*
 * ARGUMENTS:
 *	n		# of values still on the stack
 *	want		which value we want.  If want > n, use previous
 *	previous	Remembers old values
 */
{
	item_t	*it;
	char	*str;

	if (*n == want) {
		it = sem_pop();		/* Pop top and reduce count */
		*n -= 1;

		if (!it || it->type != SK_STR) {
			INTERNAL_ERROR;	/* Should _never_ happen */
		}

		str = STRDUP(it->value.str);
		destroy_item(it);

		if (*previous) {
			free(*previous);
		}
		*previous = STRDUP(str); /* This is NEW previous */
	} else {
		if (*previous) {
			str = STRDUP(*previous);
		} else {
			str = STRDUP(def);
		}
	}

	return (str);
}

/*
 *  FUNCTION: set_method
 *
 *  DESCRIPTION:
 * Used to parse the information in the methods file. An index into
 * the std_methods array is passed to this routine from the grammer.
 * The number of items is also passed (1 means just the c_symbol is
 * passed, the package and library name is inherited from previous and 2 means
 * the c_symbol and package name is passed. 3 means everything present.
 */
void
set_method(int index, int number)
{
	char *sym, *lib_dir, *pkg, *lib_name;
	static char *lastlib = NULL;
	static char	*lastlib_name = NULL;
	static char	*lastlib_dir = NULL;
	static char *lastpkg = NULL;
	static char	*lastsym = NULL;
	static int	oldfmt = 0;
	int i;
	char	*lib, *lib64;
	size_t	dirlen, namelen, mach64len, len, len64;


	if (ISNATIVE(index)) {
		/* try to set the native method */
		has_set_at_native_method = 1;
	}

	/* get the strings for the new method name and the library off  */
	/* of the stack */

	if (number == 3 || oldfmt == 1) {
		lib_dir  = grab(&number, 3, &lastlib,
			DEFAULT_METHOD);
		pkg = grab(&number, 2, &lastpkg,
			"libc");
		sym = grab(&number, 1, &lastsym,
			"");
		oldfmt = 1;
	} else {
		lib_name = grab(&number, 4, &lastlib_name,
			DEFAULT_METHOD_NAM);
		lib_dir  = grab(&number, 3, &lastlib_dir,
			DEFAULT_METHOD_DIR);
		pkg = grab(&number, 2, &lastpkg,
			"libc");
		sym = grab(&number, 1, &lastsym,
			"");
	}

	if (oldfmt == 1) {
		/* old format */
		/* lib_dir contains whole library pathname */
		lib = MALLOC(char, strlen(lib_dir) + 1);
		(void) strcpy(lib, lib_dir);
		lib64 = lib;
		free(lib_dir);
	} else {
		dirlen = strlen(lib_dir);
		namelen = strlen(lib_name);
		mach64len = strlen(MACH64);
		len = dirlen + namelen + 2;
		len64 = dirlen + mach64len + namelen + 2;
		lib = MALLOC(char, len);
		lib64 = MALLOC(char, len64);
		if (*(lib_dir + dirlen - 1) != '/') {
			(void) snprintf(lib, len, "%s/%s", lib_dir, lib_name);
			(void) snprintf(lib64, len64, "%s/%s%s",
			    lib_dir, MACH64, lib_name);
		} else {
			(void) snprintf(lib, len, "%s%s", lib_dir, lib_name);
			(void) snprintf(lib64, len64, "%s%s%s",
			    lib_dir, MACH64, lib_name);
		}
		free(lib_name);
		free(lib_dir);
	}

	for (i = 0; i <= LAST_METHOD; i++) {
	    if (lib_array[i].library == NULL) {
			/* Reached an empty slot, add lib */
			lib_array[i].library = lib;
			lib_array[i].library64 = lib64;
			break;
	    } else if (strcmp(lib_array[i].library, lib) == 0) {
			/* Found it already on our list */
			free(lib);
			if (oldfmt == 0) {
				free(lib64);
			}
			lib = lib_array[i].library;
			lib64 = lib_array[i].library64;
			break;
	    }
	    /* Keep looking */
	}

	/* add the info to the std_methods table */

	if (method_class == USR_CODESET) {
		if (METH_NAME(index))
			free(METH_NAME(index));
		if (METH_PKG(index))
			free(METH_PKG(index));
	}
	METH_NAME(index) = sym;
	METH_LIB(index) = lib;
	METH_LIB64(index) = lib64;
	METH_PKG(index) = pkg;

	/*
	 * if the user's extension file uses "libc" as a package name
	 * then we'll assume the user has specified a libc so we
	 * don't have to specify one.
	 * if the user doesn't specify a libc then we must add a -lc
	 * to our c compile line.
	 */
	if (strcmp(pkg, "libc") == 0)
	    user_specified_libc = TRUE;

	/*
	 * invalidate preset values to allow the user specified method
	 * to be loaded
	 */
	if (index == CHARMAP_MBTOWC)
		METH_OFFS(index) = (int (*)(void))NULL;

}




/*
 *  FUNCTION: load_method
 *
 *  DESCRIPTION:
 *	Load a method from a shared library.
 *  INPUTS
 *	idx	index into std_methods of entry to load.
 */

static void
load_method(int idx)
{
	char		*sym = std_methods[idx].c_symbol[method_class];
	char		*lib = std_methods[idx].lib_name[method_class];
	int		(*q)(void);			/* Function handle */
	void		*library_handle;

	/*
	 * Load the module to make certain that the referenced package is
	 * in our address space.
	 */

	library_handle = dlopen(lib, RTLD_LAZY);
	if (library_handle == (void *)NULL) {
		perror("localedef");
		error(4, gettext(ERR_LOAD_FAIL), sym, lib);
	}

	q = (int(*)(void))dlsym(library_handle, sym);

	if (q == (int(*)(void))NULL) {
		(void) dlclose(library_handle);
		error(4, gettext(ERR_LOAD_FAIL), sym, lib);
	}

	std_methods[idx].instance[method_class] = q;
}


/*
 *  FUNCTION: check_methods
 *
 *  DESCRIPTION:
 *  There are certain methods that do not have defaults because they are
 *  dependent on the process code and file code relationship. These methods
 *  must be specified by the user if they specify any new methods at all
 *
 */

void
check_methods(void)
{
	int MustDefine[] = { CHARMAP_MBFTOWC, CHARMAP_FGETWC,
		CHARMAP_MBLEN,
		CHARMAP_MBSTOWCS, CHARMAP_MBTOWC, CHARMAP_WCSTOMBS,
		CHARMAP_WCSWIDTH, CHARMAP_WCTOMB, CHARMAP_WCWIDTH,
		CHARMAP_EUCPCTOWC, CHARMAP_WCTOEUCPC,
		CHARMAP_BTOWC, CHARMAP_WCTOB, CHARMAP_MBRLEN,
		CHARMAP_MBRTOWC, CHARMAP_WCRTOMB,
		CHARMAP_MBSRTOWCS, CHARMAP_WCSRTOMBS };
	int MustDefine_at_native[] = { CHARMAP_MBFTOWC_AT_NATIVE,
		CHARMAP_FGETWC_AT_NATIVE,
		CHARMAP_MBLEN,
		CHARMAP_MBSTOWCS_AT_NATIVE,
		CHARMAP_MBTOWC_AT_NATIVE,
		CHARMAP_WCSTOMBS_AT_NATIVE,
		CHARMAP_WCSWIDTH_AT_NATIVE,
		CHARMAP_WCTOMB_AT_NATIVE, CHARMAP_WCWIDTH_AT_NATIVE,
		CHARMAP_BTOWC_AT_NATIVE, CHARMAP_WCTOB_AT_NATIVE,
		CHARMAP_MBRTOWC_AT_NATIVE, CHARMAP_WCRTOMB_AT_NATIVE,
		CHARMAP_MBSRTOWCS_AT_NATIVE, CHARMAP_WCSRTOMBS_AT_NATIVE };
	int j;
	int current_codeset;

	/*
	 * If no methods function has been specified but WIDTH or
	 * WIDTH_DEFAULT keyword has been specified in the charmap file,
	 * needs to overwrite the some default method by the appropriate
	 * ones.
	 */
	if (method_class != USR_CODESET && width_flag) {
		int	idx;
		ow_method_t	*p = ow_methods;

		while ((idx = p->method_index) <= LAST_METHOD) {
			std_methods[idx].c_symbol[method_class] =
			    p->c_symbol[method_class];
			std_methods[idx].instance[method_class] =
			    p->instance[method_class];
			p++;
		}
	}

	/*
	 * If locale is single layer, the entry for the native method
	 * contains both the user and native method API (they should be
	 * same).
	 */
	for (j = 0; j < (sizeof (MustDefine_at_native) / sizeof (int)); j++) {
		int idx = MustDefine_at_native[j];

		if (std_methods[idx].instance[method_class] == NULL) {
		/*
		 * Need to load a method to handle this operation
		 */
			if (!std_methods[idx].c_symbol[method_class]) {
		/*
		 * Did not get a definition for this method, and we need
		 * it to be defined
		 */
				diag_error(gettext(ERR_METHOD_REQUIRED),
				    std_methods[idx].method_name);
			} else {
		/*
		 * load the shared module and fill in the table slot
		 */
				load_method(idx);
			}
		}
	}
	if (single_layer == FALSE) {
		for (j = 0; j < (sizeof (MustDefine) / sizeof (int)); j++) {
			int idx = MustDefine[j];

			if (std_methods[idx].instance[method_class] == NULL) {
		/*
		 * Need to load a method to handle this operation
		 */
				if (!std_methods[idx].c_symbol[method_class]) {
		/*
		 * Did not get a definition for this method, and we need
		 * it to be defined
		 */
					diag_error(
					    gettext(ERR_METHOD_REQUIRED),
						std_methods[idx].method_name);
				} else {
		/*
		 * load the shared module and fill in the table slot
		 */
					load_method(idx);
				}
			}
		}
	}

/*
 * Finally, run thru the remaining methods and copy from the *_CODESET
 * that makes sense based on what mb_cur_max is set to.
 * (which has all "standard" methods for the remaining non-null entries.
 * We only need the symbolic info for these methods, not the function pointers
 */

	if (mb_cur_max == 1)
		current_codeset = SB_CODESET;
	else
		current_codeset = MB_CODESET;

	for (j = 0; j <= LAST_METHOD; j++) {

		if (std_methods[j].c_symbol[method_class] == NULL) {
		/*
		 * Still not set up?
		 */
			std_methods[j].c_symbol[method_class] =
				std_methods[j].c_symbol[current_codeset];
			std_methods[j].package[method_class] =
				std_methods[j].package[current_codeset];
			std_methods[j].lib_name[method_class] =
				std_methods[j].lib_name[current_codeset];
			std_methods[j].lib64_name[method_class] =
				std_methods[j].lib64_name[current_codeset];

		}
	}
}

void
check_layer(void)
{
	int	u_idx, n_idx;

	if (has_set_at_native_method) {
		/*
		 * native method has been set.
		 * But, if each native method of all is the same as the
		 * corresponding user method, treat it as single layer.
		 */
		char	*u_name, *n_name;

		for (u_idx = USER_API_START; u_idx <= USER_API_END; u_idx++) {
			n_idx = TONATIVE(u_idx);
			u_name = METH_NAME(u_idx);
			n_name = METH_NAME(n_idx);
			if (u_name && n_name) {
				if (strcmp(u_name, n_name) != 0) {
					/* not a single layer */
					return;
				}
			}
		}
	}
	/*
	 * Actually, no native method has been set.
	 * This utf8 locale should be single layer.
	 * Need to move each user API setting to
	 * the correspoinding native API entry.
	 */
	single_layer = TRUE;

	for (u_idx = USER_API_START; u_idx <= USER_API_END; u_idx++) {
		n_idx = TONATIVE(u_idx);
		if (METH_NAME(n_idx))
			free(METH_NAME(n_idx));
		METH_NAME(n_idx) = METH_NAME(u_idx);
		if (METH_PKG(n_idx))
			free(METH_PKG(n_idx));
		METH_PKG(n_idx) = METH_PKG(u_idx);

		METH_LIB(n_idx) = METH_LIB(u_idx);
		METH_LIB64(n_idx) = METH_LIB64(u_idx);
	}
}
