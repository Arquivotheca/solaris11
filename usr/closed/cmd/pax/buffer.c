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
 * buffer.c - Buffer management functions
 *
 * DESCRIPTION
 *
 *	These functions handle buffer manipulations for the archiving
 *	formats.  Functions are provided to get memory for buffers,
 *	flush buffers, read and write buffers and de-allocate buffers.
 *	Several housekeeping functions are provided as well to get some
 *	information about how full buffers are, etc.
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

#include <utime.h>
#include <libgen.h>
#include <unistd.h>
#include "pax.h"
#include "func.h"

/* Function Prototypes */

static void buf_pad(OFFSET);
static void outflush(void);
static void buf_use(uint_t);
static int buf_in_avail(char **, uint_t *);
static uint_t buf_out_avail(char **);
static int copy_data(int, int, int, OFFSET, int *, char **);

/*
 * take_action()
 *
 * Performs an action based on the invalid=<action> specified on the
 * command line with -x pax. If not in pax mode (-x pax) then the
 * default action is to print a diag message and return.
 *
 * PARAMETERS
 *
 *	char 	**new_name - new name for output file (if applicable)
 *	size_t 	**nnsize - size of the 'new_name' buffer
 *	char 	*dir - parent directory of 'new_name'
 *	int	*ndirfd	- file descriptor of the parent directory
 *			  of 'new_name'
 *	Stat	*asb	- stat block of the file
 *
 * RETURNS
 *
 * 	PAX_OK	- successful
 *	PAX_RETRY - rename action occurred, need to retry with new name
 *	PAX_FAIL - unsuccessful
 */
int
take_action(char **new_name, size_t *nnsize, char *dir, int *ndirfd, Stat *asb)
{
	char curwd[PATH_MAX + 1];

	switch (invalidopt) {
	case INV_WRITE:
		if (f_dir_create) {
			if (getcwd(curwd, PATH_MAX + 1) == NULL) {
				diag(gettext(
				    "could not get current working directory "
				    "%s : %s\n"), curwd, strerror(errno));
				return (PAX_FAIL);
			}
			*ndirfd = r_dirneed(*new_name, O_RDONLY);
			if (chdir(curwd) < 0) {
				diag(gettext(
				    "could not change to current working "
				    "directory "
				    "%s : %s\n"), curwd, strerror(errno));
				if (*ndirfd > 0) {
					(void) close(*ndirfd);
				}
				return (PAX_FAIL);
			}
			if (*ndirfd < 0) {
				diag(gettext(
				    "could not open/create directory "
				    "%s: %s\n"), dir, strerror(errno));
				return (PAX_FAIL);
			}
		} else {
			warn(*new_name,
			    gettext("Directories are not being created"));
			return (-1);
		}
		break;
	case INV_RENAME:
		diag(gettext(
		    "could not open/create path "
		    "%s: %s\n"), *new_name, strerror(errno));
		rename_interact = 1;
		if (get_newname(new_name, nnsize, asb) != 0) {
			exit_status = 1;
			return (PAX_FAIL);
		}
		return (PAX_RETRY);
		/* NOTREACHED */
		break;
	default:
		diag(gettext(
		    "Directories are not being created: "
		    "%s: %s\n"), *new_name, strerror(errno));
		return (PAX_FAIL);
		/* NOTREACHED */
		break;
	}
	return (PAX_OK);
}

/*
 * inentry - install a single archive entry
 *
 * DESCRIPTION
 *
 *	Inentry reads an archive entry from the archive file and writes it
 *	out to the named file.  If we are in PASS mode during archive
 *	processing, the pass() function is called, otherwise we will
 *	extract from the archive file.
 *
 *	Inentry actually calls indata to process the actual data to the
 *	file.
 *
 * PARAMETERS
 *
 *	char 	**new_name - new name for output file (if applicable)
 *	size_t	*nnsize - size of the 'new_name' buffer
 *	char	*name	- name of the file to extract from the archive
 *	Stat	*asb	- stat block of the file to be extracted from the
 *			  archive.
 *
 * RETURNS
 *
 * 	Returns zero if successful, -1 otherwise.
 */
int
inentry(char **new_name, size_t *nnsize, char *name, Stat *asb)
{
	Link		*linkp;
	int		ifd;
	int		ofd;
	int		ndirfd = -1;
	int		odirfd = -1;
	int		len;
	int		rw_sysattr;
	char		*dir;
	char		*namep;

	/*
	 * Open directories
	 */

begin:
	rw_sysattr = 0;
	len = strlen(*new_name) + 1;
	if ((dir = calloc(len, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}

	if (asb->xattr_info.xattrp != NULL) {
		int	my_cwd = save_cwd();

		/* open and cd into the attribute's parent fd */
		(void) fchdir(asb->xattr_info.xattr_baseparent_fd);
		(void) open_attr_dir(asb->xattr_info.xattraname,
		    asb->xattr_info.xattrfname,
		    asb->xattr_info.xattr_baseparent_fd,
		    (asb->xattr_info.xattraparent == NULL) ? NULL :
		    asb->xattr_info.xattraparent, &odirfd,
		    &rw_sysattr);
		rest_cwd(my_cwd);
		if (odirfd == -1) {
			(void) fprintf(stderr, gettext(
			    "%s: could not open attribute "
			    "directory of %s%s%sfile %s: %s\n"), myname,
			    (asb->xattr_info.xattraparent == NULL) ?
			    "" : gettext("attribute "),
			    (asb->xattr_info.xattraparent == NULL) ?
			    "" : asb->xattr_info.xattraparent,
			    (asb->xattr_info.xattraparent == NULL) ?
			    "" : gettext(" of "), asb->xattr_info.xattrfname,
			    strerror(errno));
			free(dir);
			return (-1);
		}

		if (strcmp(*new_name, name) == 0) {
			ndirfd = dup(odirfd);
			if (ndirfd == -1) {
				(void) fprintf(stderr, gettext(
				    "%s: could not dup(2) attribute directory"
				    " file descriptor: %s\n"),
				    myname, strerror(errno));
				if (odirfd > 0) {
					(void) close(odirfd);
				}
				free(dir);
				return (-1);
			}
		} else {
			(void) fchdir(asb->xattr_info.xattr_baseparent_fd);
			(void) open_attr_dir(asb->xattr_info.xattraname,
			    asb->xattr_info.xattrfname,
			    asb->xattr_info.xattr_baseparent_fd,
			    (asb->xattr_info.xattraparent == NULL) ? NULL :
			    asb->xattr_info.xattraparent, &ndirfd,
			    &rw_sysattr);
			if (ndirfd == -1) {
				diag(gettext(
				    "could not open attribute "
				    "directory of %s%s%sfile %s: %s\n"),
				    (asb->xattr_info.xattraparent == NULL) ?
				    "" : gettext("attribute "),
				    (asb->xattr_info.xattraparent == NULL) ?
				    "" : asb->xattr_info.xattraparent,
				    (asb->xattr_info.xattraparent == NULL) ?
				    "" : gettext(" of "), *new_name,
				    strerror(errno));
				free(dir);
				return (-1);
			}
		}
	} else {
		/*
		 * The directory of the pathname specified by the user
		 * may be PATH_MAX characters or less,
		 * but the full pathname may be over
		 * PATH_MAX characters.  If the full
		 * pathname is greater than PATH_MAX
		 * characters, only create or open the
		 * directory if invalid=write was
		 * specified, prompt if invalid=rename
		 * was specified, otherwise, don't
		 * create or open the directory for the
		 * pathname that is too long.
		 */
		get_parent(*new_name, dir);

		if (len > PATH_MAX) {
			if (invalidopt == INV_BYPASS) {
				/*
				 * If bypass, then ignore long pathname
				 */
				errno = ENAMETOOLONG;
				diag(gettext("%s: %s\n"),
				    *new_name, strerror(errno));
				free(dir);
				return (0);
			} else if (invalidopt == INV_RENAME) {
				/* full pathname > PATH_MAX */
				errno = ENAMETOOLONG;
				diag(gettext("%s: %s\n"),
				    *new_name, strerror(errno));
				rename_interact = 1;
				if (get_newname(new_name,
				    nnsize, asb) != 0) {
					exit_status = 1;
					free(dir);
					return (-1);
				}
				goto begin;
			} else if ((strlen(dir) > PATH_MAX) ||
			    invalidopt != INV_WRITE) {
				errno = ENAMETOOLONG;
				diag(gettext("%s: %s\n"),
				    *new_name, strerror(errno));
				exit_status = 1;
				free(dir);
				return (-1);
			}
		}

		ndirfd = open(dir, O_RDONLY);
		if (ndirfd == -1) {
			if (errno == ENOENT) {
				if (f_dir_create) {
					if (dirneed(*new_name) != 0 ||
					    (ndirfd = open(dir,
					    O_RDONLY)) < 0) {
						diag(gettext(
						    "Directories are "
						    "not being "
						    "created: "
						    "%s: %s\n"),
						    *new_name,
						    strerror(errno));
						exit_status = 1;
						free(dir);
						return (-1);
					}
				} else {
					warn(*new_name,
					    gettext("Directories are"
					    " not being created"));
					free(dir);
					return (-1);
				}
			} else if (errno == ENAMETOOLONG) {
				/*
				 * ENAMETOOLONG may be caused by a component
				 * of the path being opened longer than
				 * NAME_MAX.
				 */
				if (invalidopt == INV_BYPASS) {
					/*
					 * If bypass, then ignore long pathname
					 */
					diag(gettext("%s: %s\n"),
					    *new_name, strerror(errno));
					free(dir);
					return (0);
				}
				switch (take_action(new_name, nnsize,
				    dir, &ndirfd, asb)) {
				case PAX_FAIL:
					free(dir);
					return (-1);
					/* NOTREACHED */
					break;
				case PAX_RETRY:
					free(dir);
					goto begin;
					/* NOTREACHED */
					break;
				default:
					diag(gettext(
					    "could not open directory "
					    "%s: %s\n"),
					    dir, strerror(errno));
					free(dir);
					return (-1);
					/* NOTREACHED */
					break;
				}
			} else {
				diag(gettext(
				    "could not open directory %s: %s\n"),
				    dir, strerror(errno));
				free(dir);
				return (-1);
			}
		}

		odirfd = dup(ndirfd);

		if (odirfd == -1) {
			(void) fprintf(stderr, gettext(
			    "%s: could not dup(2)"
			    " file descriptor: %s\n"),
			    myname, strerror(errno));
			if (ndirfd > 0) {
				(void) close(ndirfd);
			}
			free(dir);
			return (-1);
		}
	}
	free(dir);

	if (asb->xattr_info.xattraname != NULL)
		namep = asb->xattr_info.xattraname;
	else
		namep = name;

	linkp = linkfrom(namep, asb);

	/*
	 * Note that inentry() is only called by read_archive(), which is used
	 * for read mode (-r) or for list mode (neither -r nor -w), but NOT for
	 * pass mode (-r and -w).  So having the ndirfd and odirfd refer to the
	 * same directory is okay, since we are only concerned with the target
	 * (ndirfd) not the original source location (odirfd) when extracting
	 * from an archive.  In pass mode, we want ndirfd and odirfd parameters
	 * to openout() to be different.
	 */

	if ((ofd = openout(new_name, nnsize, namep, asb, linkp, 0,
	    ndirfd, odirfd)) > 0) {
		if (asb->sb_size ||
		    linkp == (Link *)NULL || linkp->l_size == 0) {
			(void) indata(ofd, rw_sysattr, asb->sb_size, *new_name);
		} else if ((ifd = open(linkp->l_path->p_name, O_RDONLY)) < 0) {
			warn(linkp->l_path->p_name, strerror(errno));
		} else {
			passdata(linkp->l_path->p_name, ifd, *new_name, ofd,
			    asb);
			if (ifd > 0) {
				(void) close(ifd);
			}
		}
	} else {
		if (odirfd > 0) {
			(void) close(odirfd);
		}
		if (ndirfd > 0) {
			(void) close(ndirfd);
		}
		return (buf_skip((OFFSET) asb->sb_size) >= 0);
	}

	/*
	 * Don't try to restore time or permissions in the attribute directory
	 * of an attribute or for a read-write system attribute.
	 */
	if ((asb->xattr_info.xattraname == NULL) ||
	    ((asb->xattr_info.xattraparent == NULL) &&
	    !asb->xattr_info.xattr_rw_sysattr)) {
		/*
		 * We want to restore access time, but ustar and cpio don't
		 * store it. So we use what's in the stat buffer only if its
		 * useful (ie not -1).
		 */

		if (xhdr_flgs & _X_MTIME) {
			struct timeval timebuf[2];

			/* Extended header: use microsecs. */
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
			(void) futimesat(ndirfd, get_component(*new_name),
			    timebuf);
		} else {
			struct timeval	tstamp[2];

			/* Regular header:  use seconds. */
			tstamp[0].tv_sec = (f_extract_access_time) ?
			    (asb->sb_atime == -1) ? time((time_t *)0) :
			    asb->sb_atime : time((time_t *)0);
			tstamp[0].tv_usec = 0;
			tstamp[1].tv_sec = f_mtime ? asb->sb_mtime :
			    time((time_t *)0);
			tstamp[1].tv_usec = 0;
			(void) futimesat(ndirfd,
			    (asb->xattr_info.xattraname == NULL) ?
			    get_component(*new_name) :
			    asb->xattr_info.xattraname, tstamp);
		}

		if (f_mode && fchmod(ofd, asb->sb_mode & S_IPERM) == -1)
			warn(*new_name, gettext(
			    "unable to preserve file mode"));

		if (f_stdpax) {
			int	userid = -1;
			int	groupid = -1;

			if (f_user) {
				userid = asb->sb_uid;
			}
			if (f_group) {
				groupid = asb->sb_gid;
			}

			/*
			 * Preserve the file's owner, group, and mode
			 * if required.  In case the file we are chowning
			 * is a symlink, make sure we just chown the file
			 * itself, not the file the symlink references.
			 */
			if ((userid != -1) || (groupid != -1)) {
				if (fchownat(ndirfd, get_component(namep),
				    userid, groupid,
				    AT_SYMLINK_NOFOLLOW) < 0) {
					warn(*new_name, gettext(
					    "unable to preserve owner/group"));
				} else if (f_mode && fchmod(ofd, asb->sb_mode &
				    (S_IPERM|S_ISUID|S_ISGID)) == -1) {
					warn(*new_name, gettext(
					    "unable to preserve file mode"));
				}
			}
		} else if (f_owner) {
			/*
			 * Preserve the file's owner, group, and mode
			 * if required.  In case the file we are chowning
			 * is a symlink, make sure we just chown the file
			 * itself, not the file the symlink references.
			 */
			if (fchownat(ndirfd, get_component(namep),
			    asb->sb_uid, asb->sb_gid, AT_SYMLINK_NOFOLLOW) < 0)
				warn(*new_name,
				    gettext("unable to preserve owner/group"));
			else if (f_mode && fchmod(ofd,
			    asb->sb_mode & (S_IPERM|S_ISUID|S_ISGID)) == -1)
				warn(*new_name,
				    gettext("unable to preserve file mode"));
		}
	}

	if (ndirfd > 0) {
		(void) close(ndirfd);
	}
	if (odirfd > 0) {
		(void) close(odirfd);
	}
	if (ofd > 0) {
		(void) close(ofd);
	}
	return (0);
}


/*
 * outdata - write archive data
 *
 * DESCRIPTION
 *
 *	Outdata transfers data from the named file to the archive buffer.
 *	It knows about the file padding which is required by tar, but not
 *	by cpio.  Outdata continues after file read errors, padding with
 *	null characters if neccessary.   Closes the input file descriptor
 *	when finished.
 *
 * PARAMETERS
 *
 *	int	fd	- file descriptor of file to read data from
 *	char   *name	- name of file
 *	OFFSET	size	- size of the file
 *
 */


void
outdata(int fd, char *name, Stat *sb)
{
	uint_t		chunk;
	int		got;
	int		oops;
	uint_t		avail;
	int		pad;
	char		*buf;
	struct timeval	tstamp[2];
	OFFSET		size = sb->sb_size;

	oops = got = 0;
	if ((pad = (size % BLOCKSIZE)) != 0)
		pad = (BLOCKSIZE - pad);
	while (size) {
		avail = buf_out_avail(&buf);
		size -= (chunk = size < avail ? (uint_t)size : avail);
		if (oops == 0 &&
		    (got = read(fd, buf, (unsigned int)chunk)) < 0) {
			oops = -1;
			warn(name, strerror(errno));
			got = 0;
		}
		if (got < chunk) {
			oops = -1;
			warn(name, gettext("Early EOF"));
			while (got < chunk) {
				buf[got++] = '\0';
			}
		}
		buf_use(chunk);
	}
	if (fd > 0) {
		(void) close(fd);
	}
	if (f_access_time) {	/* -t option: preserve access time of input */
		tstamp[0].tv_sec = sb->sb_stat.st_atim.tv_sec;
		tstamp[0].tv_usec = sb->sb_stat.st_atim.tv_nsec / 1000;
		tstamp[1].tv_sec = sb->sb_stat.st_mtim.tv_sec;
		tstamp[1].tv_usec = sb->sb_stat.st_mtim.tv_nsec / 1000;
		(void) utimes(name, tstamp);
	}
	if (ar_format == TAR || ar_format == PAX)
		buf_pad((OFFSET) pad);
}


/*
 * write_eot -  write the end of archive record(s)
 *
 * DESCRIPTION
 *
 *	Write out an End-Of-Tape record.  We actually zero at least one
 *	record, through the end of the block.  Old tar writes garbage after
 *	two zeroed records -- and PDtar used to.
 */


void
write_eot(void)
{
	OFFSET	pad;
	char	header[M_STRLEN + H_STRLEN + 1];

	if (ar_format == TAR || ar_format == PAX) {
		/* write out two zero blocks for trailer */
		pad = 2 * BLOCKSIZE;
	} else {
		if ((pad =
		    (total + M_STRLEN + H_STRLEN + TRAILZ) % BLOCKSIZE) != 0) {
			pad = BLOCKSIZE - pad;
		}
		(void) strcpy(header, M_ASCII);
		(void) sprintf(header + M_STRLEN, H_PRINT, 0, 0,
		    0, 0, 0, 1, 0, (time_t)0, TRAILZ, (OFFSET)0);
		outwrite(header, (OFFSET) M_STRLEN + H_STRLEN);
		outwrite(TRAILER, (OFFSET) TRAILZ);
	}
	buf_pad(pad);
	outflush();
}


/*
 * outwrite -  write archive data
 *
 * DESCRIPTION
 *
 *	Writes out data in the archive buffer to the archive file.  The
 *	buffer index and the total byte count are incremented by the number
 *	of data bytes written.
 *
 * PARAMETERS
 *
 *	char   *idx	- pointer to data to write
 *	uint_t	len	- length of the data to write
 */


void
outwrite(char *idx, OFFSET len)
{
	OFFSET	have;
	OFFSET	want;
	char	*endx;

	endx = idx + len;
	while ((want = endx - idx) != 0) {
		if (bufend - bufidx < 0)
			fatal(gettext("Buffer overflow in write"));
		if ((have = bufend - bufidx) == 0)
			outflush();
		if (have > want)
			have = want;
		(void) memcpy(bufidx, idx, (int)have);
		bufidx += have;
		idx += have;
		total += have;
	}
}

void
outxattrhdr(char *hdr, char *data, int len)
{
	int		pad;

	outwrite(hdr, (OFFSET) BLOCKSIZE);
	outwrite(data, (OFFSET) len);

	if ((pad = (len % BLOCKSIZE)) != 0)
		pad = (BLOCKSIZE - pad);
	buf_pad(pad);
}

/*
 * Copy datasize amount of data from the input file to buffer.
 *
 * ifd		- Input file descriptor.  If -1, then use Archive file.
 * buffer	- Buffer (allocated by caller) to copy data to.
 * datasize	- The amount of data to read from the input file
 *		and copy to the buffer.
 * error	- When reading from an Archive file, indicates unreadable
 *		data was encountered, otherwise indicates errno.
 */
static size_t
read_chunk(int ifd, char *buffer, size_t datasize, int *error)
{
	char	*buf;
	size_t	got;
	size_t	avail;

	if (ifd == -1) {

		/* Read data from Archive */
		*error |= buf_in_avail(&buf, &avail);
		got = datasize < avail ? datasize : avail;
		(void) memcpy(buffer, buf, got);
		buf_use(got);

	} else {

		/* Read data from file */
		if ((got = read(ifd, buffer, datasize)) <= (uint_t)0) {
			*error = errno;
			return (0);
		}
		total += got;
	}

	return (got);
}

/*
 * Copy data from the input file to output file descriptor
 * without modifying the data.  If ifd is not set then the
 * input file is the Archive file.
 *
 * Parameters
 *	ifd		- Input file descriptor to read from.
 *	ofd		- Output file descriptor of extracted file.
 *	rw_sysattr	- Flag indicating if a file is an extended
 *			system attribute file.
 * 	datasize	- Amount of data to copy/write.
 *	corrupt		- If reading from Archive file (ifd == -1) then
 *			it is set to non-zero to indicate portions of
 *			the Archive file were unreadable, otherwise, it
 *			will contain the last errno.
 *	oops		- If an error occurs during the write to
 *			the output file descriptor, oops
 *			contains the error.
 *
 * Return code
 *	0		Success
 *	< 0		An error occurred during read of input file
 *			descriptor
 *	> 0		An error occurred during the write of output
 *			file descriptor.
 */
static int
copy_data(int ifd, int ofd, int rw_sysattr, OFFSET bytes, int *corrupt,
    char **oops)
{
	char *buf;
	int errmsg = ((oops != NULL) && (*oops != NULL));
	size_t bytesread;
	size_t initial_bytes = 0;
	size_t maxwrite;
	size_t piosize;		/* preferred I/O size */
	OFFSET got;
	struct stat tsbuf;

	/* No data to copy. */
	if (bytes == 0) {
		return (0);
	}

	/*
	 * To figure out the size of the buffer used to accumulate data
	 * from readtape() and to write to the file, we need to determine
	 * the largest chunk of data to be written to the file at one time.
	 * This is determined based on the following three things:
	 *	1) The size of the archived file.
	 *	2) The preferred I/O size of the file.
	 *	3) If the file is a read-write system attribute file.
	 *
	 * If the size of the file is less than the preferred I/O
	 * size or it's a read-write system attribute file, which must be
	 * written in one operation, then set the maximum write size to the
	 * size of the archived file.  Otherwise, the maximum write size is
	 * preferred I/O size.
	 */
	if (rw_sysattr) {
		maxwrite = bytes;
		piosize = bytes;
	} else {
		if (fstat(ofd, &tsbuf) == 0) {
			piosize = max(tsbuf.st_blksize, blocksize);
		} else {
			piosize = blocksize;
		}
		maxwrite = min(bytes, piosize);
	}
	if ((buf = malloc(maxwrite)) == NULL) {
		fatal(gettext("cannot allocate buffer"));
	}

	/*
	 * Data is read from the Archive file a fixed amount at a time,
	 * therefore, if a file is being extracted from an Archive
	 * (-w was specified without -r), then we probably have already
	 * have some of the data in the buffer.  In this case, read_chunk()
	 * will return with the remaining data that it has in the buffer.
	 * If we aren't processing a read-write system attribute file (which
	 * must be written in one operation), go ahead and write out the
	 * data we received from read_chunk().  We'll then loop through
	 * reading as much data as we can write at one time, then writing it
	 * out in one operation.
	 */
	if ((got = read_chunk(ifd, buf, maxwrite, corrupt)) <= 0) {
		if ((got <= (uint_t)0) && (ifd != -1)) {
			*corrupt = errno;
			free(buf);
			return (-1);
		}
	} else if (!rw_sysattr || (got >= bytes)) {
		errno = 0;
		if (!errmsg && (write(ofd, buf, got) < 0)) {
			*corrupt = errno;
			free(buf);
			return (1);
		}
		bytes -= got;
		if (bytes < maxwrite) {
			maxwrite = bytes;
		}
	} else {
		initial_bytes = got;
	}

	while (bytes > 0) {
		/*
		 * Read as much data as we can.  We're limited by
		 * the smallest of:
		 *	- the number of bytes left to be read,
		 *	- the preferred I/O size
		 */
		for (bytesread = initial_bytes; bytesread < maxwrite;
		    bytesread += got) {
			errno = 0;
			if ((got = read_chunk(ifd, buf + bytesread,
			    min(maxwrite - bytesread, piosize),
			    corrupt)) <= 0) {
				/*
				 * If data couldn't be read from the input file
				 * descriptor, set corrupt to indicate the error
				 * and return.
				 */
				if ((got <= (uint_t)0) && (ifd != -1)) {
					*corrupt = errno;
					free(buf);
					return (-1);
				}
			}
		}

		errno = 0;
		if (!errmsg && (write(ofd, buf, maxwrite) < 0)) {

			/*
			 * If we're not reading from the Archive file,
			 * then return, otherwise, we need to keep reading
			 * to update the Archive index.
			 */
			if (ifd != -1) {
				*corrupt = errno;
				free(buf);
				return (1);
			} else {
				if (rw_sysattr && (errno == EPERM)) {
					*oops = gettext(
					    "unable to extract system "
					    "attribute: insufficient "
					    "privileges");
				} else {
					*oops = strerror(errno);
				}
				errmsg = 1;
			}
		}
		bytes -= maxwrite;

		/*
		 * If we've reached this point and there is still data
		 * to be written, maxwrite had to have been determined
		 * by the preferred I/O size.  If the number of bytes
		 * left to write is smaller than the preferred I/O size,
		 * then we're about to do our final write to the file, so
		 * just set maxwrite to the number of bytes left to write.
		 */
		if (bytes < maxwrite) {
			maxwrite = bytes;
		}
		initial_bytes = 0;
	}
	free(buf);

	return (0);
}

/*
 * passdata - copy data to one file
 *
 * DESCRIPTION
 *
 *	Copies a file from one place to another.  Doesn't believe in input
 *	file descriptor zero (see description of kludge in openin() comments).
 *	Calling routines are expected to close both the input and output
 *	file descriptors.
 *
 *	Data/hole pairs associated with the SUN.holesdata keyword in the
 *	extended header are holes reported by the underlying
 *	file system in place when the file was placed into the archive.
 *	These data/hole pairs are used to seek over holes when the
 *	file is restored.
 *
 *	The value for SUN.holesdata takes the form
 *		<SPACE><SEEK_DATA position><SPACE><SEEK_HOLE position>...
 *	with the above repeated any number of times up to a '\n'.
 *	For example, the following entry in an extended header file
 *	50 SUN.holesdata= 0 8192 24576 32768 49152 49159
 *	indicates there is data at offset 0, 24576, and 49152.  Holes
 *	are found after this data at offset 8192, 32768, and 49159,
 *	respectively.  A minimum of 2 data/hole pairs will be contained
 *	in SUN.holesdata.  For example, if the file was just one big
 *	hole, then the following data/hole offsets would be contained in
 *	SUN.holesdata:
 *		<SPACE>0<SPACE>0<filesize><SPACE><filesize>
 *	If the data and hole offsets of a pair are the same, no data will
 *	be copied.  Thus, in the above example, we would seek to offset 0
 *	(data offset) and copy no data since the difference between the
 *	hole offset and the data offset are 0.  We would then seek to
 *	the next data offset (<filesize>).  Again no data would be copied
 *	since the difference between the hole offset and the data offset
 *	is 0.
 *
 *	If a SUN.holesdata keyword is not found in the extended header,
 *	the entire file is considered data.  No modification of data
 *	will occur, thus sequences of zero bytes found in the file
 *	will not be converted to holes as was done in the past.
 *
 * PARAMETERS
 *
 *	char	*from	- input file name (old file)
 *	int	ifd	- input file descriptor
 *	char	*to	- output file name (new file)
 *	int	ofd	- output file descriptor
 */


void
passdata(char *from, int ifd, char *to, int ofd, Stat *asb)
{
	char		*holesdata = NULL;
	int		restore_holes = 1;
	OFFSET		got = 0;
	int		goterrno;
	int		rw_sysattr = 0;

	if (asb->xattr_info.xattraname != NULL) {
		rw_sysattr = asb->xattr_info.xattr_rw_sysattr;
	}

	if (ifd) {

		/* check for holes data */
		if (!f_pax ||
		    fpathconf(ifd, _PC_MIN_HOLE_SIZE) < 0) {
			/* underlying file system doesn't supports SEEK_HOLE */
			restore_holes = 0;
		} else if (oXtarhdr.x_holesdata != NULL) {
			holesdata = strdup(oXtarhdr.x_holesdata);
		} else if (Xtarhdr.x_holesdata != NULL) {
			holesdata = strdup(Xtarhdr.x_holesdata);
		} else {
			restore_holes = 0;
		}

		if (restore_holes) {
			char		*token;
			const char	 *search = " ";
			OFFSET		hole;
			OFFSET		data;
			OFFSET		datasize;
			OFFSET		inittotal = total;

			/* Step through all data and hole offset pairs */
			token = strtok(holesdata, search);
			for (;;) {
				if (token == NULL) {
					break;
				}
				errno = 0;
				if (((data = atoll(token)) == 0) &&
				    (errno == EINVAL)) {
					warn(from, gettext(
					    "invalid SUN.holesdata value"));
					break;
				}

				if ((token = strtok(NULL, search)) == NULL) {
					warn(from, gettext(
					    "invalid SUN.holesdata value"));
					break;
				}

				errno = 0;
				if (((hole = atoll(token)) == 0) &&
				    (errno == EINVAL)) {
					warn(from, gettext(
					    "invalid SUN.holesdata value"));
					break;
				}

				datasize = hole - data;

				/*
				 * Skip to data section of the archived file
				 * and the file being restored.
				 */
				if (lseek(ifd, data, SEEK_SET) < 0) {
					diag(gettext(
					    "%s: invalid SUN.holesdata "
					    "holes offset %ol: %s\n"), from,
					    data, strerror(errno));
					exit_status = 1;
					break;
				} else {
					/* Seek over holes */
					(void) lseek(ofd, data, SEEK_SET);
				}
				total = inittotal + data;

				if ((datasize > 0) && \
				    (got = copy_data(ifd, ofd, rw_sysattr,
				    datasize, &goterrno, NULL)) != 0) {
					break;
				}

				/* Get the next data offset */
				token = strtok(NULL, search);
			}
			free(holesdata);

		} else {
			(void) lseek(ifd, (OFFSET) 0, SEEK_SET);
			got = copy_data(ifd, ofd, rw_sysattr,
			    asb->sb_size, &goterrno, NULL);
		}

		if (got != 0) {
			warn(got < 0 ? from : to, (rw_sysattr &&
			    (goterrno == EPERM)) ?
			    gettext("unable to extract system attribute: "
			    "insufficient privileges") : strerror(goterrno));
		}
	}
}


/*
 * buf_allocate - get space for the I/O buffer
 *
 * DESCRIPTION
 *
 *	buf_allocate allocates an I/O buffer using malloc.  The resulting
 *	buffer is used for all data buffering throughout the program.
 *	Buf_allocate must be called prior to any use of the buffer or any
 *	of the buffering calls.
 *
 * PARAMETERS
 *
 *	OFFSET	size	- size of the I/O buffer to request
 *
 * ERRORS
 *
 *	If an invalid size is given for a buffer or if a buffer of the
 *	required size cannot be allocated, then the function prints out an
 *	error message and returns a non-zero exit status to the calling
 *	process, terminating the program.
 *
 */


void
buf_allocate(OFFSET size)
{
	if (size <= 0)
		fatal(gettext("Invalid value for blocksize"));
	if ((bufstart = malloc((unsigned)size)) == (char *)NULL)
		fatal(gettext("Cannot allocate I/O buffer"));
	bufend = bufidx = bufstart;
	bufend += size;
}

/*
 * buf_skip - skip input archive data
 *
 * DESCRIPTION
 *
 *	Buf_skip skips past archive data.  It is used when the length of
 *	the archive data is known, and we do not wish to process the data.
 *
 * PARAMETERS
 *
 *	OFFSET	len	- number of bytes to skip
 *
 * RETURNS
 *
 * 	Returns zero under normal circumstances, -1 if unreadable data is
 * 	encountered.
 */


int
buf_skip(OFFSET len)
{
	uint_t	chunk;
	int	corrupt = 0;

	while (len) {
		if (bufend - bufidx < 0)
			fatal(gettext("Buffer overflow in skip"));
		while ((chunk = bufend - bufidx) == 0)
			corrupt |= ar_read();
		if (chunk > len)
			chunk = len;
		bufidx += chunk;
		len -= chunk;
		total += chunk;
	}
	return (corrupt);
}


/*
 * buf_read - read a given number of characters from the input archive
 *
 * DESCRIPTION
 *
 *	Reads len number of characters from the input archive and
 *	stores them in the buffer pointed at by dst.
 *
 * PARAMETERS
 *
 *	char   *dst	- pointer to buffer to store data into
 *	uint_t	len	- length of data to read
 *
 * RETURNS
 *
 * 	Returns zero with valid data, -1 if unreadable portions were
 *	replaced by null characters.
 */


int
buf_read(char *dst, uint_t len)
{
	int	have;
	int	want;
	int	corrupt = 0;
	char	*endx = dst + len;

	while ((want = endx - dst) != 0) {
		if (bufend - bufidx < 0)
			fatal(gettext("Buffer overflow in read"));
		while ((have = bufend - bufidx) == 0) {
			have = 0;
			corrupt |= ar_read();
		}
		if (have > want)
			have = want;
		(void) memcpy(dst, bufidx, have);
		bufidx += have;
		dst += have;
		total += have;
	}
	return (corrupt);
}

/*
 * indata - install data from an archive
 *
 * DESCRIPTION
 *
 *	Indata writes size bytes of data from the archive buffer to the output
 *	file specified by fd.  The filename which is being written, pointed
 *	to by name is provided only for diagnostics.
 *
 *	Data/hole pairs associated with the SUN.holesdata keyword in the
 *	extended header are holes reported by the underlying
 *	file system in place when the file was placed into the archive.
 *	These data/hole pairs are used to seek over holes when the
 *	file is restored.
 *
 *	The value for SUN.holesdata takes the form
 *		<SPACE><SEEK_DATA position><SPACE><SEEK_HOLE position>...
 *	with the above repeated any number of times up to a '\n'.
 *	For example, the following entry in an extended header file
 *	50 SUN.holesdata= 0 8192 24576 32768 49152 49159
 *	indicates there is data at offset 0, 24576, and 49152.  Holes
 *	are found after this data at offset 8192, 32768, and 49159,
 *	respectively.
 *
 *	If a SUN.holesdata keyword is not found in the extended header,
 *	the entire file is considered data.  No modification of data
 *	will occur, thus sequences of zero bytes found in the file
 *	will not be converted to holes as was done in the past.
 *
 * PARAMETERS
 *
 *	int	fd	- output file descriptor
 *	OFFSET	size	- number of bytes to write to output file
 *	char	*name	- name of file which corresponds to fd
 *
 * RETURNS
 *
 * 	Returns given file descriptor.
 */


int
indata(int fd, int is_rwsysattr, OFFSET size, char *name)
{
	char	*oops = NULL;
	int	corrupt = 0;
	int	restore_holes = 1;
	OFFSET	datasize = size;
	OFFSET	inittotal = total;
	char	*holesdata = NULL;

	/* check for holes data */
	if (!f_pax) {
		restore_holes = 0;
	} else if (oXtarhdr.x_holesdata != NULL) {
		holesdata = strdup(oXtarhdr.x_holesdata);
	} else if (Xtarhdr.x_holesdata != NULL) {
		holesdata = strdup(Xtarhdr.x_holesdata);
	} else {
		restore_holes = 0;
	}

	if (restore_holes) {
		char *token;
		const char *search = " ";
		OFFSET data;
		OFFSET hole;

		/* Step through all data and hole offset pairs */
		token = strtok(holesdata, search);
		for (;;) {
			if (token == NULL) {
				break;
			}
			errno = 0;
			if (((data = atoll(token)) == 0) &&
			    (errno == EINVAL)) {
				warn(name, gettext("holes data "
				    "does not contain a data and "
				    "hole pair"));
				break;
			}
			if ((token = strtok(NULL, search)) == NULL) {
				warn(name, gettext("holes data "
				    "does not contain a data and "
				    "hole pair"));
				break;
			}
			errno = 0;
			if (((hole = atoll(token)) == 0) &&
			    (errno == EINVAL)) {
				warn(name, gettext("holes data "
				    "does not contain a data and "
				    "hole pair"));
				break;
			}
			datasize = hole - data;

			/*
			 * Skip to data section of the archived file.
			 */
			if (buf_skip(data - (total - inittotal)) < 0) {
				warn(name, gettext(
				    "File data is corrupt"));
			}

			/* skip over holes in the extracted file */
			(void) lseek(fd, data, SEEK_SET);

			(void) copy_data(-1, fd, 0, datasize, &corrupt, &oops);

			/* Get the next data offset */
			token = strtok(NULL, search);
		}
		free(holesdata);

	} else {
		(void) copy_data(-1, fd, is_rwsysattr, datasize, &corrupt,
		    &oops);
	}

	if (corrupt) {
		warn(name, gettext("Corrupt archive data"));
	}
	if (oops) {
		warn(name, oops);
	}

	return (fd);
}


/*
 * outflush - flush the output buffer
 *
 * DESCRIPTION
 *
 *	The output buffer is written, if there is anything in it, to the
 *	archive file.
 */


static void
outflush(void)
{
	char	*buf;
	int	got;
	int	len;

	/* if (bufidx - buf > 0) */
	for (buf = bufstart, len = bufidx - buf; len > 0; len = bufidx - buf) {
		if ((got = write(archivefd, buf, f_blocking ? bufend -
		    bufstart : MIN(len, blocksize))) > 0) {
			buf += got;
		} else if ((got < 0) && (errno == ENXIO || errno == ENOSPC)) {
			next(AR_WRITE);
		} else {
			warn("write", strerror(errno));
			fatal(gettext("Tape write error"));
		}
	}
	bufend = (bufidx = bufstart) + blocksize;
}


/*
 * ar_read - fill the archive buffer
 *
 * DESCRIPTION
 *
 * 	Remembers mid-buffer read failures and reports them the next time
 *	through.  Replaces unreadable data with null characters.   Resets
 *	the buffer pointers as appropriate.
 *
 * RETURNS
 *
 *	Returns zero with valid data, -1 otherwise.
 */


int
ar_read(void)
{
	int		got;
	static int	failed;

	bufend = bufidx = bufstart;
	if (!failed) {
		if (areof) {
			if (total == 0)
				fatal(gettext("No input"));
			else
				next(AR_READ);
		}
		while (!failed && !areof &&
		    bufstart + blocksize - bufend >= blocksize) {
			if ((got = read(archivefd, bufend,
			    (unsigned int) blocksize)) > 0) {
				bufend += got;
			} else if (got < 0) {
				failed = -1;
				warnarch(strerror(errno),
				    (OFFSET) 0 - (bufend - bufidx));
			} else {
				++areof;
			}
		}
	}
	if (failed && bufend == bufstart) {
		failed = 0;
		for (got = 0; got < blocksize; ++got) {
			*bufend++ = '\0';
		}
		return (-1);
	}
	return (0);
}

/*
 * buf_pad - pad the archive buffer
 *
 * DESCRIPTION
 *
 *	Buf_pad writes len zero bytes to the archive buffer in order to
 *	pad it.
 *
 * PARAMETERS
 *
 *	OFFSET	pad	- number of zero bytes to pad
 *
 */


static void
buf_pad(OFFSET pad)
{
	int	idx;
	int	have;

	while (pad) {
		if ((have = bufend - bufidx) > pad)
			have = pad;
		for (idx = 0; idx < have; ++idx) {
			*bufidx++ = '\0';
		}
		total += have;
		pad -= have;
		if (bufend - bufidx == 0)
			outflush();
	}
}


/*
 * buf_use - allocate buffer space
 *
 * DESCRIPTION
 *
 *	Buf_use marks space in the buffer as being used; advancing both the
 *	buffer index (bufidx) and the total byte count (total).
 *
 * PARAMETERS
 *
 *	uint_t	len	- Amount of space to allocate in the buffer
 */


static void
buf_use(uint_t len)
{
	bufidx += len;
	total += len;
}


/*
 * buf_in_avail - index available input data within the buffer
 *
 * DESCRIPTION
 *
 *	Buf_in_avail fills the archive buffer, and points the bufp
 *	parameter at the start of the data.  The lenp parameter is
 *	modified to contain the number of bytes which were read.
 *
 * PARAMETERS
 *
 *	char   **bufp	- pointer to the buffer to read data into
 *	uint_t	*lenp	- pointer to the number of bytes which were read
 *			  (returned to the caller)
 *
 * RETURNS
 *
 * 	Stores a pointer to the data and its length in given locations.
 *	Returns zero with valid data, -1 if unreadable portions were
 *	replaced with nulls.
 *
 * ERRORS
 *
 *	If an error occurs in ar_read, the error code is returned to the
 *	calling function.
 *
 */


static int
buf_in_avail(char **bufp, uint_t *lenp)
{
	uint_t	have;
	int	corrupt = 0;

	while ((have = bufend - bufidx) == 0) {
		corrupt |= ar_read();
	}
	*bufp = bufidx;
	*lenp = have;
	return (corrupt);
}


/*
 * buf_out_avail  - index buffer space for archive output
 *
 * DESCRIPTION
 *
 * 	Stores a buffer pointer at a given location. Returns the number
 *	of bytes available.
 *
 * PARAMETERS
 *
 *	char	**bufp	- pointer to the buffer which is to be stored
 *
 * RETURNS
 *
 * 	The number of bytes which are available in the buffer.
 *
 */


static uint_t
buf_out_avail(char **bufp)
{
	int	have;

	if (bufend - bufidx < 0)
		fatal(gettext("Buffer overflow in buf_out_avail"));
	if ((have = bufend - bufidx) == 0) {
		outflush();
		have = bufend - bufidx;
	}
	*bufp = bufidx;
	return (have);
}


#if defined(O_XATTR)
char *
get_component(char *path)
{
	char *ptr;

	ptr = strrchr(path, '/');
	if (ptr == NULL) {
		return (path);
	} else {

		/*
		 * Handle trailing slash
		 */
		if (*(ptr + 1) == '\0') {
			return (ptr);
		} else {
			return (ptr + 1);
		}
	}
}
#else
char *
get_component(char *path)
{
	return (path);
}
#endif
