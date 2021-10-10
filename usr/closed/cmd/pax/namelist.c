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
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: namelist.c,v $ $Revision: 1.3.3.2
 * $ (OSF) $Date: 1992/09/16 15:06:39 $";
 * #endif
 */
/*
 * namelist.c - track filenames given as arguments to tar/cpio/pax
 *
 * DESCRIPTION
 *
 *	Arguments may be regular expressions, therefore all arguments will
 *	be treated as if they were regular expressions, even if they are
 *	not.
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
 * software was developed by Mark H. Colburn and sponsored by The
 * USENIX Association.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Revision 1.6  89/02/13  09:14:48  mark
 * Fixed problem with directory errors
 *
 * Revision 1.5  89/02/12  12:14:00  mark
 * Fixed misspellings
 *
 * Revision 1.4  89/02/12  11:25:19  mark
 * Modifications to compile and link cleanly under USG
 *
 * Revision 1.3  89/02/12  10:40:23  mark
 * Fixed casting problems
 *
 * Revision 1.2  89/02/12  10:04:57  mark
 * 1.2 release fixes
 *
 * Revision 1.1  88/12/23  18:02:17  mark
 * Initial revision
 *
 */

/* Headers */

#include <fnmatch.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/utime.h>
#include <sys/avl.h>
#include <libcmdutils.h>
#include "pax.h"

/* Type Definitions */

/*
 * Structure for keeping track of filenames and lists thereof.
 */
struct nm_list {
    struct nm_list *next;
	short	length;		/* cached strlen(name) */
	char	found;		/* A matching file has been found */
	char	*whatfound; 	/* a pointer to what we found */
	char	firstch;	/* First char is literally matched */
	char	regexp;		/* regexp pattern for item */
	char	name[1];	/* name of file or rexexp */
};

struct elem {
	struct elem *next;
	char *data;
};

struct list {
	struct elem *first;
	struct elem *last;
};

struct dirinfo {
	char		dirname[PATH_MAX + 1];	/* name of directory */
	struct list	filelist;	/* all the files in the directory */
	struct timeval 	accesst;	/* stored access time */
	struct timeval	modifyt;	/* store modify time */
	dev_t		dev;		/* device id */
	ino_t		ino;		/* inode number */
	struct dirinfo	*next;
};


/* Static Variables */

static struct dirinfo *stack_head = (struct dirinfo *)NULL;


/* Function Prototypes */


static void pushdir(struct dirinfo *info);
static struct dirinfo *popdir(void);
static void list_empty(struct list *);
static void list_append(struct list *, char *);
static char *list_first(struct list *);
static tree_node_t *create_tnode(dev_t, ino_t);



/* Internal Identifiers */

static struct nm_list *namelast;	/* Points to last name in list */
static struct nm_list *namelist;	/* Points to first name in list */
static struct nm_list *lastmatched = NULL;	/* last element matched */
static avl_tree_t	*stree = NULL;	/* dirs visited search tree */

/* External Identifiers */

extern int f_fnmatch_old;

/*
 * addname -  add a name to the namelist.
 *
 * DESCRIPTION
 *
 *	Addname adds the name given to the name list.  Memory for the
 *	namelist structure is dynamically allocated.  If the space for
 *	the structure cannot be allocated, then the program will exit
 *	with an out of memory error message and a non-zero return code
 *	will be returned to the caller.
 *
 * PARAMETERS
 *
 *	char *name	- A pointer to the name to add to the list
 */


static void
add_name(char *name)
{
	int		i;		/* Length of string */
	struct nm_list	*p;		/* Current struct pointer */

	i = strlen(name);
	p = (struct nm_list *)malloc((unsigned)(i + sizeof (struct nm_list)));
	if (!p)
		fatal(gettext("cannot allocate memory for namelist entry\n"));
	p->next = (struct nm_list *)NULL;
	p->length = i;
	(void) strncpy(p->name, name, i);
	p->name[i] = '\0';		/* Null term */
	p->found = 0;
	p->whatfound = NULL;
	p->firstch = isalpha(name[0]);
	if (strchr(name, '*') || strchr(name, '[') || strchr(name, '?'))
		p->regexp = 1;

	if (namelast)
		namelast->next = p;
	namelast = p;
	if (!namelist)
		namelist = p;
}


/*
 * name_match - match a name from an archive with a name from the namelist
 *
 * DESCRIPTION
 *
 *	Name_match attempts to find a name pointed at by p in the namelist.
 *	If no namelist is available, then all filenames passed in are
 *	assumed to match the filename criteria.  Name_match knows how to
 *	match names with regular expressions, etc.
 *
 * PARAMETERS
 *
 *	char	*p	- the name to match
 * 	int	isdir	- The name we are matching is a directory
 *
 * RETURNS
 *
 *	Returns 1 if the name is in the namelist, or no name list is
 *	available, otherwise returns 0
 *
 */


int
name_match(char *p, int isdir)
{
	struct nm_list	*nlp;
	int		len;
	int		all_found_in_namelist = 1;	/* found all files in */
							/* namelist */
	static int	plen;
	static int	havedir;
	static char	prefix[PATH_MAX + 1];
	static char	no_dir_in_namelist = 1;		/* was there a dir in */
							/* namelist */

	len = strlen(p);
	if ((nlp = namelist) == 0) {	/* Empty namelist is(was) easy */
		if (f_no_depth) {
			if (havedir && (plen < len) && (p[plen] == '/') &&
			    strncmp(prefix, p, plen) == 0)
				return (0);	/* subdir of root dir */
			if (isdir) {	/* match top level dir */
				havedir++;
				(void) strncpy(prefix, p, sizeof (prefix));
				plen = strlen(prefix);
				return (1);
			}
			havedir = 0;	/* no longer in a directory */
			return (1);	/* regular file at prefix level */
		} else
			return (1);
	}

	if (!f_unconditional && bad_last_match && lastmatched)
		bad_last_match = lastmatched->found = 0;

	for (; nlp != 0; nlp = nlp->next) {
		if (f_single_match && nlp->found) {
			if (!f_no_depth &&
			    (plen = strlen(nlp->whatfound)) < len &&
			    p[plen] == '/' &&
			    strncmp(p, nlp->whatfound, plen) == 0)
				return (1);
			else
				continue;
		}
		/*
		 * if we get here then we haven't yet
		 * found everything in the namelist
		 */
		all_found_in_namelist = 0;

		/* If first chars don't match, quick skip */
		if (nlp->firstch && nlp->name[0] != p[0])
			continue;

		/* Regular expressions */
		if (nlp->regexp) {
			if (fnmatch(nlp->name, p, (f_fnmatch_old) ? FNM_PERIOD :
			    FNM_PATHNAME|FNM_PERIOD) == 0) {
				nlp->found = 1;	/* Remember it matched */
				if (f_single_match) {
					if (!f_unconditional)
						lastmatched = nlp;
				nlp->whatfound = mem_str(p);
				}
				if (f_no_depth) {
					if (havedir && (plen < len) &&
					    (p[plen] == '/') &&
					    strncmp(prefix, p, plen) == 0)
						/* subdir of root dir */
						return (0);
					if (isdir) {	/* match top levl dir */
						/*
						 * remember we matched a
						 * directory in namelist
						 */
						no_dir_in_namelist = 0;
						havedir++;
						(void) strncpy(prefix, p,
						    sizeof (prefix));
						plen = strlen(prefix);
						return (1);
					}
					havedir = 0;	/* no longer in a dir */
					return (1);	/* regular file at */
							/* prefix level */
				}
				if (isdir)
					/*
					 * remember we matched a
					 * directory in namelist
					 */
					no_dir_in_namelist = 0;
				return (1);	/* We got a match */
			}
			continue;
		}

		/* Plain Old Strings */
		if (f_no_depth) {		/* don't match files in dirs */
			if ((nlp->length == len) &&
			    (strncmp(p, nlp->name, nlp->length) == 0)) {
				nlp->found = 1;
				if (f_single_match) {
					if (!f_unconditional)
						lastmatched = nlp;
					nlp->whatfound = mem_str(p);
				}
				if (isdir)
					/*
					 * remember we matched a
					 * directory in namelist
					 */
					no_dir_in_namelist = 0;
				return (1);
			}
		} else {
			/* Archive len >= specified */
			if (nlp->length <= len &&
			    (p[nlp->length] == '\0' || p[nlp->length] == '/') &&
			    strncmp(p, nlp->name, nlp->length) == 0) {
				/* Name compare */
				nlp->found = 1;	/* Remember it matched */
				if (f_single_match) {
					if (!f_unconditional)
						lastmatched = nlp;
					nlp->whatfound = mem_str(p);
				}
				return (1);		/* We got a match */
			}
		}
	}

	/*
	 * if the -n (f_no_depth) was used and we have found everything that
	 * there was in the namelist, then we can tell pax to exit, if there
	 * wasn't any directories in the namelist.  If we haven't found all
	 * entries in the namelist and/or a directory was specified in the
	 * namelist then continue as usual.
	 */
	if ((all_found_in_namelist) && (no_dir_in_namelist))
		return (-1);
	else
		return (0);		/* no match */
}


/*
 * names_notfound - print names of files in namelist that were not found
 *
 * DESCRIPTION
 *
 *	Names_notfound scans through the namelist for any files which were
 *	named, but for which a matching file was not processed by the
 *	archive.  Each of the files is listed on the standard error.
 *
 *	The program exit status is set to non-zero.
 *
 */


void
names_notfound(void)
{
	struct nm_list	*nlp;
	struct nm_list	*tmpnlp;

	for (nlp = namelist; nlp != NULL; nlp = tmpnlp) {
		tmpnlp = nlp->next;
		if (!nlp->found) {
			(void) fprintf(stderr, gettext(
			    "%s: %s not found in archive\n"),
			    myname, nlp->name);
			exit_status = 1;
		}
		free(nlp);
	}

	namelist = NULL;
	namelast = NULL;
}

/*
 * create_tnode()
 *
 * Create a node for use with the search tree which contains the
 * inode information (device id and inode number).
 *
 * Input
 *	dev	- device id
 * 	ino	- inode number
 *
 * Output
 *	tnode	- NULL on error, otherwise returns a tnode structure
 *		  which contains the input device id and inode number.
 */
static tree_node_t *
create_tnode(dev_t dev, ino_t ino)
{
	tree_node_t	*tnode;

	if ((tnode = (tree_node_t *)malloc(sizeof (tree_node_t))) != NULL) {
		tnode->node_dev = dev;
		tnode->node_ino = ino;
	}

	return (tnode);
}



/*
 * name_next - get the next name from argv or the name file.
 *
 * DESCRIPTION
 *
 *	Name next finds the next name which is to be processed in the
 *	archive.  If the named file is a directory, then the directory
 *	is recursively traversed for additional file names.  Directory
 *	names and files within the directory are kept track of by
 *	using a directory stack.  See the pushdir/popdir function for
 *	more details.
 *
 * 	The names come from argv, after options or from the standard input.
 *
 * 	The directories are read all at once and stored to prevent
 * 	the use of unreliable seekdir/telldir routines.
 *
 * PARAMETERS
 *
 *	name - a pointer to a buffer of at least PATH_MAX + 1 bytes long;
 *	namesz - size of 'name' buffer
 *	statbuf - a pointer to a stat structure
 *
 * RETURNS
 *
 *	Returns -1 if there are no names left, (e.g. EOF), otherwise returns
 *	0
 */


int
name_next(char **name, size_t *namesz, Stat *statbuf)
{
	int			err = -1;
	static int		in_subdir = 0;
	static DIR		*dirp;
	struct dirent		*d;
	static struct dirinfo	*curr_dir;
	int			len;
	char			*elem;
	struct timeval		tstamp[2];
	static dev_t		old_dev = (dev_t)-1;
	avl_index_t		where = 0;
	tree_node_t		*tnode;
	tree_node_t		*tptr;
	int			rc;

	do {
		f_cmdarg = 0;
		if (names_from_stdin && !in_subdir) {
			if (lineget(stdin, *name) < 0)
				return (-1);
			if (nameopt(*name) < 0)
				continue;
			stree = NULL;
		} else {
			if (in_subdir) {
				if ((elem = list_first(&curr_dir->filelist)) !=
				    NULL) {
					if (strlen(elem) + strlen(
					    curr_dir->dirname) >=
					    (size_t)PATH_MAX + 1) {
						warn(gettext("name too long"),
						    elem);
						continue;
					}
					(void) strcpy(*name, curr_dir->dirname);
					(void) strcat(*name, elem);
				} else {
					if (f_extract_access_time) {
						tstamp[0].tv_sec =
						    curr_dir->accesst.tv_sec;
						tstamp[0].tv_usec =
						    curr_dir->accesst.tv_usec;
					} else if (f_mtime) {
						tstamp[0].tv_sec =
						    time((time_t *)0);
						tstamp[0].tv_usec = 0;
					}
					if (f_mtime) {
						tstamp[1].tv_sec =
						    curr_dir->modifyt.tv_sec;
						tstamp[1].tv_usec =
						    curr_dir->modifyt.tv_usec;
					} else if (f_extract_access_time) {
						tstamp[1].tv_sec =
						    time((time_t *)0);
						tstamp[1].tv_usec = 0;
					}
					if (f_extract_access_time ||
					    f_mtime)  {
						(void) utimes(curr_dir->dirname,
						    tstamp);
					}
					/*
					 * If we are following links, since we
					 * are backing out, we need to remove
					 * the inode information for this
					 * directory from the search tree.
					 */
					if (f_follow_links ||
					    f_follow_first_link) {
						if ((tnode = create_tnode(
						    curr_dir->dev,
						    curr_dir->ino)) == NULL) {
							fatal(gettext(
							    "Unable to "
							    "create search "
							    "tree node"));
						}
						if ((tptr = avl_find(stree,
						    tnode, &where)) != NULL) {
							avl_remove(stree, tptr);
						}
						free(tptr);
						free(tnode);
					}
					in_subdir--;
					curr_dir = popdir();
					continue;
				}
			} else if (optind >= argc) {
				return (-1);
			} else {
				if (strlen(argv[optind] + 1) > *namesz) {
					*namesz = strlen(argv[optind]) + 1;
					if ((*name = realloc(*name,
					    *namesz)) == NULL) {
						fatal(gettext(
						    "out of memory"));
					}
				}
				(void) strcpy(*name, argv[optind++]);
				f_cmdarg = 1;
				stree = NULL;
			}
		}

		if ((err = LSTAT(*name, statbuf)) < 0) {
			warn(*name, strerror(errno));
			continue;
		}
		statbuf->symlinkref = 0;

		/*
		 * If we are following symlinks (-H or -L) process
		 * the file referenced by the symbolic link rather
		 * than the symbolic link itself.  Note:  with -H,
		 * only following symlink encountered on command
		 * line argument.
		 */
		if ((statbuf->sb_mode & S_IFMT) == S_IFLNK) {
			if ((f_follow_links) ||
			    ((f_cmdarg) && (f_follow_first_link))) {
				(void) memset(statbuf, 0, sizeof (Stat));
				if ((err = STAT(*name, statbuf)) < 0) {
					warn(*name, strerror(errno));
					continue;
				}
				statbuf->symlinkref = 1;
			}
		}

		if (old_dev == (dev_t)-1)
			old_dev = statbuf->sb_dev;

		if ((statbuf->sb_mode & S_IFMT) == S_IFDIR) {

			/* Draft 11 'X' option.  Don't cross device boundries */
			if (f_device && (statbuf->sb_dev != old_dev))
				continue;

			/* if '-d' option, don't go down into directories */
			if (f_no_depth)
				continue;

			/*
			 * If we are following links then add this
			 * directory's inode information to the
			 * search tree to keep us from visiting
			 * it again and getting into an endless
			 * loop.
			 */
			if ((f_follow_links) ||
			    (f_follow_first_link && f_cmdarg)) {
				if ((rc = add_tnode(&stree,
				    statbuf->sb_dev,
				    statbuf->sb_ino)) != 1) {
					/*
					 * We've already visited this
					 * directory.  The standard
					 * specifies that when an
					 * infinite loop is detected,
					 * then write a diagnostic
					 * message then terminate.
					 */
					if (rc == 0) {
						warn(gettext(
						    "Cycle detected"),
						    *name);
						exit(1);
					} else {
						warn(*name, gettext(
						    "Error adding inode "
						    "to search tree"));
						}
				}
			}
			if (in_subdir)
				pushdir(curr_dir);
			in_subdir++;

			/* Build new prototype name */
			if ((curr_dir = (struct dirinfo *)
			    mem_get(sizeof (struct dirinfo))) == NULL) {
				exit(2);
			}
			(void) strcpy(curr_dir->dirname, *name);
			len = strlen(curr_dir->dirname);
			while (len >= 1 && curr_dir->dirname[len - 1] == '/') {
				len--;		/* Delete trailing slashes */
			}
			curr_dir->dirname[len++] = '/';	/* Add exactly 1 back */
			curr_dir->dirname[len] = '\0';
			curr_dir->accesst.tv_sec = statbuf->sb_atime;
			curr_dir->accesst.tv_usec =
			    statbuf->sb_stat.st_atim.tv_nsec / 1000;
			curr_dir->modifyt.tv_sec = statbuf->sb_mtime;
			curr_dir->modifyt.tv_usec =
			    statbuf->sb_stat.st_mtim.tv_nsec / 1000;
			curr_dir->dev = statbuf->sb_dev;
			curr_dir->ino = statbuf->sb_ino;
			curr_dir->next = NULL;

			errno = 0;
			if ((dirp = opendir(curr_dir->dirname)) ==
			    (DIR *)NULL) {
				warn(curr_dir->dirname, gettext(
				    "error opening directory"));
				if (in_subdir > 1)
					curr_dir = popdir();
				in_subdir--;
				err = -1;
				continue;
			}

			list_empty(&curr_dir->filelist);
			while ((d = readdir(dirp)) != NULL) {
				if (strcmp(".", d->d_name) == 0 ||
				    strcmp("..", d->d_name) == 0)
					continue;
				list_append(&curr_dir->filelist, d->d_name);
			}
			(void) closedir(dirp);

		}
	} while (err < 0);
	return (0);
}


/*
 * name_gather - gather names in a list for scanning.
 *
 * DESCRIPTION
 *
 *	Name_gather takes names from the command line and adds them to
 *	the name list.
 *
 * 	We could hash the names if we really care about speed here.
 */


void
name_gather(void)
{
	while (optind < argc) {
		add_name(argv[optind++]);
	}
}


/*
 * pushdir - pushes a directory name on the directory stack
 *
 * DESCRIPTION
 *
 *	The pushdir function pushes the directory structure which is pointed
 *	to by "info" onto a stack for later processing.  The information
 *	may be retrieved later with a call to popdir().
 *
 * PARAMETERS
 *
 *	dirinfo	*info	- pointer to directory structure to save
 */


static void
pushdir(struct dirinfo *info)
{
	if (stack_head == (struct dirinfo *)NULL) {
		stack_head = info;
		stack_head->next = (struct dirinfo *)NULL;
	} else {
		info->next = stack_head;
		stack_head = info;
	}
}


/*
 * popdir - pop a directory structure off the directory stack.
 *
 * DESCRIPTION
 *
 *	The popdir function pops the most recently pushed directory
 *	structure off of the directory stack and returns it to the calling
 *	function.
 *
 * RETURNS
 *
 *	Returns a pointer to the most recently pushed directory structure
 *	or NULL if the stack is empty.
 */


static struct dirinfo *
popdir(void)
{
	struct dirinfo	*tmp;

	if (stack_head == (struct dirinfo *)NULL)
		return ((struct dirinfo *)NULL);
	else {
		tmp = stack_head;
		stack_head = stack_head->next;
	}
	return (tmp);
}


/*
 * list_empty - initialize an empty directory list
 *
 * DESCRIPTION
 *
 *	Set both elements of the list pointer to NULL.
 *
 */


static void
list_empty(struct list *listp)
{
	listp->first = listp->last = NULL;
}


/*
 * list_append - Add an element to the directory list
 *
 * DESCRIPTION
 *
 *	Add a directory entry to the end of the list.
 *
 */


static void
list_append(struct list *listp, char *data)
{
	struct elem *ep;
	int len;

	ep = (struct elem *)calloc(1, sizeof (struct elem));
	if (ep == NULL) {
		fatal(gettext("out of memory"));
	}
	ep->data = malloc(len = (strlen(data) + 1));
	if (ep->data == NULL) {
		fatal(gettext("out of memory"));
	}
	(void) strncpy(ep->data, data, len);
	if (listp->first == NULL)
		listp->first = ep;
	else
		listp->last->next = ep;
	listp->last = ep;
}


/*
 * list_first - get the first file in the list
 *
 * DESCRIPTION
 *
 * 	Get the first element from the list and free its
 * 	storage.
 *
 * RETURNS
 *
 *	Returns a pointer to the first element in the list.
 *	or NULL if the list is empty.
 */


static char *
list_first(struct list *listp)
{
	struct elem	*ep;
	char		*data;

	if ((ep = listp->first) == NULL)
		return (NULL);
	listp->first = ep->next;
	data = ep->data;
	free((char *)ep);
	return (data);
}
