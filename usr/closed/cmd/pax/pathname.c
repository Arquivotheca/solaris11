/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
#if !defined(lint) && !defined(_NOIDENT)
	static char rcsid[] = "@(#)$RCSfile: pathname.c,v $ $Revision: 1.2.2.2 "
	    "$ (OSF) $Date: 1991/10/01 15:55:03 $";
#endif
/*
 * pathname.c - directory/pathname support functions
 *
 * DESCRIPTION
 *
 *	These functions provide directory/pathname support for PAX
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 *
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice is duplicated in all such
 * forms and that any documentation, advertising materials, and other
 * materials related to such distribution and use acknowledge that the
 * software was developed * by Mark H. Colburn and sponsored by The
 * USENIX Association.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Revision 1.2  89/02/12  10:05:13  mark
 * 1.2 release fixes
 *
 * Revision 1.1  88/12/23  18:02:21  mark
 * Initial revision
 *
 */

/* Headers */

#include "pax.h"
#include <libgen.h>

int
r_unlink(char *name)
{
	char		*cp;
	char		*last;
	char		*np;
	char		tname[PATH_MAX + 1];
	int		ok = 1;
	int		savesize = 0;
	int		basesize;
	char		curwd[PATH_MAX + 1];

	last = (char *)NULL;
	for (cp = name; *cp; ) {
		if (*cp++ == '/')
			last = cp;
	}
	/* Nothing to unlink */
	if (last == (char *)NULL)
		return (0);
	*--last = '\0';
	(void) memset(tname, '\0', sizeof (tname));
	basesize = strlen(name);
	if (getcwd(curwd, PATH_MAX + 1) == NULL) {
		ok = 0;
	}
	while ((savesize < basesize) && ok) {
		/* break into PATH_MAX size chunks of name */
		(void) strlcpy(tname, name + savesize, sizeof (tname));
		if ((np = strrchr(tname, '/')) != NULL) {
			*np++ = '\0';
		}
		savesize += strlen(tname) + 1; /* + 1 for the '/' */
		if (savesize <= (basesize + 1)) {
			if (chdir(tname) < 0) {
				ok = 0;
			}
		}
	}
	if (ok) {
		if (unlink(last + 1) < 0) {
			ok = 0;
		}
	}
	if (curwd != NULL) {
		if (chdir(curwd) < 0) {
			ok = 0;
		}
	}

	*last = '/';
	return (ok ? 0 : -1);
}

/*
 * r_dirneed()
 * Recursively create/open directories of the input file hierarchy and cd
 * into them.  The calling routine is responsible for saving the current
 * working directory beforehand and cd'ing back to the current working
 * directory afterwards.
 *
 * This routine is called when an open failed to create a file name
 * with a path length > PATH_MAX.  We break the input name into PATH_MAX size
 * chunks and create/cd into the directories.  When the parent directory of
 * the file name is created, the file descriptor for the open directory is
 * returned without cd'ing into it.
 */
int
r_dirneed(char *name, mode_t mode)
{
	char		*cp;
	char		*last;
	char		*np;
	char		tname[PATH_MAX + 1];
	int		ok = 1;
	int		savesize = 0;
	int		basesize;
	int		fd;
	static Stat	sb;

	last = (char *)NULL;
	for (cp = name; *cp; ) {
		if (*cp++ == '/')
			last = cp;
	}
	if (last == (char *)NULL)
		return (STAT(".", &sb));
	*--last = '\0';
	(void) memset(tname, '\0', sizeof (tname));
	basesize = strlen(name);
	while ((savesize < basesize) && ok) {
		/* break of PATH_MAX size chunks of name */
		(void) strlcpy(tname, name + savesize,
		    sizeof (tname));
		if ((np = strrchr(tname, '/')) != NULL) {
			*np++ = '\0';
			if ((mkdirp(tname, 0777) < 0) &&
			    (errno != EEXIST)) {
				perror(tname);
				ok = 0;
			}
		/* final chunk of name */
		} else if ((strlen(tname) + savesize) == basesize) {
			/*
			 * Create the directories if extracting from
			 * an archive.
			 */
			if ((mkdirp(tname, 0777) < 0) &&
			    (errno != EEXIST)) {
				perror(tname);
				ok = 0;
			}
		}
		savesize += strlen(tname) + 1; /* + 1 for the '/' */
		if (ok && (savesize <= basesize)) {
			if (chdir(tname) < 0) {
				ok = 0;
			}
		}
	}
	if (ok) {
		fd = open(tname, mode);
	}
	*last = '/';
	return (ok ? fd : -1);
}

/*
 * dirneed  - checks for the existence of directories and possibly create
 *
 * DESCRIPTION
 *
 *	Dirneed checks to see if a directory of the name pointed to by name
 *	exists.  If the directory does exist, then dirneed returns 0.  If
 *	the directory does not exist and the f_dir_create flag is set,
 *	then dirneed will create the needed directory, recursively creating
 *	any needed intermediate directory.
 *
 *	If f_dir_create is not set, then no directories will be created
 *	and a value of -1 will be returned if the directory does not
 *	exist.
 *
 * PARAMETERS
 *
 *	name		- name of the directory to create
 *
 * RETURNS
 *
 *	Returns a 0 if the creation of the directory succeeded or if the
 *	directory already existed.  If the f_dir_create flag was not set
 *	and the named directory does not exist, or the directory creation
 *	failed, a -1 will be returned to the calling routine.
 */


int
dirneed(char *name)
{
	char		*cp;
	char		*last;
	int		ok;
	static Stat	sb;

	last = (char *)NULL;
	for (cp = name; *cp; ) {
		if (*cp++ == '/')
			last = cp;
	}
	if (last == (char *)NULL)
		return (STAT(".", &sb));
	*--last = '\0';
	ok = STAT(*name ? name : ".", &sb) == 0 ?
	    ((sb.sb_mode & S_IFMT) == S_IFDIR) :
	    (f_dir_create && mkdirp(name, 0777) == 0);
	*last = '/';
	return (ok ? 0 : -1);
}


/*
 * nameopt - optimize a pathname
 *
 * DESCRIPTION
 *
 * 	Confused by "<symlink>/.." twistiness. Returns the number of final
 * 	pathname elements (zero for "/" or ".") or -1 if unsuccessful.
 *
 * PARAMETERS
 *
 *	char	*begin	- name of the path to optimize
 *
 * RETURNS
 *
 *	Returns 0 if successful, non-zero otherwise.
 *
 */


int
nameopt(char *begin)
{
	char	*name;
	char	*item;
	int	idx;
	int	absolute;
	char	*element[PATHELEM];

	absolute = (*(name = begin) == '/');
	idx = 0;
	for (;;) {
		if (idx == PATHELEM) {
			warn(begin, gettext("Too many elements"));
			return (-1);
		}
		while (*name == '/') {
			++name;
		}
		if (*name == '\0')
			break;
		element[idx] = item = name;
		while (*name && *name != '/') {
			++name;
		}
		if (*name)
			*name++ = '\0';
		if (strcmp(item, "..") == 0) {
			if (idx == 0) {
				if (!absolute)
					++idx;
			} else if (strcmp(element[idx - 1], "..") == 0)
				++idx;
			else
				--idx;
		} else if (strcmp(item, ".") != 0)
			++idx;
	}
	if (idx == 0)
		element[idx++] = absolute ? "" : ".";
	element[idx] = (char *)NULL;
	name = begin;
	if (absolute)
		*name++ = '/';
	for (idx = 0; (item = element[idx]) != '\0'; ++idx, *name++ = '/') {
		while (*item) {
			*name++ = *item++;
		}
	}
	*--name = '\0';
	return (idx);
}


/*
 * dirmake - make a directory
 *
 * DESCRIPTION
 *
 *	Dirmake makes a directory with the appropritate permissions.
 *
 * PARAMETERS
 *
 *	char 	*name	- Name of directory make
 *	Stat	*asb	- Stat structure of directory to make
 *
 * RETURNS
 *
 * 	Returns zero if successful, -1 otherwise.
 *
 */


int
dirmake(char *name, Stat *asb)
{
	if (mkdir(name, (int)(asb->sb_mode & S_IPOPN)) < 0)
		return (-1);
	(void) chmod(name, ((asb->sb_mode & S_IPERM) | S_IWUSR));
	return (0);
}
