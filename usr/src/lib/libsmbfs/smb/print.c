/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <libintl.h>

#include <smb/smb.h>
#include <netsmb/smb_lib.h>

#include "private.h"

#define	TRUE			1

#define	SMBFS_TITLEBUFSIZ	256
#define	SMBFS_DATABUFSIZ	4096

static int smb_print_file(smb_ctx_t *, char *, int);
static int smb_open_printer(struct smb_ctx *, const char *, int, int);

/*
 * Prints file or data from stdin using the given printer share UNC path and
 * username.  If username is NULL, then the Solaris username and the SMB
 * domain from SMF are taken.  The password to authenticate the user is taken
 * from the password keychain, standard input, or prompt if standard input is
 * a tty.  If the password from keychain or prompt does not successfully
 * authenticate the user then a password will be prompted for again.
 *
 * Values we get from the command that are copied to the smb_ctx structure
 * should not be overwritten later on, so we mark values from the command
 * as "from CMD" for smb_ctx_setXXXX APIs. See: SMBCF_CMD_*
 *
 * NOTE: The again label with goto statement does not cause an infinite loop
 * since smb_get_authentication() will prompt the user if standard input is
 * a tty or returns error on retry (no data) if standard input is not a tty.
 */
int
smbfs_print(const char *username, const char *share_unc, const char *filename)
{
	struct smb_ctx *ctx = NULL;
	char *dom = NULL, *usr = NULL;
	int error, err2;
	char tmp_arg[SMBIOC_MAX_NAME];
	char titlebuf[SMBFS_TITLEBUFSIZ];
	int file = -1;

	if ((share_unc == NULL) || (filename == NULL))
		return (EINVAL);

	error = smb_ctx_alloc(&ctx);
	if (error)
		goto out;

	if (username != NULL) {
		(void) strlcpy(tmp_arg, username, sizeof (tmp_arg));
		error = smb_ctx_parsedomuser(tmp_arg, &dom, &usr);
		if (error)
			goto out;

		if (dom) {
			error = smb_ctx_setdomain(ctx, dom, TRUE);
			if (error)
				goto out;
		}

		if (usr) {
			error = smb_ctx_setuser(ctx, usr, TRUE);
			if (error)
				goto out;
		}
	}

	error = smb_ctx_parseunc(ctx, share_unc,
	    SMBL_SHARE, SMBL_SHARE, USE_SPOOLDEV, &share_unc);
	if (error)
		goto out;

	if (strcmp(filename, "-") == 0) {
		file = 0;	/* stdin */
		filename = "stdin";
	} else {
		file = open(filename, O_RDONLY, 0);
		if (file < 0) {
			smb_error("could not open file %s\n", errno, filename);
			error = errno;
			goto out;
		}
	}

	error = smb_ctx_resolve(ctx);
	if (error)
		goto out;

again:
	error = smb_ctx_get_ssn(ctx);
	if (error == EAUTH) {
		err2 = smb_get_authentication(ctx);
		if (err2 == 0)
			goto again;
	}

	if (error) {
		smb_error(gettext("//%s: login failed"),
		    error, ctx->ct_fullserver);
		goto out;
	}

	error = smb_ctx_get_tree(ctx);
	if (error) {
		smb_error(gettext("//%s/%s: tree connect failed"),
		    error, ctx->ct_fullserver, ctx->ct_origshare);
		goto out;
	}

	(void) snprintf(titlebuf, sizeof (titlebuf), "%s %s",
	    ctx->ct_user, filename);

	error = smb_print_file(ctx, titlebuf, file);

out:
	/* don't close stdin (file=0) */
	if (file > 0)
		(void) close(file);

	smb_ctx_free(ctx);

	return (error);
}

/*
 * Documentation for OPEN_PRINT_FILE is scarce.
 * It's in a 1996 MS doc. entitled:
 * SMB FILE SHARING PROTOCOL
 *
 * The extra parameters are:
 *   SetupLength: what part of the file is printer setup
 *   Mode: text or graphics (raw data)
 *   IdentifierString:  job title
 */
enum {
	MODE_TEXT = 0,	/* TAB expansion, etc. */
	MODE_GRAPHICS	/* raw data */
};

static int
smb_print_file(smb_ctx_t *ctx, char *title, int file)
{
	char databuf[SMBFS_DATABUFSIZ];
	off_t offset;
	int rcnt, wcnt;
	int setup_len = 0;		/* No printer setup data */
	int mode = MODE_GRAPHICS;	/* treat as raw data */
	int error = 0;
	int pfd = -1;

	pfd = smb_open_printer(ctx, title, setup_len, mode);
	if (pfd < 0) {
		smb_error("could not open print job", errno);
		return (errno);
	}

	offset = 0;
	for (;;) {
		rcnt = read(file, databuf, sizeof (databuf));
		if (rcnt < 0) {
			error = errno;
			smb_error("error reading input file\n", error);
			break;
		}
		if (rcnt == 0)
			break;

		wcnt = smb_fh_write(pfd, databuf, rcnt, offset);
		if (wcnt < 0) {
			error = errno;
			smb_error("error writing spool file\n", error);
			break;
		}
		if (wcnt != rcnt) {
			smb_error("incomplete write to spool file\n", 0);
			error = EIO;
			break;
		}
		offset += wcnt;
	}

	(void) smb_fh_close(pfd);
	return (error);
}

static int
smb_open_printer(struct smb_ctx *ctx, const char *title,
	int setuplen, int mode)
{
	smbioc_printjob_t ioc;
	int err, tlen, new_fd;
	int32_t from_fd;

	tlen = strlen(title);
	if (tlen >= SMBIOC_MAX_NAME)
		return (EINVAL);

	/*
	 * Will represent this SMB-level open as a new
	 * open device handle.  Get one, then duplicate
	 * the driver session and tree bindings.
	 */
	new_fd = smb_open_driver();
	if (new_fd < 0)
		return (errno);
	from_fd = ctx->ct_dev_fd;
	if (ioctl(new_fd, SMBIOC_DUP_DEV, &from_fd) == -1) {
		err = errno;
		goto errout;
	}

	/*
	 * Do the SMB-level open with the new dev handle.
	 */
	bzero(&ioc, sizeof (ioc));
	ioc.ioc_setuplen = setuplen;
	ioc.ioc_prmode = mode;
	strlcpy(ioc.ioc_title, title, SMBIOC_MAX_NAME);

	if (ioctl(new_fd, SMBIOC_PRINTJOB, &ioc) == -1) {
		err = errno;
		goto errout;
	}

	return (new_fd);

errout:
	close(new_fd);
	errno = err;
	return (-1);
}

/* See smb_fh_close */
