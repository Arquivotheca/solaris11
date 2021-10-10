/*
 * Copyright (c) 1998-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * tcap.c
 *
 * Copyright (c) 1998 NCR Corporation, Dayton, Ohio USA
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/stream.h>
#include <stropts.h>
#include <utime.h>
#include <time.h>
#include <sys/times.h>
#include <limits.h>
#include <sys/dlpi.h>
#include <libintl.h>
#include <locale.h>
#include <llc2.h>
#include <ild.h>

/*
 * ILD trace information capture utility
 *
 * this utility gathers trace information from the ILD and stores the raw
 * information in a file selected by the user, the first ulong word of the
 * file contains the offset to the first (possibly wrapped) trace record,
 * if trace information is lost then 0s are written to the output file to
 * signify the lost data
 */
#define	ONE_K	1024
#define	ONE_M	1024*1024

static char dev[] = "/dev/llc2";
static char opts[] = "hs:o:";

static int fd;
static int ofd;
static char *self;
static int active = 1;
static int pending = 0;
static int wrapped = 0;
static uint_t size = 10 * ONE_K;
static int usage_opt = 0;
static int error = 0;
static uint_t lclSeqNum = 0;

static struct tcaphdr {
	off_t	offset;
	time_t	starttime;
	clock_t	lbolttime;
} thdr;

static off_t fOffset = sizeof (struct tcaphdr);

static uchar_t zeroBuf[ONE_K];
static uchar_t capBuf[4096];
static uchar_t cmBuf[128];

extern char *optarg;
extern int optind;

static void sigCatch(int a);

static void
usage(void)
{
	(void) fprintf(stderr,
	    gettext("Usage: %s [-h] [-s size] -o outfile\n"), self);
	(void) fprintf(stderr,
	    gettext("\t-h prints this message\n"));
	(void) fprintf(stderr,
	    gettext("\t-s <size> where <size> is the maximum file size"
	    " allowed for\n"));
	(void) fprintf(stderr,
	    gettext("\t\t  capture in 1024 byte units.\n"));
	(void) fprintf(stderr,
	    gettext("\t\t  ex. -s 1024 => 1 Mbyte trace file (65535 trace"
	    " entries)\n"));
	(void) fprintf(stderr,
	    gettext("\t-o <outfile> output file path/name\n"));
}

int
main(int argc, char **argv)
{
	int flag;
	char *ofile = NULL;
	char ch;
	struct strioctl ic;
	struct strbuf ctlmsg;
	struct strbuf datmsg;
	struct stroptions opt;
	uint_t tsize = 10;
	struct utimbuf tracetime;
	struct tms tmsbuf;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	self = argv[0];

	thdr.offset = sizeof (struct tcaphdr);

	if (argc == 1) {
		usage();
		exit(1);
	}

	/*
	 * parse command line
	 */
	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 's':
			tsize = atoi(optarg);
			break;
		case 'o':
			ofile = optarg;
			break;
		case 'h':
			usage_opt = 1;
			break;
		case '?':
		default:
			error = 1;
			break;
		}
	}

	if ((usage_opt) || (error)) {
		usage();
		exit((usage_opt) ? 0 : 1);
	}

	/*
	 * set up the output file pointer
	 */
	if (ofile != (char *)NULL) {
		if ((ofd = open(ofile, O_RDWR|O_CREAT|O_TRUNC, 0664)) < 0) {
			(void) fprintf(stderr, gettext("%s: unable to open"
			    " output file %s\n"), self, ofile);
			perror(ofile);
			exit(1);
		}
	} else {
		(void) fprintf(stderr, gettext("%s: '-o outfile' must be"
		    " specified\n"), self);
		exit(1);
	}

	if ((tsize != 0) && (tsize < (2*ONE_M))) {
		size = tsize * ONE_K;
	} else {
		(void) fprintf(stderr, gettext("%s: '-s size' specified"
		    " (%u) must be 0 < 'size' < %u\n"), self, tsize, 2*ONE_M);
		exit(1);
	}

	(void) signal(SIGTERM, sigCatch);
	(void) signal(SIGINT,  sigCatch);
	(void) signal(SIGQUIT, sigCatch);
	(void) signal(SIGHUP, SIG_IGN);

	/*
	 * open the device
	 */
	if ((fd = open(dev, O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("%s: unable to open '%s'\n"),
		    self, dev);
		perror(dev);
		exit(1);
	}

	/*
	 * Put the M_SETOPTS stuff here
	 */
	ic.ic_cmd = M_SETOPTS;
	ic.ic_timout = 10;
	ic.ic_len = sizeof (struct stroptions);
	ic.ic_dp = (char *)&opt;
	opt.so_flags = SO_HIWAT|SO_LOWAT;
	opt.so_hiwat = 32768;
	opt.so_lowat = 8192;
	if (ioctl(fd, I_STR, &ic) < 0) {
		perror(dev);
		exit(1);
	}

	thdr.starttime = time(0);
	thdr.lbolttime = times(&tmsbuf);

	/*
	 * activate trace capture
	 */
	ic.ic_cmd = ILD_TCAPSTART;
	ic.ic_timout = 10;
	ic.ic_len = 0;
	ic.ic_dp = NULL;

	/*
	 * write the file offset header and creation time  header
	 */
	if (write(ofd, &thdr, sizeof (struct tcaphdr)) < 0) {
		perror(ofile);
		exit(1);
	}

	if (ioctl(fd, I_STR, &ic) < 0) {
		perror(dev);
		exit(1);
	}

	while (active) {
		tCapBuf_t *tcap;

		ctlmsg.maxlen = sizeof (cmBuf);
		ctlmsg.len = 0;
		ctlmsg.buf = (char *)cmBuf;

		datmsg.maxlen = sizeof (capBuf);
		datmsg.len = 0;
		datmsg.buf = (char *)capBuf;

		flag = 0;
		if (getmsg(fd, &ctlmsg, &datmsg, &flag) < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror(dev);
			exit(1);
		}
		if (ctlmsg.len > 0) {
			(void) fprintf(stderr, gettext("%s: unexpected control"
			    " message\n"), self);
			exit(1);
		}
		if ((datmsg.len < 8) || ((datmsg.len < sizeof (tCapBuf_t)) &&
		    !pending)) {
			continue;
		}
		datmsg.len -= 8;
		if ((datmsg.len & 0x000F) != 0) {
			(void) fprintf(stderr, gettext("%s: unexpected data"
			    " message size: %d\n"), self, datmsg.len);
			exit(1);
		}
		tcap = (tCapBuf_t *)datmsg.buf;
		while (lclSeqNum < tcap->seqNum) {
			if (write(ofd, zeroBuf, ONE_K) < 0) {
				perror(ofile);
				exit(1);
			}
			lclSeqNum += 1;
			fOffset += ONE_K;
			if (fOffset > size) {
				fOffset = sizeof (struct tcaphdr);
				if (lseek(ofd, fOffset, 0) < 0) {
					perror(ofile);
					exit(1);
				}
				wrapped = 1;
			}
		}
		if (write(ofd, tcap->t, datmsg.len) < 0) {
			perror(ofile);
			exit(1);
		}
		lclSeqNum += 1;
		fOffset += datmsg.len;

		/* check for file getting too big and rewind it back to 4 */
		if (pending && tcap->lastFlag) {
			if (wrapped) {
				if (lseek(ofd, 0, 0) < 0) {
					perror(ofile);
					exit(1);
				}
				thdr.offset = fOffset;
				if (write(ofd, &thdr,
				    sizeof (struct tcaphdr)) < 0) {
					perror(ofile);
					exit(1);
				}
			}
			if (close(ofd) < 0) {
				perror(ofile);
				exit(1);
			}
			/*
			 * change access and modification times to match when
			 * the trace started. This will let the decoder attach
			 * real times to the display output.
			 */
			tracetime.actime = tracetime.modtime = thdr.starttime;
			(void) utime(ofile, &tracetime);

			active = 0;
		} else if (fOffset > size) {
			fOffset = sizeof (struct tcaphdr);
			if (lseek(ofd, fOffset, 0) < 0) {
				perror(ofile);
				exit(1);
			}
			wrapped = 1;
		}
	}
	return (0);
}

/* ARGSUSED */
static void
sigCatch(int a)
{
	struct strioctl ic;

	/*
	 * de-activate trace capture
	 */
	ic.ic_cmd = ILD_TCAPSTOP;
	ic.ic_timout = 10;
	ic.ic_len = 0;
	ic.ic_dp = NULL;

	if (ioctl(fd, I_STR, &ic) < 0) {
		perror(dev);
		exit(1);
	}
	pending = 1;
}
