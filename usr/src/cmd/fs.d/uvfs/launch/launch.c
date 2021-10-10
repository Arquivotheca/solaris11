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

#include <libuvfs.h>

#include <unistd.h>
#include <libintl.h>
#include <signal.h>

int
main(void)
{
	libuvfs_fs_t *fs = libuvfs_create_fs(LIBUVFS_VERSION, LIBUVFS_FSID_SVC);
	const char *path = libuvfs_get_daemon_executable(fs);
	pid_t daemon;
	int rc = SMF_EXIT_OK;

	if (! libuvfs_is_daemon()) {
		(void) fprintf(stderr,
		    gettext("not part of an SMF service; must only be "
		    "executed under SMF\n"));
		rc = SMF_EXIT_ERR_NOSMF;
		goto out;
	}

	if (path == NULL) {
		(void) fprintf(stderr,
		    gettext("filesys/daemon property not configured\n"));
		rc = SMF_EXIT_ERR_CONFIG;
		goto out;
	}

	daemon = fork1();
	if (daemon == -1) {
		perror(gettext("fork"));
		rc = SMF_EXIT_ERR_FATAL;
		goto out;
	}
	if (daemon == 0) {
		(void) execlp(path, path, NULL);
		perror(path);
		rc = SMF_EXIT_ERR_FATAL;
		goto out;
	}

	if (libuvfs_daemon_start_wait(fs, LIBUVFS_DAEMON_WAIT_OTHER) != 0) {
		(void) kill(daemon, SIGABRT);
		perror(path);
		(void) atexit(libuvfs_daemon_atexit);
	}

out:
	if (fs != NULL)
		libuvfs_destroy_fs(fs);
	return (rc);
}
