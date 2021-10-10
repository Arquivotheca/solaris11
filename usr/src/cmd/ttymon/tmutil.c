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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/



#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stropts.h>
#include <sys/sad.h>
#include "ttymon.h"
#include "tmstruct.h"
#include "tmextern.h"

#define	NSTRPUSH	9	/* should agree with the tunable in	*/
				/* 		/etc/master.d/kernel	*/

/*
 * vml(modules)	- validate a list of modules
 *		- return 0 if successful, -1 if failed
 */
int
vml(char *modules)
{
	char	*modp, *svmodp;
	int	i, fd;
	struct str_mlist newmods[NSTRPUSH];	/* modlist for newlist	*/
	struct str_list	newlist;		/* modules to be pushed	*/

	if ((modules == NULL) || (*modules == '\0'))
		return (0);

	newlist.sl_modlist = newmods;
	newlist.sl_nmods = NSTRPUSH;
	if ((modp = malloc(strlen(modules) + 1)) == NULL) {
		log("vml: malloc failed");
		return (-1);
	};
	svmodp = modp;
	(void) strcpy(modp, modules);
	/*
	 * pull mod names out of comma-separated list
	 */
	for (i = 0, modp = strtok(modp, ","); modp != NULL;
	    i++, modp = strtok(NULL, ",")) {
		if (i >= NSTRPUSH) {
			log("too many modules in <%s>", modules);
			i = -1;
			break;
		}
		(void) strncpy(newlist.sl_modlist[i].l_name, modp, FMNAMESZ);
		newlist.sl_modlist[i].l_name[FMNAMESZ] = '\0';
	}
	free(svmodp);
	if (i == -1)
		return (-1);
	newlist.sl_nmods = i;

	/*
	 * Is it a valid list of modules?
	 */
	if ((fd = open(USERDEV, O_RDWR)) == -1) {
		if (errno == EBUSY) {
			log("Warning - can't validate module list,"
			    " /dev/sad/user busy");
			return (0);
		}
		log("open /dev/sad/user failed: %s", strerror(errno));
		return (-1);
	}
	if ((i = ioctl(fd, SAD_VML, &newlist)) < 0) {
		log("Validate modules ioctl failed, modules = <%s>: %s",
		    modules, strerror(errno));
		(void) close(fd);
		return (-1);
	}
	if (i != 0) {
		log("Error - invalid STREAMS module list <%s>.", modules);
		(void) close(fd);
		return (-1);
	}
	(void) close(fd);
	return (0);
}

/*
 * copystr(s1, s2) - copy string s2 to string s1
 *		   - also put '\' in front of ':'
 */
void
copystr(char *s1, char *s2)
{
	while (*s2) {
		if (*s2 == ':') {
			*s1++ = '\\';
		}
		*s1++ = *s2++;
	}
	*s1 = '\0';
}

/*PRINTFLIKE1*/
void
cons_printf(const char *fmt, ...)
{
	char buf[MAXPATHLEN * 2]; /* enough space for msg including a path */
	int fd;
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	if ((fd = open(CONSOLE, O_WRONLY|O_NOCTTY)) != -1)
		(void) write(fd, buf, strlen(buf) + 1);
	(void) close(fd);
}
