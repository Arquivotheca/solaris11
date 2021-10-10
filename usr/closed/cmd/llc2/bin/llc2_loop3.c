/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * llc2_loop3.c
 *
 * Copyright (c) 1992-1998 NCR Corporation, Dayton, OH USA
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/stream.h>
#include <libintl.h>
#include <stropts.h>
#include <locale.h>
#include <sys/dlpi.h>
#include <sys/llc2.h>

static char *opts = "v";
static char *dev = "/dev/llc2";
static int verbose = 0;

static int fd;
static struct strbuf ctlmsg;
static struct strbuf datmsg;

static unsigned char xmitbuf[1492];

static struct {
	union DL_primitives dl;
	dlsap_t dsap;
} dlp;

static dlsap_t localdlsap;

static void retrieve_response(t_uscalar_t prim, t_uscalar_t ack_prim);

int
main(int argc, char *argv[])
{
	char ch;
	int ppa, sap, frames;
	int i;
	extern int optind;
	char *usage = gettext("Usage: %s [-v] ppa sap|type frames\n");

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
	if (optind != (argc - 3)) {
		(void) fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	ppa = atol(argv[optind]);
	sap = atol(argv[optind + 1]);
	frames = atol(argv[optind + 2]);

	if ((fd = open(dev, O_RDWR)) < 0) {
		perror(dev);
		exit(1);
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

	/* bind the specified sap */
	dlp.dl.bind_req.dl_primitive = DL_BIND_REQ;
	dlp.dl.bind_req.dl_sap = sap;
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

	/* transmit specified number of ui frames */
	for (i = 0; i < frames; i++) {
		dlp.dl.unitdata_req.dl_primitive = DL_UNITDATA_REQ;
		dlp.dl.unitdata_req.dl_dest_addr_length = sizeof (dlsap_t);
		dlp.dl.unitdata_req.dl_dest_addr_offset =
			(t_uscalar_t)((t_uscalar_t)&dlp.dsap -
			    (t_uscalar_t)&dlp);

		if (sap >= 0x100) {
			dlp.dsap.ether.dl_type = sap;
			(void) memset(dlp.dsap.ether.dl_nodeaddr, 0, 6);
		} else {
			/* Is this a bug??  localdlsap is not initialized... */
			(void) memcpy(&dlp.dsap, &localdlsap,
			    sizeof (dlsap_t));
			dlp.dsap.llc.dl_sap = sap;
			(void) memset(dlp.dsap.llc.dl_nodeaddr, 0, 6);
		}

		dlp.dl.unitdata_req.dl_priority.dl_min = 0;
		dlp.dl.unitdata_req.dl_priority.dl_max = 0;

		ctlmsg.maxlen = sizeof (dlp);
		ctlmsg.len = sizeof (dlp);
		ctlmsg.buf = (char *)&dlp;

		datmsg.maxlen = sizeof (xmitbuf);
		datmsg.len = sizeof (xmitbuf);
		datmsg.buf = (char *)xmitbuf;

		if (putmsg(fd, &ctlmsg, &datmsg, 0) < 0) {
			perror(dev);
			exit(1);
		}
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
	return (0);
}

static char databuf[1000];

static void
retrieve_response(t_uscalar_t prim, t_uscalar_t ack_prim)
{
	int flag;

	ctlmsg.maxlen = sizeof (dlp);
	ctlmsg.len = sizeof (dlp);
	ctlmsg.buf = (char *)&dlp;

	datmsg.maxlen = sizeof (databuf);
	datmsg.len = 0;
	datmsg.buf = databuf;

	(void) memset(&dlp, 0, sizeof (dlp));

	flag = 0;
	if (getmsg(fd, &ctlmsg, &datmsg, &flag) < 0) {
		perror(dev);
		exit(1);
	}

	if (dlp.dl.dl_primitive == DL_ERROR_ACK) {
		(void) fprintf(stderr, gettext("LLC2: DIAGS: LOOP3:"
		    " ERROR_ACK error ="
		    " %x.\n"), dlp.dl.error_ack.dl_errno);
		exit(1);
	}
	if (dlp.dl.dl_primitive != prim) {
		(void) fprintf(stderr, gettext("LLC2: DIAGS: LOOP3:"
		    " unexpected DLPI"
		    " primitive %.4x, expected %.4x\n"),
		    dlp.dl.dl_primitive, prim);
		exit(1);
	}

	switch (dlp.dl.dl_primitive) {
	case DL_OK_ACK:
		if (dlp.dl.ok_ack.dl_correct_primitive != ack_prim) {
			(void) fprintf(stderr, gettext("LLC2: DIAG: LOOP3:"
			    " Invalid"
			    " primitive acknowledged %.4x, expected %.4x\n"),
			    dlp.dl.ok_ack.dl_correct_primitive, ack_prim);
			exit(1);
		}
		break;

	case DL_ERROR_ACK:
		(void) fprintf(stderr, gettext("LLC2: DIAG: LOOP3: Error_Ack,"
		    " primitive %.4x, errno %4d, unit_errno %d\n"),
		    dlp.dl.error_ack.dl_error_primitive,
		    dlp.dl.error_ack.dl_errno,
		    dlp.dl.error_ack.dl_unix_errno);
		exit(1);
		break;

	case DL_BIND_ACK:
		break;
	}
}
