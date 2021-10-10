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

#include <sys/uadmin.h>
#include <errno.h>
#include <libscf.h>
#include <paths.h>
#include <priv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zone.h>

int
main(void)
{
	uint16_t flags;
	int r;
	FILE *fp;

	r = zone_getattr(getzoneid(), ZONE_ATTR_FLAGS, &flags, sizeof (flags));

	if (r != sizeof (flags)) {
		(void) fprintf(stderr, "Unexpected return from "
		    "zone_getattr(ZONE_ATTR_FLAGS) (got %d, expected %d)\n",
		    r, sizeof (flags));
		return (SMF_EXIT_ERR_FATAL);
	}

	fp = fopen(_PATH_MSGLOG, "w");

	/*
	 * If we're not transiently r/w, we also remove the file.
	 * If the file exists and we're a ROZR zone, we should have mounted
	 * either with ZF_TRANSIENT_RW *or* the zone wasn't locked down;
	 * if the file exists and we can't remove it and the zone is locked,
	 * this is an error and this milestone goes into maintenance.
	 * It is possible that we mounted a read-only version of Solaris
	 * from a DVD/CD; then we don't care and we exit.
	 */
	if ((flags & ZF_TRANSIENT_RW) == 0) {
		if (unlink(_PATH_SELF_AS_REQ) != 0 && errno != ENOENT &&
		    (flags & ZF_LOCKDOWN) != 0) {
			(void) fprintf(stderr, _PATH_SELF_AS_REQ " exists, "
			    "but the zone is booted read-only\n");
			if (fp != NULL) {
				(void) fprintf(fp, _PATH_SELF_AS_REQ " exists, "
				    "but the zone is booted read-only\n");
			}
			return (SMF_EXIT_ERR_FATAL);
		}
		return (SMF_EXIT_OK);
	}

	if (unlink(_PATH_SELF_AS_REQ) != 0 && errno != ENOENT)  {
		perror("Cannot unlink " _PATH_SELF_AS_REQ);
		return (SMF_EXIT_ERR_FATAL);
	}

	/* Write on the console what we are rebooting. */
	if (fp == NULL) {
		(void) fprintf(stderr, "Cannot open " _PATH_MSGLOG "\n");
		return (SMF_EXIT_ERR_FATAL);
	}
	(void) fprintf(fp,
	    "\n[NOTICE: This read-only system transiently booted read/write]\n"
	    "[NOTICE: Now that self assembly has been completed,"
	    " the system is rebooting]\n\n");

	(void) fflush(fp);
	(void) fclose(fp);

	(void) fprintf(stderr,
	    "Rebooting after a transient read/write boot.\n");

	if (system("/usr/sbin/reboot") != 0) {
		(void) fprintf(stderr, "system(\"/usr/sbin/reboot\") failed\n");
		return (SMF_EXIT_ERR_FATAL);
	}

	return (SMF_EXIT_OK);
}
