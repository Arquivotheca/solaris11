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
 * pass.c - handle the pass option of cpio
 *
 * DESCRIPTION
 *
 *	These functions implement the pass options in PAX.  The pass option
 *	copies files from one directory hierarchy to another.
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

#include <sys/utime.h>
#include <dirent.h>
#include "pax.h"
#if defined(_PC_SATTR_ENABLED)
#include <libgen.h>
#include <attr.h>
#include <libcmdutils.h>
#endif

/* Function Prototypes */

static int passitem(char **, Stat *, int, char *, int *);
static int pass_common(char **, size_t *, char *, Stat *, int);
#if defined(O_XATTR)
static void pass_xattrs(char **, size_t *, char *, char *, int);
#endif

/*
 * pass - copy within the filesystem
 *
 * DESCRIPTION
 *
 *	Pass copies the named files from the current directory hierarchy to
 *	the directory pointed to by dirname.
 *
 * PARAMETERS
 *
 *	char	*dirname	- name of directory to copy named files to.
 *
 */


void
pass(char *dirname)
{
	char		*name;
	char		*orig_name;
	int		fd;
	int		status;
	Stat		sb;
	size_t		namesz = PATH_MAX + 1;
	size_t		osz;

	if ((name = calloc(namesz, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}
	(void) memset(&sb, 0, sizeof (sb));
	Hiddendir = 0;
	while (name_next(&name, &namesz, &sb) >= 0 &&
	    (fd = openin(name, &sb)) >= 0) {
		osz = namesz;
		if ((orig_name = calloc(osz, sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) strcpy(orig_name, name);

		status = pass_common(&name, &namesz, dirname,
		    &sb, fd);
#if defined(O_XATTR)
		if ((f_extended_attr || f_sys_attr) && status == 0) {
			pass_xattrs(&orig_name, &osz, dirname, NULL, -1);
			init_xattr_info(&sb);
			Hiddendir = 0;
		}
#endif
		free(orig_name);
		(void) memset(&sb, 0, sizeof (sb));
	}
}

/*
 * passitem - copy one file
 *
 * DESCRIPTION
 *
 *	Passitem copies a specific file to the named directory
 *
 * PARAMETERS
 *
 *	char   **from	- the name of the file to open
 *	Stat   *asb	- the stat block associated with the file to copy
 *	int    ifd	- the input file descriptor for the file to copy
 *	char   *dir	- the directory to copy it to
 *	int    *ndirfd  - directory fd of new location
 *
 * RETURNS
 *
 * 	Returns given input file descriptor or -1 if an error occurs.
 *
 * ERRORS
 */

static int
passitem(char **from, Stat *asb, int ifd,
    char *dir, int *ndirfd)
{
	int		ofd;
	size_t		len;
	struct timeval	tstamp[2];
	char		*to;
	char		*pdir;
	Link		*linkp, *linktmpp;

begin:
	len = strlen(*from) + strlen(dir) + 2; 	/* add 1 for '/' */
	if ((to = calloc(len, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}
	if ((pdir = calloc(len, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}
	if (nameopt(strcat(strcat(strncpy(to, dir, len - 1), "/"),
	    *from)) < 0) {
		free(pdir);
		free(to);
		return (-1);
	}

	/*
	 * Open directories
	 */
	*ndirfd = -1;
	if (asb->xattr_info.xattraname != NULL) {
		int	rw_sysattr;

		(void) fchdir(asb->xattr_info.xattr_baseparent_fd);
		(void) open_attr_dir(asb->xattr_info.xattraname,
		    to, asb->xattr_info.xattr_baseparent_fd,
		    (asb->xattr_info.xattraparent == NULL) ? NULL :
		    asb->xattr_info.xattraparent, ndirfd, &rw_sysattr);
		if (*ndirfd == -1) {
			(void) fprintf(stderr, gettext(
			    "%s: could not open attribute"
			    " directory of %s%s%sfile %s: %s\n"), myname,
			    (asb->xattr_info.xattraparent == NULL) ? "" :
			    gettext("attribute "),
			    (asb->xattr_info.xattraparent == NULL) ? "" :
			    asb->xattr_info.xattraparent,
			    (asb->xattr_info.xattraparent == NULL) ? "" :
			    gettext(" of "), to, strerror(errno));
			free(pdir);
			free(to);
			return (-1);
		}
	} else {
		get_parent(to, pdir);
		*ndirfd = open(pdir, O_RDONLY);
		if (*ndirfd == -1) {
			switch (errno) {
			case ENOENT:
				if (f_dir_create) {
					/*
					 * The directory name we are creating
					 * may be PATH_MAX characters or less,
					 * but the full pathname may be over
					 * PATH_MAX characters.  If the full
					 * pathname is greather than PATH_MAX
					 * characters, only create the
					 * directory if invalid=write was
					 * specified, prompt if invalid=rename
					 * was specified, otherwise, don't
					 * create the directory for the
					 * pathname that is too long.
					 */
					if ((len <= PATH_MAX) ||
					    ((len > PATH_MAX) &&
					    (invalidopt == INV_WRITE))) {
						if ((dirneed(to) != 0) ||
						    ((*ndirfd = open(pdir,
						    O_RDONLY)) < 0)) {
							diag(gettext(
							    "Directories are "
							    "not being "
							    "created: "
							    "%s: %s\n"), to,
							    strerror(errno));
							exit_status = 1;
						}

					} else if (invalidopt == INV_RENAME) {
						diag(gettext(
						    "could not "
						    "open/create "
						    "directory "
						    "%s: %s\n"), pdir,
						    strerror(errno));
						rename_interact = 1;
						if (get_newname(&to,
						    &len, asb) != 0) {
							exit_status = 1;
							free(pdir);
							free(to);
							return (-1);
						}
						free(pdir);
						free(to);
						goto begin;

					} else {
						errno = ENAMETOOLONG;
						diag(gettext(
						    "Directories are "
						    "not being "
						    "created: "
						    "%s: %s\n"), to,
						    strerror(errno));
						free(pdir);
						free(to);
						return (-1);
					}
				} else {
					warn(to,
					    gettext("Directories are "
					    "not being created"));
					free(pdir);
					free(to);
					return (-1);
				}
				break;

			case ENAMETOOLONG:
				switch (take_action(&to, &len,
				    pdir, ndirfd, asb)) {
				case PAX_FAIL:
					free(pdir);
					free(to);
					return (-1);
				case PAX_RETRY:
					free(pdir);
					free(to);
					goto begin;
				default:
					break;
				}
				break;

			default:
				break;
			}
		}
	}
	free(pdir);

	if (asb->xattr_info.xattraname != NULL) {
		if ((strlen(asb->xattr_info.xattraname) + 1) > len) {
			len = strlen(asb->xattr_info.xattraname) + 1;
			if ((to = realloc(to, len)) == NULL) {
				fatal(gettext("out of memory"));
			}
		}
		if (nameopt(strcpy(to, asb->xattr_info.xattraname)) < 0) {
			free(to);
			return (-1);
		}
	}

	if (asb->sb_nlink > 1) {
		if (asb->xattr_info.xattraname != NULL) {
			if (asb->xattr_info.xattr_linkaname != NULL) {
				free(asb->xattr_info.xattr_linkaname);
			}
			if ((asb->xattr_info.xattr_linkaname = malloc(
			    strlen(asb->xattr_info.xattraname) + 1)) == NULL) {
				fatal(gettext("out of memory"));
			}
			(void) strcpy(asb->xattr_info.xattr_linkaname,
			    asb->xattr_info.xattraname);
		}
		(void) linkto(to, asb);
	}

	linkp = islink(to, asb);
	linktmpp = NULL;

	if (f_link) {
		/*
		 * See if we have file which can be linked in the target dir.
		 * If we don't, we call linkto() once the file is created in
		 * the target directory. We don't make a hardlink of directory.
		 */
		if (linkp == NULL && (asb->sb_mode & S_IFMT) != S_IFDIR) {
			/*
			 * There is no file to be linked in the target dir.
			 * Create a temporary Link for the source file.
			 */
			if ((linktmpp = linktemp(*from, asb)) == NULL)
				fatal(gettext("out of memory"));
			linkp = linktmpp;
		}
	}

	ofd = openout(&to, &len, to, asb, linkp, 1, *ndirfd, *ndirfd);

	if (linktmpp != NULL)
		linkfree(linktmpp);

	if (ofd < 0) {
		free(to);
		return (-1);
	}
	if (ofd > 0) {
		/* Get holey file data */
		if (f_pax && (ifd > 0)) {
			get_holesdata(ifd, asb->sb_size);
		}
		passdata(*from, ifd, to, ofd, asb);

		/*
		 * We now have a file in the target directory which
		 * other symlink can link to.
		 */
		if (f_link || f_follow_links)
			(void) linkto(to, asb);
	}

	/*
	 * Don't try to preserve time or permissions for anything in the
	 * attribute directory of an attribute or for read-write system
	 * attributes.
	 */
	if ((asb->xattr_info.xattraname == NULL) ||
	    ((asb->xattr_info.xattraparent == NULL) &&
	    !asb->xattr_info.xattr_rw_sysattr)) {
		if (f_extract_access_time) {
			if (ohdr_flgs & _X_ATIME) {
				tstamp[0].tv_sec = oXtarhdr.x_atime.tv_sec;
				tstamp[0].tv_usec =
				    oXtarhdr.x_atime.tv_nsec / 1000;
			} else {
				tstamp[0].tv_sec = asb->sb_stat.st_atim.tv_sec;
				tstamp[0].tv_usec =
				    asb->sb_stat.st_atim.tv_nsec / 1000;
			}
		} else {
			tstamp[0].tv_sec = time((time_t *)0);
			tstamp[0].tv_usec = 0;
		}
		if (f_mtime) {
			if (ohdr_flgs & _X_MTIME) {
				tstamp[1].tv_sec = oXtarhdr.x_mtime.tv_sec;
				tstamp[1].tv_usec =
				    oXtarhdr.x_mtime.tv_nsec / 1000;
			} else {
				tstamp[1].tv_sec = asb->sb_stat.st_mtim.tv_sec;
				tstamp[1].tv_usec =
				    asb->sb_stat.st_mtim.tv_nsec/1000;
			}
		} else {
			tstamp[1].tv_sec = time((time_t *)0);
			tstamp[1].tv_usec = 0;
		}

		/*
		 * ofd could be set to stdin as instead of returning with a
		 * file descriptor, openout() could have returned with a
		 * status of 0, having the side effect of setting ofd to be
		 * stdin.  We need to ensure we don't change the permission
		 * on stdin.
		 */
		if ((ofd != STDIN) && f_mode && !S_ISLNK(asb->sb_mode)) {
			if (fchmod(ofd, asb->sb_mode & S_IPERM) == -1) {
				warn(myname, gettext(
				    "unable to preserve file mode"));
			}
		}

		if (f_stdpax && (f_owner || f_user || f_group)) {
			int	userid = asb->sb_uid;
			int	groupid = asb->sb_gid;

			if (ohdr_flgs & _X_UID) {
				userid = oXtarhdr.x_uid;
			}
			if (ohdr_flgs & _X_GID) {
				groupid = oXtarhdr.x_gid;
			}

			/*
			 * Preserve the user id, group id, and mode
			 * if required.  If the file is a symlink,
			 * chown the file itself, not the file the
			 * symlink references.
			 */
			if (fchownat(*ndirfd, get_component(to),
			    userid, groupid,
			    AT_SYMLINK_NOFOLLOW) < 0) {
				warn(to, gettext(
				    "unable to preserve owner/group"));
			} else if ((ofd != STDIN) &&
			    f_mode && !S_ISLNK(asb->sb_mode)) {
				if (fchmod(ofd,
				    asb->sb_mode & (S_IPERM |
				    S_ISUID | S_ISGID)) == -1) {
					warn(to, gettext("unable to "
					    "preserve file mode"));
				}
			}
		} else if (f_owner) {
			/*
			 * Preserve the user id, group id, and mode
			 * if required.  If the file is a symlink,
			 * chown the file itself, not the file the
			 * symlink references.
			 */
			if (fchownat(*ndirfd, get_component(to),
			    asb->sb_uid, asb->sb_gid,
			    AT_SYMLINK_NOFOLLOW) < 0) {
				warn(to, gettext(
				    "unable to preserve owner/group"));
			} else if ((ofd != STDIN) &&
			    f_mode && !S_ISLNK(asb->sb_mode)) {
				if (fchmod(ofd,
				    asb->sb_mode & (S_IPERM |
				    S_ISUID | S_ISGID)) == -1) {
					warn(to, gettext("unable to "
					    "preserve file mode"));
				}
			}
		}

		(void) futimesat(*ndirfd, get_component(to), tstamp);
	}

	if (ofd != STDIN) {
		(void) close(ofd);
	}

	free(to);
	return (ifd);
}

static int
pass_common(char **name, size_t *namesz, char *dirname, Stat *asb, int fd)
{
	struct timeval	tstamp[2];
	int dirfd;
	int olddirfd;

	if (rplhead != (Replstr *)NULL) {
		rpl_name(name, namesz);
		if (strlen(*name) == 0) {
			if (fd > 0)
				(void) close(fd);
			return (1);
		}
	}

	if (f_pax) {

		/* Merge extended header override data */
		if ((get_oxhdrdata() == 0) && (get_oghdrdata() == 0)) {
			merge_xhdrdata();
			if (ohdr_flgs & _X_PATH) {
				int	len = strlen(oXtarhdr.x_path) + 1;

				if (len > *namesz) {
					*namesz = len;
					if ((*name = realloc(*name, *namesz *
					    sizeof (char))) == NULL) {
						fatal(gettext("out of memory"));
					}
				}
				(void) strlcpy(*name, oXtarhdr.x_path, *namesz);
			}
		} else {
			/* skip file... */
			if (fd > 0)
				(void) close(fd);
			return (1);
		}
	}

	if (get_disposition(PASS, *name, *namesz) ||
	    get_newname(name, namesz, asb)) {
		/* skip file... */
		if (fd > 0)
			(void) close(fd);
		return (1);
	}

	if (asb->xattr_info.xattraparent != NULL) {
		(void) fchdir(asb->xattr_info.xattr_baseparent_fd);
	}
	if (passitem(name, asb, fd, dirname, &dirfd)) {
		if (fd > 0) {
			(void) close(fd);
		}

		/*
		 * Don't try to preserve time on anything in the attribute
		 * directory of an attribute or on a read-write system
		 * attribute.
		 */
		if ((asb->xattr_info.xattraname == NULL) ||
		    ((asb->xattr_info.xattraparent == NULL) &&
		    !asb->xattr_info.xattr_rw_sysattr)) {
			if (f_access_time) {
				/* -t option: preserve access time of input */
				tstamp[0].tv_sec = asb->sb_stat.st_atim.tv_sec;
				tstamp[0].tv_usec =
				    asb->sb_stat.st_atim.tv_nsec / 1000;
				tstamp[1].tv_sec = asb->sb_stat.st_mtim.tv_sec;
				tstamp[1].tv_usec =
				    asb->sb_stat.st_mtim.tv_nsec / 1000;
				if (asb->xattr_info.xattraname != NULL) {
					olddirfd = attropen(*name, ".",
					    O_RDONLY);
					if (olddirfd == -1) {
						(void) fprintf(stderr, gettext(
						    "%s: could not open "
						    "attribute directory of "
						    "file %s: %s\n"), myname,
						    *name, strerror(errno));
						return (-1);
					}
				} else {
					char	*pdir;

					if ((pdir = calloc(*namesz,
					    sizeof (char))) == NULL) {
						fatal(gettext("out of memory"));
					}
					get_parent(*name, pdir);
					olddirfd = open(pdir, O_RDONLY);
					if (olddirfd == -1) {
						(void) fprintf(stderr, gettext(
						    "%s: could not open "
						    "directory %s: %s\n"),
						    myname, pdir,
						    strerror(errno));
						free(pdir);
						return (-1);
					}
					free(pdir);
				}
				(void) futimesat(olddirfd, get_component(*name),
				    tstamp);
				if (olddirfd > 0) {
					(void) close(olddirfd);
				}
			}
		}
	}
	if (dirfd > 0) {
		(void) close(dirfd);
	}
	if (f_verbose) {
		char	*buf = NULL;
		size_t	bufsz = 0;

		if (dirname != NULL) {
			bufsz = strlen(dirname);
			if (dirname[bufsz - 1] != '/') {
				bufsz++;
			}
		}
		if (*name != NULL) {
			bufsz += strlen(*name);
			if ((*name)[0] == '/') {
				bufsz--;
			}
		}
		if (bufsz > 0) {
			if ((buf = calloc(++bufsz, sizeof (char))) == NULL) {
				fatal(gettext("out of memory"));
			}

			if (dirname != NULL) {
				(void) strlcpy(buf, dirname, bufsz);
				if (buf[strlen(buf) - 1] != '/')
					(void) strlcat(buf, "/", bufsz);
			}
			if (*name != NULL) {
				(void) strlcat(buf,
				    ((*name)[0] == '/' ? &(*name)[1] : *name),
				    bufsz);
			}
		}
		/*
		 * If attribute fix up xattrfname
		 */
		if (asb->xattr_info.xattraname != NULL) {
			if (asb->xattr_info.xattrfname != NULL) {
				free(asb->xattr_info.xattrfname);
			}
			if (bufsz > 0) {
				if ((asb->xattr_info.xattrfname = malloc(
				    bufsz)) == NULL) {
					fatal(gettext("out of memory"));
				}
				(void) strlcpy(asb->xattr_info.xattrfname, buf,
				    bufsz);
			} else {
				asb->xattr_info.xattrfname = NULL;
			}
		}
		print_entry(buf, asb);
	}

	if (Xtarhdr.x_holesdata != NULL) {
		free(Xtarhdr.x_holesdata);
		Xtarhdr.x_holesdata = NULL;
	}
	return (0);
}

#if defined(O_XATTR)
static void
pass_xattrs(char **name, size_t *namesz, char *dirname, char *attrparent,
    int baseparent_fd)
{
	Stat		sb;
	char		*filename = (attrparent == NULL) ? *name : attrparent;
	int		fd;
	int		ifd;
	int		dirfd;
	int		arc_rwsysattr = 0;
	int		rw_sysattr = 0;
	int		ext_attr = 0;
	int		anamelen = 0;
	int		apathlen;
	int		rc;
	DIR		*dirp;
	struct dirent   *dp;

	/*
	 *  If the underlying file system supports it, then archive the extended
	 * attributes if -@ was specified, and the extended system attributes
	 * if -/ was specified.
	 */
	if (verify_attr_support(filename, (attrparent == NULL), ARC_CREATE,
	    &ext_attr) != ATTR_OK) {
		return;
	}

	/*
	 * Only want to archive a read-write extended system attribute file
	 * if it contains extended system attribute settings that are not the
	 * default values.
	 */
#if defined(_PC_SATTR_ENABLED)
	if (f_sys_attr) {
		int	filefd;
		nvlist_t *slist = NULL;

		/* Determine if there are non-transient system attributes */
		errno = 0;
		if ((filefd = open(filename, O_RDONLY)) == -1) {
			if (attrparent == NULL) {
				(void) fprintf(stderr, gettext(
				    "%s: unable to open file %s: %s"),
				    myname, *name, strerror(errno));
			}
			return;
		}
		if (((slist = sysattr_list(basename(myname), filefd,
		    filename)) != NULL) || (errno != 0)) {
			arc_rwsysattr = 1;
		}
		if (slist != NULL) {
			(void) nvlist_free(slist);
			slist = NULL;
		}
		(void) close(filefd);
	}

	/*
	 * If we aren't archiving extended system attributes, and we are
	 * processing an attribute, or if we are archiving extended system
	 * attributes, and there are are no extended attributes, then there's
	 * no need to open up the attribute directory of the file unless the
	 * extended system attributes are not transient (i.e, the system
	 * attributes are not the default values).
	 */
	if ((arc_rwsysattr == 0) && ((attrparent != NULL) ||
	    (f_sys_attr && !ext_attr))) {
		return;
	}

#endif	/* _PC_SATTR_ENABLED */

	/* open the parent attribute directory */
	if ((fd = attropen(filename, ".", O_RDONLY)) == -1) {
		(void) fprintf(stderr, gettext(
		    "%s: cannot open attribute directory of "
		    "%s%s%sfile %s: %s\n"), myname,
		    (attrparent == NULL) ? "" : gettext("attribute "),
		    (attrparent == NULL) ? "" : attrparent,
		    (attrparent == NULL) ? "" : gettext(" of "),
		    *name, strerror(errno));
		return;
	}

	dirfd = dup(fd);
	if (dirfd == -1) {
		(void) fprintf(stderr,
		    gettext(
		    "%s: cannot dup(2) attribute"
		    " directory descriptor of file %s: %s\n"),
		    myname, *name, strerror(errno));
		if (fd > 0) {
			(void) close(fd);
		}
		return;
	}

	if ((dirp = fdopendir(dirfd)) == NULL) {
		(void) fprintf(stderr,
		    gettext(
		    "%s: cannot convert attribute descriptor into DIR: %s\n"),
		    myname, strerror(errno));
		if (dirfd > 0) {
			(void) close(dirfd);
		}
		if (fd > 0) {
			(void) close(fd);
		}
		return;
	}

	if (attrparent == NULL) {
		baseparent_fd = save_cwd();
	}

	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, "..") == 0) {
			continue;
		}

		/* Determine if this attribute should be archived */
		if (verify_attr(dp->d_name, attrparent, arc_rwsysattr,
		    &rw_sysattr) != ATTR_OK) {
			continue;
		}

		if (strcmp(dp->d_name, ".") == 0) {
			Hiddendir = 1;
		} else {
			Hiddendir = 0;
		}

		(void) memset(&sb, 0, sizeof (sb));
		if (fstatat(fd, dp->d_name, &sb.sb_stat,
		    AT_SYMLINK_NOFOLLOW) == -1) {
			(void) fprintf(stderr,
			    gettext("%s: Cannot stat %s%s%sattribute %s of"
			    " file %s: %s\n"), myname,
			    (attrparent == NULL) ? "" : rw_sysattr ?
			    gettext("system attribute ") :
			    gettext("attribute "),
			    (attrparent == NULL) ? "" : dp->d_name,
			    (attrparent == NULL) ? "" : gettext(" of "),
			    (attrparent == NULL) ? dp->d_name : attrparent,
			    *name, strerror(errno));
			continue;
		}

		ifd = openat(fd, dp->d_name, O_RDONLY);
		if (ifd == -1) {
			(void) fprintf(stderr, gettext(
			    "%s: Cannot open %s%s%sattribute %s of file %s: "
			    "%s\n"), myname,
			    (attrparent == NULL) ? "" : rw_sysattr ?
			    gettext("system attribute ") :
			    gettext("attribute "),
			    (attrparent == NULL) ? "" : dp->d_name,
			    (attrparent == NULL) ? "" : gettext(" of "),
			    (attrparent == NULL) ? dp->d_name : attrparent,
			    *name, strerror(errno));
			continue;
		}

		anamelen = strlen(dp->d_name) + 1;
		apathlen = anamelen;
		if (attrparent != NULL) {
			apathlen += strlen(attrparent) + 1; /* add 1 for '/' */
		}

		if ((sb.xattr_info.xattraname = malloc(anamelen)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) strcpy(sb.xattr_info.xattraname, dp->d_name);

		if (attrparent != NULL) {
			STRDUP(sb.xattr_info.xattraparent, attrparent);
		}

		if ((sb.xattr_info.xattrapath = malloc(apathlen)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) snprintf(sb.xattr_info.xattrapath, apathlen, "%s%s%s",
		    (attrparent == NULL) ? "" : attrparent,
		    (attrparent == NULL) ? "" : "/", dp->d_name);

		if ((sb.xattr_info.xattrfname = malloc(
		    strlen(*name) + 1)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) strcpy(sb.xattr_info.xattrfname, *name);

		sb.xattr_info.xattr_rw_sysattr = rw_sysattr;
		sb.xattr_info.xattr_baseparent_fd = baseparent_fd;

		rc = pass_common(name, namesz, dirname, &sb, ifd);
		init_xattr_info(&sb);

#if defined(_PC_SATTR_ENABLED)
		/*
		 * If both -/ and -@ were specified, then archive the
		 * attribute's extended system attributes and hidden directory
		 * by making a recursive call to xattrs_put().
		 */
		if (!rw_sysattr && f_sys_attr && f_extended_attr &&
		    (rc != 1) && (Hiddendir == 0)) {

			/*
			 * Change into the attribute's parent attribute
			 * directory to determine to archive the system
			 * attributes.
			 */
			if (fchdir(fd) < 0) {
				(void)  fprintf(stderr, gettext(
				    "%s: cannot change to attribute "
				    "directory of %s%s%sfile %s: %s\n"), myname,
				    (attrparent == NULL) ? "" :
				    gettext("attribute "),
				    (attrparent == NULL) ? "" : attrparent,
				    (attrparent == NULL) ? "" : gettext(" of "),
				    *name, strerror(errno));
				(void) closedir(dirp);
				(void) close(fd);
				return;
			}

			pass_xattrs(name, namesz, dirname, dp->d_name,
			    baseparent_fd);
		}
#endif	/* _PC_SATTR_ENABLED */
	}

	(void) closedir(dirp);
	(void) close(fd);
	if (attrparent == NULL) {
		rest_cwd(baseparent_fd);
	}
}
#endif
