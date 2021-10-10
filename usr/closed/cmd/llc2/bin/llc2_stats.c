/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * llc2stats.c
 *
 * Copyright (c) 1998 NCR Corporation, Dayton, Ohio USA
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#define	LLC2STATS_C

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stropts.h>
#include <unistd.h>
#include <libintl.h>
#include <locale.h>
#include <llc2.h>
#include <ild.h>
#include <ildlock.h>

/* ioctl() timeout value. */
#define	CTL_TIMEOUT	10

static void printLlc2Stats(llc2_ioctl_t *ctlPtr);
static void Usage(void);

/*
 *  main entry function for llc2stats
 *
 *  description:
 *	User space function to acquire station, sap, or connection
 *	statistics.  It allows the clearing, "resetting", of the statistical
 *	counters after reading them.
 *
 *  execution state:
 *
 *
 *  parameters:
 *	ppa			Physical Point of Attachment, a station.
 *	-r			Reset statistical counters to 0 after reading.
 *	-s sap			Service Access Point, 0 - 127 saps per station.
 *	-c connection		0 - 255 connections per sap.
 *
 *  returns:			nothing
 */

int
main(int argc, char **argv)
{
	uchar_t reset	= 0;
	uchar_t sap;
	int ch;
	char *dev	= "/dev/llc2";
	char *e_open	= gettext("LLC2: LLC2STATS: open of device '%s'"
			    " failed (%d)");
	char *e_close	= gettext("LLC2: LLC2STATS: close of device '%s'"
			    " failed (%d)");
	char *e_ioctl	= gettext("%s %d: ioctl error = %d, errno = %d (%s)");
	char eBuf[80]	= "";
	char *opts	= "vrs:c:";
	int conLimit	= 0;
	int error;
	int fd		= -1;
	int optLimit	= 0;
	int sapLimit	= 0;
	int ppaTmp;
	int ppa;
	ushort_t con;
	struct strioctl ctl;

	llc2GetStaStats_t staStats;
	llc2GetSapStats_t sapStats;
	llc2GetConStats_t conStats;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	/* There must be a ppa parameter. */
	if (argc <= 1) {
		(void) printf(gettext("\nPPA must be specified.\n"));
		Usage();
	}

	/* Parse command line. */
	while (((ch = getopt(argc, argv, opts)) != EOF) &&
	    (optLimit <= 1) && (conLimit <= 1)) {
		switch (ch) {
		case 'r':
			/* Only allow root to reset counters. */
			if (getuid() == 0)
				reset = 1;
			else
				Usage();
			break;
		case 's':
			++optLimit;
			++sapLimit;
			/* Sanity check to make sure the input is OK. */
			if (strlen(optarg) > 2 ||
			    strchr(optarg, 'x') != NULL) {
				(void) fprintf(stderr, gettext("\nSAP entered"
				    " must be 0 - 7f.\n"));
				Usage();
			}
			sap = (uchar_t)strtol(optarg, (char **)NULL, 16);
			if (sap > 0x7f) {
				(void) fprintf(stderr, gettext("\nSAP entered"
				    " must be 0 - 7f.\n"));
				Usage();
			}
			break;
		case 'c':
			++conLimit;
			/* Sanity check to make sure the input is OK. */
			if (strlen(optarg) > 2 ||
			    strchr(optarg, 'x') != NULL) {
				(void) fprintf(stderr, gettext("\nConnection"
				    " entered must be 0 - ff.\n"));
				Usage();
			}
			con = (ushort_t)strtol(optarg, (char **)NULL, 16);
			if (con > 0xff) {
				(void) fprintf(stderr, gettext("\nConnection"
				    " entered must be 0 - ff.\n"));
				Usage();
			}
			break;
		default:
			Usage();
			break;
		}
	}
	ppaTmp = atoi(argv[optind]);
	if ((ppaTmp < 0) || (ppaTmp > MAXPPA)) {
		(void) fprintf(stderr, gettext("Invalid PPA.\n"));
		Usage();
	} else {
		ppa = ppaTmp;
	}

	if ((optLimit > 1) || (conLimit > 1)) {
		Usage();
	}
	if ((conLimit > 0) && (sapLimit == 0)) {
		(void) fprintf(stderr, gettext("Must specify a connection.\n"));
		Usage();
	}
	if (sapLimit > 0) {
		if (conLimit > 0) {
			ctl.ic_cmd = ILD_LLC2;
			ctl.ic_timout = CTL_TIMEOUT;
			ctl.ic_len = sizeof (conStats);
			ctl.ic_dp = (char *)&conStats;
			conStats.ppa = ppa;
			conStats.cmd = LLC2_GET_CON_STATS;
			conStats.sap = sap;
			conStats.con = con;
			conStats.clearFlag = reset;
		} else {
			ctl.ic_cmd = ILD_LLC2;
			ctl.ic_timout = CTL_TIMEOUT;
			ctl.ic_len = sizeof (sapStats);
			ctl.ic_dp = (char *)&sapStats;
			sapStats.ppa = ppa;
			sapStats.cmd = LLC2_GET_SAP_STATS;
			sapStats.sap = sap;
			sapStats.clearFlag = reset;
		}
	}
	if (optLimit == 0) {
		ctl.ic_cmd = ILD_LLC2;
		ctl.ic_timout = CTL_TIMEOUT;
		ctl.ic_len = sizeof (staStats);
		ctl.ic_dp = (char *)&staStats;
		staStats.ppa = ppa;
		staStats.cmd = LLC2_GET_STA_STATS;
		staStats.clearFlag = reset;
	}

	/* Open the device */
	if ((fd = open(dev, O_RDWR)) < 0) {
		(void) sprintf(eBuf, e_open, dev, errno);
		perror(eBuf);
		Usage();
	}
	if ((error = ioctl(fd, I_STR, &ctl)) < 0) {
		(void) fprintf(stderr, e_ioctl, __FILE__, __LINE__, error,
		    errno, strerror(errno));
		Usage();
	}
	printLlc2Stats((llc2_ioctl_t *)ctl.ic_dp);

	if (fd >= 0) {
		if (close(fd) < 0) {
			(void) sprintf(eBuf, e_close, dev, errno);
			perror(eBuf);
		}
	}
	return (0);
}


static void
Usage(void)
{
	char *usage = gettext("\n"
	"usage: llc2_stats [-v] [-r] [-s sap] [-c connection] ppa\n"
	"    ppa      = Physical Point of Attachment, a station number.\n"
	"    -r       = Reset statistical counters to zero after reading\n"
	"               (Must be root to reset counters)              \n"
	"    -s sap   = SAP for a station, enter in hex & no leading 0x.\n"
	"    -c conn  = Connection for a SAP, enter in hex & no leading 0x.\n"
	"\n"
	"    Statistics may be acquired for a station, a SAP on a station,\n"
	"    or a connection on a SAP.\n");

	(void) fprintf(stderr, usage);
	exit(1);
}

/*
 *  print LLC2 statistics
 *
 *  description:
 *	User space function to print out the statistics for the station,
 *	sap, or connection.
 *
 *  execution state:
 *
 *
 *  parameters:
 *	ctlPtr			Pointer to the llc2_ioctl_t block where the
 *				returned statistics are stored.
 */
static void
printLlc2Stats(llc2_ioctl_t *ctlPtr)
{
	uchar_t i, len;
	int j;
	llc2GetStaStats_t *staPtr;
	llc2GetSapStats_t *sapPtr;
	llc2GetConStats_t *conPtr;

	/* switch on the returned structure's cmd field */
	switch (ctlPtr->cmd) {
	case LLC2_GET_STA_STATS:
		staPtr = (llc2GetStaStats_t *)ctlPtr;
		(void) printf("Station values received:\n");
		(void) printf("ppa		   = 0x%08x  ", staPtr->ppa);
		(void) printf("clearFlag	   = 0x%02x\n",
		    staPtr->clearFlag);
		(void) printf("# of saps (hex)	   = 0x%04x\n",
		    staPtr->numSaps);
		(void) printf("saps (hex)	   =");
		j = 21;
		for (i = 0; i < staPtr->numSaps; i++) {
			(void) printf(" %02x", staPtr->saps[i]);
			j = j + 3;
			if (j == 78) {
				(void) printf("\n                    ");
				j = 21;
			}
		}
		(void) printf("\n");
		(void) printf("state		   = 0x%02x\n", staPtr->state);
		(void) printf("nullSapXidCmdRcvd  = 0x%08x\n",
		    staPtr->nullSapXidCmdRcvd);
		(void) printf("nullSapXidRspSent  = 0x%08x\n",
		    staPtr->nullSapXidRspSent);
		(void) printf("nullSapTestCmdRcvd = 0x%08x\n",
		    staPtr->nullSapTestCmdRcvd);
		(void) printf("nullSapTestRspSent = 0x%08x\n",
		    staPtr->nullSapTestRspSent);
		(void) printf("outOfState	   = 0x%08x\n",
		    staPtr->outOfState);
		(void) printf("allocFail	   = 0x%08x\n",
		    staPtr->allocFail);
		(void) printf("protocolError	   = 0x%08x\n",
		    staPtr->protocolError);
		break;
	case LLC2_GET_SAP_STATS:
		sapPtr = (llc2GetSapStats_t *)ctlPtr;
		(void) printf("Sap values received:\n");
		(void) printf("ppa		   = 0x%08x  ", sapPtr->ppa);
		(void) printf("clearFlag	   = 0x%02x\n",
		    sapPtr->clearFlag);
		(void) printf("sap		   = 0x%02x\n", sapPtr->sap);
		(void) printf("state		   = 0x%02x\n", sapPtr->state);
		(void) printf("# of cons (hex)	   = 0x%08x\n",
		    sapPtr->numCons);
		(void) printf("connections (hex)  =");
		j = 21;
		for (i = 0; i < sapPtr->numCons; i++) {
			(void) printf(" %04x", sapPtr->cons[i]);
			j = j + 5;
			if (j == 76) {
				(void) printf("\n                    ");
				j = 21;
			}
		}
		(void) printf("\n");
		(void) printf("xidCmdSent	   = 0x%08x\n",
		    sapPtr->xidCmdSent);
		(void) printf("xidCmdRcvd	   = 0x%08x\n",
		    sapPtr->xidCmdRcvd);
		(void) printf("xidRspSent	   = 0x%08x\n",
		    sapPtr->xidRspSent);
		(void) printf("xidRspRcvd	   = 0x%08x\n",
		    sapPtr->xidRspRcvd);
		(void) printf("testCmdSent	   = 0x%08x\n",
		    sapPtr->testCmdSent);
		(void) printf("testCmdRcvd	   = 0x%08x\n",
		    sapPtr->testCmdRcvd);
		(void) printf("testRspSent	   = 0x%08x\n",
		    sapPtr->testRspSent);
		(void) printf("testRspRcvd	   = 0x%08x\n",
		    sapPtr->testRspRcvd);
		(void) printf("uiSent		   = 0x%08x\n",
		    sapPtr->uiSent);
		(void) printf("uiRcvd		   = 0x%08x\n",
		    sapPtr->uiRcvd);
		(void) printf("outOfState	   = 0x%08x\n",
		    sapPtr->outOfState);
		(void) printf("allocFail	   = 0x%08x\n",
		    sapPtr->allocFail);
		(void) printf("protocolError	   = 0x%08x\n",
		    sapPtr->protocolError);
		break;
	case LLC2_GET_CON_STATS:
		conPtr = (llc2GetConStats_t *)ctlPtr;
		(void) printf("Connection values received:\n");
		(void) printf("ppa          = 0x%08x  ", conPtr->ppa);
		(void) printf("clearFlag    = 0x%02x\n", conPtr->clearFlag);
		(void) printf("sap          = 0x%02x        ", conPtr->sap);
		(void) printf("con          = 0x%04x     ", conPtr->con);
		(void) printf("sid           = 0x%08x\n", conPtr->sid);
		(void) printf("stateOldest  = 0x%02x        ",
		    conPtr->stateOldest);
		(void) printf("stateOlder   = 0x%02x       ",
		    conPtr->stateOlder);
		(void) printf("stateOld      = 0x%02x\n", conPtr->stateOld);
		(void) printf("state        = 0x%02x\n", conPtr->state);
		(void) printf("dl_nodeaddr  = 0x");
		for (i = 0; i < 6; i++) {
			(void) printf("%02x", conPtr->rem.llc.dl_nodeaddr[i]);
		}
		(void) printf("  dl_sap   = 0x%02x", conPtr->rem.llc.dl_sap);
		(void) printf("\n");
		(void) printf("flag         = 0x%02x        ", conPtr->flag);
		(void) printf("dataFlag     = 0x%02x       ", conPtr->dataFlag);
		(void) printf("timerOn       = 0x%02x\n", conPtr->timerOn);
		(void) printf("vs           = 0x%02x        ", conPtr->vs);
		(void) printf("vr = 0x%02x        ", conPtr->vr);
		(void) printf("nrRcvd = 0x%02x        ", conPtr->nrRcvd);
		(void) printf("k = 0x%02x\n", conPtr->k);
		(void) printf("retryCount   = 0x%04x      ",
		    conPtr->retryCount);
		(void) printf("numToBeAcked = 0x%08x ", conPtr->numToBeAcked);
		(void) printf("numToResend   = 0x%08x\n", conPtr->numToResend);
		(void) printf("macOutSave   = 0x%08x  ", conPtr->macOutSave);
		(void) printf("macOutDump   = 0x%08x\n", conPtr->macOutDump);
		(void) printf("iSent        = 0x%08x  ", conPtr->iSent);
		(void) printf("iRcvd        = 0x%08x\n", conPtr->iRcvd);
		(void) printf("frmrSent     = 0x%08x  ", conPtr->frmrSent);
		(void) printf("frmrRcvd     = 0x%08x\n", conPtr->frmrRcvd);
		(void) printf("rrSent       = 0x%08x  ", conPtr->rrSent);
		(void) printf("rrRcvd       = 0x%08x\n", conPtr->rrRcvd);
		(void) printf("rnrSent      = 0x%08x  ", conPtr->rnrSent);
		(void) printf("rnrRcvd      = 0x%08x\n", conPtr->rnrRcvd);
		(void) printf("rejSent      = 0x%08x  ", conPtr->rejSent);
		(void) printf("rejRcvd      = 0x%08x\n", conPtr->rejRcvd);
		(void) printf("sabmeSent    = 0x%08x  ", conPtr->sabmeSent);
		(void) printf("sabmeRcvd    = 0x%08x\n", conPtr->sabmeRcvd);
		(void) printf("uaSent       = 0x%08x  ", conPtr->uaSent);
		(void) printf("uaRcvd       = 0x%08x ", conPtr->uaRcvd);
		(void) printf("discSent      = 0x%08x\n", conPtr->discSent);
		(void) printf("outOfState   = 0x%08x  ", conPtr->outOfState);
		(void) printf("allocFail    = 0x%08x ", conPtr->allocFail);
		(void) printf("protocolError = 0x%08x\n",
		    conPtr->protocolError);
		(void) printf("localBusy    = 0x%08x  ", conPtr->localBusy);
		(void) printf("remoteBusy   = 0x%08x ", conPtr->remoteBusy);
		(void) printf("maxRetryFail  = 0x%08x\n", conPtr->maxRetryFail);
		(void) printf("ackTimerExp  = 0x%08x  ", conPtr->ackTimerExp);
		(void) printf("pollTimerExp = 0x%08x ", conPtr->pollTimerExp);
		(void) printf("rejTimerExp   = 0x%08x\n", conPtr->rejTimerExp);
		(void) printf("remBusyTimerExp  = 0x%08x\n",
		    conPtr->remBusyTimerExp);
		(void) printf("inactTimerExp    = 0x%08x\n",
		    conPtr->inactTimerExp);
		(void) printf("sendAckTimerExp  = 0x%08x\n",
		    conPtr->sendAckTimerExp);
		break;
	default:
		break;
	}
}
