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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	All Rights Reserved	*/

#include <atomic.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <locale.h>
#include <libintl.h>
#include <libscf.h>

#define	TZSYNC_FILE	_PATH_SYSVOL "/tzsync"

static int	init_file(void);
static int	do_update(int);
static void	refresh_cron_svc(void);

/*
 * There are undocumented command line options:
 * -l	list the value of semaphore.
 * -I	initialize the semaphore file (ie _PATH_SYSVOL/tzsync)
 */

int
main(int argc, char **argv)
{
	int	arg;
	int	get = 0, init = 0;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((arg = getopt(argc, argv, "lI")) != EOF) {
		switch (arg) {
		case 'l':
			get = 1;
			break;
		case 'I':
			init = 1;
			break;
		default:
			(void) fprintf(stderr, gettext("Usage: tzreload\n"));
			return (2);
		}
	}

	if (init)
		return (init_file());

	return (do_update(get));
}

/*
 * Create _PATH_SYSVOL/tzsync atomically.
 *
 * While creating the _PATH_SYSVOL/tzsync initially, there is a timing window
 * that the file is created but no disk block is allocated (empty file).
 * If apps mmap'ed the file at the very moment, it succeeds but accessing
 * the memory page causes a segfault since disk block isn't yet allocated.
 * To avoid this situation, we create a temp file which has pagesize block
 * assigned, and then rename it to tzsync.
 */
static int
init_file(void)
{
	char	path[MAXPATHLEN];
	char	*buf;
	int	fd;
	long	pgsz;

	/*
	 * If the file exists, then proceed to simply update it as if we were
	 * called without -I.  This allows one to run tzreload -I even if it
	 * has already been run, and to still cause a "kick" to running apps.
	 * This allows the timezoneinfo-cache SMF to simply run tzreload -I
	 * in its start method, and allows a user to start, restart or refresh
	 * that service to cause timezone information to update.
	 */
	if (access(TZSYNC_FILE, F_OK) == 0)
		return (do_update(0));

	pgsz = sysconf(_SC_PAGESIZE);

	(void) strcpy(path, TZSYNC_FILE "XXXXXX");
	if ((fd = mkstemp(path)) == -1) {
		(void) fprintf(stderr,
		    gettext("failed to create a temporary file.\n"));
		return (1);
	}

	if ((buf = calloc(1, pgsz)) == NULL) {
		(void) fprintf(stderr, gettext("Insufficient memory.\n"));
		(void) close(fd);
		(void) unlink(path);
		return (1);
	}

	if (write(fd, buf, pgsz) != pgsz) {
		(void) fprintf(stderr, gettext("Failed to create %s: %s\n"),
		    TZSYNC_FILE, strerror(errno));
		(void) close(fd);
		(void) unlink(path);
		return (1);
	}
	(void) close(fd);

	/* link it */
	if (link(path, TZSYNC_FILE) != 0) {
		if (errno == EEXIST) {
			(void) fprintf(stderr, gettext("%s already exists.\n"),
			    TZSYNC_FILE);
		} else {
			(void) fprintf(stderr, gettext("failed to create %s\n"),
			    TZSYNC_FILE);
		}
		(void) unlink(path);
		return (1);
	}
	(void) unlink(path);

	/*
	 * Unprivileged apps may fail to open the file until the chmod
	 * below succeeds. However, it's okay as long as open() fails;
	 * ctime() won't cache zoneinfo until file is opened and mmap'd.
	 */

	/* _PATH_SYSVOL/tzsync has been made. Adjust permission */
	if (chmod(TZSYNC_FILE, 0644) != 0) {
		(void) fprintf(stderr,
		    gettext("failed to change permission of %s\n"),
		    TZSYNC_FILE);
		(void) unlink(TZSYNC_FILE);
		return (1);
	}
	return (0);
}

/*
 * Open the _PATH_SYSVOL/tzsync, then set or get the semaphore.
 *
 * get		get/set semaphore
 */
static int
do_update(int get)
{
	int	fd, prot, mode;
	uint32_t counter;
	caddr_t addr;

	mode = get ? O_RDONLY : O_RDWR;
	prot = get ? PROT_READ : PROT_READ|PROT_WRITE;

	if ((fd = open(TZSYNC_FILE, mode)) < 0) {
		(void) fprintf(stderr, gettext("Can't open %s: %s\n"),
		    TZSYNC_FILE, strerror(errno));
		return (1);
	}

	addr = mmap(NULL, sizeof (uint32_t), prot, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		(void) fprintf(stderr, gettext("Error mapping semaphore: %s\n"),
		    strerror(errno));
		(void) close(fd);
		return (1);
	}

	if (get) {
		counter = *(uint32_t *)(uintptr_t)addr;
		(void) munmap(addr, sizeof (uint32_t));
		(void) printf("%u\n", counter);
	} else {	/* update the counter, refresh cron */
		/*LINTED*/
		atomic_add_32((uint32_t *)addr, 1);
		(void) munmap(addr, sizeof (uint32_t));
		refresh_cron_svc();
	}
	(void) close(fd);
	return (0);
}

static void
refresh_cron_svc(void)
{
	int err;
	err = smf_refresh_instance("svc:/system/cron:default");
	if (err != 0) {
		(void) fprintf(stderr, "Could not refresh cron service: %s",
		    scf_strerror(scf_error()));
	}
}
