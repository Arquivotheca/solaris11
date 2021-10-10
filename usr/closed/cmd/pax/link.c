/*
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	COPYRIGHT NOTICE
 *
 *	This source code is designated as Restricted Confidential Information
 *	and is subject to special restrictions in a confidential disclosure
 *	agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 *	this source code outside your company without OSF's specific written
 *	approval.  This source code, and all copies and derivative works
 *	thereof, must be returned or destroyed at request. You must retain
 *	this notice on any copies which you make.
 *
 *	(c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 *	ALL RIGHTS RESERVED
 */
/*
 *	OSF/1 1.2
 */
#if !defined(lint) && !defined(_NOIDENT)
	static char rcsid[] = "@(#)$RCSfile: link.c,v $ $Revision: 1.2.2.4 "
	    "$ (OSF) $Date: 1992/03/23 19:14:42 $";
#endif
/*
 * link.c - functions for handling multiple file links
 *
 * DESCRIPTION
 *
 *	These function manage the link chains which are used to keep track
 *	of outstanding links during archive reading and writing.
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
 * Revision 1.2  89/02/12  10:04:38  mark
 * 1.2 release fixes
 *
 * Revision 1.1  88/12/23  18:02:12  mark
 * Initial revision
 *
 */

/* Headers */

#include "pax.h"

/* Defines */

/*
 *	Address link information base.
 */
#define	LINKHASH(ino)	(linkbase + (ino) % NEL(linkbase))

/*
 *	Number of array elements.
 */
#define	NEL(a)		(sizeof (a) / sizeof (*(a)))



/* Internal Identifiers */

static Link    *linkbase[256];	/* Unresolved link information */

/*
 * linkmatch - match a file to a node in the link chain
 *
 * DESCRIPTION
 *
 *	 Linkmatch returns true if the current node from the link chain
 *	 is where we want to be.  This function was added because when
 *	 extracting from a tar archive we cannot just check the ino and
 *	 dev fields since the ino and dev are not stored in the tar file
 *	 header.   So in the case of reading a tar archive,  we have to
 *	 search the link chain based on the name of the file that the
 *	 file to match is a link to(ie. the linkname).  In the cpio case,
 *	 we simply check the file ino and dev against the linkptr ino and
 *	 dev.  If they are equal then a match!
 *
 * PARAMETERS
 *
 *	 Link *linkptr - a pointer to a Link structure from the link chain
 *	 Stat *asb     - stat structure of file to find a link to
 *
 * RETURNS
 *
 *	 Returns true if a match was found in the link chain,
 *	 else returns false.
 *
 */

static int
linkmatch(Link *linkptr, Stat *asb)
{
	int t;

	if ((ar_format == TAR || ar_format == PAX) && (f_extract || f_list)) {
		if (asb->linkname == (char *)NULL) {
			return (0);
		} else {
			t = strcmp(asb->xattr_info.xattr_linkaname,
			    linkptr->l_attr);

			return (t == 0 &&
			    (strcmp(asb->linkname, linkptr->l_name) == 0));

		}
	} else
		return ((linkptr->l_ino == asb->sb_ino) &&
		    (linkptr->l_dev == asb->sb_dev));
}


/*
 * linkfrom - find a file to link from
 *
 * DESCRIPTION
 *
 *	Linkfrom searches the link chain to see if there is a file in the
 *	link chain which has the same inode number as the file specified
 *	by the stat block pointed at by asb.  If a file is found, the
 *	name is returned to the caller, otherwise a NULL is returned.
 *
 * PARAMETERS
 *
 *	char    *name   - name of the file which we are attempting
 *	                       to find a link for
 *	Stat	*asb	- stat structure of file to find a link to
 *
 * RETURNS
 *
 *	Returns a pointer to a link structure, or NULL if unsuccessful.
 *
 */


Link *
linkfrom(char *name, Stat *asb)
{
	Link	*linkp;
	Link	*linknext;
	Path	*path;
	Path	*pathnext;
	Link	**abase;
	char	*namep = name;

	if (asb->xattr_info.xattr_linkfname != NULL)
		namep = asb->xattr_info.xattr_linkfname;

	for (linkp = *(abase = LINKHASH(asb->sb_ino)); linkp;
	    linkp = linknext) {
		if (linkp->l_nlink == 0) {
			if (linkp->l_name)
				free((char *)linkp->l_name);
			linknext = linkp->l_forw;
			if (linknext)
				linknext->l_back = linkp->l_back;
			if (linkp->l_back)
				linkp->l_back->l_forw = linkp->l_forw;
			free((char *)linkp);
			*abase = (Link *)NULL;
		} else if (linkmatch(linkp, asb)) {
			/*
			 * check to see if a file with the name "name" exists
			 * in the chain of files which we have for this
			 * particular link
			 */
			for (path = linkp->l_path; path; path = pathnext) {
				if (strcmp(path->p_name, namep) == 0 &&
				    (strcmp(path->p_attr,
				    asb->xattr_info.xattraname) == 0)) {
					--linkp->l_nlink;
					if (path->p_name)
						free(path->p_name);
					pathnext = path->p_forw;
					if (pathnext)
						pathnext->p_back = path->p_back;
					if (path->p_back)
						path->p_back->p_forw = pathnext;
					if (linkp->l_path == path)
						linkp->l_path = pathnext;
					free(path);
					return (linkp);
				}
				pathnext = path->p_forw;
			}
			return ((Link *)NULL);
		} else
			linknext = linkp->l_forw;
	}
	return ((Link *)NULL);
}


/*
 * islink - determine whether a given file really a link
 *
 * DESCRIPTION
 *
 *	Islink searches the link chain to see if there is a file in the
 *	link chain which has the same inode number as the file specified
 *	by the stat block pointed at by asb.  If a file is found, a
 *	non-zero value is returned to the caller, otherwise a 0 is
 *	returned.
 *
 * PARAMETERS
 *
 *	char    *name   - name of file to check to see if it is link.
 *	Stat	*asb	- stat structure of file to find a link to
 *
 * RETURNS
 *
 *	Returns a pointer to a link structure, or NULL if unsuccessful.
 *
 */


Link *
islink(char *name, Stat *asb)
{
	Link	*linkp;
	Link	*linknext;

	for (linkp = *(LINKHASH(asb->sb_ino)); linkp; linkp = linknext) {
		if (linkmatch(linkp, asb)) {
			if ((strcmp(name, linkp->l_name) == 0) &&
			    (strcmp(asb->xattr_info.xattraname,
			    linkp->l_attr) == 0))
				return ((Link *)NULL);
			return (linkp);
		} else
			linknext = linkp->l_forw;
	}
	return ((Link *)NULL);
}


/*
 * linkto  - remember a file with outstanding links
 *
 * DESCRIPTION
 *
 *	Linkto adds the specified file to the link chain.  Any subsequent
 *	calls to linkfrom which have the same inode will match the file
 *	just entered.  If not enough space is available to make the link
 *	then the item is not added to the link chain, and a NULL is
 *	returned to the calling function.
 *
 * PARAMETERS
 *
 *	char	*name	- name of file to remember
 *	Stat	*asb	- pointer to stat structure of file to remember
 *
 * RETURNS
 *
 *	Returns a pointer to the associated link structure, or NULL when
 *	linking is not possible.
 *
 */


Link *
linkto(char *name, Stat *asb)
{
	Link	*linkp;
	Link	*linknext;
	Path	*path = NULL;
	Link	**abase;

	for (linkp = *(LINKHASH(asb->sb_ino)); linkp; linkp = linknext) {
		if (linkmatch(linkp, asb)) {
			if (((strcmp(name, linkp->l_name) == 0) &&
			    (strcmp(linkp->l_attr,
			    asb->xattr_info.xattraname) == 0)) ||
			    (path = (Path *)mem_get(sizeof (Path))) == NULL ||
			    (path->p_name = mem_rpl_name(name)) == NULL ||
			    (((path->p_attr = mem_rpl_name(
			    asb->xattr_info.xattraname)) == NULL) &&
			    (asb->xattr_info.xattraname != NULL))) {
				if (path != NULL) {
					if (path->p_name != NULL) {
						free(path->p_name);
					}
					if (path->p_attr != NULL) {
						free(path->p_attr);
					}
					free(path);
				}
				return ((Link *)NULL);
			}
			/*
			 * Insert this path at the head of the pathlist
			 * for this link.
			 */
			path->p_forw = linkp->l_path;
			if (path->p_forw)
				linkp->l_path->p_back = path;
			linkp->l_path = path;
			path->p_back = (Path *)NULL;

			return ((Link *)NULL);
		} else
			linknext = linkp->l_forw;
	}
	/*
	 * This is a brand new link, for which there is no other information
	 */

	if ((asb->sb_mode & S_IFMT) == S_IFDIR ||
	    (linkp = (Link *)mem_get(sizeof (Link))) == NULL ||
	    (linkp->l_name = mem_rpl_name(name)) == NULL ||
	    (((linkp->l_attr = mem_rpl_name(asb->xattr_info.xattr_linkaname))
	    == NULL) && (asb->xattr_info.xattr_linkaname != NULL))) {
		if (linkp != NULL) {
			if (linkp->l_name != NULL) {
				free(linkp->l_name);
			}
			if (linkp->l_attr != NULL) {
				free(linkp->l_attr);
			}
			free(linkp);
		}
		return ((Link *)NULL);
	}
	linkp->l_dev = asb->sb_dev;
	linkp->l_ino = asb->sb_ino;
	linkp->l_nlink = asb->sb_nlink - 1;
	linkp->l_size = asb->sb_size;
	linkp->l_path = (Path *)NULL;
	/*
	 * Insert at the beginning of list
	 */
	linkp->l_forw = *(abase = LINKHASH(asb->sb_ino));
	if (linkp->l_forw)
		linkp->l_forw->l_back = linkp;
	*abase = linkp;
	linkp->l_back = (Link *)NULL;

	return (linkp);
}


/*
 * linkleft - complain about files with unseen links
 *
 * DESCRIPTION
 *
 *	linkleft scans through the link chain to see if there were any
 *	files which have outstanding links that were not processed by the
 *	archive.  For each file in the link chain for which there was not
 *	a file, an error message is printed.
 */


void
linkleft(void)
{
	Link	*lp;
	Link	**base;

	for (base = linkbase; base < linkbase + NEL(linkbase); ++base) {
		for (lp = *base; lp; lp = lp->l_forw) {
			if (lp->l_nlink) {
				warn(lp->l_path->p_name,
				    gettext("Unseen link(s)"));
			}
		}
	}
}

Link *
linktemp(char *name, Stat *asb)
{
	Link *linkp;

	if ((linkp = (Link *)mem_get(sizeof (Link))) == (Link *)NULL ||
	    (linkp->l_name = mem_rpl_name(name)) == (char *)NULL ||
	    (((linkp->l_attr = mem_rpl_name(asb->xattr_info.xattr_linkaname))
	    == (char *)NULL) && (asb->xattr_info.xattr_linkaname != NULL))) {
		return (NULL);
	}
	linkp->l_dev = asb->sb_dev;
	linkp->l_ino = asb->sb_ino;
	linkp->l_nlink = asb->sb_nlink - 1;
	linkp->l_size = asb->sb_size;
	linkp->l_path = NULL;

	return (linkp);
}

void
linkfree(Link *linkp)
{
	free(linkp->l_name);
	free(linkp->l_attr);
	free(linkp);
}
