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
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This code is MKS code ported to Solaris originally with minimum
 * modifications so that upgrades from MKS would readily integrate.
 * The MKS basis for this modification was:
 *
 *	$Id: wordexp.c 1.22 1994/11/21 18:24:50 miked
 *
 * Additional modifications have been made to this code to make it
 * 64-bit clean.
 */

/*
 * wordexp, wordfree -- POSIX.2 D11.2 word expansion routines.
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 * Modified by Roland Mainz <roland.mainz@nrubsig.org> to support ksh93.
 */

#pragma	weak _wordexp = wordexp
#pragma	weak _wordfree = wordfree

#include "lint.h"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <alloca.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h>
#include <unistd.h>
#include <wordexp.h>
#include <stdio.h>
#include <spawn.h>
#include <errno.h>

#define	INITIAL	8		/* initial pathv allocation */
#define	BUFSZ	256		/* allocation unit of the line buffer */

/*
 * Needs no locking if fetched only once.
 * See getenv()/putenv()/setenv().
 */
extern	const char **_environ;
extern  int __xpg4;	/* defined in _xpg4.c; 0 if not xpg4-compiled program */

/* Local prototypes */
static int	append(wordexp_t *, char *);
static char	*mystpcpy(char *, const char *);

/*
 * Do word expansion.
 * We build a mini-script in |buff| which takes care of all details,
 * including stdin/stdout/stderr redirection, WRDE_NOCMD mode and
 * the word expansion itself.
 */
int
wordexp(const char *word, wordexp_t *wp, int flags)
{
	wordexp_t wptmp;
	pid_t pid;
	char *line, *eob, *cp;		/* word from shell */
	int rv = WRDE_ERRNO;
	int tmpalloc;
	int i, n, error, status;
	int pv[2];			/* pipe from shell stdout */
	size_t si;
	FILE *fp;			/* pipe read stream */
	char *wd;
	const char **env = NULL, **envp, *ev;
	posix_spawnattr_t attr;
	posix_spawn_file_actions_t fact;
	int cancel_state;
	char options[9];
	char *args[4];

	static const char *sun_path = "/usr/bin/ksh93";
	static const char *xpg4_path = "/usr/xpg4/bin/sh";
	const char *path;

	/*
	 * Do absolute minimum necessary for the REUSE flag. Eventually
	 * want to be able to actually avoid excessive malloc calls.
	 */
	if (flags & WRDE_REUSE)
		wordfree(wp);

	/*
	 * Initialize wordexp_t
	 *
	 * XPG requires that the struct pointed to by wp not be modified
	 * unless wordexp() either succeeds, or fails on WRDE_NOSPACE.
	 * So we work with wptmp, and only copy wptmp to wp if one of the
	 * previously mentioned conditions is satisfied.
	 */
	wptmp = *wp;

	/*
	 * Man page says:
	 * 2. All of the calls must set WRDE_DOOFFS, or all must not
	 *    set it.
	 * Therefore, if it's not set, we_offs will always be reset.
	 */
	if ((flags & WRDE_DOOFFS) == 0)
		wptmp.we_offs = 0;

	/*
	 * If we get APPEND|REUSE, how should we do?
	 * allocating buffer anyway to avoid segfault.
	 */
	tmpalloc = 0;
	if ((flags & WRDE_APPEND) == 0 || (flags & WRDE_REUSE)) {
		wptmp.we_wordc = 0;
		wptmp.we_wordn = wptmp.we_offs + INITIAL;
		wptmp.we_wordv = malloc(sizeof (char *) * wptmp.we_wordn);
		if (wptmp.we_wordv == NULL)
			return (WRDE_NOSPACE);
		wptmp.we_wordp = wptmp.we_wordv + wptmp.we_offs;
		for (si = 0; si < wptmp.we_offs; si++)
			wptmp.we_wordv[si] = NULL;
		tmpalloc = 1;
	}

	/*
	 * The UNIX98 Posix conformance test suite requires
	 * |wordexp()| to not be a cancellation point.
	 */
	(void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state);

	/*
	 * Make sure PWD is in the environment.
	 */
	if ((envp = _environ) == NULL) {
		/* can happen when processing a SunOS 4.x AOUT file */
		ev = NULL;
		n = 0;
	} else {
		for (n = 0; (ev = envp[n]) != NULL; n++) {
			if (*ev == 'P' && strncmp(ev, "PWD=", 4) == 0)
				break;
		}
	}
	if (ev == NULL) {	/* PWD missing from the environment */
		/* allocate a new environment */
		if ((env = malloc((n + 2) * sizeof (char *))) == NULL ||
		    (wd = malloc(PATH_MAX + 4)) == NULL)
			goto cleanup;
		for (i = 0; i < n; i++)
			env[i] = envp[i];
		(void) strcpy(wd, "PWD=");
		if (getcwd(&wd[4], PATH_MAX) == NULL)
			(void) strcpy(&wd[4], "/");
		env[i] = wd;
		env[i + 1] = NULL;
		envp = env;
	}

	if (__xpg4) {
		/*
		 * Turn flags into shell options
		 */
		i = 0;
		options[i++] = '-';
		options[i++] = (char)0x05;	/* ksh -^E */
		if (flags & WRDE_UNDEF)
			options[i++] = 'u';
		if (flags & WRDE_NOCMD)
			options[i++] = 'N';
		options[i] = '\0';

		path = xpg4_path;
		args[0] = strrchr(path, '/') + 1;
		args[1] = options;
		args[2] = (char *)word;
		args[3] = NULL;
	} else {
		size_t bufflen; /* Length of |buff| */
		char *buff;
		char *currbuffp; /* Current position of '\0' in |buff| */

		/*
		 * Calculate size of required buffer (which is size of the
		 * input string (|word|) plus all string literals below;
		 * this value MUST be adjusted each time the literals are
		 * changed!!).
		 */
		bufflen = 165 + strlen(word);
		buff = alloca(bufflen);
		i = 0;

		/* Start filling the buffer */
		buff[0] = '\0';
		currbuffp = buff;

		if (flags & WRDE_UNDEF)
			currbuffp = mystpcpy(currbuffp, "set -o nounset\n");
		if ((flags & WRDE_SHOWERR) == 0) {
			/*
			 * The newline ('\n') is neccesary to make sure that
			 * the redirection to /dev/null is already active in
			 * the case the printf below contains a syntax
			 * error...
			 */
			currbuffp = mystpcpy(currbuffp, "exec 2>/dev/null\n");
		}
		/* Squish stdin */
		currbuffp = mystpcpy(currbuffp, "exec 0</dev/null\n");

		if (flags & WRDE_NOCMD) {
			/*
			 * Switch to restricted shell (rksh) mode here to
			 * put the word expansion into a "cage" which
			 * prevents users from executing external commands
			 * (outside those listed by ${PATH} (which we set
			 * explicitly to /usr/no/such/path/element/)).
			 */
			currbuffp = mystpcpy(currbuffp,
			    "export PATH=/usr/no/such/path/element/ ; "
			    "set -o restricted\n");
		}

		(void) snprintf(currbuffp, bufflen,
		    "print -f '%%s\\000' -- %s", word);

		path = sun_path;
		args[0] = strrchr(path, '/') + 1;
		args[1] = "-c";
		args[2] = buff;
		args[3] = NULL;
	}

	if ((error = posix_spawnattr_init(&attr)) != 0) {
		errno = error;
		goto cleanup;
	}
	if ((error = posix_spawn_file_actions_init(&fact)) != 0) {
		(void) posix_spawnattr_destroy(&attr);
		errno = error;
		goto cleanup;
	}

	/*
	 * Set up pipe from shell stdout to "fp" for us
	 */
	if (pipe(pv) < 0) {
		error = errno;
		(void) posix_spawnattr_destroy(&attr);
		(void) posix_spawn_file_actions_destroy(&fact);
		errno = error;
		goto cleanup;
	}

	/*
	 * Spawn shell
	 */
	error = posix_spawnattr_setflags(&attr,
	    POSIX_SPAWN_NOSIGCHLD_NP | POSIX_SPAWN_WAITPID_NP);
	if (error == 0)
		error = posix_spawn_file_actions_adddup2(&fact, pv[1], 1);
	if (error == 0 && pv[0] != 1)
		error = posix_spawn_file_actions_addclose(&fact, pv[0]);
	if (error == 0 && pv[1] != 1)
		error = posix_spawn_file_actions_addclose(&fact, pv[1]);
	if (error == 0 && !(flags & WRDE_SHOWERR))
		error = posix_spawn_file_actions_addopen(&fact, 2,
		    "/dev/null", O_WRONLY, 0);

	if (error == 0)
		error = posix_spawn(&pid, path, &fact, &attr,
		    (char *const *)args, (char *const *)envp);
	(void) posix_spawnattr_destroy(&attr);
	(void) posix_spawn_file_actions_destroy(&fact);
	(void) close(pv[1]);
	if (error) {
		(void) close(pv[0]);
		errno = error;
		goto cleanup;
	}

	if ((fp = fdopen(pv[0], "rF")) == NULL) {
		error = errno;
		(void) close(pv[0]);
		errno = error;
		goto wait_cleanup;
	}

	/*
	 * Read words from shell, separated with '\0'.
	 * Since there is no way to disable IFS splitting,
	 * it would be possible to separate the output with '\n'.
	 */
	cp = line = malloc(BUFSZ);
	if (line == NULL) {
		error = errno;
		(void) fclose(fp);
		errno = error;
		goto wait_cleanup;
	}
	eob = line + BUFSZ;

	rv = 0;
	flockfile(fp);
	while ((i = getc_unlocked(fp)) != EOF) {
		*cp++ = (char)i;
		if (i == '\0') {
			cp = line;
			if ((rv = append(&wptmp, cp)) != 0) {
				break;
			}
		}
		if (cp == eob) {
			size_t bs = (eob - line);
			char *nl;

			if ((nl = realloc(line, bs + BUFSZ)) == NULL) {
				rv = WRDE_NOSPACE;
				break;
			}
			line = nl;
			cp = line + bs;
			eob = cp + BUFSZ;
		}
	}
	funlockfile(fp);

	wptmp.we_wordp[wptmp.we_wordc] = NULL;

	free(line);
	(void) fclose(fp);	/* kill shell if still writing */

wait_cleanup:
	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			if (rv == 0)
				rv = WRDE_ERRNO;
			break;
		}
	}
	if (rv == 0)
		rv = WEXITSTATUS(status); /* shell WRDE_* status */

cleanup:
	if (rv == 0)
		*wp = wptmp;
	else if (tmpalloc)
		wordfree(&wptmp);

	if (env) {
		free(env);
		free(wd);
	}

	if (__xpg4) {
		/*
		 * Map xpg4-sh errors to wordexp() errors
		 */
		if (rv == 4)
			rv = WRDE_CMDSUB;
		else if (rv == 5)
			rv = WRDE_BADVAL;
		else if (rv == 6)
			rv = WRDE_SYNTAX;
	} else {
		/*
		 * Map ksh93 errors to |wordexp()| errors
		 */
		if (rv == 1)
			rv = WRDE_BADVAL;
		else if (rv == 127)
			rv = WRDE_BADCHAR;
	}

	(void) pthread_setcancelstate(cancel_state, NULL);
	return (rv);
}

/*
 * Free all space owned by wordexp_t.
 */
void
wordfree(wordexp_t *wp)
{
	size_t i;

	if (wp->we_wordv == NULL)
		return;
	for (i = wp->we_offs; i < wp->we_offs + wp->we_wordc; i++)
		free(wp->we_wordv[i]);
	free((void *)wp->we_wordv);
	wp->we_wordc = 0;
	wp->we_wordv = NULL;
}

/*
 * Append a word to the wordexp_t structure, growing it as necessary.
 */
static int
append(wordexp_t *wp, char *str)
{
	char *cp;
	char **nwp;

	/*
	 * We will be adding one entry and later adding
	 * one more NULL. So we need 2 more free slots.
	 */
	if ((wp->we_wordp + wp->we_wordc) ==
	    (wp->we_wordv + wp->we_wordn - 1)) {
		nwp = realloc(wp->we_wordv,
		    (wp->we_wordn + INITIAL) * sizeof (char *));
		if (nwp == NULL)
			return (WRDE_NOSPACE);
		wp->we_wordn += INITIAL;
		wp->we_wordv = nwp;
		wp->we_wordp = wp->we_wordv + wp->we_offs;
	}
	if ((cp = strdup(str)) == NULL)
		return (WRDE_NOSPACE);
	wp->we_wordp[wp->we_wordc++] = cp;
	return (0);
}

/*
 * |mystpcpy| - like |strcpy()| but returns the end of the buffer
 * We'll add this later (and a matching multibyte/widechar version)
 * as normal libc function.
 *
 * Copy string s2 to s1.  s1 must be large enough.
 * return s1-1 (position of string terminator ('\0') in destination buffer).
 */
static char *
mystpcpy(char *s1, const char *s2)
{
	while (*s1++ = *s2++)
		;
	return (s1-1);
}
