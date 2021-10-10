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

/*
 * Support for running a function within the given zone, waiting for its
 * result.
 */

#include <libcontract.h>
#include <libcontract_priv.h>

#include <sys/contract.h>
#include <sys/contract/process.h>
#include <sys/ctfs.h>
#include <sys/wait.h>

#include <errno.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <thread.h>
#include <fcntl.h>

#include "zoneadmd.h"

int
init_template(void)
{
	int fd;
	int err = 0;

	fd = open64(CTFS_ROOT "/process/template", O_RDWR);
	if (fd == -1)
		return (-1);

	/*
	 * For now, zoneadmd doesn't do anything with the contract.
	 * Deliver no events, don't inherit, and allow it to be orphaned.
	 */
	err |= ct_tmpl_set_critical(fd, 0);
	err |= ct_tmpl_set_informative(fd, 0);
	err |= ct_pr_tmpl_set_fatal(fd, CT_PR_EV_HWERR);
	err |= ct_pr_tmpl_set_param(fd, CT_PR_PGRPONLY | CT_PR_REGENT);
	if (err || ct_tmpl_activate(fd)) {
		(void) close(fd);
		return (-1);
	}

	return (fd);
}

/*ARGSUSED*/
static int
close_all_but_stderr(void *data, int fd)
{
	if (fd != STDERR_FILENO)
		(void) close(fd);
	return (0);
}

int
zonerun(zlog_t *zlogp, zoneid_t zoneid, int (*func)(void *), void *data,
    int timeout)
{
	int pipefds[2] = { -1, -1 };
	sigset_t old_set;
	int child_status;
	ctid_t ct = -1;
	int tmpl_fd;
	pid_t child;
	int rc;

	if (timeout != 0) {
		sigset_t unblock_set;
		(void) sigemptyset(&unblock_set);
		(void) sigaddset(&unblock_set, SIGALRM);
		(void) thr_sigsetmask(SIG_UNBLOCK, &unblock_set, &old_set);
	}

	if ((tmpl_fd = init_template()) == -1) {
		rc = errno;
		zerror(zlogp, B_TRUE, "failed to create contract");
		goto out;
	}

	if (pipe(pipefds)) {
		rc = errno;
		zerror(zlogp, B_TRUE, "failed to create pipe");
		goto out;
	}

	if ((fcntl(pipefds[0], F_SETFL,
	    fcntl(pipefds[0], F_GETFL) | O_NONBLOCK)) == -1) {
		rc = errno;
		zerror(zlogp, B_TRUE, "failed to set O_NONBLOCK");
		goto out;
	}

	(void) mutex_lock(&msglock);
	child = fork();
	(void) mutex_unlock(&msglock);

	if (child == -1) {
		rc = errno;
		(void) ct_tmpl_clear(tmpl_fd);
		(void) close(tmpl_fd);
		zerror(zlogp, B_TRUE, "failed to fork");
		goto out;
	} else if (child == 0) {
		(void) ct_tmpl_clear(tmpl_fd);

		if (pipefds[1] != STDERR_FILENO)
			(void) dup2(pipefds[1], STDERR_FILENO);
		(void) fdwalk(close_all_but_stderr, 0);

		if (zone_enter(zoneid) == -1)
			_exit(errno);

		_exit(func(data));
	}

	/* parent */
	if ((rc = contract_latest(&ct)) != 0) {
		rc = errno;
		goto out;
	}

	(void) ct_tmpl_clear(tmpl_fd);
	(void) close(tmpl_fd);

	if (timeout != 0)
		(void) alarm(timeout);

	if (waitpid(child, &child_status, 0) != child) {
		rc = errno;

		/*
		 * We might have a child process (or zombie) in the
		 * zone, so we really want to try to reap it.
		 */
		if (rc == EINTR) {
			(void) kill(child, SIGKILL);
			(void) waitpid(child, NULL, 0);
		} else {
			zerror(zlogp, B_TRUE, "waitpid() failed");
		}
	} else if (WIFSIGNALED(child_status)) {
		zerror(zlogp, B_FALSE, "child %ld was signalled: %d",
		    child, WTERMSIG(child_status));
		rc = EFAULT;
	} else {
		rc = WEXITSTATUS(child_status);

		/*
		 * If the child reported something to stderr, log it.
		 * Otherwise, assume that our caller will log any
		 * failures as necessary.
		 */
		if (rc != 0) {
			char buf[1024];
			int bytes;

			bytes = read(pipefds[0], buf, sizeof (buf) - 1);
			if (bytes > 0) {
				buf[bytes] = '\0';
				zerror(zlogp, B_FALSE, "%s", buf);
			}
		}
	}

out:
	if (timeout != 0) {
		(void) alarm(0);
		(void) thr_sigsetmask(SIG_SETMASK, &old_set, NULL);
	}

	if (ct != -1)
		(void) contract_abandon_id(ct);

	if (pipefds[0] != -1)
		(void) close(pipefds[0]);
	if (pipefds[1] != -1)
		(void) close(pipefds[1]);

	return (rc);
}
