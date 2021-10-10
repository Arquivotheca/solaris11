/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * llc2_loop4.c
 *
 * Copyright (c) 1992-1998 NCR Corporation, Dayton, Ohio USA
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/stream.h>
#include <stropts.h>
#include <libintl.h>
#include <locale.h>
#include <sys/dlpi.h>
#include <sys/llc2.h>

static char *opts = "v";
static char *dev = "/dev/llc2";
static int verbose = 0;

static int fd;
static struct strbuf ctlmsg;
static struct strbuf datmsg;

static unsigned char xmitbuf[256];

static struct {
	union DL_primitives dl;
	dlsap_t dsap;
} dlp;

static int going = 1;
static int responders = 0;

static void breakout(int signum);
static void retrieve_response(t_uscalar_t prim, t_uscalar_t ack_prim);

int
main(int argc, char *argv[])
{
	char ch;
	int ppa;
	int i;
	extern int optind;
	dl_test_req_t *dl = (dl_test_req_t *)&dlp.dl;
	dlsap_t *dsap;
	char *usage = gettext("Usage: %s [-v] ppa\n");

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	/* parse command line */
	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'v':
			verbose++;
			break;
		default:
			(void) fprintf(stderr, usage, argv[0]);
			exit(1);
		}
	}
	if (optind != (argc-1)) {
		(void) fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	ppa = atoi(argv[optind]);

	if ((fd = open(dev, O_RDWR)) < 0) {
		perror(dev);
		exit(1);
	}

	if (verbose) {
		(void) printf(gettext("    - Attaching\n"));
	}

	/* issue ATTACH command for specified ppa */
	dlp.dl.attach_req.dl_primitive = DL_ATTACH_REQ;
	dlp.dl.attach_req.dl_ppa = ppa;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = 0;
	datmsg.len = 0;
	datmsg.buf = 0;

	if (putmsg(fd, &ctlmsg, &datmsg, 0) < 0) {
		perror(dev);
		exit(1);
	}

	/* get response (expect DL_OK_ACK) */
	retrieve_response(DL_OK_ACK, DL_ATTACH_REQ);

	if (verbose) {
		(void) printf(gettext("    - Binding\n"));
	}

	/* bind the specified sap */
	dlp.dl.bind_req.dl_primitive = DL_BIND_REQ;
	dlp.dl.bind_req.dl_sap = 0x04;
	dlp.dl.bind_req.dl_max_conind = 0x0;
	dlp.dl.bind_req.dl_service_mode = DL_CLDLS;
	dlp.dl.bind_req.dl_conn_mgmt = 0;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = 0;
	datmsg.len = 0;
	datmsg.buf = 0;

	if (putmsg(fd, &ctlmsg, &datmsg, 0) < 0) {
		perror(dev);
		exit(1);
	}

	/* get response (expect DL_BIND_ACK) */
	retrieve_response(DL_BIND_ACK, 0);

	for (i = 0; i < sizeof (xmitbuf); i++) {
		xmitbuf[i] = i;
	}

	if (verbose) {
		(void) printf(gettext("    - Sending TEST\n"));
	}

	dl->dl_primitive = DL_TEST_REQ;
	dl->dl_flag = 0;
	dl->dl_dest_addr_length = sizeof (dlsap_t);
	dl->dl_dest_addr_offset =
		(t_uscalar_t)(((t_uscalar_t)&dlp.dsap) - ((t_uscalar_t)&dlp));

	dsap = (dlsap_t *)&dlp.dsap;
	dsap->llc.dl_sap = 0x00;
	dsap->llc.dl_nodeaddr[0] = 0xff;
	dsap->llc.dl_nodeaddr[1] = 0xff;
	dsap->llc.dl_nodeaddr[2] = 0xff;
	dsap->llc.dl_nodeaddr[3] = 0xff;
	dsap->llc.dl_nodeaddr[4] = 0xff;
	dsap->llc.dl_nodeaddr[5] = 0xff;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = 0;
	datmsg.len = 0;
	datmsg.buf = 0;

	if (putmsg(fd, &ctlmsg, &datmsg, 0) < 0) {
		perror(dev);
		exit(1);
	}

	if (verbose) {
		(void) printf(gettext("    - Responders:\n"));
	}

	(void) signal(SIGALRM, breakout);
	(void) alarm(2);
	while (going) {
		retrieve_response(DL_TEST_CON, 0);
	}

	if (verbose) {
		(void) printf(gettext("    - Unbinding\n"));
	}

	/* unbind the sap */
	dlp.dl.unbind_req.dl_primitive = DL_UNBIND_REQ;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = 0;
	datmsg.len = 0;
	datmsg.buf = 0;

	if (putmsg(fd, &ctlmsg, &datmsg, 0) < 0) {
		perror(dev);
		exit(1);
	}

	/* get response (expect DL_OK_ACK for DL_UNBIND_REQ) */
	retrieve_response(DL_OK_ACK, DL_UNBIND_REQ);

	if (verbose) {
		(void) printf(gettext("    - detaching\n"));
	}

	/* detach the queue */
	dlp.dl.detach_req.dl_primitive = DL_DETACH_REQ;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = 0;
	datmsg.len = 0;
	datmsg.buf = 0;

	if (putmsg(fd, &ctlmsg, &datmsg, 0) < 0) {
		perror(dev);
		exit(1);
	}

	/* get response (expect DL_OK_ACK for DL_UNBIND_REQ) */
	retrieve_response(DL_OK_ACK, DL_DETACH_REQ);

	(void) close(fd);

	if (!responders) {
		(void) fprintf(stderr, gettext("LLC2: DIAGS: LOOP4: No nodes"
		    " responding\n"));
		exit(1);
	}
	if (verbose) {
		(void) printf(gettext("%d Nodes responding.\n"), responders);
	}
	return (0);
}

static char databuf[1000];

static void
retrieve_response(t_uscalar_t prim, t_uscalar_t ack_prim)
{
	int flag;
	dl_test_con_t *test_ind = (dl_test_con_t *)&dlp.dl;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = sizeof (databuf);
	datmsg.len = 0;
	datmsg.buf = databuf;

	(void) memset(&dlp, 0, sizeof (dlp));

	flag = 0;
	if (getmsg(fd, &ctlmsg, &datmsg, &flag) < 0) {
		if (errno == EINTR)
			return;
		perror(dev);
		exit(1);
	}

	if (dlp.dl.dl_primitive != prim) {
		(void) fprintf(stderr, gettext("LLC2: DIAGS: LOOP4:"
		    " unexpected DLPI"
		    " primitive %.4x, expected %.4x\n"),
		    dlp.dl.dl_primitive, prim);
		exit(1);
	}

	switch (dlp.dl.dl_primitive) {
	case DL_OK_ACK:
		if (dlp.dl.ok_ack.dl_correct_primitive != ack_prim) {
			(void) fprintf(stderr, gettext("LLC2: DIAG: LOOP4:"
			    " Invalid"
			    " primitive acknowledged %.4x, expected %.4x\n"),
			    dlp.dl.ok_ack.dl_correct_primitive, ack_prim);
			exit(1);
		}
		break;

	case DL_ERROR_ACK:
		(void) fprintf(stderr, gettext("LLC2: DIAG: LOOP4:"
		    " Error_Ack, primitive"
		    " %.4x, errno %4d, unit_errno %d\n"),
		    dlp.dl.error_ack.dl_error_primitive,
		    dlp.dl.error_ack.dl_errno,
		    dlp.dl.error_ack.dl_unix_errno);
		exit(1);
		break;

	case DL_BIND_ACK:
		break;

	case DL_TEST_IND:
	case DL_TEST_CON: {
		int i;
		uchar_t *cp = (uchar_t *)((t_uscalar_t)test_ind) +
			test_ind->dl_src_addr_offset;
		responders++;
		if (verbose) {
			(void) printf("       %d-", responders);
			for (i = 0; i < 6; i++)
				(void) printf("%.2x", *cp++);
			(void) printf("\n");
		}
	}
	break;
	}
}

/* ARGSUSED */
void
breakout(int signum)
{
	going = 0;
}
