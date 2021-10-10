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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * Common code and structures used by name-service-switch "files" backends.
 */

/*
 * An implementation that used mmap() sensibly would be a wonderful thing,
 *   but this here is just yer standard fgets() thang.
 */

#include "files_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <exec_attr.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define	MSLEEP(m)		(void) usleep((m) * 1000)
/* NOTE: this definition exists in two places, nscd:cache.c */
#define	LOCALFILE		"local-entries"

/*
 * Open either the firsts or the next file in the directory
 * in directory order (i.e., random).
 * If we're iterating a "file", we can open it once and then we
 * fail.
 * The first file we open is the LOCALFILE as the entries
 * added by the administator are preferred.
 */
static int
opennextfile(files_backend_ptr_t be, boolean_t rew)
{
	struct dirent *dp;
	int fd;

	/* Handle the simple file case */
	if (!(be->flags & FC_FLAG_USEDIR)) {
		if (be->f != NULL && !rew)
			return (-1);
		if (be->f == NULL) {
			be->f = fopen(be->filename, "rF");
			if (be->f == NULL)
				return (-1);
		} else
			rewind(be->f);
		return (0);
	}

	/* If f == NULL, we have rewinded. */
	if (be->f != NULL) {
		(void) fclose(be->f);
		be->f = NULL;
	} else {
		rew = B_TRUE;
	}
	if (be->dir == NULL) {
		if ((be->dir = opendir(be->filename)) == NULL)
			return (-1);
		rew = B_TRUE;
	} else if (rew)
		rewinddir(be->dir);

	/* First open the LOCALFILE */
	if (rew) {
		fd = openat(dirfd(be->dir),  LOCALFILE, O_RDONLY);
		if (fd >= 0) {
			be->f = fdopen(fd, "rF");
			if (be->f != NULL)
				return (0);
			(void) close(fd);
			/* Out of memory */
			return (-1);
		}
	}

	while ((dp = readdir(be->dir)) != NULL) {
		struct stat buf;

		/* Only enumerate non hidden actual files, skip LOCALFILE */
		if (dp->d_name[0] == '.' || strcmp(LOCALFILE, dp->d_name) == 0)
			continue;
		if (fstatat(dirfd(be->dir), dp->d_name, &buf, 0) == -1)
			continue;
		if (!S_ISREG(buf.st_mode))
			continue;

		fd = openat(dirfd(be->dir),  dp->d_name, O_RDONLY);

		if (fd < 0)
			continue;

		be->f = fdopen(fd, "rF");

		if (be->f != NULL)
			return (0);

		(void) close(fd);
		/* Break because we have run out of memory */
		break;
	}
	return (-1);
}

/*
 * If we're caching a whole directory, return the most recent
 * timestamp of the directory <x>.d and the file <x>.
 */
static int
repr_stat64(const char *filename, struct stat64 *st)
{
	struct stat64 statf;
	int ret;
	int fd;

	ret = stat64(filename, st);
	if (ret != 0 || !S_ISDIR(st->st_mode))
		return (ret);

	fd = open(filename, O_SEARCH | O_DIRECTORY);

	if (fd == -1)
		return (0);

	ret = fstatat64(fd, LOCALFILE, &statf, 0);
	(void) close(fd);

	if (ret == -1)
		return (0);

	if (statf.st_mtim.tv_sec < st->st_mtim.tv_sec ||
	    (statf.st_mtim.tv_sec == st->st_mtim.tv_sec &&
	    statf.st_mtim.tv_nsec <= st->st_mtim.tv_nsec)) {
		return (0);
	}
	/* The file is more recent, use its timestamp. */
	*st = statf;
	return (0);
}


/*
 * Load the whole file or the whole directory of files; dump everything
 * in a buffer with one additional newline after each file.
 */
static char *
loadwholefile(files_backend_ptr_t be, struct stat64 *st, boolean_t *retry)
{
	char *buf;
	size_t totlen, off;
	struct stat stbuf;

	*retry = B_FALSE;

	if (!(be->flags & FC_FLAG_USEDIR)) {
		if (st->st_size > SSIZE_MAX)
			return (NULL);

		/* Make sure we always reopen the file, it has changed! */
		if (be->f != NULL) {
			(void) fclose(be->f);
			be->f = NULL;
		}
	}

	if (opennextfile(be, B_TRUE) != 0)
		return (NULL);

	totlen = 0;

	/*
	 * Concatenate all the fragments; we add an additional newline after
	 * each file so we don't accidentily merge unrelated lines from
	 * different files.
	 */
	do {
		if (fstat(fileno(be->f), &stbuf) != 0)
			return (NULL);
		totlen += stbuf.st_size + 1;
		/* Guard against integer overflow */
		if (totlen < stbuf.st_size)
			return (NULL);
	} while (opennextfile(be, B_FALSE) == 0);

	if ((buf = malloc(totlen)) == NULL)
		return (NULL);

	*retry = B_TRUE;

	if (opennextfile(be, B_TRUE) != 0) {
		free(buf);
		return (NULL);
	}

	off = 0;

	do {
		if (fstat(fileno(be->f), &stbuf) != 0 ||
		    stbuf.st_size + 1 > totlen - off ||
		    read(fileno(be->f), &buf[off], stbuf.st_size) !=
		    stbuf.st_size) {
			free(buf);
			return (NULL);
		}
		off += stbuf.st_size;
		buf[off++] = '\n';
	} while (opennextfile(be, B_FALSE) == 0);

	if (off != totlen) {
		free(buf);
		return (NULL);
	}
	/* If we're collecting fragments, return the size of the sum minus 1. */
	if ((be->flags & FC_FLAG_USEDIR))
		st->st_size = off - 1;

	return (buf);
}

/*
 * Shared hash functions for multiple databases.
 */
uint_t
hash_string(const char *name, int namelen)
{
	int i;
	uint_t 		hash = 0;

	for (i = 0; i < namelen; i++)
		hash = hash * 15 + name[i];

	return (hash);
}

/*
 * Fields are numbered 1, 2 ... n
 */
uint_t
hash_field(const char *line, int linelen, int field)
{
	const char *name;

	while (field > 0 && linelen > 0) {
		if (field == 1)
			name = line;
		while (linelen-- > 0 && *line++ != ':')
			;

		if (linelen < 0)
			line++;

		if (field == 1)
			return (hash_string(name, line - name - 1));
		field--;
	}
	return (0);
}

uint_t
hash_name(nss_XbyY_args_t *argp, int keyhash, const char *line, int linelen)
{
	const char	*name;
	int		namelen;

	if (keyhash) {
		name = argp->key.name;
		namelen = strlen(name);
	} else {
		name = line;
		namelen = 0;
		while (linelen-- && *line++ != ':')
			namelen++;
	}

	return (hash_string(name, namelen));
}

/*
 * Hash the uid or the gid.  Uid and gid are at the same field,
 * the argument is passed in the same field of the union and
 * UID_NOBODY and GID_NOBODY are the same.
 */
uint_t
hash_ugid(nss_XbyY_args_t *argp, int keyhash, const char *line, int linelen)
{
	uint_t		id;
	const char	*linep, *limit, *end;

	linep = line;
	limit = line + linelen;

	if (keyhash)
		return ((uint_t)argp->key.uid);

	while (linep < limit && *linep++ != ':') /* skip username */
		continue;
	while (linep < limit && *linep++ != ':') /* skip password */
		continue;
	if (linep == limit)
		return (UID_NOBODY);

	/* uid */
	end = linep;
	id = (uint_t)strtoul(linep, (char **)&end, 10);

	/* empty uid */
	if (linep == end)
		return (UID_NOBODY);

	return (id);
}

/*
 * Push all records with the same key for later merging.
 */
static int
push_line(line_matches_t **plinem, const char *line, int len)
{
	line_matches_t *linem;

	/* drop duplicates */
	for (linem = *plinem; linem != NULL; linem = linem->next) {
		if (linem->len == len && strncmp(line, linem->line, len) == 0)
			return (1);
	}

	linem = malloc(sizeof (*linem));

	if (linem == NULL)
		return (0);

	linem->line = strndup(line, len);
	if (linem->line == NULL) {
		free(linem);
		return (0);
	}

	linem->len = len;
	linem->next = *plinem;
	*plinem = linem;

	return (1);
}


/*ARGSUSED*/
nss_status_t
_nss_files_setent(files_backend_ptr_t be, void *dummy)
{
	be->hentry = 0;

	/* Make sure it does not open anything yet; we may never in nscd */
	if (be->flags & FC_FLAG_USEDIR) {
		if (be->f != NULL) {
			(void) fclose(be->f);
			be->f = NULL;
		}
		if (be->dir != NULL)
			rewinddir(be->dir);
	} else if (be->f != NULL)
		rewind(be->f);

	return (NSS_SUCCESS);
}

/*ARGSUSED*/
nss_status_t
_nss_files_endent(files_backend_ptr_t be, void *dummy)
{
	be->hentry = 0;

	if (be->f != NULL) {
		(void) fclose(be->f);
		be->f = NULL;
	}
	if (be->dir != NULL) {
		(void) closedir(be->dir);
		be->dir = NULL;
	}
	if (be->buf != NULL) {
		free(be->buf);
		be->buf = NULL;
	}
	return (NSS_SUCCESS);
}

/*
 * This routine reads a line, including the processing of continuation
 * characters.  It always leaves (or inserts) \n\0 at the end of the line.
 * It returns the length of the line read, excluding the \n\0.  Who's idea
 * was this?
 * Returns -1 on EOF.
 *
 * Note that since each concurrent call to _nss_files_read_line has
 * it's own FILE pointer, we can use getc_unlocked w/o difficulties,
 * a substantial performance win.
 */
int
_nss_files_read_line(files_backend_ptr_t be, char *buffer, int buflen)
{
	int			linelen;	/* 1st unused slot in buffer */
	int			c;
	FILE			*f;

	if (be->f == NULL && opennextfile(be, B_FALSE) != 0)
		return (-1);

	f = be->f;

	for (;;) {
		linelen = 0;
		while (linelen < buflen - 1) {	/* "- 1" saves room for \n\0 */
			switch (c = getc_unlocked(f)) {
			case EOF:
				if (linelen == 0 &&
				    opennextfile(be, B_FALSE) == 0) {
					f = be->f;
					continue;
				} else if (linelen == 0 ||
				    buffer[linelen - 1] == '\\') {
					return (-1);
				} else {
					buffer[linelen    ] = '\n';
					buffer[linelen + 1] = '\0';
					return (linelen);
				}
			case '\n':
				if (linelen > 0 &&
				    buffer[linelen - 1] == '\\') {
					--linelen;  /* remove the '\\' */
				} else {
					buffer[linelen    ] = '\n';
					buffer[linelen + 1] = '\0';
					return (linelen);
				}
				break;
			default:
				buffer[linelen++] = c;
			}
		}
		/* Buffer overflow -- eat rest of line and loop again */
		/* ===> Should syslog() */
		do {
			c = getc_unlocked(f);
			if (c == EOF) {
				return (-1);
			}
		} while (c != '\n');
	}
	/*NOTREACHED*/
}

/*
 * used only for getgroupbymem() now.
 */
nss_status_t
_nss_files_do_all(files_backend_ptr_t be, void *args, const char *filter,
    files_do_all_func_t func)
{
	long			grlen;
	char			*buffer;
	int			buflen;
	nss_status_t		res;

	if (be->buf == NULL) {
		if ((grlen = sysconf(_SC_GETGR_R_SIZE_MAX)) > 0)
			be->minbuf = grlen;
		if ((be->buf = malloc(be->minbuf)) == NULL)
			return (NSS_UNAVAIL);
	}
	buffer = be->buf;
	buflen = be->minbuf;

	if ((res = _nss_files_setent(be, NULL)) != NSS_SUCCESS) {
		return (res);
	}

	res = NSS_NOTFOUND;

	do {
		int		linelen;

		if ((linelen = _nss_files_read_line(be, buffer, buflen)) < 0) {
			/* End of file */
			break;
		}
		if (filter != NULL && strstr(buffer, filter) == 0) {
			/*
			 * Optimization:  if the entry doesn't contain the
			 *   filter string then it can't be the entry we want,
			 *   so don't bother looking more closely at it.
			 */
			continue;
		}
		res = (*func)(buffer, linelen, args);

	} while (res == NSS_NOTFOUND);

	(void) _nss_files_endent(be, NULL);
	return (res);
}

/*
 * Could implement this as an iterator function on top of _nss_files_do_all(),
 * but the shared code is small enough that it'd be pretty silly.
 * Additional arguments apart from the usual suspects:
 *	netdb	- whether it uses netdb format or not
 *	filter	- advisory, to speed up string search
 *	check	- check function, NULL means one-shot, for getXXent
 */
nss_status_t
_nss_files_XY_all(files_backend_ptr_t be, nss_XbyY_args_t *args,
    int netdb, const char *filter, files_XY_check_func check)
{
	char			*r;
	nss_status_t		res;
	int			parsestat;
	int			 (*func)();
	struct line_matches	*line_m = NULL;

	if (filter != NULL && *filter == '\0')
		return (NSS_NOTFOUND);
	if (be->buf == NULL || (be->minbuf < args->buf.buflen)) {
		if (be->minbuf < args->buf.buflen) {
			if (be->buf == NULL) {
				be->minbuf = args->buf.buflen;
			} else if (
			    (r = realloc(be->buf, args->buf.buflen)) != NULL) {
				be->buf = r;
				be->minbuf = args->buf.buflen;
			}
		}
		if (be->buf == NULL && (be->buf = malloc(be->minbuf)) == NULL)
			return (NSS_UNAVAIL);
	}

	if (check != NULL) {
		if ((res = _nss_files_setent(be, NULL)) != NSS_SUCCESS) {
			return (res);
		}
	}

	res = NSS_NOTFOUND;

	for (;;) {
		char		*instr	= be->buf;
		int		linelen;

		if ((linelen = _nss_files_read_line(be, instr,
		    be->minbuf)) < 0) {
			/* End of file */
			args->returnval = 0;
			args->returnlen = 0;
			break;
		}
		if (filter != NULL && strstr(instr, filter) == NULL) {
			/*
			 * Optimization:  if the entry doesn't contain the
			 *   filter string then it can't be the entry we want,
			 *   so don't bother looking more closely at it.
			 */
			continue;
		}
		if (netdb) {
			char		*first;
			char		*last;

			if ((last = strchr(instr, '#')) == NULL) {
				last = instr + linelen;
			}
			*last-- = '\0';		/* Nuke '\n' or #comment */

			/*
			 * Skip leading whitespace.  Normally there isn't
			 *   any, so it's not worth calling strspn().
			 */
			for (first = instr;  isspace(*first);  first++) {
				;
			}
			if (*first == '\0') {
				continue;
			}
			/*
			 * Found something non-blank on the line.  Skip back
			 * over any trailing whitespace;  since we know
			 * there's non-whitespace earlier in the line,
			 * checking for termination is easy.
			 */
			while (isspace(*last)) {
				--last;
			}

			linelen = last - first + 1;
			if (first != instr) {
					instr = first;
			}
		}

		args->returnval = 0;
		args->returnlen = 0;

		if (check != NULL && (*check)(args, instr, linelen) == 0)
			continue;

		/*
		 * Special case for passwd and group wherein we
		 * replace uids/gids > MAXUID by ID_NOBODY
		 * because files backend does not support
		 * ephemeral ids.
		 * Special case for prof_attr as we need to merge
		 * several entries.
		 */
		if (be->flags & FC_FLAG_PASSWD) {
			parsestat = validate_passwd_ids(instr,
			    &linelen, be->minbuf, 2);
		} else if (be->flags & FC_FLAG_GROUP) {
			parsestat = validate_group_ids(instr,
			    &linelen, be->minbuf, 2, check);
		} else if (check != NULL && (be->flags & FC_FLAG_MERGEATTR)) {
			parsestat = NSS_STR_PARSE_SUCCESS;
			if (!push_line(&line_m, instr, linelen)) {
				res = NSS_UNAVAIL;
				break;
			}
			res = NSS_SUCCESS;
			continue;
		} else
			parsestat = NSS_STR_PARSE_SUCCESS;

		if (parsestat == NSS_STR_PARSE_SUCCESS) {
			func = args->str2ent;
			parsestat = (*func)(instr, linelen, args->buf.result,
			    args->buf.buffer, args->buf.buflen);
		}

		if (parsestat == NSS_STR_PARSE_SUCCESS) {
			args->returnval = (args->buf.result != NULL) ?
			    args->buf.result : args->buf.buffer;
			args->returnlen = linelen;
			res = NSS_SUCCESS;

			if (check != NULL && (be->flags & FC_FLAG_EXECATTR)) {
				/* exec_attr may return multiple matches */
				_priv_execattr  *pe = args->key.attrp;
				if (!IS_GET_ONE(pe->search_flag)) {
					if (_doexeclist(args) != 0)
						continue;
					res = NSS_UNAVAIL;
				}
			}
			break;
		} else if (parsestat == NSS_STR_PARSE_ERANGE) {
			args->erange = 1;
			break;
		} /* else if (parsestat == NSS_STR_PARSE_PARSE) don't care ! */
	}

	/*
	 * stayopen is set to 0 by default in order to close the opened
	 * file.  Some applications may break if it is set to 1.
	 */
	if (check != NULL) {
		if (!args->stayopen)
			(void) _nss_files_endent(be, NULL);
		if (be->flags & FC_FLAG_MERGEATTR)
			res = finish_attr(line_m, args, res, be->flags);
	}

	return (res);
}

/*
 * File hashing support.  Critical for sites with large (e.g. 1000+ lines)
 * /etc/passwd or /etc/group files.  Currently only used by getpw*() and
 * getgr*() and get*attr() routines, but any files backend can use this stuff.
 */
static void
_nss_files_hash_destroy(files_hash_t *fhp)
{
	free(fhp->fh_table);
	fhp->fh_table = NULL;
	free(fhp->fh_line);
	fhp->fh_line = NULL;
	free(fhp->fh_file_start);
	fhp->fh_file_start = NULL;
}

/*
 * It turns out the hashing stuff really needs to be disabled for processes
 * other than the nscd; the consumption of swap space and memory is otherwise
 * unacceptable when the nscd is killed w/ a large passwd file (4M) active.
 * See 4031930 for details.
 * So we just use this pseudo function to enable the hashing feature.  Since
 * this function name is private, we just create a function w/ the name
 *  __nss_use_files_hash in the nscd itself and everyone else uses the old
 * interface.
 * There are two different functions using the hash table.
 * _nss_files_XY_hash		- searches the hash table for a particular key.
 * _nss_files_XY_hashgetent	- implements getent on the hash table.
 */

#pragma weak __nss_use_files_hash

extern void  __nss_use_files_hash(void);

static boolean_t rebuild_hash(files_backend_ptr_t, int, files_hash_t *,
    struct stat64 *);

nss_status_t
_nss_files_XY_hash(files_backend_ptr_t be, nss_XbyY_args_t *args,
    int netdb, files_hash_t *fhp, int hashop, files_XY_check_func check)
{
	int retries, pstat;
	uint_t hash, line;
	files_hashent_t *hp, *htab;
	struct stat64 st;
	nss_status_t res;
	struct line_matches *line_m = NULL;

	if (__nss_use_files_hash == NULL)
		return (_nss_files_XY_all(be, args, netdb, NULL, check));

	(void) mutex_lock(&fhp->fh_lock);
retry:
	res = NSS_NOTFOUND;
	retries = 100;
	while (repr_stat64(be->filename, &st) < 0) {
		/*
		 * On a healthy system this can't happen except during brief
		 * periods when the file is being modified/renamed.  Keep
		 * trying until things settle down, but eventually give up.
		 */
		if (--retries == 0)
			goto unavail;
		MSLEEP(100);
	}

	/*
	 * When we have a "fragmented" file, we require that updates
	 * modify the mtime of the directory or the local-entries file.
	 * This should be automatic as files installed or removed in the
	 * fragment directory will update the mtime of the directory.
	 */
	if (st.st_mtim.tv_sec == fhp->fh_mtime.tv_sec &&
	    st.st_mtim.tv_nsec == fhp->fh_mtime.tv_nsec &&
	    fhp->fh_table != NULL) {
		htab = &fhp->fh_table[hashop * fhp->fh_size];
		hash = fhp->fh_hash_func[hashop](args, 1, NULL, 0);
		for (hp = htab[hash % fhp->fh_size].h_first; hp != NULL;
		    hp = hp->h_next) {
			if (hp->h_hash != hash)
				continue;
			line = hp->h_line;
			args->returnval = 0;
			args->returnlen = 0;
			if ((*check)(args, fhp->fh_line[line].l_start,
			    fhp->fh_line[line].l_len) == 0)
				continue;

			if (be->flags & FC_FLAG_PASSWD) {
				pstat = validate_passwd_ids(
				    fhp->fh_line[line].l_start,
				    &fhp->fh_line[line].l_len,
				    fhp->fh_line[line].l_len + 1,
				    1);
			} else if (be->flags & FC_FLAG_GROUP) {
				pstat = validate_group_ids(
				    fhp->fh_line[line].l_start,
				    &fhp->fh_line[line].l_len,
				    fhp->fh_line[line].l_len + 1,
				    1, check);
			} else if (be->flags & FC_FLAG_MERGEATTR) {
				if (!push_line(&line_m,
				    fhp->fh_line[line].l_start,
				    fhp->fh_line[line].l_len)) {
					res = NSS_UNAVAIL;
					break;
				}
				res = NSS_SUCCESS;
				continue;
			} else if (be->flags & FC_FLAG_HOSTNAME) {
				if (!push_line(&line_m,
				    fhp->fh_line[line].l_start,
				    fhp->fh_line[line].l_len)) {
					res = NSS_UNAVAIL;
					break;
				}
				res = NSS_SUCCESS;
				continue;
			} else
				pstat = NSS_STR_PARSE_SUCCESS;

			if (pstat != NSS_STR_PARSE_SUCCESS) {
				if (pstat == NSS_STR_PARSE_ERANGE)
					args->erange = 1;
				continue;
			}

			if ((*args->str2ent)(fhp->fh_line[line].l_start,
			    fhp->fh_line[line].l_len, args->buf.result,
			    args->buf.buffer, args->buf.buflen) ==
			    NSS_STR_PARSE_SUCCESS) {
				args->returnval = (args->buf.result) ?
				    args->buf.result:args->buf.buffer;
				args->returnlen = fhp->fh_line[line].l_len;

				res = NSS_SUCCESS;

				if (be->flags & FC_FLAG_EXECATTR) {
					_priv_execattr *pe = args->key.attrp;
					if (!IS_GET_ONE(pe->search_flag)) {
						if (_doexeclist(args) != 0)
							continue;
						res = NSS_UNAVAIL;
					}
				}
				(void) mutex_unlock(&fhp->fh_lock);
				return (res);
			} else {
				args->erange = 1;
			}
		}

		if (be->flags & FC_FLAG_MERGEATTR)
			res = finish_attr(line_m, args, res, be->flags);
		else if (be->flags & FC_FLAG_HOSTNAME)
			res = finish_hostname(line_m, args, res);

		(void) mutex_unlock(&fhp->fh_lock);
		return (res);
	}

	if (rebuild_hash(be, netdb, fhp, &st))
		goto retry;

unavail:
	_nss_files_hash_destroy(fhp);
	(void) mutex_unlock(&fhp->fh_lock);
	return (NSS_UNAVAIL);
}

/*
 * Compare two hashed keys; first we compare the hash and
 * then we compare the actual key.  This is only needed for
 * prof_attr where we need to merge entries and we therefor want
 * the same keys next to each other in the hash table.
 */
static int
cmpkey(files_hash_t *fh, files_hashent_t *hpa, files_hashent_t *hpb)
{
	const char *ai, *bi;
	files_linetab_t *linea, *lineb;
	int la, lb, len, res;

	if (hpa->h_hash < hpb->h_hash)
		return (-1);
	if (hpa->h_hash > hpb->h_hash)
		return (1);

	linea = &fh->fh_line[hpa - fh->fh_table];
	ai = memchr(linea->l_start, ':', linea->l_len);
	la = ai == NULL ? linea->l_len : ai - linea->l_start;

	lineb = &fh->fh_line[hpb - fh->fh_table];
	bi = memchr(lineb->l_start, ':', lineb->l_len);
	lb = bi == NULL ? lineb->l_len : bi - lineb->l_start;

	if (lb < la)
		len = lb;
	else
		len = la;

	res = strncmp(linea->l_start, lineb->l_start, len);

	if (res != 0)
		return (res);

	/* Prefix is the same, longer string is greater */
	return (la - lb);
}

static void
insertsorted(files_hash_t *fh, files_hashent_t **hpp, files_hashent_t *new)
{
	while (*hpp != NULL && cmpkey(fh, new, *hpp) > 0)
		hpp = &(*hpp)->h_next;

	new->h_next = *hpp;
	*hpp = new;
}

static boolean_t
keymatch(files_linetab_t *linea, files_linetab_t *lineb)
{
	const char *colona = memchr(linea->l_start, ':', linea->l_len);
	const char *colonb = memchr(lineb->l_start, ':', lineb->l_len);
	int la, lb;

	if (colona == NULL)
		la = linea->l_len;
	else
		la = colona - linea->l_start;

	if (colonb == NULL)
		lb = lineb->l_len;
	else
		lb = colonb - lineb->l_start;

	return (la == lb && memcmp(linea->l_start, lineb->l_start, la) == 0);
}

/*
 *
 * fix_host_hash_table expands the last fh_table with aliases. It transforms
 * h_arr_hash to new entries of fh_table and free the array.
 * For success it returns B_TRUE with pointer to new fh_table in fhp->fh_table,
 * otherwise B_FALSE and fhp->fh_table was not changed.
 */
static boolean_t
fix_host_hash_table(files_hash_t *fhp, uint_t line)
{
	int lindex, hindex, tindex;
	files_hashent_t	*new_table, *hp;

	new_table = realloc(fhp->fh_table,
	    (fhp->fh_nhtab * fhp->fh_size + fhp->fh_aliases) *
	    sizeof (files_hashent_t));

	if (new_table == NULL)
		return (B_FALSE);

	fhp->fh_table = new_table;

	/* expand line hash arrays */
	hindex = fhp->fh_size + line + fhp->fh_aliases;
	for (lindex = fhp->fh_size + line; lindex >= fhp->fh_size;
	    lindex--) {
		hp = &fhp->fh_table[lindex];
		if (hp->h_arr_hash != NULL) {
			for (tindex = hp->h_arr_hash[0]; tindex > 0; tindex--) {
				fhp->fh_table[hindex].h_line = hp->h_line;
				fhp->fh_table[hindex].h_hash =
				    hp->h_arr_hash[tindex];
				fhp->fh_table[hindex].h_first = NULL;
				fhp->fh_table[hindex].h_next = NULL;
				hindex--;
			}
			free(hp->h_arr_hash);
			hp->h_arr_hash = NULL;
		} else {
			fhp->fh_table[hindex].h_line = hp->h_line;
			fhp->fh_table[hindex].h_hash = 0;
			fhp->fh_table[hindex].h_first = NULL;
			fhp->fh_table[hindex].h_next = NULL;
			hindex--;
		}
	}

	return (B_TRUE);
}

/*
 * Rebuild the hashfile; hash lock must be held.  It returns B_TRUE
 * when building was successful or when building can be retried.
 * It returns B_FALSE when it failed permanently.
 */
static boolean_t
rebuild_hash(files_backend_ptr_t be, int netdb, files_hash_t *fhp,
    struct stat64 *st)
{
	int ht;
	uint_t line, f;
	files_hashent_t *hp, *htab;
	char *cp, *first, *last;
	boolean_t ret;

	_nss_files_hash_destroy(fhp);

	if ((fhp->fh_file_start = loadwholefile(be, st, &ret)) == NULL)
		return (ret);

	fhp->fh_file_end = fhp->fh_file_start + (off_t)st->st_size;
	*fhp->fh_file_end = '\n';
	fhp->fh_mtime = st->st_mtim;

	/*
	 * If the file changed since we read it, or if it's less than
	 * 1-2 seconds old, don't trust it; its modification may still
	 * be in progress.  The latter is a heuristic hack to minimize
	 * the likelihood of damage if someone modifies /etc/mumble
	 * directly (as opposed to editing and renaming a temp file).
	 *
	 * Note: the cast to uint_t is there in case (1) someone rdated
	 * the system backwards since the last modification of /etc/mumble
	 * or (2) this is a diskless client whose time is badly out of sync
	 * with its server.  The 1-2 second age hack doesn't cover these
	 * cases -- oh well.
	 *
	 * We don't need this check for fragmented files; in loadwholefile()
	 * we stat and then read all the files twice in two loops over all
	 * the files in the directory. If there is a change in any of
	 * the files or if files are added or removed during this process,
	 * loadwholefile() will fail and we retry. If it succeeds we have
	 * loaded valid data.
	 */
	if (repr_stat64(be->filename, st) < 0 ||
	    st->st_mtim.tv_sec != fhp->fh_mtime.tv_sec ||
	    st->st_mtim.tv_nsec != fhp->fh_mtime.tv_nsec ||
	    ((be->flags & FC_FLAG_USEDIR) == 0 &&
	    (uint_t)(time(0) - st->st_mtim.tv_sec + 2) < 4)) {
		MSLEEP(500);
		return (B_TRUE);
	}

	line = 1;
	for (cp = fhp->fh_file_start; cp < fhp->fh_file_end; cp++)
		if (*cp == '\n')
			line++;

	for (f = 2; f * f <= line; f++) {	/* find next largest prime */
		if (line % f == 0) {
			f = 1;
			line++;
		}
	}

	fhp->fh_size = line;
	fhp->fh_aliases = 0;
	fhp->fh_line = malloc(line * sizeof (files_linetab_t));
	fhp->fh_table = calloc(line * fhp->fh_nhtab, sizeof (files_hashent_t));
	if (fhp->fh_line == NULL || fhp->fh_table == NULL)
		return (B_FALSE);

	line = 0;
	cp = fhp->fh_file_start;
	while (cp < fhp->fh_file_end) {
		first = cp;
		while (*cp != '\n')
			cp++;
		if (cp > first && *(cp - 1) == '\\') {
			(void) memmove(first + 2, first, cp - first - 1);
			cp = first + 2;
			continue;
		}
		last = cp;
		*cp++ = '\0';
		if (netdb) {
			if ((last = strchr(first, '#')) == NULL)
				last = cp - 1;
			*last-- = '\0';		/* nuke '\n' or #comment */
			while (isspace(*first))	/* nuke leading whitespace */
				first++;
			if (*first == '\0')	/* skip content-free lines */
				continue;
			while (isspace(*last))	/* nuke trailing whitespace */
				--last;
			*++last = '\0';
		}
		for (ht = 0; ht < fhp->fh_nhtab; ht++) {
			hp = &fhp->fh_table[ht * fhp->fh_size + line];
			if ((fhp->fh_hash_arr_func != NULL) &&
			    (fhp->fh_hash_arr_func[ht] != NULL)) {
				hp->h_arr_hash = fhp->fh_hash_arr_func[ht](
				    NULL, 0, first, last - first);
				fhp->fh_aliases += hp->h_arr_hash[0] - 1;
			} else {
				hp->h_hash = fhp->fh_hash_func[ht](NULL, 0,
				    first, last - first);
			}
			hp->h_line = line;

		}
		fhp->fh_line[line].l_start = first;
		fhp->fh_line[line++].l_len = last - first;
	}
	/* Make sure that hashgetent terminates. */
	if (line < fhp->fh_size)
		fhp->fh_line[line].l_start = NULL;

	/* move one back to final line */
	line--;

	/* hostname aliases must be expanded */
	if ((be->flags & (FC_FLAG_HOSTNAME | FC_FLAG_IP)) &&
	    (fix_host_hash_table(fhp, line) == B_FALSE))
			return (B_FALSE);

	/*
	 * Populate the hash tables in reverse order so that the hash chains
	 * end up in forward order.  This ensures that hashed lookups find
	 * things in the same order that a linear search of the file would.
	 * This is essential in cases where there could be multiple matches.
	 * For example: until 2.7, root and smtp both had uid 0; but we
	 * certainly wouldn't want getpwuid(0) to return smtp.
	 * But in the case of profattr, we need to make sure the buckets
	 * are sorted.
	 */
	for (ht = 0; ht < fhp->fh_nhtab; ht++) {
		htab = &fhp->fh_table[ht * fhp->fh_size];
		/* use all entries if there can be aliases */
		hp = &htab[(ht != 1) ? line : (line + fhp->fh_aliases)];
		for (; hp >= htab; hp--) {
			uint_t bucket = hp->h_hash % fhp->fh_size;
			if (ht == 0 && (be->flags & FC_FLAG_MERGEATTR)) {
				insertsorted(fhp, &htab[bucket].h_first, hp);
			} else {
				hp->h_next = htab[bucket].h_first;
				htab[bucket].h_first = hp;
			}
		}
	}

	/* We bump the generation count, invalidates earlier getent states. */
	fhp->fh_gen++;

	return (B_TRUE);
}

/*
 * Enumerate the entries in the hash table, this allows us to run
 * getent on the hashed tables without requiring a re-reading of
 * the files.
 */
nss_status_t
_nss_files_XY_hashgetent(files_backend_ptr_t be, nss_XbyY_args_t *args,
    int netdb)
{
	int retries, pstat;
	files_hashent_t *hp, *htab;
	struct stat64 st;
	nss_status_t res;
	struct line_matches *line_m = NULL;
	files_hash_t *fhp = be->hashinfo;

	if (__nss_use_files_hash == NULL || fhp == NULL)
		return (_nss_files_XY_all(be, args, netdb, NULL, NULL));

	(void) mutex_lock(&fhp->fh_lock);
retry:
	res = NSS_NOTFOUND;
	retries = 100;
	while (repr_stat64(be->filename, &st) < 0) {
		/*
		 * On a healthy system this can't happen except during brief
		 * periods when the file is being modified/renamed.  Keep
		 * trying until things settle down, but eventually give up.
		 */
		if (--retries == 0)
			goto unavail;
		MSLEEP(100);
	}

	/*
	 * When we have a "fragmented" file, we require that updates
	 * modifiy the mtime of the directory.
	 */
	if (st.st_mtim.tv_sec != fhp->fh_mtime.tv_sec ||
	    st.st_mtim.tv_nsec != fhp->fh_mtime.tv_nsec ||
	    fhp->fh_table == NULL) {
		/* Rebuild may not have build the hash, so jump to retry */
		if (rebuild_hash(be, netdb, fhp, &st))
			goto retry;
		else
			goto unavail;
	}

	if (be->hentry == 0)
		be->hgen = fhp->fh_gen;
	else if (be->hgen != fhp->fh_gen)
		goto unavail;

	/*
	 * We walk the hash tables line by line so that the getent returns
	 * the values as they are in the file. But for specific database,
	 * prof_attr and user_attr, we need to merge the entries.  For
	 * those database we find the hash entry and locate them in the
	 * hash table and use all the entries with the same key; as we
	 * only want to return those keys just once, we only return a merged
	 * entry when the current entry is the first with that key in the
	 * hash chain.  (One of the reasons why we use insertsorted)
	 */
	htab = fhp->fh_table;

	args->erange = 0;

	while (be->hentry < fhp->fh_size &&
	    fhp->fh_line[be->hentry].l_start != NULL) {
		files_linetab_t *linep;
		files_linetab_t *match;
		files_linetab_t *lastl = NULL;

		linep = &fhp->fh_line[be->hentry];
		hp = &htab[be->hentry++];

		if (be->flags & FC_FLAG_MERGEATTR) {
			uint_t hash = hp->h_hash;
			uint_t bucket = hash % fhp->fh_size;
			files_hashent_t *shp;

			for (shp = htab[bucket].h_first; shp != NULL;
			    shp = shp->h_next) {
				match = &fhp->fh_line[shp - htab];
				if (shp->h_hash == hash &&
				    keymatch(match, linep)) {
					break;
				}
			}
			/*
			 * We have found the first entry matching with the
			 * current key. If this is the current line, then
			 * merge with the other entries with the same key.
			 * If that is not the case, we already merged this
			 * and and we continue with the next entry in the file.
			 */
			if (hp != shp)
				continue;
		}

		for (; hp != NULL; hp = hp->h_next) {

			args->returnval = 0;
			args->returnlen = 0;

			linep = &fhp->fh_line[hp - htab];

			if (be->flags & FC_FLAG_PASSWD) {
				pstat = validate_passwd_ids(linep->l_start,
				    &linep->l_len, linep->l_len + 1, 1);
			} else if (be->flags & FC_FLAG_GROUP) {
				pstat = validate_group_ids(linep->l_start,
				    &linep->l_len, linep->l_len + 1, 1, NULL);
			} else if (be->flags & FC_FLAG_MERGEATTR) {
				if (line_m != NULL && !keymatch(lastl, linep))
					break;
				if (!push_line(&line_m,
				    linep->l_start, linep->l_len)) {
					res = NSS_UNAVAIL;
					break;
				}
				res = NSS_SUCCESS;
				lastl = linep;
				continue;
			} else
				pstat = NSS_STR_PARSE_SUCCESS;

			if (pstat != NSS_STR_PARSE_SUCCESS)
				break;

			if (args->str2ent(linep->l_start, linep->l_len,
			    args->buf.result, args->buf.buffer,
			    args->buf.buflen) == NSS_STR_PARSE_SUCCESS) {
				args->returnval = (args->buf.result) ?
				    args->buf.result : args->buf.buffer;
				args->returnlen = linep->l_len;

				res = NSS_SUCCESS;
			}
			break;
		}
		if (res != NSS_NOTFOUND)
			break;
	}
	if (be->flags & FC_FLAG_MERGEATTR)
		res = finish_attr(line_m, args, res, be->flags);

	(void) mutex_unlock(&fhp->fh_lock);
	return (res);

unavail:
	_nss_files_hash_destroy(fhp);
	(void) mutex_unlock(&fhp->fh_lock);
	return (res);
}

nss_status_t
_nss_files_getent_rigid(files_backend_ptr_t be, void *a)
{
	return (_nss_files_XY_hashgetent(be, a, 0));
}

nss_status_t
_nss_files_getent_netdb(files_backend_ptr_t be, void *a)
{
	return (_nss_files_XY_hashgetent(be, a, 1));
}

/*ARGSUSED*/
nss_status_t
_nss_files_destr(files_backend_ptr_t be, void *dummy)
{
	if (be != NULL) {
		(void) _nss_files_endent(be, NULL);

		if (be->hashinfo != NULL) {
			(void) mutex_lock(&be->hashinfo->fh_lock);
			/* Don't destroy: we're nscd so we keep it. */
			--be->hashinfo->fh_refcnt;
			(void) mutex_unlock(&be->hashinfo->fh_lock);
		}
		free(be);
	}
	return (NSS_SUCCESS);	/* In case anyone is dumb enough to check */
}

nss_backend_t *
_nss_files_constr(files_backend_op_t ops[], int n_ops, const char *filename,
    int min_bufsize, files_hash_t *fhp, int flags)
{
	files_backend_ptr_t	be;
	struct stat		stbuf;

	if ((be = malloc(sizeof (*be))) == NULL) {
		return (0);
	}

	be->ops		= ops;
	be->n_ops	= n_ops;
	be->filename	= filename;
	be->minbuf	= min_bufsize;
	be->f		= NULL;
	be->buf		= NULL;
	be->hashinfo	= fhp;
	be->dir		= NULL;
	be->flags	= flags;
	be->hentry	= 0;

	if (stat(filename, &stbuf) == 0 && S_ISDIR(stbuf.st_mode))
		be->flags |= FC_FLAG_USEDIR;

	if (fhp != NULL) {
		(void) mutex_lock(&fhp->fh_lock);
		fhp->fh_refcnt++;
		(void) mutex_unlock(&fhp->fh_lock);
	}

	return ((nss_backend_t *)be);
}

int
_nss_files_check_name_colon(nss_XbyY_args_t *argp, const char *line,
    int linelen)
{
	const char	*linep, *limit;
	const char	*keyp = argp->key.name;

	linep = line;
	limit = line + linelen;
	while (*keyp && linep < limit && *keyp == *linep) {
		keyp++;
		linep++;
	}
	return (linep < limit && *keyp == '\0' && *linep == ':');
}

/*
 * This routine is used to parse lines of the form:
 * 	name number aliases
 * It returns 1 if the key in argp matches any one of the
 * names in the line, otherwise 0
 * Used by rpc, networks, protocols
 */
int
_nss_files_check_name_aliases(nss_XbyY_args_t *argp, const char *line,
    int linelen)
{
	const char	*limit, *linep, *keyp;

	linep = line;
	limit = line + linelen;
	keyp = argp->key.name;

	/* compare name */
	while (*keyp && linep < limit && !isspace(*linep) && *keyp == *linep) {
		keyp++;
		linep++;
	}
	if (*keyp == '\0' && linep < limit && isspace(*linep))
		return (1);
	/* skip remainder of the name, if any */
	while (linep < limit && !isspace(*linep))
		linep++;
	/* skip the delimiting spaces */
	while (linep < limit && isspace(*linep))
		linep++;
	/* compare with the aliases */
	while (linep < limit) {
		/*
		 * 1st pass: skip number
		 * Other passes: skip remainder of the alias name, if any
		 */
		while (linep < limit && !isspace(*linep))
			linep++;
		/* skip the delimiting spaces */
		while (linep < limit && isspace(*linep))
			linep++;
		/* compare with the alias name */
		keyp = argp->key.name;
		while (*keyp && linep < limit && !isspace(*linep) &&
		    *keyp == *linep) {
			keyp++;
			linep++;
		}
		if (*keyp == '\0' && (linep == limit || isspace(*linep)))
			return (1);
	}
	return (0);
}
