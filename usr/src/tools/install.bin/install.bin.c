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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <ctype.h>
#include "stdusers.h"


#define	FILE_BUFF	40960

static int suppress = 0;
static int stripcddl = 0;

static void usage(void);
static void file_copy(char *src_file, char *dest_file);
static void chown_file(const char *file, const char *group, const char *owner);
static void formclosed(char *root, char *closedroot);
static char *find_basename(const char *str);
static int creatdir(char *fn);

static const char cddl_end[] = "CDDL HEADER END";

void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: install [-CsdO][-m mode][-g group][-u owner] "
	    "-f dir file ...\n");
}

/*
 * We've detected a CDDL and we're now removing it.
 * We've found pointer to the start of CDDL HEADER START and END;
 * we remove the bits between those two but we also remove:
 *	- from start of line to CDDL HEADER START
 *	- from the end of CDDL HEADER END up to and including the newline.
 *	- determine the prefix for the comment block and remove all
 *	  lines containing just the prefix before and after the CDDL block
 *	  except for the last one.
 *	- if we're left with an empty C-style block comment,
 *	  we remove those two lines too.
 */

/* Define the size of the Comment Start/End strings, without NUL. */
#define	SCS	3
#define	SCE	4
static const char cs[SCS] = "/*\n";
static const char ce[SCE] = " */\n";

static void
elide_cddl(char *buf, int count, char *s, char *e, int fd)
{
	char *ep = s;
	char *sp, *le;
	int plen;

	while (s > buf && s[-1] != '\n')
		s--;

	sp = s;

	/* Find the prefix for the comments: it is from s .. ep */
	while (ep > s && isspace(ep[-1]))
		ep--;

	/*
	 * Remove the lines starting with the prefix before the cddl block.
	 * sp points to the start of the prefix; just before it there is a \n.
	 * ep points just after the prefix.
	 * We're looking \n<prefix>\n, from s - (prefixlen + 2) to s - 1.
	 */
	plen = ep - sp;
	while (s >= buf + (plen + 2) && s[-1] == '\n' &&
	    strncmp(s - (plen + 2), sp - 1, plen + 1) == 0) {
		s -= plen + 1;
	}
	/* Remove the prefix at the beginning of the buffer. */
	if (s == buf + (plen + 1) && s[-1] == '\n' &&
	    (sp == ep || strncmp(buf, sp, plen) == 0)) {
		s = buf;
	}

	e += sizeof (cddl_end) - 1;
	while (e < &buf[count] && *e != '\n')
		e++;
	e++;

	/*
	 * Remove the lines with just prefixes; looking for prefix\n ....
	 * We keep a pointer to the last removed prefix as we might want
	 * to keep that.
	 */
	le = e;
	while (e < &buf[count] - (plen + 1) && e[plen] == '\n' &&
	    (ep == sp || strncmp(e, sp,  plen) == 0)) {
		le = e;
		e += plen + 1;
	}

	/* See if we are left with an empty C comment. */
	if (s >= buf + SCS && strncmp(s - SCS, cs, SCS) == 0 &&
	    e <= &buf[count] - SCE && strncmp(e, ce, SCE) == 0) {
		s -= SCS;
		e += SCE;
		le = e;
	}

	if (s == buf) {
		/* Removing leading newlines. */
		while (e < &buf[count] && *e == '\n')
			le = ++e;
	}
	/*
	 * Leave the last line with prefix when the next line is not empty
	 * and it doesn't follow the C block comment start.
	 */
	if (e < &buf[count] && *e != '\n' && (s < buf + SCS ||
	    strncmp(s - SCS, cs, SCS) != 0))
		e = le;
	if (s > buf)
		(void) write(fd, buf, s - buf);
	if (e < &buf[count])
		(void) write(fd, e, count - (e - buf));
}

void
file_copy(char *src_file, char *dest_file)
{
	int	src_fd;
	int	dest_fd;
	int	count = 1;
	static char file_buff[FILE_BUFF];

	if ((src_fd = open(src_file, O_RDONLY))  == -1) {
		(void) fprintf(stderr, "install:file_copy: %s failed "
		    "(%d): %s\n", src_file, errno, strerror(errno));
		exit(1);
	}

	if ((dest_fd = open(dest_file, O_CREAT|O_WRONLY|O_TRUNC, 0755)) == -1) {
		(void) fprintf(stderr, "install:file_copy: %s failed "
		    "(%d): %s\n", dest_file, errno, strerror(errno));
		exit(1);
	}

	if (stripcddl == 1) {
		char *s, *e;

		if ((count = read(src_fd, file_buff, FILE_BUFF)) >= SELFMAG &&
		    (file_buff[EI_MAG0] != ELFMAG0 ||
		    file_buff[EI_MAG1] != ELFMAG1 ||
		    file_buff[EI_MAG2] != ELFMAG2 ||
		    file_buff[EI_MAG3] != ELFMAG3) &&
		    (s = strnstr(file_buff, "CDDL HEADER START", count))
		    != NULL &&
		    (e = strnstr(s, cddl_end, count - (s - file_buff)))
		    != NULL) {
			elide_cddl(file_buff, count, s, e, dest_fd);
		} else if (count > 0) {
			(void) write(dest_fd, file_buff, count);
		}
	}

	if (count > 0) {
		while ((count = read(src_fd, file_buff, FILE_BUFF)) > 0) {
			(void) write(dest_fd, file_buff, count);
		}
	}

	if (count == -1) {
		(void) fprintf(stderr, "install:file_copy:read failed "
		    "(%d): %s\n", errno, strerror(errno));
		exit(1);
	}

	if (!suppress)
		(void) printf("%s installed as %s\n", src_file, dest_file);

	(void) close(src_fd);
	(void) close(dest_fd);
}


void
chown_file(const char *file, const char *group, const char *owner)
{
	gid_t	grp = (gid_t)-1;
	uid_t	own = (uid_t)-1;

	if (group) {
		grp = stdfind(group, groupnames);
		if (grp < 0)
			(void) fprintf(stderr, "unknown group(%s)\n", group);
	}

	if (owner) {
		own = stdfind(owner, usernames);
		if (own < 0) {
			(void) fprintf(stderr, "unknown owner(%s)\n", owner);
			exit(1);
		}

	}

	if (chown(file, own, grp) == -1) {
		(void) fprintf(stderr, "install:chown_file: failed "
		    "(%d): %s\n", errno, strerror(errno));
		exit(1);
	}
}


void
formclosed(char *root, char *closedroot)
{
	int wholelen, residlen;
	char *temp;

	wholelen = strlen(root);
	temp = strstr(strstr(root, "proto/root_"), "/");
	temp++;
	temp = strstr(temp, "/");
	residlen = strlen(temp);
	(void) strlcpy(closedroot, root, wholelen - residlen + 1);
	(void) strlcat(closedroot, "-closed", MAXPATHLEN);
	(void) strlcat(closedroot, temp, MAXPATHLEN);
}


char *
find_basename(const char *str)
{
	int	i;
	int	len;

	len = strlen(str);

	for (i = len-1; i >= 0; i--)
		if (str[i] == '/')
			return ((char *)(str + i + 1));
	return ((char *)str);
}

int
creatdir(char *fn) {

	errno = 0;

	if (mkdirp(fn, 0755) == -1) {
		if (errno != EEXIST)
			return (errno);
	} else if (!suppress) {
		(void) printf("directory %s created\n", fn);
	}
	return (0);
}


int
main(int argc, char **argv)
{
	int	c;
	int	errflg = 0;
	int	dirflg = 0;
	char	*group = NULL;
	char	*owner = NULL;
	char	*dirb = NULL;
	char	*ins_file = NULL;
	int	mode = -1;
	char	dest_file[MAXPATHLEN];
	char    shadow_dest[MAXPATHLEN];
	char	shadow_dirb[MAXPATHLEN];
	int	tonic = 0;
	int	rv = 0;

	while ((c = getopt(argc, argv, "Cf:sm:du:g:O")) != EOF) {
		switch (c) {
		case 'C':
			stripcddl = 1;
			break;
		case 'f':
			dirb = optarg;
			break;
		case 'g':
			group = optarg;
			break;
		case 'u':
			owner = optarg;
			break;
		case 'd':
			dirflg = 1;
			break;
		case 'm':
			mode = strtol(optarg, NULL, 8);
			break;
		case 's':
			suppress = 1;
			break;
		case 'O':
			tonic = 1;
			break;
		case '?':
			errflg++;
			break;
		}
	}

	if (errflg) {
		usage();
		return (1);
	}

	if (argc == optind) {
		usage();
		return (1);
	}

	if (!dirflg && (dirb == NULL)) {
		(void) fprintf(stderr,
		    "install: no destination directory specified.\n");
		return (1);
	}

	for (c = optind; c < argc; c++) {
		ins_file = argv[c];

		if (dirflg) {
			if (tonic) {
				formclosed(ins_file, shadow_dest);
				rv = creatdir(shadow_dest);
				if (rv) {
					(void) fprintf(stderr,
					    "install: tonic creatdir "
					    "%s (%d): (%s)\n",
					    shadow_dest, errno,
					    strerror(errno));
					return (rv);
				}
			}
			rv = creatdir(ins_file);
			if (rv) {
				(void) fprintf(stderr,
				    "install: creatdir %s (%d): %s\n",
				    ins_file, errno, strerror(errno));
				return (rv);
			}
			(void) strlcpy(dest_file, ins_file, MAXPATHLEN);

		} else {
			(void) strcat(strcat(strcpy(dest_file, dirb), "/"),
			    find_basename(ins_file));
			file_copy(ins_file, dest_file);

			if (tonic) {
				formclosed(dirb, shadow_dirb);
				/*
				 * The standard directories in the proto
				 * area are created as part of "make setup",
				 * but that doesn't create them in the
				 * closed proto area. So if the target
				 * directory doesn't exist, we need to
				 * create it now.
				 */
				rv = creatdir(shadow_dirb);
				if (rv) {
					(void) fprintf(stderr,
					    "install: tonic creatdir(f) "
					    "%s (%d): %s\n",
					    shadow_dirb, errno,
					    strerror(errno));
					return (rv);
				}
				(void) strcat(strcat(strcpy(shadow_dest,
				    shadow_dirb), "/"),
				    find_basename(ins_file));
				file_copy(ins_file, shadow_dest);
			}
		}

		if (group || owner) {
			chown_file(dest_file, group, owner);
			if (tonic)
				chown_file(shadow_dest, group, owner);
		}
		if (mode != -1) {
			(void) umask(0);
			if (chmod(dest_file, mode) == -1) {
				(void) fprintf(stderr,
				    "install: chmod of %s to mode %o failed "
				    "(%d): %s\n",
				    dest_file, mode, errno, strerror(errno));
				return (1);
			}
			if (tonic) {
				if (chmod(shadow_dest, mode) == -1) {
					(void) fprintf(stderr,
					    "install: tonic chmod of %s "
					    "to mode %o failed (%d): %s\n",
					    shadow_dest, mode,
					    errno, strerror(errno));
					return (1);
				}
			}
		}
	}
	return (0);
}
