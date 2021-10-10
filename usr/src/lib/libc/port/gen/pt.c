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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma weak _ptsname = ptsname
#pragma weak _grantpt = grantpt
#pragma weak _unlockpt = unlockpt

#include "lint.h"
#include "libc.h"
#include "mtlib.h"
#include <sys/types.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/mkdev.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ptms.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <spawn.h>
#include <grp.h>
#include "tsd.h"

#define	PTSNAME "/dev/pts/"		/* slave name */
#define	PTLEN   32			/* slave name length */
#define	DEFAULT_TTY_GROUP	"tty"	/* slave device group owner */

static void itoa(int, char *);

/*
 *  Check that fd argument is a file descriptor of an opened master.
 *  Do this by sending an ISPTM ioctl message down stream. Ioctl()
 *  will fail if:(1) fd is not a valid file descriptor.(2) the file
 *  represented by fd does not understand ISPTM(not a master device).
 *  If we have a valid master, get its minor number via fstat().
 *  Concatenate it to PTSNAME and return it as the name of the slave
 *  device.
 */
static dev_t
ptsdev(int fd)
{
	struct stat64 status;
	struct strioctl istr;

	istr.ic_cmd = ISPTM;
	istr.ic_len = 0;
	istr.ic_timout = 0;
	istr.ic_dp = NULL;

	if (ioctl(fd, I_STR, &istr) < 0 || fstat64(fd, &status) < 0)
		return (NODEV);

	return (minor(status.st_rdev));
}

char *
ptsname(int fd)
{
	dev_t dev;
	char *sname;

	if ((dev = ptsdev(fd)) == NODEV)
		return (NULL);

	sname = tsdalloc(_T_PTSNAME, PTLEN, NULL);
	if (sname == NULL)
		return (NULL);
	(void) strcpy(sname, PTSNAME);
	itoa(dev, sname + strlen(PTSNAME));

	/*
	 * This lookup will create the /dev/pts node (if the corresponding
	 * pty exists.
	 */
	if (access(sname, F_OK) ==  0)
		return (sname);

	return (NULL);
}

/*
 * Send an ioctl down to the master device requesting the
 * master/slave pair be unlocked.
 */
int
unlockpt(int fd)
{
	struct strioctl istr;

	istr.ic_cmd = UNLKPT;
	istr.ic_len = 0;
	istr.ic_timout = 0;
	istr.ic_dp = NULL;

	if (ioctl(fd, I_STR, &istr) < 0)
		return (-1);

	return (0);
}

int
grantpt(int fd)
{
	struct strioctl istr;
	pt_own_t pto;
	struct group *gr_name;

	/* validate the file descriptor before proceeding */
	if (ptsdev(fd) == NODEV)
		return (-1);

	pto.pto_ruid = getuid();

	gr_name = getgrnam(DEFAULT_TTY_GROUP);
	if (gr_name)
		pto.pto_rgid = gr_name->gr_gid;
	else
		pto.pto_rgid = getgid();

	istr.ic_cmd = OWNERPT;
	istr.ic_len = sizeof (pt_own_t);
	istr.ic_timout = 0;
	istr.ic_dp = (char *)&pto;

	if (ioctl(fd, I_STR, &istr) != 0) {
		errno = EACCES;
		return (-1);
	}

	return (0);
}

/*
 * Send an ioctl down to the master device requesting the master/slave pair
 * be assigned to the given zone.
 */
int
zonept(int fd, zoneid_t zoneid)
{
	struct strioctl istr;

	istr.ic_cmd = ZONEPT;
	istr.ic_len = sizeof (zoneid);
	istr.ic_timout = 0;
	istr.ic_dp = (char *)&zoneid;

	if (ioctl(fd, I_STR, &istr) != 0) {
		return (-1);
	}
	return (0);
}


static void
itoa(int i, char *ptr)
{
	int dig = 0;
	int tempi;

	tempi = i;
	do {
		dig++;
		tempi /= 10;
	} while (tempi);

	ptr += dig;
	*ptr = '\0';
	while (--dig >= 0) {
		*(--ptr) = i % 10 + '0';
		i /= 10;
	}
}


/*
 * added for SUSv3 standard
 *
 * Open a pseudo-terminal device.  External interface.
 */

int
posix_openpt(int oflag)
{
	return (open("/dev/ptmx", oflag));
}
