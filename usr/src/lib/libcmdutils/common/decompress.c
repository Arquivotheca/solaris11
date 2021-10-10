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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <thread.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

/* The magic numbers from /etc/magic */

#define	GZIP_MAGIC	"\037\213"
#define	BZIP_MAGIC	"BZh"
#define	COMP_MAGIC	"\037\235"

#define	BZCAT		"/usr/bin/bzcat"
#define	GZCAT		"/usr/bin/gzcat"
#define	ZCAT		"/usr/bin/zcat"

#define	MAX_HDR_MAGIC	3	/* Max. no. of compressed header magic bytes */

struct fds {
	int out;
	int in;
};

/*
 * Given a file descriptor that sources a possibly compressed file, returns
 * one that will source the uncompressed one. Supported compression types
 * are bzip2, gzip and compress.
 *
 * if the input file is seekable:
 *     read the magic number from the file, rewind it.
 *
 *     if compressed:
 *         fork and exec approp. decompressor as needed w/ stdin as
 *         the input.  The stdout would be a pipe, the read end
 *         fd of which is returned to the caller.
 *
 *     else:                     # not compressed
 *         return input fd
 *
 * else:                         # not seekable
 *     read the magic number from the input file.
 *
 *     if compressed:
 *         fork and exec approp. decompressor as needed w/ stdin as
 *         the input.  The stdout would be a pipe, the read end
 *         fd of which will be returned to the caller.   Write header
 *         that was read from input file to stdin of decompressor.
 *         Spawn worker thread that copies remainder of input file to
 *         stdin of decompressor. Return stdout of decompressor to caller.
 *
 *     else:                      # not compressed
 *         create pipe
 *         write header that was read from input file to pipe.
 *         spawn worker thread that copies remainder of input file to pipe.
 *         return other end of pipe to caller.
 *
 * Limitations:
 *
 * 1/ The routine provides no method of waiting for its children.
 *    This means zombie processes may be left if the caller doesn't exit
 *    shortly after finishing the reading and that callers that wait for
 *    children may find unexpected children exiting.
 *
 * 2/ There is no specific error handling. If it fails for any reason,
 *    it just returns -1.
 */

/*
 * Check if we can seek fd w/ affecting it.
 */

static int
is_seekable(int fd)
{
	return (lseek(fd, 0, SEEK_CUR) != -1);
}


/*
 * Read potential header into magic from input fd;
 * return decompressor needed, NULL if none.
 */

static const char *
check_magic_header(char *magic, int inputfd)
{
	if (read(inputfd, magic, sizeof (char) * MAX_HDR_MAGIC) !=
	    sizeof (char) * MAX_HDR_MAGIC)
		return (NULL);			/* will die later on anyway */

	if (memcmp(magic, GZIP_MAGIC, 2) == 0) {
		return (GZCAT);
	} else if (memcmp(magic, BZIP_MAGIC, 3) == 0) {
		return (BZCAT);
	} else if (memcmp(magic, COMP_MAGIC, 2) == 0) {
		return (ZCAT);
	} else {
		return (NULL);
	}
}

/*
 * Pass contents of inputfd through command;
 * return fd from which output may be read.
 */

static int
filter_stream(int inputfd, const char *command)
{
	int fd[2];

	if (pipe(fd) < 0) {
		return (-1);
	}

	switch (fork()) {
	case -1:
		perror("fork");
		return (-1);

	case 0:				/* child */
		(void) dup2(inputfd, 0); /* refer to inputfd as 0 */
		(void) dup2(fd[0], 1);   /* refer out fd[0] as stdout */
		(void) close(fd[1]);	/* this is other end used by parent */
		closefrom(3);
		(void) execlp(command, command, NULL);
		return (-1);

	default:			/* parent */
		(void) close(fd[0]);    /* client's output */
		return (fd[1]);
	}

/*NOTREACHED*/
}

/*
 * Worker thread for splice routine.
 */

static void
splice_thr(void * arg)
{
	struct fds *args = (struct fds *)arg;
	char buf[8192];
	int nread, nwrite;

	while ((nread = read(args->in, buf, sizeof (buf))) != 0) {
		if (nread < 0)
			perror("splice: read");
		nwrite = write(args->out, buf, nread);
		if (nwrite < 0)
			perror("splice: write");
	}

/* Avoid closing source fd to avoid it's reuse. */

	(void) close(args->out);
}

/*
 * Copy from input fd to output fd in separate thread.
 */

static int
splice(int output, int input)
{
	thread_t remainder_thr;
	struct fds *args = malloc(sizeof (struct fds));

	args->in = input;
	args->out = output;

	if (thr_create(NULL, 0, (void *(*)(void *))splice_thr,
	    (void *) args, THR_NEW_LWP, &remainder_thr) != 0) {
		perror("thr_create");
		return (-1);
	}

	return (0);
}

/*
 * Returns fd from which contents of fd may be read,
 * decompressing if needed. Works on non-seekable file
 * descriptors. Returns -1 upon error.
 */

int
decompress_from_fd(int fd)
{
	char buffer[sizeof (char) * MAX_HDR_MAGIC];

	/* check if we need to decompress */
	const char *decompressor = check_magic_header(buffer, fd);

	if (is_seekable(fd)) {
		(void) lseek(fd, 0, SEEK_SET); /* rewind to start of file */

		if (decompressor != NULL) {
			/*
			 * file is compressed; filter it through
			 * appropriate decompressor.
			 */
			return (filter_stream(fd, decompressor));
		} else {
			return (fd);
		}
	} else {	/* not seekable */
		int fds[2];

		(void) pipe(fds);
		(void) write(fds[0], buffer, sizeof (buffer));
		if (splice(fds[0], fd) == 0) {
			if (decompressor != NULL) {
				return (filter_stream(fds[1], decompressor));
			} else {
				return (fds[1]);
			}
		} else {
			return (-1);
		}
	}
}
