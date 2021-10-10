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

#pragma init(init)

#include <s10_brand.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <link.h>
#include <limits.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <strings.h>

/* MAXCOMLEN is only defined in user.h in the kernel. */
#define	MAXCOMLEN	16

/* Replacement buffer for the processes' AT_SUN_EXECNAME aux vector */
static	char execname_buf[MAXPATHLEN + 1];

/*
 * This is a library that is LD_PRELOADed into native processes.
 * Its primary function is to perform one brand operation, B_S10_NATIVE,
 * which checks that this is actually a native process.  If it is, then
 * the operation changes the executable name and args so that they are no
 * longer ld.so.1 and ld's args.  Instead it changes them to be the name and
 * args of the wrapped executable that we're running.  This allows things like
 * pgrep to work as expected.  This operation also updates the AT_SUN_EXECNAME
 * aux vector to be the name of the wrapped command.  Finally, the argc,
 * argv, and argv pointers on the process are updated to match the wrapped
 * executable.  These together allow pargs -aex to function correctly.
 */
void
init(void)
{
	int i;
	Dl_argsinfo_t argsinfo;
	sysret_t rval;
	const char *s10_arg0 = NULL;
	const char *s10_execname = NULL;
	const char *s10_pcomm;
	char	pcomm_buf[MAXCOMLEN + 1];
	char	args_buf[PSARGSZ];
	auxv_t	*auxp;
	s10_proc_args_t s10_args;

	if (dlinfo(RTLD_SELF, RTLD_DI_ARGSINFO, &argsinfo) == -1)
		return;

	if ((s10_arg0 = getenv("__S10_BRAND_ARG0")) == NULL)
		return;

	if ((s10_execname = getenv("__S10_BRAND_EXECNAME")) == NULL)
		return;

	/* Determine basename of arg0 */
	if ((s10_pcomm = strrchr(s10_arg0, '/')) != NULL)
		s10_pcomm = s10_pcomm + 1;
	else
		s10_pcomm = s10_arg0;

	(void) strlcpy(pcomm_buf, s10_pcomm, sizeof (pcomm_buf));
	(void) strlcpy(execname_buf, s10_execname, sizeof (execname_buf));
	(void) strlcpy(args_buf, s10_arg0, sizeof (args_buf));

	(void) unsetenv("__S10_BRAND_ARG0");
	(void) unsetenv("__S10_BRAND_EXECNAME");

	/* Create a string of the args */
	for (i = 1; i < argsinfo.dla_argc; i++) {
		(void) strlcat(args_buf, " ", sizeof (args_buf));
		if (strlcat(args_buf, argsinfo.dla_argv[i], sizeof (args_buf))
		    >= sizeof (args_buf))
			break;
	}

	/* Fix AT_SUN_EXECNAME on stack */
	for (auxp = argsinfo.dla_auxv;
	    auxp->a_type != AT_NULL;
	    auxp++) {
		if (auxp->a_type == AT_SUN_EXECNAME) {
			auxp->a_un.a_ptr = execname_buf;
			break;
		}
	}

	/* Pass argc, argv, and envp */
	s10_args.sa_argc = argsinfo.dla_argc;
	s10_args.sa_argv = (uintptr_t)argsinfo.dla_argv;
	s10_args.sa_envp = (uintptr_t)argsinfo.dla_envp;

	/* Update the process using the brand syscall */
	(void) __systemcall(&rval, SYS_brand, B_S10_NATIVE, pcomm_buf,
	    execname_buf, args_buf, &s10_args);

}
