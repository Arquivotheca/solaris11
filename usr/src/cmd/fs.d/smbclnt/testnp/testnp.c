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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Test program for the smbfs named pipe API.
 */

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>

#include <netsmb/smbfs_api.h>
#include <netsmb/smb_keychain.h>

/*
 * This is a quick hack for testing client-side named pipes.
 * Its purpose is to test the ability to connect to a server,
 * open a pipe, send and recv. data.  The "hack" aspect is
 * the use of hand-crafted RPC messages, which allows testing
 * of the named pipe API separately from the RPC libraries.
 *
 * I captured the two small name pipe messages sent when
 * requesting a share list via RPC over /pipe/srvsvc and
 * dropped them into the arrays below (bind and enum).
 * This program sends the two messages (with adjustments)
 * and just dumps whatever comes back over the pipe.
 * Use wireshark if you want to see decoded messages.
 */

/* This is a DCE/RPC bind call for "srvsvc". */
const uchar_t srvsvc_bind[] = {
	0x05, 0x00, 0x0b, 0x03, 0x10, 0x00, 0x00, 0x00,
	0x48, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
	0xc8, 0x4f, 0x32, 0x4b, 0x70, 0x16, 0xd3, 0x01,
	0x12, 0x78, 0x5a, 0x47, 0xbf, 0x6e, 0xe1, 0x88,
	0x03, 0x00, 0x00, 0x00, 0x04, 0x5d, 0x88, 0x8a,
	0xeb, 0x1c, 0xc9, 0x11, 0x9f, 0xe8, 0x08, 0x00,
	0x2b, 0x10, 0x48, 0x60, 0x02, 0x00, 0x00, 0x00 };

/* This is a srvsvc "enum servers" call, in two parts */
const uchar_t srvsvc_enum1[] = {
	0x05, 0x00, 0x00, 0x03, 0x10, 0x00, 0x00, 0x00,
#define	ENUM_RPCLEN_OFF	8
	/* V - RPC frag length */
	0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0f, 0x00,
#define	ENUM_SLEN1_OFF	28
#define	ENUM_SLEN2_OFF	36
	/* server name, length 14 vv ... */
	0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00 };
	/* UNC server here, i.e.: "\\192.168.1.6" */

const uchar_t srvsvc_enum2[] = {
	0x01, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 };

uchar_t sendbuf[1024];
uchar_t recvbuf[4096];

char *unc_server;

int pipetest(void);

extern char *optarg;
extern int optind, opterr, optopt;

void
testnp_usage(void)
{
	(void) printf(
	    "usage: testnp [-d domain][-u user][-p passwd] //server\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int c, error;
	char *dom = NULL;
	char *usr = NULL;
	char *pw = NULL;

	while ((c = getopt(argc, argv, "d:u:p:")) != -1) {
		switch (c) {
		case 'd':
			dom = optarg;
			break;
		case 'u':
			usr = optarg;
			break;
		case 'p':
			pw = optarg;
			break;
		case '?':
			testnp_usage();
			break;
		}
	}
	if (optind >= argc)
		testnp_usage();
	unc_server = argv[optind];

	if (pw != NULL && (dom == NULL || usr == NULL)) {
		(void) fprintf(stderr, "%s: -p arg requires -d dom -u usr\n",
		    argv[0]);
		testnp_usage();
	}
	if (dom)
		smbfs_set_default_domain(dom);
	if (usr)
		smbfs_set_default_user(usr);
	if (pw)
		(void) smbfs_keychain_add((uid_t)-1, dom, usr, pw);

	/*
	 * Do some named pipe I/O.
	 */
	error = pipetest();
	if (error) {
		smb_error("pipetest failed", error);
		return (1);
	}

	return (0);
}

void
hexdump(const uchar_t *buf, int len) {
	int ofs = 0;

	while (len--) {
		if (ofs % 16 == 0)
			(void) printf("\n%02X: ", ofs);
		(void) printf("%02x ", *buf++);
		ofs++;
	}
	(void) printf("\n");
}

/*
 * Put a unicode UNC server name, including the null.
 * Quick-n-dirty, just for this test...
 */
static int
put_uncserver(const char *s, uchar_t *buf)
{
	uchar_t *p = buf;
	char c;

	do {
		c = *s++;
		if (c == '/')
			c = '\\';
		*p++ = c;
		*p++ = '\0';

	} while (c != 0);

	return (p - buf);
}

/*
 * Send the bind and read the ack.
 * This tests smb_fh_xactnp.
 */
static int
do_bind(int fid)
{
	int err, len, more;

	more = 0;
	len = sizeof (recvbuf);

	err = smb_fh_xactnp(fid,
	    sizeof (srvsvc_bind), (char *)srvsvc_bind,
	    &len, (char *)recvbuf, &more);
	if (err) {
		(void) printf("xact bind, err=%d\n", err);
		return (err);
	}

	(void) printf("bind ack, len=%d\n", len);
	hexdump(recvbuf, len);

	if (more > 0) {
		if (more > sizeof (recvbuf)) {
			(void) printf("bogus more=%d\n", more);
			more = sizeof (recvbuf);
		}

		len = smb_fh_read(fid, recvbuf, more, 0);
		if (len == -1) {
			err = EIO;
			(void) printf("read enum resp, err=%d\n", err);
			return (err);
		}

		(void) printf("bind ack (more), len=%d\n", len);
		hexdump(recvbuf, len);
	}

	return (0);
}

static int
do_enum(int fid)
{
	int err, len, rlen, wlen;
	uchar_t *p;

	/*
	 * Build the enum request - three parts.
	 * See above: srvsvc_enum1, srvsvc_enum2
	 *
	 * First part: RPC header, etc.
	 */
	p = sendbuf;
	len = sizeof (srvsvc_enum1); /* 40 */
	(void) memcpy(p, srvsvc_enum1, len);
	p += len;

	/* Second part: UNC server name */
	len = put_uncserver(unc_server, p);
	p += len;
	sendbuf[ENUM_SLEN1_OFF] = len / 2;
	sendbuf[ENUM_SLEN2_OFF] = len / 2;

	/* Third part: level, etc. (align4) */
	for (len = (p - sendbuf) & 3; len; len--)
		*p++ = '\0';
	len = sizeof (srvsvc_enum2); /* 28 */
	(void) memcpy(p, srvsvc_enum2, len);
	p += len;

	/*
	 * Compute total length, and fixup RPC header.
	 */
	len = p - sendbuf;
	sendbuf[ENUM_RPCLEN_OFF] = len;

	/*
	 * Send the enum request, read the response.
	 * This tests smb_fh_write, smb_fh_read.
	 */
	wlen = smb_fh_write(fid, sendbuf, len, 0);
	if (wlen == -1) {
		err = errno;
		(void) printf("write enum req, err=%d\n", err);
		return (err);
	}
	if (wlen != len) {
		(void) printf("write enum req, short write %d\n", wlen);
		return (EIO);
	}

	rlen = smb_fh_read(fid, recvbuf, sizeof (recvbuf), 0);
	if (rlen == -1) {
		err = errno;
		(void) printf("read enum resp, err=%d\n", err);
		return (err);
	}
	(void) printf("enum recv, len=%d\n", rlen);
	hexdump(recvbuf, rlen);

	return (0);
}

int
pipetest(void)
{
	static char pipe[256];
	static uchar_t key[16];
	int err, fd;

	(void) snprintf(pipe, 256, "%s/PIPE/srvsvc", unc_server);
	(void) printf("open pipe: %s\n", pipe);

	fd = smb_fh_open(pipe, O_RDWR);
	if (fd < 0) {
		perror(pipe);
		return (errno);
	}

	/* Test this too. */
	err = smb_fh_getssnkey(fd, key, sizeof (key));
	if (err) {
		(void) printf("getssnkey: %d\n", err);
		goto out;
	}

	err = do_bind(fd);
	if (err) {
		(void) printf("do_bind: %d\n", err);
		goto out;
	}
	err = do_enum(fd);
	if (err)
		(void) printf("do_enum: %d\n", err);

out:
	(void) smb_fh_close(fd);
	return (err);
}
