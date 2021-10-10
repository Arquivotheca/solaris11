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
	static char rcsid[] = "@(#)$RCSfile: names.c,v $ $Revision: 1.2.2.2 "
	    "$ (OSF) $Date: 1991/10/01 15:54:49 $";
#endif
/*
 * names.c - Look up user and/or group names.
 *
 * DESCRIPTION
 *
 *	These functions support UID and GID name lookup.  The results are
 *	cached to improve performance.
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
 * Revision 1.2  89/02/12  10:05:05  mark
 * 1.2 release fixes
 *
 * Revision 1.1  88/12/23  18:02:19  mark
 * Initial revision
 *
 */

#include <pwd.h>
#include <grp.h>
#include "pax.h"

/* Internal Identifiers */

static uid_t	saveuid = (uid_t)-1;
static char	saveuname[TUNMLEN];

static gid_t	savegid = (gid_t)-1;
static char	savegname[TGNMLEN];


/*
 * finduname - find a user name from a uid
 *
 * DESCRIPTION
 *
 * 	Look up a user name from a uid, maintaining a cache.
 *
 * PARAMETERS
 *
 *	char	uname[]		- name (to be returned to user)
 *	int	uuid		- id of name to find
 *
 * RETURNS
 *
 *	Returns a name which is associated with the user id given.  If there
 *	is no name which corresponds to the user-id given, then a pointer
 *	to a string of zero length is returned.
 *
 * Possible improvements:
 *
 * 	1. for now it's a one-entry cache.
 *	2. The "-1" is to reduce the chance of a hit on the first lookup.
 */
char *
finduname(uid_t uuid)
{
	struct passwd *pw;

	if (uuid != saveuid) {
		saveuid = uuid;
		saveuname[0] = '\0';
		if ((pw = getpwuid(uuid)) != NULL)
			(void) strncpy(saveuname, pw->pw_name, TL_UNAME);
	}
	return (saveuname);
}


/*
 * finduid - get the uid for a given user name
 *
 * DESCRIPTION
 *
 *	This does just the opposit of finduname.  Given a user name it
 *	finds the corresponding UID for that name.
 *
 * PARAMETERS
 *
 *	char	uname[]		- username to find a UID for
 *
 * RETURNS
 *
 *	The UID which corresponds to the uname given, if any.  If no UID
 *	could be found, then return the uid of -1 to indicate the failure
 *	to the caller so it can apply recovery strategies, if any.
 */
uid_t
finduid(char *uname)
{
	struct passwd *pw;

	if (uname[0] != saveuname[0] ||
	    0 != strncmp(uname, saveuname, TL_UNAME)) {
		(void) strncpy(saveuname, uname, TL_UNAME);
		if ((pw = getpwnam(uname)) != NULL)
			saveuid = pw->pw_uid;
		else
			saveuid = (uid_t)-1;
	}

	return (saveuid);
}


/*
 * findgname - look up a group name from a gid
 *
 * DESCRIPTION
 *
 * 	Look up a group name from a gid, maintaining a cache.
 *
 *
 * PARAMETERS
 *
 *	int	ggid		- goupid of group to find
 *
 * RETURNS
 *
 *	A string which is associated with the group ID given.  If no name
 *	can be found, a string of zero length is returned.
 */
char *
findgname(gid_t ggid)
{
	struct group *gr;

	if (ggid != savegid) {
		savegid = ggid;
		savegname[0] = '\0';
		if ((gr = getgrgid(ggid)) != NULL)
			(void) strncpy(savegname, gr->gr_name, TL_GNAME);
	}
	return (savegname);
}



/*
 * findgid - get the gid for a given group name
 *
 * DESCRIPTION
 *
 *	This does just the opposite of finduname.  Given a group name it
 *	finds the corresponding GID for that name.
 *
 * PARAMETERS
 *
 *	char	gname[]		- groupname to find a GID for
 *
 * RETURNS
 *
 *	The GID which corresponds to the uname given, if any.  If no GID
 *	could be found, then return the gid of -1 to indicate the failure
 *	to the caller so it can apply recovery strategies, if any.
 */
gid_t
findgid(char *gname)
{
	struct group *gr;

	if (gname[0] != savegname[0] ||
	    strncmp(gname, savegname, TL_GNAME) != 0) {
		(void) strncpy(savegname, gname, TL_GNAME);
		if ((gr = getgrnam(gname)) != NULL)
			savegid = gr->gr_gid;
		else
			savegid = (gid_t)-1;
	}

	return (savegid);
}
