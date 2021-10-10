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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/ctfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <assert.h>
#include <libuutil.h>
#include <libintl.h>
#include <string.h>
#include <procfs.h>
#include <libcontract.h>
#include <libcontract_priv.h>
#include "libcontract_impl.h"
#include "process_dump.h"
#include "device_dump.h"


contract_type_t types[CTT_MAXTYPE] = {
	{ "process", event_process },
	{ "device", event_device }
};

static int
close_on_exec(int fd)
{
	int flags = fcntl(fd, F_GETFD, 0);
	if ((flags != -1) && (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1))
		return (0);
	return (-1);
}

int
contract_latest(ctid_t *id)
{
	int cfd, r;
	ct_stathdl_t st;
	ctid_t result;

	if ((cfd = open64(CTFS_ROOT "/process/latest", O_RDONLY)) == -1)
		return (errno);

	if ((r = ct_status_read(cfd, CTD_COMMON, &st)) != 0) {
		(void) close(cfd);
		return (r);
	}

	result = ct_status_get_id(st);
	ct_status_free(st);
	(void) close(cfd);

	*id = result;
	return (0);
}

int
contract_open(ctid_t ctid, const char *type, const char *file, int oflag)
{
	char path[PATH_MAX];
	int n, fd;

	assert((oflag & O_CREAT) == 0);

	if (type == NULL)
		type = "all";

	n = snprintf(path, PATH_MAX, CTFS_ROOT "/%s/%ld/%s", type, ctid, file);
	if (n >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	fd = open64(path, oflag);
	if (fd != -1) {
		if (close_on_exec(fd) == -1) {
			int err = errno;
			(void) close(fd);
			errno = err;
			return (-1);
		}
	}
	return (fd);
}

int
contract_abandon_id(ctid_t ctid)
{
	int fd, err;

	fd = contract_open(ctid, "all", "ctl", O_WRONLY);
	if (fd == -1)
		return (errno);

	err = ct_ctl_abandon(fd);
	(void) close(fd);

	return (err);
}

ctid_t
getctid(void)
{
	int fd;
	psinfo_t ps;

	if ((fd = open("/proc/self/psinfo", O_RDONLY)) == -1)
		return (-1);
	if (read(fd, &ps, sizeof (ps)) != sizeof (ps)) {
		(void) close(fd);
		return (-1);
	}
	(void) close(fd);
	return (ps.pr_contract);
}

void
contract_event_dump(FILE *file, ct_evthdl_t hdl, int verbose)
{
	ct_typeid_t type;
	struct ctlib_event_info *info = hdl;

	type = info->event.ctev_cttype;
	types[type].type_event(file, hdl, verbose);
}

void
contract_negend_dump(FILE *file, ct_evthdl_t ev)
{
	ctevid_t nevid = 0;
	ctid_t my_ctid = ct_event_get_ctid(ev);
	ctid_t new_ctid = 0;
	char *s;

	(void) ct_event_get_nevid(ev, &nevid);
	(void) ct_event_get_newct(ev, &new_ctid);

	if (new_ctid != my_ctid) {
		s = dgettext(TEXT_DOMAIN, "negotiation %llu succeeded\n");
	} else {
		s = dgettext(TEXT_DOMAIN, "negotiation %llu failed\n");
	}
	/*LINTED*/
	(void) fprintf(file, s, (unsigned long long)nevid);
}

/*
 * return 0 if all supplied decorations match
 *        1 if at least only one supplied decoration diverges
 *        on error, errno is set and -1 is returned
 */
int
contract_pr_check_decorations(const char *svc_fmri, ctid_t ctid,
    const char *svc_creator, const char *svc_aux)
{
	ctid_t	my_ctid, ct_ctid;
	ct_stathdl_t status;
	char *ct_svc_fmri;
	char *ct_creator;
	char *ct_svc_aux;
	int fd;
	int e;
	int r = 0;

	if ((my_ctid = getctid()) == -1) {
		return (-1);
	}

	if ((fd = contract_open(my_ctid, "process", "status",
	    O_RDONLY)) == -1) {
		return (-1);
	}

	if ((e = ct_status_read(fd, CTD_ALL, &status)) != 0) {
		(void) close(fd);
		errno = e;
		return (-1);
	}
	(void) close(fd);

	if (svc_fmri != NULL) {
		if ((e = ct_pr_status_get_svc_fmri(status,
		    &ct_svc_fmri)) != 0) {
			r = -1;
			goto out;
		}
		if (strcmp(ct_svc_fmri, svc_fmri) != 0) {
			r = 1;
			goto out;
		}
	}

	if (ctid != -1) {
		if ((e = ct_pr_status_get_svc_ctid(status, &ct_ctid)) != 0) {
			r = -1;
			goto out;
		}
		if (ct_ctid != ctid) {
			r = 1;
			goto out;
		}
	}

	if (svc_creator != NULL) {
		if ((e = ct_pr_status_get_svc_creator(status,
		    &ct_creator)) != 0) {
			r = -1;
			goto out;
		}
		if (strcmp(ct_creator, svc_creator) != 0) {
			r = 1;
			goto out;
		}
	}

	if (svc_aux != NULL) {
		if ((e = ct_pr_status_get_svc_aux(status, &ct_svc_aux)) != 0) {
			r = -1;
			goto out;
		}
		if (strcmp(ct_svc_aux, svc_aux) != 0) {
			r = 1;
			goto out;
		}
	}

out:
	ct_status_free(status);
	if (r == -1)
		errno = e;

	return (r);
}
