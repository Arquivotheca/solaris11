/*
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

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
 */

/*
 * fileio.c - file I/O functions for all archive interfaces
 *
 * DESCRIPTION
 *
 *	These function all do I/O of some form or another.  They are
 *	grouped here mainly for convienence.
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

#include <libgen.h>
#include "pax.h"

/*
 * open_archive -  open an archive file.
 *
 * DESCRIPTION
 *
 *	Open_archive will open an archive file for reading or writing,
 *	setting the proper file mode, depending on the "mode" passed to
 *	it.  All buffer pointers are reset according to the mode
 *	specified.
 *
 * PARAMETERS
 *
 * 	int	mode 	- specifies whether we are reading or writing.
 *
 * RETURNS
 *
 *	Returns a zero if successfull, or -1 if an error occured during
 *	the open.
 */


int
open_archive(int mode)
{
	if (ar_file[0] == '-' && ar_file[1] == '\0') {
		if (mode == AR_READ) {
			archivefd = STDIN;
			bufend = bufidx = bufstart;
		} else {
			archivefd = STDOUT;
		}
	} else if (mode == AR_READ) {
		archivefd = open(ar_file, O_RDONLY);
		bufend = bufidx = bufstart;	/* set up for initial read */
	} else if (mode == AR_WRITE) {
		archivefd = open(ar_file, O_WRONLY|O_TRUNC|O_CREAT,
		    0666);
	} else if (mode == AR_APPEND) {
		archivefd = open(ar_file, O_RDWR, 0666);
		bufend = bufidx = bufstart;	/* set up for initial read */
	}

	if (archivefd < 0) {
		warnarch(strerror(errno), (OFFSET) 0);
		return (-1);
	}
	++arvolume;
	return (0);
}


/*
 * close_archive - close the archive file
 *
 * DESCRIPTION
 *
 *	Closes the current archive and resets the archive end of file
 *	marker.
 */


void
close_archive(void)
{
	if (archivefd != STDIN && archivefd != STDOUT) {
		(void) close(archivefd);
	}
	areof = 0;
}


/*
 * openout - open an output file
 *
 * DESCRIPTION
 *
 *	Openo opens the named file for output.  The file mode and type are
 *	set based on the values stored in the stat structure for the file.
 *	If the file is a special file, then no data will be written, the
 *	file/directory/Fifo, etc., will just be created.  Appropriate
 *	permission may be required to create special files.  If a
 *	filename substitution has been done with -i or -s, the new
 *	filename will be used when creating the output file.
 *
 * PARAMETERS
 *
 *	char 	**new_name	- new file name (if appropriate)
 *	size_t	*nnsize		- size of 'new_name' buffer
 *	char 	*name		- The name of the file in the archive
 *	Stat	*asb		- Stat structure for the file
 *	Link	*linkp;		- pointer to link chain for this file
 *	int	 ispass		- true if we are operating in "pass" mode
 *
 * RETURNS
 *
 * 	Returns the output file descriptor, 0 if no data is required or -1
 *	if unsuccessful. Note that UNIX open() will never return 0 because
 *	the standard input is in use.
 */


int
openout(char **new_name, size_t *nnsize, char *name,
	Stat *asb, Link *linkp, int ispass, int ndirfd, int odirfd)
{
	int		n_exists;
	int		o_exists;
	int		fd;
	mode_t		perm, otype, ntype;
	mode_t		operm = 0;
	Stat		n_osb, o_osb;
	Dirlist		*dp;
	int		ret;
	int		savefd;			/* save directory fd */
	char		*linkfromp;
	char		*linktop;
	char		*new_namep = *new_name;
	char		buf[PATH_MAX + 1];
#ifdef	S_IFLNK
	int		ssize;
	char		sname[PATH_MAX + 1];
	char		*slinkname;
#endif	/* S_IFLNK */

	/*
	 * The old name is used for archive member selection (governed by
	 * the -c, -n and -u options); the new name for file-system
	 * concerns.
	 */

begin:
	if (asb->xattr_info.xattrp != NULL) {
		new_namep = asb->xattr_info.xattraname;
	}
	n_exists = (fstatat(ndirfd, get_component(new_namep),
	    &n_osb.sb_stat, AT_SYMLINK_NOFOLLOW) == 0);
	o_exists = (fstatat(odirfd, get_component(name),
	    &o_osb.sb_stat, AT_SYMLINK_NOFOLLOW) == 0);

	if (n_exists) {
		/*
		 * Don't check if a file exists if it is under the
		 * attribute directory of an attribute or is a read-write
		 * system attribute.
		 */
		if ((asb->xattr_info.xattraname == NULL) ||
		    ((asb->xattr_info.xattraparent == NULL) &&
		    !asb->xattr_info.xattr_rw_sysattr && !Hiddendir)) {
			if (f_no_overwrite) {
				warn(*new_name, gettext(
				    "exists - will not overwrite"));
				return (-1);
			}
			if (ispass && n_osb.sb_ino == asb->sb_ino &&
			    n_osb.sb_dev == asb->sb_dev) {
				warn(*new_name, gettext("Same file"));
				return (-1);
			}
		}

		otype = n_osb.sb_mode & S_IFMT;  /* file type in filesystem */
		ntype = asb->sb_mode & S_IFMT; /* file type in archive */

		if (otype == ntype)
			operm = n_osb.sb_mode & S_IPERM;
		/* POSIX2 file recreation requires this behavior */
		else if ((otype == S_IFDIR) || (otype == S_IFIFO) ||
		    (otype == S_IFREG)) {
			if (f_stdpax) {
				/*
				 * If invalid=write was specified then
				 * overwrite the existing file.
				 */
				if (f_no_overwrite &&
				    ((asb->xattr_info.xattrp == NULL) ||
				    (!asb->xattr_info.xattr_rw_sysattr &&
				    !Hiddendir))) {
					warn(*new_name, gettext(
					    "exists - will not overwrite"));
					return (-1);
				}
			} else if ((asb->xattr_info.xattrp == NULL) ||
			    (!asb->xattr_info.xattr_rw_sysattr && !Hiddendir)) {
				warn(*new_name, gettext(
				    "exists - will not overwrite"));
				return (-1);
			}
		} else if (REMOVE(*new_name, &n_osb) < 0) {
			warn(*new_name, strerror(errno));
			return (-1);
		} else
			n_exists = 0;
	}

	if (linkp != (Link *)NULL) {
		if ((savefd = open(".", O_RDONLY)) == -1) {
			(void) fprintf(stderr, gettext(
			    "%s: cannot determine current working "
			    "directory: %s\n"),
			    myname, strerror(errno));
			return (-1);
		}

		if (asb->xattr_info.xattraname != NULL &&
		    fchdir(ndirfd) == -1) {
			(void) fprintf(stderr, gettext(
			    "%s: cannot fchdir to attribute directory %s\n"),
			    myname, strerror(errno));
			(void) close(savefd);
			return (-1);
		}
		if ((f_stdpax) && (f_link) && (asb->symlinkref) &&
		    ((f_follow_links) ||
		    ((f_follow_first_link) && (f_cmdarg)))) {
			int cc;

			cc = resolvepath(linkp->l_name, buf, sizeof (buf));
			if (cc == -1) {
				(void) fprintf(stderr, gettext(
				    "%s: cannot resolve path %s, %s\n"),
				    myname, linkp->l_name, strerror(errno));
				(void) close(savefd);
				return (-1);
			}
			buf[cc] = '\0';
			linkfromp = buf;
		} else {
			linkfromp = linkp->l_name;
		}
		linktop = *new_name;
		if (o_exists) {
			if (!f_unconditional &&
			    ((o_osb.sb_mtime > asb->sb_mtime) ||
			    ((o_osb.sb_mtime == asb->sb_mtime) &&
			    (o_osb.sb_stat.st_mtim.tv_nsec >
			    asb->sb_stat.st_mtim.tv_nsec)))) {
				/*
				 * indicate to name_match() this one
				 * doesn't count
				 */
				bad_last_match = 1;
				warn(*new_name, gettext("Newer file exists"));
				(void) fchdir(savefd);
				(void) close(savefd);
				return (-1);
			}
		}

		if (linkp && asb->xattr_info.xattr_linkfname != NULL) {
			linkfromp = linkp->l_attr;
			linktop = asb->xattr_info.xattraname;
			if ((strlen(asb->xattr_info.xattr_linkfname) + 1) >
			    *nnsize) {
				*nnsize = strlen(
				    asb->xattr_info.xattr_linkfname) + 1;
				if ((*new_name = realloc(*new_name, *nnsize))
				    == NULL) {
					fatal(gettext("out of memory"));
				}
			}
			(void) strcpy(*new_name,
			    asb->xattr_info.xattr_linkfname);
		}

		if (n_exists) {
			if (asb->sb_ino == n_osb.sb_ino &&
			    asb->sb_dev == n_osb.sb_dev) {
				(void) fchdir(savefd);
				(void) close(savefd);
				return (0);
			} else if (unlinkat(ndirfd,
			    get_component(linktop), 0) < 0) {
				warn(*new_name, strerror(errno));
				(void) fchdir(savefd);
				(void) close(savefd);
				return (-1);
			} else {
				n_exists = 0;
			}
		}


		if (link(linkfromp, linktop) != 0) {
			if (errno == ENOENT) {
				if (f_dir_create) {
					if (dirneed(linktop) != 0 ||
					    link(linkfromp, linktop) != 0) {
						char err[PATH_MAX+10];
						/* sprintf can change errno */
						int se = errno;
						(void) snprintf(err,
						    sizeof (err), "link: %s",
						    linkfromp);
						warn(err, strerror(se));
						(void) fchdir(savefd);
						(void) close(savefd);
						return (-1);
					}
				} else {
					warn(*new_name, gettext(
					    "Directories are"
					    " not being created"));
				}
				(void) fchdir(savefd);
				(void) close(savefd);
				return (0);
			} else if ((errno != EXDEV) || (!ispass)) {
				warn(*new_name, strerror(errno));
				(void) fchdir(savefd);
				(void) close(savefd);
				return (-1);
			}
		} else {
			(void) fchdir(savefd);
			(void) close(savefd);
			return (0);
		}
	}
	perm = asb->sb_mode & S_IPERM;

	switch (asb->sb_mode & S_IFMT) {

	case S_IFBLK:
	case S_IFCHR:
		fd = 0;
		if (n_exists) {
			if (asb->sb_rdev == n_osb.sb_rdev) {
				if (perm != operm &&
				    chmod(*new_name, perm) < 0) {
					warn(*new_name, strerror(errno));
					return (-1);
				} else {
					break;
				}
			} else if (REMOVE(*new_name, &n_osb) < 0) {
				warn(*new_name, strerror(errno));
				return (-1);
			} else {
				n_exists = 0;
			}
		}
		if (mknod(*new_name, asb->sb_mode,
		    asb->sb_rdev) < 0) {
			if (errno == ENOENT) {
				if (f_dir_create) {
					if (dirneed(*new_name) < 0 ||
					    mknod(*new_name, (int)asb->sb_mode,
					    (int)asb->sb_rdev) < 0) {
						warn(*new_name,
						    strerror(errno));
						return (-1);
					}
				} else {
					warn(*new_name, gettext(
					    "Directories are"
					    " not being created"));
				}
			} else {
				warn(*new_name, strerror(errno));
				return (-1);
			}
		}

		/*
		 * Preserve the file's user id, group id,
		 * and file mode if required.
		 */
		if (f_stdpax) {
			if (f_user) {
				if (chown(*new_name, asb->sb_uid,
				    -1) != 0) {
					warn(*new_name, gettext(
					    "unable to preserve owner"));
					return (-1);
				}
			}
			if (f_group) {
				if (chown(*new_name, -1,
				    asb->sb_gid) != 0) {
					warn(*new_name, gettext(
					    "unable to preserve group"));
					return (-1);
				}
			}
		} else if (f_owner) {
			if (chown(*new_name, asb->sb_uid,
			    asb->sb_gid) != 0) {
				warn(*new_name, gettext(
				    "unable to preserve owner/group"));
				return (-1);
			}
		}

		if (xhdr_flgs & _X_MTIME) {
			/* Extended hdr: use microsecs */
			struct timeval  timebuf[2];

			if (xhdr_flgs & _X_ATIME && f_extract_access_time) {
				timebuf[0].tv_sec = asb->sb_stat.st_atim.tv_sec;
				timebuf[0].tv_usec =
				    asb->sb_stat.st_atim.tv_nsec/1000;
			} else {
				timebuf[0].tv_sec = time((time_t *)0);
				timebuf[0].tv_usec = 0;
			}
			if (f_mtime) {
				timebuf[1].tv_sec = asb->sb_stat.st_mtim.tv_sec;
				timebuf[1].tv_usec =
				    asb->sb_stat.st_mtim.tv_nsec/1000;
			} else {
				timebuf[1].tv_sec = time((time_t *)0);
				timebuf[1].tv_usec = 0;
			}
			(void) utimes(*new_name, timebuf);
		} else {
			struct timeval  tstamp[2];

			tstamp[0].tv_sec = (f_extract_access_time) ?
			    (asb->sb_atime == -1) ? time((time_t *)0) :
			    asb->sb_atime : time((time_t *)0);
			tstamp[0].tv_usec = 0;
			tstamp[1].tv_sec = f_mtime ? asb->sb_mtime :
			    time((time_t *)0);
			tstamp[1].tv_usec = 0;
			(void) utimes(*new_name, tstamp);
		}

		if (f_mode && (perm != operm) &&
		    (chmod(*new_name, perm) < 0)) {
			warn(*new_name, strerror(errno));
			return (-1);
		}

		/*
		 * The UNIX2003 spec states that if the typeflag field is
		 * set to 3 (character special file), 4 (block special file),
		 * or 6 (FIFO), the meaning of the size field is unspecified.
		 * No logical records are stored on the medium.
		 */
		asb->sb_size = 0;
		return (0);

	case S_IFDIR:

		if (Hiddendir) {
			if (f_mode && perm != operm &&
			    fchmod(ndirfd, perm) < 0) {
				(void) fprintf(stderr,
				    gettext("can't set mode of "
				    "attribute directory of"
				    " file %s\n"),
				    asb->xattr_info.xattrfname);
				return (-1);
			}

			/*
			 * Preserve the hidden directory's user id,
			 * group id, and file mode if required.
			 */
			if (f_stdpax) {
				if (f_user) {
					if (fchownat(ndirfd, ".", asb->sb_uid,
					    -1, 0) != 0) {
						(void) fprintf(stderr, gettext(
						    "can't set owner/group"
						    " of attribute directory of"
						    " file %s\n"),
						    asb->xattr_info.xattrfname);
						return (-1);
					}
				}
				if (f_group) {
					if (fchownat(ndirfd, ".", -1,
					    asb->sb_gid, 0) != 0) {
						(void) fprintf(stderr, gettext(
						    "can't set owner/group"
						    " of attribute directory of"
						    " file %s\n"),
						    asb->xattr_info.xattrfname);
						return (-1);
					}
				}
			} else if (f_owner) {
				if (fchownat(ndirfd, ".", asb->sb_uid,
				    asb->sb_gid, 0) != 0) {
					(void) fprintf(stderr,
					    gettext("can't set owner/group"
					    " of attribute directory of"
					    " file %s\n"),
					    asb->xattr_info.xattrfname);
					return (-1);
				}
			}
			return (0);
		}
		if (n_exists) {
			/* Don't change permissions by default */
			if (f_mode && perm != operm &&
			    chmod(*new_name, perm) < 0) {
				warn(*new_name, strerror(errno));
				return (-1);
			}
		} else if (f_dir_create) {
			if (dirmake(*new_name, asb) < 0 &&
			    dirneed(*new_name) < 0) {
				warn(*new_name, strerror(errno));
				return (-1);
			}
		} else {
			warn(*new_name, gettext("Directories are not being "
			    "created"));
			return (0);
		}
		/*
		 * Add this directory to the list so we can go back
		 * and restore modification times and permissions later.
		 */
		if (((dp = (Dirlist *)mem_get(sizeof (Dirlist))) != NULL) &&
		    ((dp->name = mem_str(*new_name)) != NULL)) {
			dp->perm = asb->sb_mode;
			dp->atime.tv_sec = asb->sb_atime;
			dp->atime.tv_usec = asb->sb_stat.st_atim.tv_nsec / 1000;
			dp->mtime.tv_sec = asb->sb_mtime;
			dp->mtime.tv_usec = asb->sb_stat.st_mtim.tv_nsec / 1000;
			dp->uid = asb->sb_uid;
			dp->gid = asb->sb_gid;
			dp->next = NULL;
			if (dirhead == NULL)
				dirhead = dp;
			else
				dirtail->next = dp;
			dirtail = dp;
		}
		asb->sb_size = 0;
		return (0);

#ifdef	S_IFIFO
	case S_IFIFO:
		fd = 0;
		if (n_exists) {
			/* Don't change permissions by default */
			if (f_mode && perm != operm &&
			    chmod(*new_name, perm) < 0) {
				warn(*new_name, strerror(errno));
				return (-1);
			}
		} else if (mknod(*new_name, (int)asb->sb_mode, 0) < 0) {
			if (errno == ENOENT) {
				if (f_dir_create) {
					if (dirneed(*new_name) < 0 ||
					    mknod(*new_name, (int)asb->sb_mode,
					    0) < 0) {
						warn(*new_name,
						    strerror(errno));
						return (-1);
					}
				} else {
					warn(*new_name, gettext(
					    "Directories are"
					    " not being created"));
				}
			} else {
				warn(*new_name, strerror(errno));
				return (-1);
			}
		}

		/*
		 * Preserve the file's user id, group id, and file mode
		 * if required.
		 */
		if (f_stdpax) {
			if (f_user) {
				if (chown(*new_name, asb->sb_uid,
				    -1) != 0) {
					warn(*new_name, gettext(
					    "unable to preserve owner"));
					return (-1);
				}
			}
			if (f_group) {
				if (chown(*new_name, -1,
				    asb->sb_gid) != 0) {
					warn(*new_name, gettext(
					    "unable to preserve group"));
					return (-1);
				}
			}
		} else if (f_owner) {
			if (chown(*new_name, asb->sb_uid,
			    asb->sb_gid) != 0) {
				warn(*new_name, gettext(
				    "unable to preserve owner/group"));
				return (-1);
			}
		}

		if (xhdr_flgs & _X_MTIME) {
			/* Extended hdr: use microsecs */
			struct timeval  timebuf[2];

			if (xhdr_flgs & _X_ATIME && f_extract_access_time) {
				timebuf[0].tv_sec = asb->sb_stat.st_atim.tv_sec;
				timebuf[0].tv_usec =
				    asb->sb_stat.st_atim.tv_nsec/1000;
			} else {
				timebuf[0].tv_sec = time((time_t *)0);
				timebuf[0].tv_usec = 0;
			}
			if (f_mtime) {
				timebuf[1].tv_sec = asb->sb_stat.st_mtim.tv_sec;
				timebuf[1].tv_usec =
				    asb->sb_stat.st_mtim.tv_nsec/1000;
			} else {
				timebuf[1].tv_sec = time((time_t *)0);
				timebuf[1].tv_usec = 0;
			}
			(void) utimes(*new_name, timebuf);
		} else {
			struct timeval  tstamp[2];

			tstamp[0].tv_sec = (f_extract_access_time) ?
			    (asb->sb_atime == -1) ? time((time_t *)0) :
			    asb->sb_atime : time((time_t *)0);
			tstamp[0].tv_usec = 0;
			tstamp[1].tv_sec = f_mtime ? asb->sb_mtime :
			    time((time_t *)0);
			tstamp[1].tv_usec = 0;
			(void) utimes(*new_name, tstamp);
		}

		if (f_mode && (perm != operm) &&
		    (chmod(*new_name, perm) < 0)) {
			warn(*new_name, strerror(errno));
			return (-1);
		}

		/*
		 * The UNIX2003 spec states that if the typeflag field is
		 * set to 3 (character special file), 4 (block special file),
		 * or 6 (FIFO), the meaning of the size field is unspecified.
		 * No logical records are stored on the medium.
		 */
		asb->sb_size = 0;
		return (0);
#endif				/* S_IFIFO */

#ifdef	S_IFLNK
	case S_IFLNK:
		if (n_exists) {
			if ((ssize = readlink(*new_name, sname, sizeof (sname)))
			    < 0) {
				warn(*new_name, strerror(errno));
				return (-1);
			} else if (strncmp(sname, asb->sb_link, ssize) == 0) {
				return (0);
			} else if (REMOVE(*new_name, &n_osb) < 0) {
				warn(*new_name, strerror(errno));
				return (-1);
			} else {
				n_exists = 0;
			}
		}
		/*
		 * If we had an extended header override
		 * for linkpath, then we need use it.
		 */
		if (f_stdpax && (ohdr_flgs & _X_LINKPATH)) {
			slinkname = oXtarhdr.x_linkpath;
		} else {
			slinkname = asb->sb_link;
		}
		if (symlink(slinkname, *new_name) < 0) {
			if (errno == ENOENT) {
				if (f_dir_create) {
					if (dirneed(*new_name) < 0 ||
					    symlink(slinkname, *new_name) <
					    0) {
						warn(*new_name,
						    strerror(errno));
						return (-1);
					}
				} else {
					warn(*new_name, gettext(
					    "Directories are"
					    " not being created"));
				}
			/*
			 * If f_stdpax, and the extended header
			 * override contained a symlink name which
			 * is longer than PATH_MAX characters,
			 * we need to go ahead and issue a warning
			 * as there is no way to get around symlink
			 * failing with ENAMETOOLONG.
			 */
			} else {
				warn(*new_name, strerror(errno));
				return (-1);
			}
		}

		/*
		 * Preserve the file's user id, group id, and file mode
		 * if required.  Make sure to chown the file itself
		 * rather than the file the symlink references.
		 */
		if (f_stdpax) {
			if (f_user) {
				if (lchown(*new_name, asb->sb_uid,
				    -1) != 0) {
					warn(*new_name, gettext(
					    "unable to preserve owner"));
					return (-1);
				}
			}
			if (f_group) {
				if (lchown(*new_name, -1,
				    asb->sb_gid) != 0) {
					warn(*new_name, gettext(
					    "unable to preserve group"));
					return (-1);
				}
			}
		} else if (f_owner) {
			if (lchown(*new_name, asb->sb_uid,
			    asb->sb_gid) != 0) {
				warn(*new_name, gettext(
				    "unable to preserve owner/group"));
				return (-1);
			}
		}

		/*
		 * Don't need to preserve file times on a symbolic link,
		 * as IEEE Std 1003.1-2001 does not require any association
		 * of file times with symbolic links.
		 */
		return (0);
#endif				/* S_IFLNK */

	case S_IFREG:
		if (o_exists) {
			if (!f_unconditional &&
			    ((o_osb.sb_mtime > asb->sb_mtime) ||
			    ((o_osb.sb_mtime == asb->sb_mtime) &&
			    (o_osb.sb_stat.st_mtim.tv_nsec >
			    asb->sb_stat.st_mtim.tv_nsec)))) {
				/*
				 * indicate to name_match() this one
				 * doesn't count
				 */
				bad_last_match = 1;
				warn(*new_name, gettext("Newer file exists"));
				return (-1);
			}
		}

		if (n_exists) {
			/* Don't unlink unless we intend to preserve owner */
			if (f_stdpax) {
				if (f_owner && (!f_no_overwrite)) {
					if (strlen(*new_name) <= PATH_MAX) {
						ret = unlink(*new_name);
					} else {
						ret = r_unlink(*new_name);
					}
					if (ret < 0) {
						warn(*new_name,
						    strerror(errno));
						return (-1);
					} else
						n_exists = 0;
				}
			} else {
				if (f_owner) {
					ret = unlink(*new_name);
					if (ret < 0) {
						warn(*new_name,
						    strerror(errno));
						return (-1);
					} else
						n_exists = 0;
				}
			}
		}
		if ((fd = openat(ndirfd, get_component(new_namep),
		    O_CREAT|O_RDWR|O_TRUNC, perm)) < 0) {
			if (errno == ENOENT) {
				if (f_dir_create) {
					if (dirneed(new_namep) < 0 ||
					    (fd = openat(ndirfd,
					    get_component(new_namep),
					    O_CREAT|O_RDWR, perm)) < 0) {
						warn(new_namep,
						    strerror(errno));
						return (-1);
					}
				} else {
					/*
					 * the file requires a directory which
					 * does not exist and which the user
					 * does not want created, so skip the
					 * file...
					 */
					warn(new_namep,
					    gettext("Directories are"
					    " not being created"));
					return (0);
				}

			} else if (errno == ENAMETOOLONG) {
				switch (invalidopt) {
				case INV_WRITE:
					/*
					 * The file name is too long.
					 * Print out an error message
					 * and skip the file.
					 */
					if (STREQUAL(new_namep,
					    get_component(new_namep))) {
						warn(new_namep,
						    strerror(ENAMETOOLONG));
						return (-1);
					}

					/*
					 * Try to create the long path
					 * name.
					 */
					if ((f_stdpax) && (f_dir_create)) {
						char	curwd[PATH_MAX + 1];
						if ((getcwd(curwd, PATH_MAX + 1)
						    == NULL)) {
							diag(gettext(
							    "could not get "
							    "current working "
							    "directory "
							    "%s : %s\n"), curwd,
							    strerror(errno));
							return (-1);
						}
						ndirfd = r_dirneed(new_namep,
						    O_RDONLY);
						if (chdir(curwd) < 0) {
							diag(gettext(
							    "could not change "
							    "to current "
							    "working directory "
							    "%s : %s\n"), curwd,
							    strerror(errno));
							if (ndirfd > 0) {
								(void) close(
								    ndirfd);
							}
							return (-1);
						}
						if (ndirfd < 0) {
							diag(gettext(
							    "could not "
							    "open/create "
							    "%s: %s\n"),
							    new_namep,
							    strerror(errno));
							return (-1);
						} else {
							/*
							 * We have the file
							 * descriptor of the
							 * parent directory,
							 * now try to open the
							 * file again.
							 */
							if ((fd = openat(ndirfd,
							    get_component(
							    new_namep), O_CREAT|
							    O_RDWR|O_TRUNC,
							    perm)) < 0) {
								warn(new_namep,
								    strerror(
								    errno));
								return (-1);
							}
						}
					} else {
						/*
						 * The name is too long.
						 * Print out an error message
						 * and skip the file.
						 */
						warn(new_namep,
						    strerror(errno));
						return (-1);
					}
					break;

				case INV_RENAME:
					/*
					 * Prompt user for new file name.
					 * We'll need to start over with
					 * the new file name provided.
					 */
					diag(gettext(
					    "could not open/create %s : %s\n"),
					    new_namep, strerror(errno));
					rename_interact = 1;
					if (get_newname(&new_namep,
					    nnsize, asb) != 0) {
						exit_status = 1;
						return (-1);
					}
					goto begin;

				case INV_BYPASS:
					/*
					 * Skip the file.  Doesn't affect
					 * exit status.
					 */
					diag(gettext(
					    "could not open file %s : %s\n"),
					    new_namep, strerror(errno));
					return (-1);

				default:
					warn(new_namep, strerror(errno));
					return (-1);
				}
			} else {
				warn(new_namep, strerror(errno));
				return (-1);
			}
		}
		break;

	default:
		warn(*new_name, gettext("Unknown filetype"));
		return (-1);

	}

	return (fd);
}


#if defined(O_XATTR)
static int
retry_open_attr(int pdirfd, int cwd, char *fullname, char *pattr, char *name,
    int oflag, mode_t mode)
{
	int dirfd;
	int ofilefd = -1;
	struct timeval times[2];
	mode_t newmode;
	struct stat parentstat;

	/*
	 * We couldn't get to attrdir. See if its
	 * just a mode problem on the parent file.
	 * for example: a mode such as r-xr--r--
	 * on a ufs file system without extended
	 * system attribute support won't let us
	 * create an attribute dir if it doesn't
	 * already exist, and on a ufs file system
	 * with extended system attribute support
	 * won't let us open the attribute for
	 * write.
	 *
	 * If file has a non-trivial ACL, then save it
	 * off so that we can place it back on after doing
	 * chmod's.
	 */
	if ((dirfd = openat(cwd, (pattr == NULL) ? fullname : pattr,
	    O_RDONLY)) == -1) {
		return (-1);
	}
	if (fstat(dirfd, &parentstat) == -1) {
		(void) fprintf(stderr, gettext(
		    "%s: Cannot stat %sfile %s"), myname,
		    (pdirfd == -1) ? "" : gettext("parent of "),
		    (pdirfd == -1) ? fullname : name);
		(void) close(dirfd);
		return (-1);
	}

	newmode = S_IWUSR | parentstat.st_mode;
	if (fchmod(dirfd, newmode) == -1) {
		(void) fprintf(stderr, gettext(
		    "%s: Cannot change mode of %sfile %s to %o"), myname,
		    (pdirfd == -1) ? "" : gettext("parent of "),
		    (pdirfd == -1) ? fullname : name, newmode);
		(void) close(dirfd);
		return (-1);
	}


	if (pdirfd == -1) {
		/*
		 * We weren't able to create the attribute directory before.
		 * Now try again.
		 */
		ofilefd = attropen(fullname, ".", oflag);
	} else {
		/*
		 * We weren't able to create open the attribute before.
		 * Now try again.
		 */
		ofilefd = openat(pdirfd, name, oflag, mode);
	}

	/*
	 * Put mode back to original
	 */
	if (fchmod(dirfd, parentstat.st_mode) == -1) {
		(void) fprintf(stderr, gettext(
		    "Cannot restore permissions of %sfile %s to %o"),
		    (pdirfd == -1) ? "" : gettext("parent of "),
		    (pdirfd == -1) ? fullname : name, newmode);
	}

	/*
	 * Put back time stamps
	 */

	times[0].tv_sec = parentstat.st_atime;
	times[0].tv_usec = 0;
	times[1].tv_sec = parentstat.st_mtime;
	times[1].tv_usec = 0;

	(void) futimesat(cwd, (pattr == NULL) ? fullname : pattr, times);

	(void) close(dirfd);

	return (ofilefd);
}
#endif


#if defined(O_XATTR)
/*
 * Recursively open attribute directories until the attribute directory
 * containing the specified attribute, attrname, is opened.
 *
 * Currently, only 2 directory levels of attributes are supported, (i.e.,
 * extended system attributes on extended attributes).  The following are
 * the possible input combinations:
 *	1.  Open the attribute directory of the base file (don't change
 *	    into it).
 *		attr_parent = NULL
 *		attrname = '.'
 *	2.  Open the attribute directory of the base file and change into it.
 *		attr_parent = NULL
 *		attrname = <attr> | <sys_attr>
 *	3.  Open the attribute directory of the base file, change into it,
 *	    then recursively call open_attr_dir() to open the attribute's
 *	    parent directory (don't change into it).
 *		attr_parent = <attr>
 *		attrname = '.'
 *	4.  Open the attribute directory of the base file, change into it,
 *	    then recursively call open_attr_dir() to open the attribute's
 *	    parent directory and change into it.
 *		attr_parent = <attr>
 *		attrname = <attr> | <sys_attr>
 *
 * An attribute directory will be opened only if the underlying file system
 * supports the attribute type, and if the command line specifications
 * (f_extended_attr and f_sys_attr) enable the processing of the attribute
 * type.
 *
 * On succesful return, attr_parentfd will be the file descriptor of the
 * opened attribute directory.  In addition, if the attribute is a read-write
 * extended system attribute, rw_sysattr will be set to 1, otherwise
 * it will be set to 0.
 *
 * Possible return values:
 * 	ATTR_OK		Successfully opened and, if needed, changed into the
 *			attribute directory containing attrname.
 *	ATTR_SKIP	The command line specifications don't enable the
 *			processing of the attribute type.
 * 	ATTR_CHDIR_ERR	An error occurred while trying to change into an
 *			attribute directory.
 * 	ATTR_OPEN_ERR	An error occurred while trying to open an
 *			attribute directory.
 *	ATTR_XATTR_ERR	The underlying file system doesn't support extended
 *			attributes.
 *	ATTR_SATTR_ERR	The underlying file system doesn't support extended
 *			system attributes.
 */
int
open_attr_dir(char *attrname, char *dirp, int cwd, char *attr_parent,
    int *attr_parentfd, int *rw_sysattr)
{
	attr_status_t	rc;
	int		firsttime = (*attr_parentfd == -1);
	int		saveerrno;
	int		ext_attr;
	char		*dirpath = dirp;

	/*
	 * open_attr_dir() was recursively called (input combination number 4),
	 * close the previously opened file descriptor as we've already changed
	 * into it.
	 */
	if (!firsttime) {
		(void) close(*attr_parentfd);
		*attr_parentfd = -1;
	}

	/*
	 * Verify that the underlying file system supports the restoration
	 * of the attribute.
	 */
	if ((rc = verify_attr_support(dirpath, firsttime, ARC_RESTORE,
	    &ext_attr)) != ATTR_OK) {
		if (*attr_parentfd != -1) {
			(void) close(*attr_parentfd);
			*attr_parentfd = -1;
		}
		return (rc);
	}

	/* Open the base file's attribute directory */
	if ((*attr_parentfd = attropen(dirpath, ".", O_RDONLY)) == -1) {
		/*
		 * Save the errno from the attropen so it can be reported
		 * if the retry of the attropen fails.
		 */
		saveerrno = errno;
		if (!f_extract || (*attr_parentfd = retry_open_attr(-1, cwd,
		    dirpath, NULL, ".", O_RDONLY, 0)) == -1) {
			(void) close(*attr_parentfd);
			*attr_parentfd = -1;
			errno = saveerrno;
			return (ATTR_OPEN_ERR);
		}
	}

	/*
	 * Change into the parent attribute's directory unless we are
	 * processing the hidden attribute directory of the base file itself.
	 */
	if ((Hiddendir == 0) || (firsttime && (attr_parent != NULL))) {
		if (fchdir(*attr_parentfd) != 0) {
			saveerrno = errno;
			(void) close(*attr_parentfd);
			*attr_parentfd = -1;
			errno = saveerrno;
			return (ATTR_CHDIR_ERR);
		}
	}

	/* Determine if the attribute should be processed */
	if ((rc = verify_attr(attrname, attr_parent, 1,
	    rw_sysattr)) != ATTR_OK) {
		saveerrno = errno;
		(void) close(*attr_parentfd);
		*attr_parentfd = -1;
		errno = saveerrno;
		return (rc);
	}

	/*
	 * If the attribute is an extended system attribute of an attribute
	 * (i.e., <attr>/<sys_attr>), then recursively call open_attr_dir() to
	 * open the attribute directory of the parent attribute.
	 */
	if (firsttime && (attr_parent != NULL)) {
		return (open_attr_dir(attrname, attr_parent, *attr_parentfd,
		    attr_parent, attr_parentfd, rw_sysattr));
	}

	return (ATTR_OK);
}
#endif

/*
 * openin - open the next input file
 *
 * DESCRIPTION
 *
 *	Openin will attempt to open the next file for input.  If the file is
 *	a special file, such as a directory, FIFO, link, character- or
 *	block-special file, then the file size field of the stat structure
 *	is zeroed to make sure that no data is written out for the file.
 *	If the file is a special file, then a file descriptor of 0 is
 *	returned to the caller, which is handled specially.  If the file
 *	is a regular file, then the file is opened and a file descriptor
 *	to the open file is returned to the caller.
 *
 * PARAMETERS
 *
 *	char   *name	- pointer to the name of the file to open
 *	Stat   *asb	- pointer to the stat block for the file to open
 *
 * RETURNS
 *
 * 	Returns a file descriptor, 0 if no data exists, or -1 at EOF. This
 *	kludge works because standard input is in use, preventing open() from
 *	returning zero.
 */


int
openin(char *name, Stat *asb)
{
	int	fd;

	switch (asb->sb_mode & S_IFMT) {

	case S_IFDIR:
		asb->sb_nlink = 1;
		asb->sb_size = 0;
		return (0);

#ifdef	S_IFLNK
	case S_IFLNK:
		if ((asb->sb_size = readlink(name,
		    asb->sb_link, sizeof (asb->sb_link) - 1)) < 0) {
			warn(name, strerror(errno));
			return (0);
		}
		asb->sb_link[asb->sb_size] = '\0';
		return (0);
#endif	/* S_IFLNK */

	case S_IFREG:
		if (asb->sb_size == 0) {
			return (0);
		}
#if defined(O_XATTR)
		if (asb->xattr_info.xattraname != NULL) {
			int	rw_sysattr;
			int	tmpfd = -1;

			/* open and cd into the attribute's parent fd */
			(void) fchdir(asb->xattr_info.xattr_baseparent_fd);
			(void) open_attr_dir(asb->xattr_info.xattraname,
			    asb->xattr_info.xattrfname,
			    asb->xattr_info.xattr_baseparent_fd,
			    (asb->xattr_info.xattraparent == NULL) ? NULL :
			    asb->xattr_info.xattraparent, &tmpfd,
			    &rw_sysattr);

			if (tmpfd != -1) {
				(void) close(tmpfd);
			}

			/* open the attribute */
			if ((tmpfd < 0) || ((fd = open(
			    asb->xattr_info.xattraname, O_RDONLY)) < 0)) {
				(void) fprintf(stderr, gettext(
				    "%s: %sattribute %s: %s\n"), myname, name,
				    asb->xattr_info.xattr_rw_sysattr ? gettext(
				    "system ") : "", asb->xattr_info.xattrapath,
				    strerror(errno));
			}
		} else {
			if ((fd = open(name, O_RDONLY)) < 0) {
				warn(name, strerror(errno));
			}
		}
#else
		if ((fd = open(name, O_RDONLY)) < 0) {
			warn(name, strerror(errno));
		}
#endif
		return (fd);

	default:
		asb->sb_size = 0;
		return (0);

	}
}

/*
 * skip over extra slashes in string.
 *
 * For example:
 * /usr/tmp/////
 *
 * would return pointer at
 * /usr/tmp/////
 *         ^
 */
static char *
skipslashes(char *string, char *start)
{
	while ((string > start) && *(string - 1) == '/') {
		string--;
	}

	return (string);
}

void
chop_endslashes(char *path)
{
	char *end, *ptr;

	end = &path[strlen(path) -1];
	if (*end == '/' && end != path) {
		ptr = skipslashes(end, path);
		if (ptr != NULL && ptr != path) {
			*ptr = '\0';
		}
	}
}

/*
 * Return the parent directory of a given path.
 *
 * Examples:
 * /usr/tmp return /usr
 * /usr/tmp/file return /usr/tmp
 * /  returns .
 * /usr returns /
 * file returns .
 *
 * dir is assumed to be at least as big as path.
 */
void
get_parent(char *path, char *dir)
{
	char	*s;
	char	*tmpdir;
	int	len;

	len = strlen(path);
	if ((tmpdir = calloc(1, len + 1)) == NULL) {
		fatal(gettext("out of memory"));
	}
	if (!f_stdpax) {
		if (len > PATH_MAX) {
			fatal(gettext("pathname is too long"));
		}
	}
	(void) strncpy(tmpdir, path, len);
	chop_endslashes(tmpdir);

	if ((s = strrchr(tmpdir, '/')) == NULL) {
		(void) strcpy(dir, ".");
	} else {
		s = skipslashes(s, tmpdir);
		*s = '\0';
		if (s == tmpdir)
			(void) strcpy(dir, "/");
		else
			(void) strcpy(dir, tmpdir);
	}
	free(tmpdir);
}
#if !defined(O_XATTR)
int
openat64(int fd, char *name, int oflag, mode_t cmode)
{
	return (open64(name, oflag, cmode));
}

int
openat(int fd, char *name, int oflag, mode_t cmode)
{
	return (open(name, oflag, cmode));
}

int
fchownat(int fd, char *name, uid_t owner, gid_t group, int flag)
{
	if (flag == AT_SYMLINK_NOFOLLOW)
		return (lchown(name, owner, group));
	else
		return (chown(name, owner, group));
}

int
renameat(int fromfd, char *old, int tofd, char *new)
{
	return (rename(old, new));
}

int
futimesat(int fd, char *path, struct timeval times[2])
{
	return (utimes(path, times));
}

int
unlinkat(int dirfd, char *path, int flag)
{
	if (flag == AT_REMOVEDIR) {
		return (rmdir(path));
	} else {
		return (unlink(path));
	}
}

int
fstatat(int fd, char *path, struct stat *buf, int flag)
{
	if (flag == AT_SYMLINK_NOFOLLOW)
		return (lstat(path, buf));
	else
		return (stat(path, buf));
}

int
attropen(char *file, char *attr, int omode, mode_t cmode)
{
	errno = ENOTSUP;
	return (-1);
}
#endif
