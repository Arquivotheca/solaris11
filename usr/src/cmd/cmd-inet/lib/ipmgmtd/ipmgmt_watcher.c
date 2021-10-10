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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <port.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <libinetutil.h>

#include "ipmgmt_impl.h"

#define	IPMGMT_WATCHER_DIR	"/etc"

static struct dirent **prev_de = NULL;
static int prev_num = 0;

#define	IPMGMT_HOSTNAME	"hostname"
#define	IPMGMT_DHCP	"dhcp"

static size_t hostname_strlen = sizeof (IPMGMT_HOSTNAME) - 1;
static size_t dhcp_strlen = sizeof (IPMGMT_DHCP) - 1;

/*
 * Returns a value of 1 if the file is one of interest (i.e., a hostname[6].*
 * or dhcp.* file) and a zero otherwise.
 */
static int
i_ipmgmt_is_netfile(const struct dirent *de)
{
	const char		*name = de->d_name;
	ifspec_t		ifs;

	if (strncmp(name, IPMGMT_HOSTNAME, hostname_strlen) == 0) {
		name += hostname_strlen;
		if (*name == '6')
			name++;
	} else if (strncmp(name, IPMGMT_DHCP, dhcp_strlen) == 0) {
		name += dhcp_strlen;
	} else {
		return (0);
	}

	if (*name != '.')
		return (0);
	name++;

	if (ifparse_ifspec(name, &ifs))
		return (1);

	return (0);
}

/*
 * Compare an entry from the current snapshot against
 * the previous snapshot. If the entry is a new one, then return B_FALSE.
 * Otherwise, return B_TRUE. Always return with 'pnxt' updated.
 *
 * As an optimization, free entries from the previous snapshot as
 * they are compared.
 */
static boolean_t
i_ipmgmt_match_dirent(struct dirent **cde, int *pnxt)
{
	struct dirent	**pde;
	int		ret;

	/*
	 * Compare the current entry against the previous snapshot. Ignore
	 * previous entries that are no longer in the current snapshot.
	 */
	while (*pnxt < prev_num) {
		pde = &prev_de[*pnxt];
		ret = strcoll((*cde)->d_name, (*pde)->d_name);
		if (ret < 0)
			return (B_FALSE);
		free(*pde);
		(*pnxt)++;
		if (ret == 0)
			return (B_TRUE);
	}

	/*
	 * There are no more entries in the previous snapshot.
	 * All current entries at this point are new.
	 */
	return (B_FALSE);
}

static void
i_ipmgmt_process_dir(char *dir)
{
	struct dirent	**curr_de;
	int		curr_num;
	int		pnxt;
	int		i;

	if (chdir(dir) < 0) {
		ipmgmt_log(LOG_ERR, "i_process_dir: %s\n", strerror(errno));
		return;
	}

	/*
	 * Retrieve a list of "hostname[6].*" and "dhcp[6].*" files.
	 */
	curr_num = scandir(".", &curr_de, i_ipmgmt_is_netfile, alphasort);
	if (curr_num < 0) {
		ipmgmt_log(LOG_ERR, "i_process_dir: %s\n", strerror(errno));
		return;
	}

	/*
	 * Compare last snapshot of "hostname" files against
	 * current snapshot. New entries are identified by a
	 * return of B_FALSE from i_ipmgmt_match_dirent().
	 */
	for (i = 0, pnxt = 0; i < curr_num; i++) {
		if (!i_ipmgmt_match_dirent(&curr_de[i], &pnxt))
			ipmgmt_log(LOG_NOTICE, "/etc/%s files are no "
			    "longer a supported mechanism for defining "
			    "persistence. See ipadm(1m).\n",
			    curr_de[i]->d_name);
	}

	/*
	 * Need to free any files removed since last snapshot.
	 */
	while (pnxt < prev_num) {
		free(prev_de[pnxt]);
		pnxt++;
	}
	free(prev_de);

	/*
	 * And what is current becomes previous for next event.
	 */
	prev_de = curr_de;
	prev_num = curr_num;
}

/* ARGSUSED */
static void *
i_ipmgmt_watcher_thread(void *arg)
{
	file_obj_t	fobj;
	file_obj_t	*fobjp = &fobj;
	int		port;

	if ((port = port_create()) < 0) {
		ipmgmt_log(LOG_ERR, "i_ipmgmt_watcher_thread: error creating "
		    "event port: %s\n", strerror(errno));
		return (NULL);
	}

	/*
	 * Monitor /etc for any modification events. Whenver, they
	 * are received, go read /etc to see if there are any new
	 * files of interest.
	 */
	for (;;) {
		port_event_t pe;

		fobjp->fo_name = IPMGMT_WATCHER_DIR;
		fobjp->fo_mtime.tv_sec = 0;
		fobjp->fo_mtime.tv_nsec = 0;

		if (port_associate(port, PORT_SOURCE_FILE, (uintptr_t)fobjp,
		    FILE_MODIFIED, NULL) < 0) {
			ipmgmt_log(LOG_ERR, "i_ipmgmt_watcher_thread: error "
			    "re-associating event port: %s\n", strerror(errno));
			free(prev_de);
			(void) close(port);
			return (NULL);
		}

		if (port_get(port, &pe, NULL) == -1) {
			ipmgmt_log(LOG_ERR, "i_ipmgmt_watcher_thread: error "
			    "getting port event: %s\n", strerror(errno));
			free(prev_de);
			(void) close(port);
			return (NULL);
		}

		if (!(pe.portev_events & (FILE_EXCEPTION)))
			i_ipmgmt_process_dir(IPMGMT_WATCHER_DIR);
	}
}

/*
 * Creates a thread that monitors /etc for any file system modification events.
 * Modifications to /etc will result in the thread reading /etc to catch the
 * creation of any hostname*.* files.
 */
void
ipmgmt_init_watcher()
{
	pthread_attr_t	attr;

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	(void) pthread_create(NULL, &attr, i_ipmgmt_watcher_thread, NULL);
	(void) pthread_attr_destroy(&attr);
}
