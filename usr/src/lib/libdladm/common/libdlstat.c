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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kstat.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sys/dld.h>
#include <sys/ddi.h>
#include <zone.h>

#include <libdllink.h>
#include <libdlflow.h>
#include <libdlstat.h>
#include <libdlaggr.h>
#include <libinetutil.h>

/*
 * x86 <sys/regs> ERR conflicts with <curses.h> ERR.
 * Include curses.h last.
 */
#if	defined(ERR)
#undef  ERR
#endif
#include <curses.h>

#define	DLSTAT_MAC_RX_HWLANE	"mac_rx_hwlane"
#define	DLSTAT_MAC_TX_HWLANE	"mac_tx_hwlane"
#define	DLSTAT_MAC_LINK_STAT	"link"
#define	DLSTAT_MAC_PHYS_STAT	"phys"
#define	DLSTAT_MAC_RX_RING	"mac_rx_ring"
#define	DLSTAT_MAC_TX_RING	"mac_tx_ring"

struct flowlist {
	char		flowname[MAXFLOWNAMELEN];
	char		linkname[MAXLINKNAMELEN];
	datalink_id_t	linkid;
	int		fd;
	uint64_t	ifspeed;
	boolean_t	first;
	boolean_t	display;
	pktsum_t 	prevstats;
	pktsum_t	diffstats;
};

static	int	maxx, maxy, redraw = 0;
static	volatile uint_t handle_resize = 0, handle_break = 0;

pktsum_t		totalstats;
struct flowlist		*stattable = NULL;
static int		statentry = -1, maxstatentries = 0;

#define	STATGROWSIZE	16

/*
 * Search for flowlist entry in stattable which matches
 * the flowname and linkid.  If no match is found, use
 * next available slot.  If no slots are available,
 * reallocate table with  more slots.
 *
 * Return: *flowlist of matching flow
 *         NULL if realloc fails
 */

static struct flowlist *
findstat(const char *flowname, datalink_id_t linkid)
{
	int match = 0;
	struct flowlist *flist;

	/* Look for match in the stattable */
	for (match = 0, flist = stattable;
	    match <= statentry;
	    match++, flist++) {

		if (flist == NULL)
			break;
		/* match the flowname */
		if (flowname != NULL) {
			if (strncmp(flowname, flist->flowname, MAXFLOWNAMELEN)
			    == NULL)
				return (flist);
		/* match the linkid */
		} else {
			if (linkid == flist->linkid)
				return (flist);
		}
	}

	/*
	 * No match found in the table.  Store statistics in the next slot.
	 * If necessary, make room for this entry.
	 */
	statentry++;
	if ((maxstatentries == 0) || (maxstatentries == statentry)) {
		maxstatentries += STATGROWSIZE;
		stattable = realloc(stattable,
		    maxstatentries * sizeof (struct flowlist));
		if (stattable == NULL) {
			perror("realloc");
			return (struct flowlist *)(NULL);
		}
	}
	flist = &stattable[statentry];
	bzero(flist, sizeof (struct flowlist));

	if (flowname != NULL)
		(void) strncpy(flist->flowname, flowname, MAXFLOWNAMELEN);
	flist->linkid = linkid;
	flist->fd = INT32_MAX;

	return (flist);
}

/*ARGSUSED*/
static void
print_flow_stats(dladm_handle_t handle, struct flowlist *flist)
{
	struct flowlist *fcurr;
	double ikbs, okbs;
	double ipks, opks;
	double dlt;
	int fcount;
	static boolean_t first = B_TRUE;

	if (first) {
		first = B_FALSE;
		(void) printw("please wait...\n");
		return;
	}

	for (fcount = 0, fcurr = flist;
	    fcount <= statentry;
	    fcount++, fcurr++) {
		if (fcurr->flowname && fcurr->display) {
			dlt = (double)fcurr->diffstats.snaptime/(double)NANOSEC;
			ikbs = fcurr->diffstats.rbytes * 8 / dlt / 1024;
			okbs = fcurr->diffstats.obytes * 8 / dlt / 1024;
			ipks = fcurr->diffstats.ipackets / dlt;
			opks = fcurr->diffstats.opackets / dlt;
			(void) printw("%-15.15s", fcurr->flowname);
			(void) printw("%-10.10s", fcurr->linkname);
			(void) printw("%9.2f %9.2f %9.2f %9.2f ",
			    ikbs, okbs, ipks, opks);
			(void) printw("\n");
		}
	}
}

/*ARGSUSED*/
static int
flow_kstats(dladm_handle_t handle, dladm_flow_attr_t *attr, void *arg)
{
	kstat_ctl_t 	*kcp = (kstat_ctl_t *)arg;
	kstat_t		*ksp;
	struct flowlist	*flist;
	pktsum_t	currstats, *prevstats, *diffstats;
	char		flowname[MAXFLOWNAMELEN], zonename[ZONENAME_MAX];
	zoneid_t	flowzoneid = attr->fa_flow_desc.fd_zoneid;

	if (flowzoneid == getzoneid()) {
		(void) snprintf(flowname, sizeof (flowname), "%s",
		    attr->fa_flowname);
	} else {
		ssize_t s;
		/* need to check err code, if zone is halt */
		s = getzonenamebyid(flowzoneid, zonename, sizeof (zonename));
		if (s < 0)
			return (DLADM_STATUS_NOTFOUND);

		(void) snprintf(flowname, sizeof (flowname), "%s/%s",
		    zonename, attr->fa_flowname);
	}

	flist = findstat(flowname, attr->fa_linkid);
	if (flist == NULL)
		return (DLADM_WALK_CONTINUE);

	flist->display = B_FALSE;
	prevstats = &flist->prevstats;
	diffstats = &flist->diffstats;

	(void) dladm_datalink_id2info(handle, attr->fa_linkid, NULL, NULL,
	    NULL, flist->linkname, sizeof (flist->linkname));

	/* lookup kstat entry */
	ksp = dladm_kstat_lookup(kcp, NULL, -1, flowname, "flow");

	if (ksp == NULL)
		return (DLADM_WALK_CONTINUE);

	/* read packet and byte stats */
	dladm_get_stats(kcp, ksp, &currstats);

	if (flist->ifspeed == 0)
		(void) dladm_kstat_value(ksp, "ifspeed", KSTAT_DATA_UINT64,
		    &flist->ifspeed);

	if (flist->first) {
		flist->first = B_FALSE;
	} else {
		dladm_stats_diff(diffstats, &currstats, prevstats);
		if (diffstats->snaptime == 0)
			return (DLADM_WALK_CONTINUE);
		dladm_stats_total(&totalstats, diffstats, &totalstats);
	}

	bcopy(&currstats, prevstats, sizeof (pktsum_t));
	flist->display = B_TRUE;

	return (DLADM_WALK_CONTINUE);
}

/*ARGSUSED*/
static void
print_link_stats(dladm_handle_t handle, struct flowlist *flist)
{
	struct flowlist *fcurr;
	double ikbs, okbs;
	double ipks, opks;
	double util;
	double dlt;
	int fcount;
	static boolean_t first = B_TRUE;

	if (first) {
		first = B_FALSE;
		(void) printw("please wait...\n");
		return;
	}

	for (fcount = 0, fcurr = flist;
	    fcount <= statentry;
	    fcount++, fcurr++) {
		if ((fcurr->linkid != DATALINK_INVALID_LINKID) &&
		    fcurr->display)  {
			dlt = (double)fcurr->diffstats.snaptime/(double)NANOSEC;
			ikbs = (double)fcurr->diffstats.rbytes * 8 / dlt / 1024;
			okbs = (double)fcurr->diffstats.obytes * 8 / dlt / 1024;
			ipks = (double)fcurr->diffstats.ipackets / dlt;
			opks = (double)fcurr->diffstats.opackets / dlt;
			(void) printw("%-10.10s", fcurr->linkname);
			(void) printw("%9.2f %9.2f %9.2f %9.2f ",
			    ikbs, okbs, ipks, opks);
			if (fcurr->ifspeed != 0)
				util = ((ikbs + okbs) * 1024) *
				    100/ fcurr->ifspeed;
			else
				util = (double)0;
			(void) attron(A_BOLD);
			(void) printw("    %6.2f", util);
			(void) attroff(A_BOLD);
			(void) printw("\n");
		}
	}
}

/*
 * This function is called through the dladm_walk_datalink_id() walker and
 * calls the dladm_walk_flow() walker.
 */

/*ARGSUSED*/
static int
link_flowstats(dladm_handle_t handle, datalink_id_t linkid, void *arg)
{
	dladm_status_t	status;

	status = dladm_walk_flow(flow_kstats, handle, linkid, arg, B_FALSE);
	if (status == DLADM_STATUS_OK)
		return (DLADM_WALK_CONTINUE);
	else
		return (DLADM_WALK_TERMINATE);
}

/*ARGSUSED*/
static int
link_kstats(dladm_handle_t handle, datalink_id_t linkid, void *arg)
{
	kstat_ctl_t		*kcp = (kstat_ctl_t *)arg;
	struct flowlist		*flist;
	pktsum_t		currstats, *prevstats, *diffstats;
	datalink_class_t	class;
	kstat_t			*ksp;
	char			dnlink[MAXPATHLEN];

	/* find the flist entry */
	flist = findstat(NULL, linkid);
	if (flist == NULL)
		return (DLADM_WALK_CONTINUE);

	flist->display = B_FALSE;
	prevstats = &flist->prevstats;
	diffstats = &flist->diffstats;

	if (dladm_datalink_id2info(handle, linkid, NULL, &class, NULL,
	    flist->linkname, sizeof (flist->linkname)) != DLADM_STATUS_OK)
		return (DLADM_WALK_CONTINUE);

	if (flist->fd == INT32_MAX) {
		if (class == DATALINK_CLASS_PHYS) {
			(void) snprintf(dnlink, MAXPATHLEN, "/dev/net/%s",
			    flist->linkname);
			if ((flist->fd = open(dnlink, O_RDWR)) < 0)
				return (DLADM_WALK_CONTINUE);
		} else {
			flist->fd = -1;
		}
		(void) kstat_chain_update(kcp);
	}

	/* lookup kstat entry */
	ksp = dladm_kstat_lookup(kcp, NULL, -1, flist->linkname, "net");

	if (ksp == NULL)
		return (DLADM_WALK_CONTINUE);

	/* read packet and byte stats */
	dladm_get_stats(kcp, ksp, &currstats);

	if (flist->ifspeed == 0)
		(void) dladm_kstat_value(ksp, "ifspeed", KSTAT_DATA_UINT64,
		    &flist->ifspeed);

	if (flist->first) {
		flist->first = B_FALSE;
	} else {
		dladm_stats_diff(diffstats, &currstats, prevstats);
		if (diffstats->snaptime == 0)
			return (DLADM_WALK_CONTINUE);
	}

	bcopy(&currstats, prevstats, sizeof (*prevstats));
	flist->display = B_TRUE;

	return (DLADM_WALK_CONTINUE);
}

static void
closedevnet()
{
	int index = 0;
	struct flowlist *flist;

	/* Close all open /dev/net/ files */

	for (flist = stattable; index < maxstatentries; index++, flist++) {
		if (flist->linkid == DATALINK_INVALID_LINKID)
			break;
		if (flist->fd != -1 && flist->fd != INT32_MAX)
			(void) close(flist->fd);
	}
}

/*ARGSUSED*/
static void
sig_break(int s)
{
	handle_break = 1;
}

/*ARGSUSED*/
static void
sig_resize(int s)
{
	handle_resize = 1;
}

static void
curses_init()
{
	maxx = maxx;	/* lint */
	maxy = maxy;	/* lint */

	/* Install signal handlers */
	(void) signal(SIGINT,  sig_break);
	(void) signal(SIGQUIT, sig_break);
	(void) signal(SIGTERM, sig_break);
	(void) signal(SIGWINCH, sig_resize);

	/* Initialize ncurses */
	(void) initscr();
	(void) cbreak();
	(void) noecho();
	(void) curs_set(0);
	timeout(0);
	getmaxyx(stdscr, maxy, maxx);
}

static void
curses_fin()
{
	(void) printw("\n");
	(void) curs_set(1);
	(void) nocbreak();
	(void) endwin();

	free(stattable);
}

static void
stat_report(dladm_handle_t handle, kstat_ctl_t *kcp,  datalink_id_t linkid,
    const char *flowname, int opt)
{

	double dlt, ikbs, okbs, ipks, opks;

	struct flowlist *fstable = stattable;

	if ((opt != LINK_REPORT) && (opt != FLOW_REPORT))
		return;

	/* Handle window resizes */
	if (handle_resize) {
		(void) endwin();
		(void) initscr();
		(void) cbreak();
		(void) noecho();
		(void) curs_set(0);
		timeout(0);
		getmaxyx(stdscr, maxy, maxx);
		redraw = 1;
		handle_resize = 0;
	}

	/* Print title */
	(void) erase();
	(void) attron(A_BOLD);
	(void) move(0, 0);
	if (opt == FLOW_REPORT)
		(void) printw("%-15.15s", "Flow");
	(void) printw("%-10.10s", "Link");
	(void) printw("%9.9s %9.9s %9.9s %9.9s ",
	    "iKb/s", "oKb/s", "iPk/s", "oPk/s");
	if (opt == LINK_REPORT)
		(void) printw("    %6.6s", "%Util");
	(void) printw("\n");
	(void) attroff(A_BOLD);

	(void) move(2, 0);

	/* Print stats for each link or flow */
	bzero(&totalstats, sizeof (totalstats));
	if (opt == LINK_REPORT) {
		/* Display all links */
		if (linkid == DATALINK_ALL_LINKID) {
			(void) dladm_walk_datalink_id(link_kstats, handle,
			    (void *)kcp, DATALINK_CLASS_ALL,
			    DATALINK_ANY_MEDIATYPE, DLADM_OPT_ACTIVE);
		/* Display 1 link */
		} else {
			(void) link_kstats(handle, linkid, kcp);
		}
		print_link_stats(handle, fstable);

	} else if (opt == FLOW_REPORT) {
		/* Display 1 flow */
		if (flowname != NULL) {
			dladm_flow_attr_t fattr;
			if (dladm_flow_info(handle, flowname, &fattr) !=
			    DLADM_STATUS_OK)
				return;
			(void) flow_kstats(handle, &fattr, kcp);
		/* Display all flows on all links */
		} else if (linkid == DATALINK_ALL_LINKID) {
			(void) dladm_walk_datalink_id(link_flowstats, handle,
			    (void *)kcp, DATALINK_CLASS_ALL,
			    DATALINK_ANY_MEDIATYPE, DLADM_OPT_ACTIVE);
		/* Display all flows on a link */
		} else if (linkid != DATALINK_INVALID_LINKID) {
			(void) dladm_walk_flow(flow_kstats, handle, linkid, kcp,
			    B_FALSE);
		}
		print_flow_stats(handle, fstable);

		/* Print totals */
		(void) attron(A_BOLD);
		dlt = (double)totalstats.snaptime / (double)NANOSEC;
		ikbs = totalstats.rbytes / dlt / 1024;
		okbs = totalstats.obytes / dlt / 1024;
		ipks = totalstats.ipackets / dlt;
		opks = totalstats.opackets / dlt;
		(void) printw("\n%-25.25s", "Totals");
		(void) printw("%9.2f %9.2f %9.2f %9.2f ",
		    ikbs, okbs, ipks, opks);
		(void) attroff(A_BOLD);
	}

	if (redraw)
		(void) clearok(stdscr, 1);

	if (refresh() == ERR)
		return;

	if (redraw) {
		(void) clearok(stdscr, 0);
		redraw = 0;
	}
}

/* Exported functions */

/*
 * Continuously display link or flow statstics using a libcurses
 * based display.
 */

void
dladm_continuous(dladm_handle_t handle, datalink_id_t linkid,
    const char *flowname, int interval, int opt)
{
	kstat_ctl_t *kcp;

	if ((kcp = kstat_open()) == NULL) {
		warn("kstat open operation failed");
		return;
	}

	curses_init();

	for (;;) {

		if (handle_break)
			break;

		stat_report(handle, kcp, linkid, flowname, opt);

		(void) sleep(max(1, interval));
	}

	closedevnet();
	curses_fin();
	(void) kstat_close(kcp);
}

/*
 * dladm_kstat_lookup() is a modified version of kstat_lookup which
 * adds the class as a selector.
 */

kstat_t *
dladm_kstat_lookup(kstat_ctl_t *kcp, const char *module, int instance,
    const char *name, const char *class)
{
	kstat_t *ksp = NULL;
	char kstat_name[MAXLINKNAMELEN];
	char kstat_module[MAXLINKNAMELEN];
	const char *ksname = name;
	const char *ksmodule = module;
	zoneid_t zid;

	/*
	 * For zonename prefixed linknames retrieve the linkname
	 * and set kstats instance to the zone ID. zonename prefixed
	 * linknames refer to NGZ datalinks and their kstats are
	 * exposed in the GZ under instance set to the zone ID.
	 * Callers can pass the linkname in either the module
	 * or name argument, the instance can be 0 or -1.
	 */
	if (class == NULL || strcmp(class, "flow") != 0) {
		if (instance <= 0 && name != NULL && strchr(name, '/') !=
		    NULL) {
			(void) dlparse_zonelinkname(name, kstat_name, &zid);
			ksname = kstat_name;
			instance = zid;
		} else if (instance <= 0 && module != NULL &&
		    strchr(module, '/') != NULL) {
			(void) dlparse_zonelinkname(module, kstat_module, &zid);
			ksmodule = kstat_module;
			instance = zid;
		}
	}

	for (ksp = kcp->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
		if ((ksmodule == NULL || strcmp(ksp->ks_module, ksmodule) ==
		    0) && (instance == -1 || ksp->ks_instance == instance) &&
		    (ksname == NULL || strcmp(ksp->ks_name, ksname) == 0) &&
		    (class == NULL || strcmp(ksp->ks_class, class) == 0))
			return (ksp);
	}

	errno = ENOENT;
	return (NULL);
}

/*
 * dladm_get_stats() populates the supplied pktsum_t structure with
 * the input and output  packet and byte kstats from the kstat_t
 * found with dladm_kstat_lookup.
 */
void
dladm_get_stats(kstat_ctl_t *kcp, kstat_t *ksp, pktsum_t *stats)
{

	if (kstat_read(kcp, ksp, NULL) == -1)
		return;

	stats->snaptime = gethrtime();

	if (dladm_kstat_value(ksp, "ipackets64", KSTAT_DATA_UINT64,
	    &stats->ipackets) < 0) {
		if (dladm_kstat_value(ksp, "ipackets", KSTAT_DATA_UINT64,
		    &stats->ipackets) < 0)
			return;
	}

	if (dladm_kstat_value(ksp, "opackets64", KSTAT_DATA_UINT64,
	    &stats->opackets) < 0) {
		if (dladm_kstat_value(ksp, "opackets", KSTAT_DATA_UINT64,
		    &stats->opackets) < 0)
			return;
	}

	if (dladm_kstat_value(ksp, "rbytes64", KSTAT_DATA_UINT64,
	    &stats->rbytes) < 0) {
		if (dladm_kstat_value(ksp, "rbytes", KSTAT_DATA_UINT64,
		    &stats->rbytes) < 0)
			return;
	}

	if (dladm_kstat_value(ksp, "obytes64", KSTAT_DATA_UINT64,
	    &stats->obytes) < 0) {
		if (dladm_kstat_value(ksp, "obytes", KSTAT_DATA_UINT64,
		    &stats->obytes) < 0)
			return;
	}

	if (dladm_kstat_value(ksp, "ierrors", KSTAT_DATA_UINT32,
	    &stats->ierrors) < 0) {
		if (dladm_kstat_value(ksp, "ierrors", KSTAT_DATA_UINT64,
		    &stats->ierrors) < 0)
		return;
	}

	if (dladm_kstat_value(ksp, "oerrors", KSTAT_DATA_UINT32,
	    &stats->oerrors) < 0) {
		if (dladm_kstat_value(ksp, "oerrors", KSTAT_DATA_UINT64,
		    &stats->oerrors) < 0)
			return;
	}
}

int
dladm_kstat_value(kstat_t *ksp, const char *name, uint8_t type, void *buf)
{
	kstat_named_t	*knp;

	if ((knp = kstat_data_lookup(ksp, (char *)name)) == NULL)
		return (-1);

	if (knp->data_type != type)
		return (-1);

	switch (type) {
	case KSTAT_DATA_UINT64:
		*(uint64_t *)buf = knp->value.ui64;
		break;
	case KSTAT_DATA_UINT32:
		*(uint32_t *)buf = knp->value.ui32;
		break;
	default:
		return (-1);
	}

	return (0);
}

dladm_status_t
dladm_get_single_mac_stat(dladm_handle_t handle, datalink_id_t linkid,
    const char *name, uint8_t type, void *val)
{
	kstat_ctl_t	*kcp;
	char		module[DLPI_LINKNAME_MAX];
	uint_t		instance;
	char 		link[DLPI_LINKNAME_MAX];
	dladm_status_t	status;
	uint32_t	flags, media;
	kstat_t		*ksp;
	dladm_phys_attr_t dpap;

	if ((status = dladm_datalink_id2info(handle, linkid, &flags, NULL,
	    &media, link, DLPI_LINKNAME_MAX)) != DLADM_STATUS_OK)
		return (status);

	if (media != DL_ETHER)
		return (DLADM_STATUS_LINKINVAL);

	status = dladm_phys_info(handle, linkid, &dpap, DLADM_OPT_PERSIST);

	if (status != DLADM_STATUS_OK)
		return (status);

	if (!dlparse_drvppa(dpap.dp_dev, module, sizeof (module), &instance))
		return (DLADM_STATUS_LINKINVAL);

	if ((kcp = kstat_open()) == NULL) {
		warn("kstat_open operation failed");
		return (-1);
	}

	/*
	 * The kstat query could fail if the underlying MAC
	 * driver was already detached.
	 */
	if ((ksp = kstat_lookup(kcp, module, instance, DLSTAT_MAC_PHYS_STAT))
	    == NULL && (ksp = kstat_lookup(kcp, module, instance, NULL))
	    == NULL) {
		goto bail;
	}

	if (kstat_read(kcp, ksp, NULL) == -1)
		goto bail;

	if (dladm_kstat_value(ksp, name, type, val) < 0)
		goto bail;

	(void) kstat_close(kcp);
	return (DLADM_STATUS_OK);

bail:
	(void) kstat_close(kcp);
	return (dladm_errno2status(errno));
}

/* Compute sum of 2 pktsums (s1 = s2 + s3) */
void
dladm_stats_total(pktsum_t *s1, pktsum_t *s2, pktsum_t *s3)
{
	s1->rbytes    = s2->rbytes    + s3->rbytes;
	s1->ipackets  = s2->ipackets  + s3->ipackets;
	s1->ierrors   = s2->ierrors   + s3->ierrors;
	s1->obytes    = s2->obytes    + s3->obytes;
	s1->opackets  = s2->opackets  + s3->opackets;
	s1->oerrors   = s2->oerrors   + s3->oerrors;
	s1->snaptime  = s2->snaptime;
}

#define	DIFF_STAT(s2, s3) ((s2) > (s3) ? ((s2) - (s3)) : 0)


/* Compute differences between 2 pktsums (s1 = s2 - s3) */
void
dladm_stats_diff(pktsum_t *s1, pktsum_t *s2, pktsum_t *s3)
{
	s1->rbytes    = DIFF_STAT(s2->rbytes,   s3->rbytes);
	s1->ipackets  = DIFF_STAT(s2->ipackets, s3->ipackets);
	s1->ierrors   = DIFF_STAT(s2->ierrors,  s3->ierrors);
	s1->obytes    = DIFF_STAT(s2->obytes,   s3->obytes);
	s1->opackets  = DIFF_STAT(s2->opackets, s3->opackets);
	s1->oerrors   = DIFF_STAT(s2->oerrors,  s3->oerrors);
	s1->snaptime  = DIFF_STAT(s2->snaptime, s3->snaptime);
}

typedef struct {
	const char	*si_name;
	uint_t		si_offset;
} stat_info_t;

#define	A_CNT(arr)	(sizeof (arr) / sizeof (arr[0]))

/* Definitions for rx lane stats */
#define	RL_OFF(f)	(offsetof(rx_lane_stat_t, f))

static	stat_info_t	rx_hwlane_stats_list[] = {
	{"ipackets",		RL_OFF(rl_ipackets)},
	{"rbytes",		RL_OFF(rl_rbytes)},
	{"intrs",		RL_OFF(rl_intrs)},
	{"intrbytes",		RL_OFF(rl_intrbytes)},
	{"polls",		RL_OFF(rl_polls)},
	{"pollbytes",		RL_OFF(rl_pollbytes)},
	{"idrops",		RL_OFF(rl_idrops)},
	{"idropbytes",		RL_OFF(rl_idropbytes)}
};
#define	RX_HWLANE_STAT_SIZE	A_CNT(rx_hwlane_stats_list)

static	stat_info_t	rx_lane_stats_list[] = {
	{"ipackets",		RL_OFF(rl_ipackets)},
	{"rbytes",		RL_OFF(rl_rbytes)},
	{"rxlocal",		RL_OFF(rl_lclpackets)},
	{"rxlocalbytes",	RL_OFF(rl_lclbytes)},
	{"intrs",		RL_OFF(rl_intrs)},
	{"intrbytes",		RL_OFF(rl_intrbytes)},
	{"polls",		RL_OFF(rl_polls)},
	{"pollbytes",		RL_OFF(rl_pollbytes)},
	{"idrops",		RL_OFF(rl_idrops)},
	{"idropbytes",		RL_OFF(rl_idropbytes)},
};
#define	RX_LANE_STAT_SIZE	A_CNT(rx_lane_stats_list)

/* Definitions for tx lane stats */
#define	TL_OFF(f)	(offsetof(tx_lane_stat_t, f))

static	stat_info_t	tx_lane_stats_list[] = {
	{"opackets",	TL_OFF(tl_opackets)},
	{"obytes",	TL_OFF(tl_obytes)},
	{"blockcnt",	TL_OFF(tl_blockcnt)},
	{"unblockcnt",	TL_OFF(tl_unblockcnt)},
	{"odrops",	TL_OFF(tl_odrops)},
	{"odropbytes",	TL_OFF(tl_odropbytes)}
};
#define	TX_LANE_STAT_SIZE	A_CNT(tx_lane_stats_list)

/* Definitions for tx/rx link stats */
#define	L_OFF(f)	(offsetof(link_stat_t, f))

static	stat_info_t	link_stats_list[] = {
	{"multircv",		L_OFF(ls_multircv)},
	{"brdcstrcv",		L_OFF(ls_brdcstrcv)},
	{"multixmt",		L_OFF(ls_multixmt)},
	{"brdcstxmt",		L_OFF(ls_brdcstxmt)},
	{"multircvbytes",	L_OFF(ls_multircvbytes)},
	{"bcstrcvbytes",	L_OFF(ls_brdcstrcvbytes)},
	{"multixmtbytes",	L_OFF(ls_multixmtbytes)},
	{"bcstxmtbytes",	L_OFF(ls_brdcstxmtbytes)},
	{"txerrors",		L_OFF(ls_txerrors)},
	{"macspoofed",		L_OFF(ls_macspoofed)},
	{"ipspoofed",		L_OFF(ls_ipspoofed)},
	{"dhcpspoofed",		L_OFF(ls_dhcpspoofed)},
	{"restricted",		L_OFF(ls_restricted)},
	{"ipackets",		L_OFF(ls_ipackets)},
	{"rbytes",		L_OFF(ls_rbytes)},
	{"rxlocal",		L_OFF(ls_rxlocal)},
	{"rxlocalbytes",	L_OFF(ls_rxlocalbytes)},
	{"txlocal",		L_OFF(ls_txlocal)},
	{"txlocalbytes",	L_OFF(ls_txlocalbytes)},
	{"intrs",		L_OFF(ls_intrs)},
	{"intrbytes",		L_OFF(ls_intrbytes)},
	{"polls",		L_OFF(ls_polls)},
	{"pollbytes",		L_OFF(ls_pollbytes)},
	{"idrops",		L_OFF(ls_idrops)},
	{"idropbytes",		L_OFF(ls_idropbytes)},
	{"obytes",		L_OFF(ls_obytes)},
	{"opackets",		L_OFF(ls_opackets)},
	{"blockcnt",		L_OFF(ls_blockcnt)},
	{"unblockcnt",		L_OFF(ls_unblockcnt)},
	{"odrops",		L_OFF(ls_odrops)},
	{"odropbytes",		L_OFF(ls_odropbytes)}
};
#define	LINK_STAT_SIZE		A_CNT(link_stats_list)

/* Definitions for rx ring stats */
#define	R_OFF(f)	(offsetof(ring_stat_t, f))

static	stat_info_t	rx_ring_stats_list[] = {
	{"ipackets",	R_OFF(r_packets)},
	{"rbytes",	R_OFF(r_bytes)}
};
#define	RX_RING_STAT_SIZE	A_CNT(rx_ring_stats_list)

/* Definitions for tx ring stats */
static	stat_info_t	tx_ring_stats_list[] = {
	{"opackets",	R_OFF(r_packets)},
	{"obytes",	R_OFF(r_bytes)}
};
#define	TX_RING_STAT_SIZE	A_CNT(tx_ring_stats_list)

/* Definitions for total stats */
#define	T_OFF(f)	(offsetof(total_stat_t, f))

static	stat_info_t	total_stats_list[] = {
	{"ipackets64",	T_OFF(ts_ipackets)},
	{"rbytes64",	T_OFF(ts_rbytes)},
	{"opackets64",	T_OFF(ts_opackets)},
	{"obytes64",	T_OFF(ts_obytes)}
};
#define	TOTAL_STAT_SIZE		A_CNT(total_stats_list)

/* Definitions for aggr stats */
#define	AP_OFF(f)	(offsetof(aggr_port_stat_t, f))

static	stat_info_t	aggr_port_stats_list[] = {
	{"ipackets64",	AP_OFF(ap_ipackets)},
	{"rbytes64",	AP_OFF(ap_rbytes)},
	{"opackets64",	AP_OFF(ap_opackets)},
	{"obytes64",	AP_OFF(ap_obytes)}
};
#define	AGGR_PORT_STAT_SIZE	A_CNT(aggr_port_stats_list)

/* Definitions for flow stats */
#define	FL_OFF(f)	(offsetof(flow_stat_t, f))

static	stat_info_t	flow_stats_list[] = {
	{"ipackets",	FL_OFF(fl_ipackets)},
	{"rbytes",	FL_OFF(fl_rbytes)},
	{"idrops",	FL_OFF(fl_idrops)},
	{"idropbytes",	FL_OFF(fl_idropbytes)},
	{"opackets",	FL_OFF(fl_opackets)},
	{"obytes",	FL_OFF(fl_obytes)},
	{"odrops",	FL_OFF(fl_odrops)},
	{"odropbytes",	FL_OFF(fl_odropbytes)},
};
#define	FLOW_STAT_SIZE		A_CNT(flow_stats_list)

/* Rx lane specific functions */
void *			dlstat_rx_lane_stats(dladm_handle_t, datalink_id_t);
static boolean_t	i_dlstat_rx_lane_match(void *, void *);
static void *		i_dlstat_rx_lane_stat_entry_diff(void *, void *);

/* Tx lane specific functions */
void *			dlstat_tx_lane_stats(dladm_handle_t, datalink_id_t);
static boolean_t	i_dlstat_tx_lane_match(void *, void *);
static void *		i_dlstat_tx_lane_stat_entry_diff(void *, void *);

/* Rx lane total specific functions */
void *			dlstat_rx_lane_total_stats(dladm_handle_t,
			    datalink_id_t);

/* Tx lane total specific functions */
void *			dlstat_tx_lane_total_stats(dladm_handle_t,
			    datalink_id_t);

/* Rx ring specific functions */
void *			dlstat_rx_ring_stats(dladm_handle_t, datalink_id_t);
static boolean_t	i_dlstat_rx_ring_match(void *, void *);
static void *		i_dlstat_rx_ring_stat_entry_diff(void *, void *);

/* Tx ring specific functions */
void *			dlstat_tx_ring_stats(dladm_handle_t, datalink_id_t);
static boolean_t	i_dlstat_tx_ring_match(void *, void *);
static void *		i_dlstat_tx_ring_stat_entry_diff(void *, void *);

/* Rx ring total specific functions */
void *			dlstat_rx_ring_total_stats(dladm_handle_t,
			    datalink_id_t);

/* Tx ring total specific functions */
void *			dlstat_tx_ring_total_stats(dladm_handle_t,
			    datalink_id_t);

/* Summary specific functions */
void *			dlstat_total_stats(dladm_handle_t, datalink_id_t);
static boolean_t	i_dlstat_total_match(void *, void *);
static void *		i_dlstat_total_stat_entry_diff(void *, void *);

/* Aggr port specific functions */
void *			dlstat_aggr_port_stats(dladm_handle_t, datalink_id_t);
static boolean_t	i_dlstat_aggr_port_match(void *, void *);
static void *		i_dlstat_aggr_port_stat_entry_diff(void *, void *);

/* Link stat specific functions */
void *			dlstat_link_stats(dladm_handle_t, datalink_id_t);

typedef void *		dladm_stat_query_t(dladm_handle_t, datalink_id_t);
typedef boolean_t	dladm_stat_match_t(void *, void *);
typedef void *		dladm_stat_diff_t(void *, void *);

typedef struct dladm_stat_desc_s {
	dladm_stat_type_t	ds_stattype;
	dladm_stat_query_t	*ds_querystat;
	dladm_stat_match_t	*ds_matchstat;
	dladm_stat_diff_t	*ds_diffstat;
	uint_t			ds_offset;
	stat_info_t		*ds_statlist;
	uint_t			ds_statsize;
} dladm_stat_desc_t;

/*
 * dladm_stat_table has one entry for each supported stat. ds_querystat returns
 * a chain of 'stat entries' for the queried stat.
 * Each stat entry has set of identifiers (ids) and an object containing actual
 * stat values. These stat entry objects are chained together in a linked list
 * of datatype dladm_stat_chain_t. Head of this list is returned to the caller
 * of dladm_link_stat_query.
 *
 * One node in the chain is shown below:
 *
 *	-------------------------
 *	| dc_statentry	        |
 *	|    --------------     |
 *	|    |     ids     |	|
 *	|    --------------     |
 *	|    | stat fields |	|
 *	|    --------------     |
 *	-------------------------
 *	|      dc_next ---------|------> to next stat entry
 *	-------------------------
 *
 * In particular, for query DLADM_STAT_RX_LANE, dc_statentry carries pointer to
 * object of type rx_lane_stat_entry_t.
 *
 * dladm_link_stat_query_all returns similar chain. However, instead of storing
 * stat fields as raw numbers, it stores those as chain of <name, value> pairs.
 * The resulting structure is depicted below:
 *
 *	-------------------------
 *	| dc_statentry	        |
 *	|    --------------     |   ---------------
 *	|    |  nv_header  |	|   |   name, val  |
 *	|    --------------     |   ---------------
 *	|    | nve_stats---|----|-->| nv_nextstat--|---> to next name, val pair
 *	|    --------------     |   ---------------
 *	-------------------------
 *	|      dc_next ---------|------> to next stat entry
 *	-------------------------
 */
static dladm_stat_desc_t  dladm_stat_table[] = {
{ DLADM_STAT_RX_LANE,		dlstat_rx_lane_stats,
    i_dlstat_rx_lane_match,	i_dlstat_rx_lane_stat_entry_diff,
    offsetof(rx_lane_stat_entry_t, rle_stats),
    rx_lane_stats_list,		RX_LANE_STAT_SIZE},

{ DLADM_STAT_TX_LANE,		dlstat_tx_lane_stats,
    i_dlstat_tx_lane_match,	i_dlstat_tx_lane_stat_entry_diff,
    offsetof(tx_lane_stat_entry_t, tle_stats),
    tx_lane_stats_list,		TX_LANE_STAT_SIZE},

{ DLADM_STAT_RX_LANE_TOTAL,	dlstat_rx_lane_total_stats,
    i_dlstat_rx_lane_match,	i_dlstat_rx_lane_stat_entry_diff,
    offsetof(rx_lane_stat_entry_t, rle_stats),
    rx_lane_stats_list,		RX_LANE_STAT_SIZE},

{ DLADM_STAT_TX_LANE_TOTAL,	dlstat_tx_lane_total_stats,
    i_dlstat_tx_lane_match,	i_dlstat_tx_lane_stat_entry_diff,
    offsetof(tx_lane_stat_entry_t, tle_stats),
    tx_lane_stats_list,		TX_LANE_STAT_SIZE},

{ DLADM_STAT_RX_RING,		dlstat_rx_ring_stats,
    i_dlstat_rx_ring_match,	i_dlstat_rx_ring_stat_entry_diff,
    offsetof(ring_stat_entry_t, re_stats),
    rx_ring_stats_list,		RX_RING_STAT_SIZE},

{ DLADM_STAT_TX_RING,		dlstat_tx_ring_stats,
    i_dlstat_tx_ring_match,	i_dlstat_tx_ring_stat_entry_diff,
    offsetof(ring_stat_entry_t, re_stats),
    tx_ring_stats_list,		TX_RING_STAT_SIZE},

{ DLADM_STAT_RX_RING_TOTAL,	dlstat_rx_ring_total_stats,
    i_dlstat_rx_ring_match,	i_dlstat_rx_ring_stat_entry_diff,
    offsetof(ring_stat_entry_t, re_stats),
    rx_ring_stats_list,		RX_RING_STAT_SIZE},

{ DLADM_STAT_TX_RING_TOTAL,	dlstat_tx_ring_total_stats,
    i_dlstat_tx_ring_match,	i_dlstat_tx_ring_stat_entry_diff,
    offsetof(ring_stat_entry_t, re_stats),
    tx_ring_stats_list,		TX_RING_STAT_SIZE},

{ DLADM_STAT_TOTAL,		dlstat_total_stats,
    i_dlstat_total_match,	i_dlstat_total_stat_entry_diff,
    offsetof(total_stat_entry_t, tse_stats),
    total_stats_list,		TOTAL_STAT_SIZE},

{ DLADM_STAT_AGGR_PORT,		dlstat_aggr_port_stats,
    i_dlstat_aggr_port_match,	i_dlstat_aggr_port_stat_entry_diff,
    offsetof(aggr_port_stat_entry_t, ape_stats),
    aggr_port_stats_list,	AGGR_PORT_STAT_SIZE},
/*
 * We don't support -i <interval> for all link stat query. Several table fields
 * are left uninitialized thus.
 */
{ DLADM_STAT_LINK,		dlstat_link_stats,
    NULL,			NULL,
    0,
    link_stats_list,		LINK_STAT_SIZE}
};

/* Internal functions */
static void *
dlstat_diff_stats(void *arg1, void *arg2, dladm_stat_type_t stattype)
{
	return (dladm_stat_table[stattype].ds_diffstat(arg1, arg2));
}

static boolean_t
dlstat_match_stats(void *arg1, void *arg2, dladm_stat_type_t stattype)
{
	return (dladm_stat_table[stattype].ds_matchstat(arg1, arg2));
}

/* Diff between two stats */
static void
i_dlstat_diff_stats(void *diff, void *op1, void *op2,
    stat_info_t stats_list[], uint_t size)
{
	int	i;

	for (i = 0; i < size; i++) {
		uint64_t *op1_val  = (void *)
		    ((uchar_t *)op1 + stats_list[i].si_offset);
		uint64_t *op2_val = (void *)
		    ((uchar_t *)op2  + stats_list[i].si_offset);
		uint64_t *diff_val = (void *)
		    ((uchar_t *)diff + stats_list[i].si_offset);

		*diff_val = DIFF_STAT(*op1_val, *op2_val);
	}
}

/*
 * Perform diff = s1 - s2,  where diff, s1, s2 are structure objects of same
 * datatype. slist is list of offsets of the fields within the structure.
 */
#define	DLSTAT_DIFF_STAT(s1, s2, diff, f, slist, sz) {			\
	if (s2 == NULL) {						\
		bcopy(&s1->f, &diff->f, sizeof (s1->f));		\
	} else {							\
		i_dlstat_diff_stats(&diff->f, &s1->f,			\
		    &s2->f, slist, sz);					\
	}								\
}

/* Sum two stats */
static void
i_dlstat_sum_stats(void *sum, void *op1, void *op2,
    stat_info_t stats_list[], uint_t size)
{
	int	i;

	for (i = 0; i < size; i++) {
		uint64_t *op1_val = (void *)
		    ((uchar_t *)op1 + stats_list[i].si_offset);
		uint64_t *op2_val = (void *)
		    ((uchar_t *)op2 + stats_list[i].si_offset);
		uint64_t *sum_val = (void *)
		    ((uchar_t *)sum + stats_list[i].si_offset);

		*sum_val =  *op1_val + *op2_val;
	}
}

/* Given a linkid, resolve kstat name for querying link level stats */
static boolean_t
i_dlstat_resolve_kstat_link_names(dladm_handle_t dh, datalink_id_t linkid,
    char *modname)
{
	datalink_id_t		ulinkid;
	datalink_class_t	class;
	dladm_phys_attr_t	dpa;

	/* Query underlying datalink's (if applicable) link id */
	dladm_query_primary_linkid(dh, linkid, &ulinkid);

	/* Query underlying datalink's name (could be vanity name) */
	if (dladm_datalink_id2info(dh, ulinkid, NULL, &class, NULL, modname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		return (B_FALSE);
	}

	if (class == DATALINK_CLASS_ETHERSTUB) {
		if (dladm_datalink_id2info(dh, linkid, NULL, &class, NULL,
		    modname, DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
			return (B_FALSE);
		}
	} else if (class != DATALINK_CLASS_AGGR) {
		/* Resolve vanity name to phys name */
		if (dladm_phys_info(dh, ulinkid, &dpa, DLADM_OPT_ACTIVE) !=
		    DLADM_STATUS_OK) {
			return (B_FALSE);
		}
		(void) strlcpy(modname, dpa.dp_dev, MAXLINKNAMELEN);
	}
	return (B_TRUE);
}

/* Given a linkid, resolve kstat name for querying phys level stats */
static boolean_t
i_dlstat_resolve_kstat_phys_names(dladm_handle_t dh, datalink_id_t linkid,
    char *modname)
{
	datalink_class_t	class;
	dladm_phys_attr_t	dpa;

	/*
	 * kstats corresponding to physical device rings continue to use
	 * device names even if the link is renamed using dladm rename-link.
	 * Thus, given a linkid, we lookup the physical device name.
	 * However, if an aggr is renamed, kstats corresponding to its
	 * pseudo rings are renamed as well.
	 */
	if (dladm_datalink_id2info(dh, linkid, NULL, &class, NULL, modname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		return (B_FALSE);
	}

	if (class != DATALINK_CLASS_AGGR) {
		/* Resolve vanity name to phys name */
		if (dladm_phys_info(dh, linkid, &dpa, DLADM_OPT_ACTIVE) !=
		    DLADM_STATUS_OK) {
			return (B_FALSE);
		}
		(void) strlcpy(modname, dpa.dp_dev, MAXLINKNAMELEN);
	}
	return (B_TRUE);
}

/* Look up kstat value */
static void
i_dlstat_get_stats(kstat_ctl_t *kcp, kstat_t *ksp, void *stats,
    stat_info_t stats_list[], uint_t size)
{
	int	i;

	if (kstat_read(kcp, ksp, NULL) == -1)
		return;

	for (i = 0; i < size; i++) {
		uint64_t *val = (void *)
		    ((uchar_t *)stats + stats_list[i].si_offset);

		if (dladm_kstat_value(ksp, stats_list[i].si_name,
		    KSTAT_DATA_UINT64, val) < 0)
			return;
	}
}

/* Append linked list list1 to linked list list2 and return resulting list */
static dladm_stat_chain_t *
i_dlstat_join_lists(dladm_stat_chain_t *list1, dladm_stat_chain_t *list2)
{
	dladm_stat_chain_t	*curr;

	if (list1 == NULL)
		return (list2);

	/* list1 has at least one element, find last element in list1 */
	curr = list1;
	while (curr->dc_next != NULL)
		curr = curr->dc_next;

	curr->dc_next = list2;
	return (list1);
}

typedef enum {
	DLSTAT_RX_RING_IDLIST,
	DLSTAT_TX_RING_IDLIST,
	DLSTAT_RX_HWLANE_IDLIST,
	DLSTAT_TX_HWLANE_IDLIST
} dlstat_idlist_type_t;

void
dladm_sort_index_list(uint_t idlist[], uint_t size)
{
	int 	i, j;

	for (j = 1; j < size; j++) {
		int key = idlist[j];
		for (i = j - 1; (i >= 0) && (idlist[i] > key); i--)
			idlist[i + 1] = idlist[i];
		idlist[i + 1] = key;
	}
}

/* Support for legacy drivers */
void
i_query_legacy_stats(const char *linkname, pktsum_t *stats)
{
	kstat_ctl_t	*kcp;
	kstat_t		*ksp;

	bzero(stats, sizeof (*stats));

	if ((kcp = kstat_open()) == NULL)
		return;

	ksp = dladm_kstat_lookup(kcp, "link", 0, linkname, NULL);

	if (ksp != NULL)
		dladm_get_stats(kcp, ksp, stats);

	(void) kstat_close(kcp);
}

void *
i_dlstat_legacy_rx_lane_stats(const char *linkname)
{
	dladm_stat_chain_t	*head = NULL;
	pktsum_t		stats;
	rx_lane_stat_entry_t	*rx_lane_stat_entry;

	bzero(&stats, sizeof (pktsum_t));

	/* Query for dls stats */
	i_query_legacy_stats(linkname, &stats);

	/* Convert to desired data type */
	rx_lane_stat_entry = calloc(1, sizeof (rx_lane_stat_entry_t));
	if (rx_lane_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry->rle_index = DLSTAT_INVALID_ENTRY;
	rx_lane_stat_entry->rle_id = L_SWLANE;

	rx_lane_stat_entry->rle_stats.rl_ipackets = stats.ipackets;
	rx_lane_stat_entry->rle_stats.rl_intrs = stats.ipackets;
	rx_lane_stat_entry->rle_stats.rl_rbytes = stats.rbytes;

	/* Allocate memory for wrapper */
	head = malloc(sizeof (dladm_stat_chain_t));
	if (head == NULL) {
		free(rx_lane_stat_entry);
		goto done;
	}

	head->dc_statentry = rx_lane_stat_entry;
	head->dc_next = NULL;
done:
	return (head);
}

void *
i_dlstat_legacy_tx_lane_stats(const char *linkname)
{
	dladm_stat_chain_t	*head = NULL;
	pktsum_t		stats;
	tx_lane_stat_entry_t	*tx_lane_stat_entry;

	bzero(&stats, sizeof (pktsum_t));

	/* Query for dls stats */
	i_query_legacy_stats(linkname, &stats);

	/* Convert to desired data type */
	tx_lane_stat_entry = calloc(1, sizeof (tx_lane_stat_entry_t));
	if (tx_lane_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry->tle_index = DLSTAT_INVALID_ENTRY;
	tx_lane_stat_entry->tle_id = L_SWLANE;

	tx_lane_stat_entry->tle_stats.tl_opackets = stats.opackets;
	tx_lane_stat_entry->tle_stats.tl_obytes = stats.obytes;

	/* Allocate memory for wrapper */
	head = malloc(sizeof (dladm_stat_chain_t));
	if (head == NULL) {
		free(tx_lane_stat_entry);
		goto done;
	}

	head->dc_statentry = tx_lane_stat_entry;
	head->dc_next = NULL;
done:
	return (head);
}

static void
i_dlstat_get_idlist(dladm_handle_t dh, datalink_id_t linkid,
    dlstat_idlist_type_t idlist_type, uint_t idlist[], uint_t *size)
{
	int	i;

	*size = 0;

	switch (idlist_type) {
	case DLSTAT_RX_RING_IDLIST:
	case DLSTAT_TX_RING_IDLIST: {
		uint_t	nrings;

		dladm_get_ring_counts(dh, linkid,
		    idlist_type == DLSTAT_RX_RING_IDLIST ? &nrings : NULL,
		    idlist_type == DLSTAT_TX_RING_IDLIST ? &nrings : NULL);

		*size = nrings;
		for (i = 0; i < *size; i++)
			idlist[i] = i;
		break;
	}
	case DLSTAT_RX_HWLANE_IDLIST:
	case DLSTAT_TX_HWLANE_IDLIST: {
		if (idlist_type == DLSTAT_RX_HWLANE_IDLIST) {
			dladm_get_ring_index_list(dh, linkid,
			    idlist, size, NULL, NULL);
		} else if (idlist_type == DLSTAT_TX_HWLANE_IDLIST) {
			dladm_get_ring_index_list(dh, linkid,
			    NULL, NULL, idlist, size);
		}
		dladm_sort_index_list(idlist, *size);
		break;
	}
	default:
		*size = 0;
	}
}

static dladm_stat_chain_t *
i_dlstat_query_stats(const char *modname, const char *prefix,
    uint_t idlist[], uint_t idlist_size,
    void * (*fn)(kstat_ctl_t *, kstat_t *, int))
{
	kstat_ctl_t		*kcp;
	kstat_t			*ksp;
	char			statname[MAXLINKNAMELEN];
	int 			i = 0;
	dladm_stat_chain_t 	*head = NULL, *prev = NULL;
	dladm_stat_chain_t	*curr;

	if ((kcp = kstat_open()) == NULL) {
		warn("kstat_open operation failed");
		return (NULL);
	}

	for (i = 0; i < idlist_size; i++) {
		uint_t 	index = idlist[i];

		(void) snprintf(statname, sizeof (statname), "%s%d", prefix,
		    index);

		ksp = dladm_kstat_lookup(kcp, modname, 0, statname, NULL);
		if (ksp == NULL)
			continue;

		curr = malloc(sizeof (dladm_stat_chain_t));
		if (curr == NULL)
			break;

		curr->dc_statentry = fn(kcp, ksp, index);
		if (curr->dc_statentry == NULL) {
			free(curr);
			break;
		}

		(void) strlcpy(curr->dc_statheader, statname,
		    sizeof (curr->dc_statheader));
		curr->dc_next = NULL;

		if (head == NULL)	/* First node */
			head = curr;
		else
			prev->dc_next = curr;

		prev = curr;
	}
done:
	(void) kstat_close(kcp);
	return (head);
}

static link_stat_entry_t *
i_dlstat_link_stats(const char *modname)
{
	kstat_ctl_t		*kcp;
	kstat_t			*ksp;
	link_stat_entry_t 	*link_stat_entry = NULL;

	if ((kcp = kstat_open()) == NULL)
		return (NULL);

	ksp = dladm_kstat_lookup(kcp, modname, 0, DLSTAT_MAC_LINK_STAT, NULL);
	if (ksp == NULL)
		goto done;

	link_stat_entry = calloc(1, sizeof (link_stat_entry_t));
	if (link_stat_entry == NULL)
		goto done;

	i_dlstat_get_stats(kcp, ksp, &link_stat_entry->lse_stats,
	    link_stats_list, LINK_STAT_SIZE);
done:
	(void) kstat_close(kcp);
	return (link_stat_entry);
}

/* Rx lane statistic specific functions */
static boolean_t
i_dlstat_rx_lane_match(void *arg1, void *arg2)
{
	rx_lane_stat_entry_t *s1 = arg1;
	rx_lane_stat_entry_t *s2 = arg2;

	return (s1->rle_index == s2->rle_index &&
	    s1->rle_id == s2->rle_id);
}

static void *
i_dlstat_rx_lane_stat_entry_diff(void *arg1, void *arg2)
{
	rx_lane_stat_entry_t *s1 = arg1;
	rx_lane_stat_entry_t *s2 = arg2;
	rx_lane_stat_entry_t *diff_entry;

	diff_entry = malloc(sizeof (rx_lane_stat_entry_t));
	if (diff_entry == NULL)
		goto done;

	diff_entry->rle_index = s1->rle_index;
	diff_entry->rle_id = s1->rle_id;

	DLSTAT_DIFF_STAT(s1, s2, diff_entry, rle_stats, rx_lane_stats_list,
	    RX_LANE_STAT_SIZE);

done:
	return (diff_entry);
}

static void *
i_dlstat_rx_hwlane_retrieve_stat(kstat_ctl_t *kcp, kstat_t *ksp, int i)
{
	rx_lane_stat_entry_t	*rx_lane_stat_entry;

	rx_lane_stat_entry = calloc(1, sizeof (rx_lane_stat_entry_t));
	if (rx_lane_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry->rle_index = i;
	rx_lane_stat_entry->rle_id = L_HWLANE;

	i_dlstat_get_stats(kcp, ksp, &rx_lane_stat_entry->rle_stats,
	    rx_hwlane_stats_list, RX_HWLANE_STAT_SIZE);

done:
	return (rx_lane_stat_entry);
}

static dladm_stat_chain_t *
i_dlstat_rx_local_stats(const char *modname)
{
	link_stat_entry_t	*link_stat_entry = NULL;
	dladm_stat_chain_t	*local_stats = NULL;
	rx_lane_stat_entry_t	*rx_lane_stat_entry;

	link_stat_entry = i_dlstat_link_stats(modname);
	if (link_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry = calloc(1, sizeof (rx_lane_stat_entry_t));
	if (rx_lane_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry->rle_index = DLSTAT_INVALID_ENTRY;
	rx_lane_stat_entry->rle_id = L_LOCAL;

	rx_lane_stat_entry->rle_stats.rl_ipackets =
	    link_stat_entry->lse_stats.ls_rxlocal;
	rx_lane_stat_entry->rle_stats.rl_rbytes =
	    link_stat_entry->lse_stats.ls_rxlocalbytes;

	local_stats = malloc(sizeof (dladm_stat_chain_t));
	if (local_stats == NULL) {
		free(rx_lane_stat_entry);
		goto done;
	}

	local_stats->dc_statentry = rx_lane_stat_entry;
	local_stats->dc_next = NULL;
	if (local_stats != NULL) {
		(void) strlcpy(local_stats->dc_statheader, "mac_rx_local",
		    sizeof (local_stats->dc_statheader));
	}

done:
	free(link_stat_entry);
	return (local_stats);
}

static dladm_stat_chain_t *
i_dlstat_rx_bcast_stats(const char *modname)
{
	link_stat_entry_t	*link_stat_entry = NULL;
	dladm_stat_chain_t	*head = NULL;
	rx_lane_stat_entry_t	*rx_lane_stat_entry;

	link_stat_entry = i_dlstat_link_stats(modname);
	if (link_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry = calloc(1, sizeof (rx_lane_stat_entry_t));
	if (rx_lane_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry->rle_index = DLSTAT_INVALID_ENTRY;
	rx_lane_stat_entry->rle_id = L_BCAST;

	rx_lane_stat_entry->rle_stats.rl_ipackets =
	    link_stat_entry->lse_stats.ls_brdcstrcv +
	    link_stat_entry->lse_stats.ls_multircv;
	rx_lane_stat_entry->rle_stats.rl_rbytes =
	    link_stat_entry->lse_stats.ls_brdcstrcvbytes +
	    link_stat_entry->lse_stats.ls_multircvbytes;

	head = malloc(sizeof (dladm_stat_chain_t));
	if (head == NULL) {
		free(rx_lane_stat_entry);
		goto done;
	}

	head->dc_statentry = rx_lane_stat_entry;
	head->dc_next = NULL;

done:
	free(link_stat_entry);
	return (head);
}

static dladm_stat_chain_t *
i_dlstat_rx_hwlane_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	uint_t	rx_hwlane_idlist[MAX_RINGS_PER_GROUP];
	uint_t	rx_hwlane_idlist_size;
	char	ulinkname[MAXLINKNAMELEN];

	/*
	 * For every hardware ring dedicated to a datalink, the datalink is said
	 * to have a hardware lane (hwlane). During the lifetime of a device
	 * instance (from device attach till detach), a device ring could be
	 * used by different datalinks (primary, vnics, vlans etc.). However,
	 * hwlane kstats are created only once - when the rings are initialized
	 * by the device attach. Thus, these kstats are *not* recreated even if
	 * a ring is moved from one datalink to another.
	 *
	 * These "persistent" (for the lifetime of device) kstats are named
	 * using the name of the underlying datalink and the id corresponding
	 * to the hardware ring. Thus, hwlane stat query on a datalink needs to
	 * dig in this information before it can issue a kstat query.
	 */

	/* Find name of the underlying link */
	if (!i_dlstat_resolve_kstat_link_names(dh, linkid, ulinkname))
		return (NULL);

	/* Query for ids of dedicated rings for this datalink */
	i_dlstat_get_idlist(dh, linkid, DLSTAT_RX_HWLANE_IDLIST,
	    rx_hwlane_idlist, &rx_hwlane_idlist_size);

	return (i_dlstat_query_stats(ulinkname, DLSTAT_MAC_RX_HWLANE,
	    rx_hwlane_idlist, rx_hwlane_idlist_size,
	    i_dlstat_rx_hwlane_retrieve_stat));
}

/*ARGSUSED*/
static dladm_stat_chain_t *
i_dlstat_rx_swlane_stats(const char *linkname)
{
	link_stat_entry_t	*link_stat_entry = NULL;
	dladm_stat_chain_t	*swlane_stats = NULL;
	rx_lane_stat_entry_t	*rx_lane_stat_entry;

	link_stat_entry = i_dlstat_link_stats(linkname);
	if (link_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry = calloc(1, sizeof (rx_lane_stat_entry_t));
	if (rx_lane_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry->rle_index = DLSTAT_INVALID_ENTRY;
	rx_lane_stat_entry->rle_id = L_SWLANE;

	rx_lane_stat_entry->rle_stats.rl_ipackets =
	    link_stat_entry->lse_stats.ls_intrs +
	    link_stat_entry->lse_stats.ls_polls;

	rx_lane_stat_entry->rle_stats.rl_rbytes =
	    link_stat_entry->lse_stats.ls_intrbytes +
	    link_stat_entry->lse_stats.ls_pollbytes;

	swlane_stats = malloc(sizeof (dladm_stat_chain_t));
	if (swlane_stats == NULL) {
		free(rx_lane_stat_entry);
		goto done;
	}

	swlane_stats->dc_statentry = rx_lane_stat_entry;
	swlane_stats->dc_next = NULL;
	if (swlane_stats != NULL) {
		(void) strlcpy(swlane_stats->dc_statheader, "mac_rx_swlane",
		    sizeof (swlane_stats->dc_statheader));
	}

done:
	free(link_stat_entry);
	return (swlane_stats);
}

void *
dlstat_rx_lane_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	dladm_stat_chain_t	*head = NULL;
	dladm_stat_chain_t 	*local_stats = NULL;
	dladm_stat_chain_t 	*bcast_stats = NULL;
	dladm_stat_chain_t 	*lane_stats = NULL;
	boolean_t		is_legacy_driver;
	char			linkname[MAXLINKNAMELEN];

	if (dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL, linkname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		goto done;
	}

	/* Check if it is legacy driver */
	if (dladm_linkprop_is_set(dh, linkid, DLADM_PROP_VAL_CURRENT,
	    "_softmac", &is_legacy_driver) != DLADM_STATUS_OK) {
		goto done;
	}

	if (is_legacy_driver) {
		head = i_dlstat_legacy_rx_lane_stats(linkname);
		goto done;
	}

	local_stats = i_dlstat_rx_local_stats(linkname);
	bcast_stats = i_dlstat_rx_bcast_stats(linkname);
	lane_stats = i_dlstat_rx_hwlane_stats(dh, linkid);
	if (lane_stats == NULL)
		lane_stats = i_dlstat_rx_swlane_stats(linkname);

	head = i_dlstat_join_lists(local_stats, bcast_stats);
	head = i_dlstat_join_lists(head, lane_stats);
done:
	return (head);
}

/* Tx lane statistic specific functions */
static boolean_t
i_dlstat_tx_lane_match(void *arg1, void *arg2)
{
	tx_lane_stat_entry_t *s1 = arg1;
	tx_lane_stat_entry_t *s2 = arg2;

	return (s1->tle_index == s2->tle_index &&
	    s1->tle_id == s2->tle_id);
}

static void *
i_dlstat_tx_lane_stat_entry_diff(void *arg1, void *arg2)
{
	tx_lane_stat_entry_t *s1 = arg1;
	tx_lane_stat_entry_t *s2 = arg2;
	tx_lane_stat_entry_t *diff_entry;

	diff_entry = malloc(sizeof (tx_lane_stat_entry_t));
	if (diff_entry == NULL)
		goto done;

	diff_entry->tle_index = s1->tle_index;
	diff_entry->tle_id = s1->tle_id;

	DLSTAT_DIFF_STAT(s1, s2, diff_entry, tle_stats, tx_lane_stats_list,
	    TX_LANE_STAT_SIZE);

done:
	return (diff_entry);
}

static void *
i_dlstat_tx_hwlane_retrieve_stat(kstat_ctl_t *kcp, kstat_t *ksp, int i)
{
	tx_lane_stat_entry_t	*tx_lane_stat_entry;

	tx_lane_stat_entry = calloc(1, sizeof (tx_lane_stat_entry_t));
	if (tx_lane_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry->tle_index	= i;
	tx_lane_stat_entry->tle_id	= L_HWLANE;

	i_dlstat_get_stats(kcp, ksp, &tx_lane_stat_entry->tle_stats,
	    tx_lane_stats_list, TX_LANE_STAT_SIZE);

done:
	return (tx_lane_stat_entry);
}

static dladm_stat_chain_t *
i_dlstat_tx_local_stats(const char *modname)
{
	link_stat_entry_t	*link_stat_entry = NULL;
	dladm_stat_chain_t	*local_stats = NULL;
	tx_lane_stat_entry_t	*tx_lane_stat_entry;

	link_stat_entry = i_dlstat_link_stats(modname);
	if (link_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry = calloc(1, sizeof (tx_lane_stat_entry_t));
	if (tx_lane_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry->tle_index = DLSTAT_INVALID_ENTRY;
	tx_lane_stat_entry->tle_id = L_LOCAL;

	tx_lane_stat_entry->tle_stats.tl_opackets =
	    link_stat_entry->lse_stats.ls_txlocal;
	tx_lane_stat_entry->tle_stats.tl_obytes =
	    link_stat_entry->lse_stats.ls_txlocalbytes;

	local_stats = malloc(sizeof (dladm_stat_chain_t));
	if (local_stats == NULL) {
		free(tx_lane_stat_entry);
		goto done;
	}

	local_stats->dc_statentry = tx_lane_stat_entry;
	local_stats->dc_next = NULL;
	if (local_stats != NULL) {
		(void) strlcpy(local_stats->dc_statheader, "mac_tx_local",
		    sizeof (local_stats->dc_statheader));
	}

done:
	free(link_stat_entry);
	return (local_stats);
}

static dladm_stat_chain_t *
i_dlstat_tx_bcast_stats(const char *modname)
{
	link_stat_entry_t	*link_stat_entry = NULL;
	dladm_stat_chain_t	*head = NULL;
	tx_lane_stat_entry_t	*tx_lane_stat_entry;

	link_stat_entry = i_dlstat_link_stats(modname);
	if (link_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry = calloc(1, sizeof (tx_lane_stat_entry_t));
	if (tx_lane_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry->tle_index = DLSTAT_INVALID_ENTRY;
	tx_lane_stat_entry->tle_id = L_BCAST;

	tx_lane_stat_entry->tle_stats.tl_opackets =
	    link_stat_entry->lse_stats.ls_brdcstxmt +
	    link_stat_entry->lse_stats.ls_multixmt;
	tx_lane_stat_entry->tle_stats.tl_obytes =
	    link_stat_entry->lse_stats.ls_brdcstxmtbytes +
	    link_stat_entry->lse_stats.ls_multixmtbytes;

	head = malloc(sizeof (dladm_stat_chain_t));
	if (head == NULL) {
		free(tx_lane_stat_entry);
		goto done;
	}

	head->dc_statentry = tx_lane_stat_entry;
	head->dc_next = NULL;

done:
	free(link_stat_entry);
	return (head);
}

static dladm_stat_chain_t *
i_dlstat_tx_hwlane_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	uint_t	tx_hwlane_idlist[MAX_RINGS_PER_GROUP];
	uint_t	tx_hwlane_idlist_size;
	char	ulinkname[MAXLINKNAMELEN];

	/*
	 * For every hardware ring dedicated to a datalink, the datalink is said
	 * to have a hardware lane (hwlane). During the lifetime of a device
	 * instance (from device attach till detach), a device ring could be
	 * used by different datalinks (primary, vnics, vlans etc.). However,
	 * hwlane kstats are created only once - when the rings are initialized
	 * by the device attach. Thus, these kstats are *not* recreated even if
	 * a ring is moved from one datalink to another.
	 *
	 * These "persistent" (for the lifetime of device) kstats are named
	 * using the name of the underlying datalink and the id corresponding
	 * to the hardware ring. Thus, hwlane stat query on a datalink needs to
	 * dig in this information before it can issue a kstat query.
	 */

	/* Find the name of the underlying link */
	if (!i_dlstat_resolve_kstat_link_names(dh, linkid, ulinkname))
		return (NULL);

	/* Query for ids of dedicated rings for this datalink */
	i_dlstat_get_idlist(dh, linkid, DLSTAT_TX_HWLANE_IDLIST,
	    tx_hwlane_idlist, &tx_hwlane_idlist_size);

	return (i_dlstat_query_stats(ulinkname, DLSTAT_MAC_TX_HWLANE,
	    tx_hwlane_idlist, tx_hwlane_idlist_size,
	    i_dlstat_tx_hwlane_retrieve_stat));
}

/*ARGSUSED*/
static dladm_stat_chain_t *
i_dlstat_tx_swlane_stats(const char *linkname)
{
	link_stat_entry_t	*link_stat_entry = NULL;
	dladm_stat_chain_t	*swlane_stats = NULL;
	tx_lane_stat_entry_t	*tx_lane_stat_entry;

	link_stat_entry = i_dlstat_link_stats(linkname);
	if (link_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry = calloc(1, sizeof (tx_lane_stat_entry_t));
	if (tx_lane_stat_entry == NULL)
		goto done;

	tx_lane_stat_entry->tle_index = DLSTAT_INVALID_ENTRY;
	tx_lane_stat_entry->tle_id = L_SWLANE;

	/*
	 * Swlane stat counts on-wire unicast traffic only. Thus,
	 * swlane = total tx traffic - (local + multicast + broadcast).
	 */
	tx_lane_stat_entry->tle_stats.tl_opackets =
	    link_stat_entry->lse_stats.ls_opackets -
	    (link_stat_entry->lse_stats.ls_txlocal +
	    link_stat_entry->lse_stats.ls_brdcstxmt +
	    link_stat_entry->lse_stats.ls_multixmt);

	tx_lane_stat_entry->tle_stats.tl_obytes =
	    link_stat_entry->lse_stats.ls_obytes -
	    (link_stat_entry->lse_stats.ls_txlocalbytes +
	    link_stat_entry->lse_stats.ls_brdcstxmtbytes +
	    link_stat_entry->lse_stats.ls_multixmtbytes);

	swlane_stats = malloc(sizeof (dladm_stat_chain_t));
	if (swlane_stats == NULL) {
		free(tx_lane_stat_entry);
		goto done;
	}

	swlane_stats->dc_statentry = tx_lane_stat_entry;
	swlane_stats->dc_next = NULL;
	if (swlane_stats != NULL) {
		(void) strlcpy(swlane_stats->dc_statheader, "mac_tx_swlane",
		    sizeof (swlane_stats->dc_statheader));
	}
done:
	free(link_stat_entry);
	return (swlane_stats);
}

void *
dlstat_tx_lane_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	dladm_stat_chain_t	*head = NULL;
	dladm_stat_chain_t 	*local_stats = NULL;
	dladm_stat_chain_t 	*bcast_stats = NULL;
	dladm_stat_chain_t 	*lane_stats = NULL;
	boolean_t		is_legacy_driver;
	char			linkname[MAXLINKNAMELEN];

	if (dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL, linkname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		goto done;
	}

	/* Check if it is legacy driver */
	if (dladm_linkprop_is_set(dh, linkid, DLADM_PROP_VAL_CURRENT,
	    "_softmac", &is_legacy_driver) != DLADM_STATUS_OK) {
		goto done;
	}

	if (is_legacy_driver) {
		head = i_dlstat_legacy_tx_lane_stats(linkname);
		goto done;
	}

	local_stats = i_dlstat_tx_local_stats(linkname);
	bcast_stats = i_dlstat_tx_bcast_stats(linkname);
	lane_stats = i_dlstat_tx_hwlane_stats(dh, linkid);
	if (lane_stats == NULL)
		lane_stats = i_dlstat_tx_swlane_stats(linkname);

	head = i_dlstat_join_lists(local_stats, bcast_stats);
	head = i_dlstat_join_lists(head, lane_stats);
done:
	return (head);
}

/* Rx lane total statistic specific functions */
void *
dlstat_rx_lane_total_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	dladm_stat_chain_t	*total_head = NULL;
	link_stat_entry_t	*link_stat_entry = NULL;
	rx_lane_stat_entry_t	*total_stats;
	char 			linkname[MAXLINKNAMELEN];
	boolean_t		is_legacy_driver;

	/* Query per mcip kstats (don't need to look at the underlying link) */
	if (dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL, linkname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		goto done;
	}

	if (dladm_linkprop_is_set(dh, linkid, DLADM_PROP_VAL_CURRENT,
	    "_softmac", &is_legacy_driver) != DLADM_STATUS_OK) {
		goto done;
	}

	if (is_legacy_driver) {
		total_head = i_dlstat_legacy_rx_lane_stats(linkname);
		goto done;
	}

	link_stat_entry = i_dlstat_link_stats(linkname);

	if (link_stat_entry == NULL)
		goto done;

	total_stats = calloc(1, sizeof (rx_lane_stat_entry_t));
	if (total_stats == NULL)
		goto done;

	total_stats->rle_index = DLSTAT_INVALID_ENTRY;
	total_stats->rle_id = DLSTAT_INVALID_ENTRY;

	total_stats->rle_stats.rl_ipackets =
	    link_stat_entry->lse_stats.ls_ipackets;
	total_stats->rle_stats.rl_rbytes =
	    link_stat_entry->lse_stats.ls_rbytes;

	total_stats->rle_stats.rl_intrs =
	    link_stat_entry->lse_stats.ls_intrs;
	total_stats->rle_stats.rl_intrbytes =
	    link_stat_entry->lse_stats.ls_intrbytes;

	total_stats->rle_stats.rl_polls =
	    link_stat_entry->lse_stats.ls_polls;
	total_stats->rle_stats.rl_pollbytes =
	    link_stat_entry->lse_stats.ls_pollbytes;

	total_stats->rle_stats.rl_idrops =
	    link_stat_entry->lse_stats.ls_idrops;
	total_stats->rle_stats.rl_idropbytes =
	    link_stat_entry->lse_stats.ls_idropbytes;

	total_head = malloc(sizeof (dladm_stat_chain_t));
	if (total_head == NULL) {
		free(total_stats);
		goto done;
	}

	total_head->dc_statentry = total_stats;
	total_head->dc_next = NULL;
	if (total_stats != NULL) {
		(void) strlcpy(total_head->dc_statheader, "mac_rx_lane_total",
		    sizeof (total_head->dc_statheader));
	}

done:
	free(link_stat_entry);
	return (total_head);
}

/* Tx lane total statistic specific functions */
void *
dlstat_tx_lane_total_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	dladm_stat_chain_t	*total_head = NULL;
	link_stat_entry_t	*link_stat_entry = NULL;
	tx_lane_stat_entry_t	*total_stats;
	char 			linkname[MAXLINKNAMELEN];
	boolean_t		is_legacy_driver;

	/* Query per mcip kstats (don't need to look at the underlying link) */
	if (dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL, linkname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		return (NULL);
	}

	if (dladm_linkprop_is_set(dh, linkid, DLADM_PROP_VAL_CURRENT,
	    "_softmac", &is_legacy_driver) != DLADM_STATUS_OK) {
		goto done;
	}

	if (is_legacy_driver) {
		total_head = i_dlstat_legacy_tx_lane_stats(linkname);
		goto done;
	}

	link_stat_entry = i_dlstat_link_stats(linkname);

	if (link_stat_entry == NULL)
		goto done;

	total_stats = calloc(1, sizeof (tx_lane_stat_entry_t));
	if (total_stats == NULL)
		goto done;

	total_stats->tle_index = DLSTAT_INVALID_ENTRY;
	total_stats->tle_id = DLSTAT_INVALID_ENTRY;

	total_stats->tle_stats.tl_opackets =
	    link_stat_entry->lse_stats.ls_opackets;
	total_stats->tle_stats.tl_obytes =
	    link_stat_entry->lse_stats.ls_obytes;

	total_stats->tle_stats.tl_odrops =
	    link_stat_entry->lse_stats.ls_odrops;
	total_stats->tle_stats.tl_odropbytes =
	    link_stat_entry->lse_stats.ls_odropbytes;

	total_head = malloc(sizeof (dladm_stat_chain_t));
	if (total_head == NULL) {
		free(total_stats);
		goto done;
	}

	total_head->dc_statentry = total_stats;
	total_head->dc_next = NULL;
	if (total_stats != NULL) {
		(void) strlcpy(total_head->dc_statheader, "mac_tx_lane_total",
		    sizeof (total_head->dc_statheader));
	}

done:
	free(link_stat_entry);
	return (total_head);
}

/* Rx ring statistic specific functions */
static boolean_t
i_dlstat_rx_ring_match(void *arg1, void *arg2)
{
	rx_lane_stat_entry_t	*s1 = arg1;
	rx_lane_stat_entry_t	*s2 = arg2;

	return (s1->rle_index == s2->rle_index);
}

static void *
i_dlstat_rx_ring_stat_entry_diff(void *arg1, void *arg2)
{
	ring_stat_entry_t 	*s1 = arg1;
	ring_stat_entry_t 	*s2 = arg2;
	ring_stat_entry_t 	*diff_entry;

	diff_entry = malloc(sizeof (ring_stat_entry_t));
	if (diff_entry == NULL)
		goto done;

	diff_entry->re_index	= s1->re_index;

	DLSTAT_DIFF_STAT(s1, s2, diff_entry, re_stats, rx_ring_stats_list,
	    RX_RING_STAT_SIZE);

done:
	return (diff_entry);
}

static void *
i_dlstat_rx_ring_retrieve_stat(kstat_ctl_t *kcp, kstat_t *ksp, int i)
{
	ring_stat_entry_t	*rx_ring_stat_entry;

	rx_ring_stat_entry = calloc(1, sizeof (ring_stat_entry_t));
	if (rx_ring_stat_entry == NULL)
		goto done;

	rx_ring_stat_entry->re_index	= i;

	i_dlstat_get_stats(kcp, ksp, &rx_ring_stat_entry->re_stats,
	    rx_ring_stats_list, RX_RING_STAT_SIZE);

done:
	return (rx_ring_stat_entry);
}

void *
dlstat_rx_ring_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	uint_t	rx_ring_idlist[MAX_RINGS_PER_GROUP];
	uint_t	rx_ring_idlist_size;
	char	modname[MAXLINKNAMELEN];

	if (!i_dlstat_resolve_kstat_phys_names(dh, linkid, modname))
		return (NULL);

	i_dlstat_get_idlist(dh, linkid, DLSTAT_RX_RING_IDLIST,
	    rx_ring_idlist, &rx_ring_idlist_size);

	return (i_dlstat_query_stats(modname, DLSTAT_MAC_RX_RING,
	    rx_ring_idlist, rx_ring_idlist_size,
	    i_dlstat_rx_ring_retrieve_stat));
}

/* Tx ring statistic specific functions */
static boolean_t
i_dlstat_tx_ring_match(void *arg1, void *arg2)
{
	tx_lane_stat_entry_t	*s1 = arg1;
	tx_lane_stat_entry_t	*s2 = arg2;

	return (s1->tle_index == s2->tle_index);
}

static void *
i_dlstat_tx_ring_stat_entry_diff(void *arg1, void *arg2)
{
	ring_stat_entry_t	*s1 = arg1;
	ring_stat_entry_t	*s2 = arg2;
	ring_stat_entry_t	*diff_entry;

	diff_entry = malloc(sizeof (ring_stat_entry_t));
	if (diff_entry == NULL)
		goto done;

	diff_entry->re_index	= s1->re_index;

	DLSTAT_DIFF_STAT(s1, s2, diff_entry, re_stats, tx_ring_stats_list,
	    TX_RING_STAT_SIZE);

done:
	return (diff_entry);
}

static void *
i_dlstat_tx_ring_retrieve_stat(kstat_ctl_t *kcp, kstat_t *ksp, int i)
{
	ring_stat_entry_t	*tx_ring_stat_entry;

	tx_ring_stat_entry = calloc(1, sizeof (ring_stat_entry_t));
	if (tx_ring_stat_entry == NULL)
		goto done;

	tx_ring_stat_entry->re_index	= i;

	i_dlstat_get_stats(kcp, ksp, &tx_ring_stat_entry->re_stats,
	    tx_ring_stats_list, TX_RING_STAT_SIZE);

done:
	return (tx_ring_stat_entry);
}

void *
dlstat_tx_ring_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	uint_t	tx_ring_idlist[MAX_RINGS_PER_GROUP];
	uint_t	tx_ring_idlist_size;
	char	modname[MAXLINKNAMELEN];

	if (!i_dlstat_resolve_kstat_phys_names(dh, linkid, modname))
		return (NULL);

	i_dlstat_get_idlist(dh, linkid, DLSTAT_TX_RING_IDLIST,
	    tx_ring_idlist, &tx_ring_idlist_size);

	return (i_dlstat_query_stats(modname, DLSTAT_MAC_TX_RING,
	    tx_ring_idlist, tx_ring_idlist_size,
	    i_dlstat_tx_ring_retrieve_stat));
}

static total_stat_entry_t *
i_dlstat_ring_stats(const char *linkname)
{
	kstat_ctl_t		*kcp;
	kstat_t			*ksp;
	total_stat_entry_t	*total_stat_entry = NULL;
	char			module[DLPI_LINKNAME_MAX];
	uint_t			instance;

	if (!dlparse_drvppa(linkname, module, sizeof (module), &instance))
		goto done;

	if ((kcp = kstat_open()) == NULL) {
		warn("kstat open operation failed");
		return (NULL);
	}

	ksp = dladm_kstat_lookup(kcp, module, instance, DLSTAT_MAC_PHYS_STAT,
	    NULL);
	if (ksp == NULL)
		goto done;

	total_stat_entry = calloc(1, sizeof (total_stat_entry_t));
	if (total_stat_entry == NULL)
		goto done;

	i_dlstat_get_stats(kcp, ksp, &total_stat_entry->tse_stats,
	    total_stats_list, TOTAL_STAT_SIZE);
done:
	(void) kstat_close(kcp);
	return (total_stat_entry);
}

static void *
i_dlstat_ring_total_stats(dladm_handle_t dh, datalink_id_t linkid,
    dladm_stat_type_t stattype)
{
	dladm_stat_chain_t	*stat_head = NULL;
	total_stat_entry_t	*total_stats = NULL;
	ring_stat_entry_t	*ring_stats;
	dladm_phys_attr_t	dpa;
	char			linkname[MAXLINKNAMELEN];
	char			*modname;
	datalink_class_t	class;
	char			*statheader;

	/*
	 * kstats corresponding to physical device rings continue to use
	 * device names even if the link is renamed using dladm rename-link.
	 * Thus, given a linkid, we lookup the physical device name.
	 * However, if an aggr is renamed, kstats corresponding to its
	 * pseudo rings are renamed as well.
	 */
	if (dladm_datalink_id2info(dh, linkid, NULL, &class, NULL, linkname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		return (NULL);
	}

	if (class != DATALINK_CLASS_AGGR) {
		if (dladm_phys_info(dh, linkid, &dpa, DLADM_OPT_ACTIVE) !=
		    DLADM_STATUS_OK) {
			return (NULL);
		}
		modname = dpa.dp_dev;
	} else
		modname = linkname;

	total_stats = i_dlstat_ring_stats(modname);
	if (total_stats == NULL)
		goto done;

	ring_stats = calloc(1, sizeof (ring_stat_entry_t));
	if (ring_stats == NULL)
		goto done;

	ring_stats->re_index = DLSTAT_INVALID_ENTRY;

	if (stattype == DLADM_STAT_RX_RING_TOTAL) {
		ring_stats->re_stats.r_packets =
		    total_stats->tse_stats.ts_ipackets;
		ring_stats->re_stats.r_bytes = total_stats->tse_stats.ts_rbytes;
	} else if (stattype == DLADM_STAT_TX_RING_TOTAL) {
		ring_stats->re_stats.r_packets =
		    total_stats->tse_stats.ts_opackets;
		ring_stats->re_stats.r_bytes = total_stats->tse_stats.ts_obytes;
	}

	stat_head = malloc(sizeof (dladm_stat_chain_t));
	if (stat_head == NULL) {
		free(total_stats);
		goto done;
	}

	stat_head->dc_statentry = ring_stats;
	stat_head->dc_next = NULL;
	if (ring_stats != NULL) {
		statheader = (stattype == DLADM_STAT_RX_RING_TOTAL) ?
		    "mac_rx_ring_total" : "mac_tx_ring_total";

		(void) strlcpy(stat_head->dc_statheader, statheader,
		    sizeof (stat_head->dc_statheader));
	}

done:
	free(total_stats);
	return (stat_head);
}

/* Rx ring total statistic specific functions */
void *
dlstat_rx_ring_total_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	return (i_dlstat_ring_total_stats(dh, linkid,
	    DLADM_STAT_RX_RING_TOTAL));
}

/* Tx ring total statistic specific functions */
void *
dlstat_tx_ring_total_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	return (i_dlstat_ring_total_stats(dh, linkid,
	    DLADM_STAT_TX_RING_TOTAL));
}

/* Summary statistic specific functions */
/*ARGSUSED*/
static boolean_t
i_dlstat_total_match(void *arg1, void *arg2)
{					/* Always single entry for total */
	return (B_TRUE);
}

static void *
i_dlstat_total_stat_entry_diff(void *arg1, void *arg2)
{
	total_stat_entry_t	*s1 = arg1;
	total_stat_entry_t	*s2 = arg2;
	total_stat_entry_t	*diff_entry;

	diff_entry = malloc(sizeof (total_stat_entry_t));
	if (diff_entry == NULL)
		goto done;

	DLSTAT_DIFF_STAT(s1, s2, diff_entry, tse_stats, total_stats_list,
	    TOTAL_STAT_SIZE);

done:
	return (diff_entry);
}

void *
dlstat_total_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	dladm_stat_chain_t	*head = NULL;
	dladm_stat_chain_t	*rx_total = NULL;
	dladm_stat_chain_t	*tx_total = NULL;
	total_stat_entry_t	*total_stat_entry;
	rx_lane_stat_entry_t	*rx_lane_stat_entry;
	tx_lane_stat_entry_t	*tx_lane_stat_entry;

	/* Get total rx lane stats */
	rx_total = dlstat_rx_lane_total_stats(dh, linkid);
	if (rx_total == NULL)
		goto done;

	/* Get total tx lane stats */
	tx_total = dlstat_tx_lane_total_stats(dh, linkid);
	if (tx_total == NULL)
		goto done;

	/* Build total stat */
	total_stat_entry = calloc(1, sizeof (total_stat_entry_t));
	if (total_stat_entry == NULL)
		goto done;

	rx_lane_stat_entry = rx_total->dc_statentry;
	tx_lane_stat_entry = tx_total->dc_statentry;

	/* Extract total rx ipackets, rbytes */
	total_stat_entry->tse_stats.ts_ipackets =
	    rx_lane_stat_entry->rle_stats.rl_ipackets;
	total_stat_entry->tse_stats.ts_rbytes =
	    rx_lane_stat_entry->rle_stats.rl_rbytes;

	/* Extract total tx opackets, obytes */
	total_stat_entry->tse_stats.ts_opackets =
	    tx_lane_stat_entry->tle_stats.tl_opackets;
	total_stat_entry->tse_stats.ts_obytes =
	    tx_lane_stat_entry->tle_stats.tl_obytes;

	head = malloc(sizeof (dladm_stat_chain_t));
	if (head == NULL) {
		free(total_stat_entry);
		goto done;
	}

	head->dc_statentry = total_stat_entry;
	(void) strlcpy(head->dc_statheader, "mac_lane_total",
	    sizeof (head->dc_statheader));
	head->dc_next = NULL;

done:
	dladm_link_stat_free(rx_total);
	dladm_link_stat_free(tx_total);
	return (head);
}

/* Aggr total statistic(summed across all component ports) specific functions */
void *
dlstat_aggr_total_stats(dladm_stat_chain_t *head)
{
	dladm_stat_chain_t	*curr;
	dladm_stat_chain_t	*total_head;
	aggr_port_stat_entry_t	*total_stats;

	total_stats = calloc(1, sizeof (aggr_port_stat_entry_t));
	if (total_stats == NULL)
		goto done;

	total_stats->ape_portlinkid = DATALINK_INVALID_LINKID;

	for (curr = head; curr != NULL; curr = curr->dc_next) {
		aggr_port_stat_entry_t	*curr_aggr_port_stats;

		curr_aggr_port_stats = curr->dc_statentry;

		i_dlstat_sum_stats(&total_stats->ape_stats,
		    &curr_aggr_port_stats->ape_stats, &total_stats->ape_stats,
		    aggr_port_stats_list, AGGR_PORT_STAT_SIZE);
	}

	total_head = malloc(sizeof (dladm_stat_chain_t));
	if (total_head == NULL) {
		free(total_stats);
		goto done;
	}

	total_head->dc_statentry = total_stats;
	total_head->dc_next = NULL;

done:
	return (total_head);
}

/* Aggr port statistic specific functions */
static boolean_t
i_dlstat_aggr_port_match(void *arg1, void *arg2)
{
	aggr_port_stat_entry_t *s1 = arg1;
	aggr_port_stat_entry_t *s2 = arg2;

	return (s1->ape_portlinkid == s2->ape_portlinkid);
}

static void *
i_dlstat_aggr_port_stat_entry_diff(void *arg1, void *arg2)
{
	aggr_port_stat_entry_t	*s1 = arg1;
	aggr_port_stat_entry_t	*s2 = arg2;
	aggr_port_stat_entry_t	*diff_entry;

	diff_entry = malloc(sizeof (aggr_port_stat_entry_t));
	if (diff_entry == NULL)
		goto done;

	diff_entry->ape_portlinkid = s1->ape_portlinkid;

	DLSTAT_DIFF_STAT(s1, s2, diff_entry, ape_stats, aggr_port_stats_list,
	    AGGR_PORT_STAT_SIZE);

done:
	return (diff_entry);
}

/*
 * Query dls stats for the aggr port. This results in query for stats into
 * the corresponding device driver.
 */
static aggr_port_stat_entry_t *
i_dlstat_single_port_stats(const char *portname, datalink_id_t linkid)
{
	kstat_ctl_t		*kcp;
	kstat_t			*ksp;
	char			module[DLPI_LINKNAME_MAX];
	uint_t			instance;
	aggr_port_stat_entry_t	*aggr_port_stat_entry = NULL;

	if (!dlparse_drvppa(portname, module, sizeof (module), &instance))
		goto done;

	if ((kcp = kstat_open()) == NULL) {
		warn("kstat open operation failed");
		return (NULL);
	}

	ksp = dladm_kstat_lookup(kcp, module, instance, DLSTAT_MAC_PHYS_STAT,
	    NULL);
	if (ksp == NULL)
		goto done;

	aggr_port_stat_entry = calloc(1, sizeof (aggr_port_stat_entry_t));
	if (aggr_port_stat_entry == NULL)
		goto done;

	/* Save port's linkid */
	aggr_port_stat_entry->ape_portlinkid = linkid;

	i_dlstat_get_stats(kcp, ksp, &aggr_port_stat_entry->ape_stats,
	    aggr_port_stats_list, AGGR_PORT_STAT_SIZE);
done:
	(void) kstat_close(kcp);
	return (aggr_port_stat_entry);
}

void *
dlstat_aggr_port_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	dladm_aggr_grp_attr_t	ginfo;
	int			i;
	dladm_aggr_port_attr_t	 *portp;
	dladm_phys_attr_t	dpa;
	aggr_port_stat_entry_t	*aggr_port_stat_entry;
	dladm_stat_chain_t	*head = NULL, *prev = NULL, *curr;
	dladm_stat_chain_t	*total_stats;

	/* Get aggr info */
	bzero(&ginfo, sizeof (dladm_aggr_grp_attr_t));
	if (dladm_aggr_info(dh, linkid, &ginfo, DLADM_OPT_ACTIVE)
	    != DLADM_STATUS_OK)
		goto done;
	/* For every port that is member of this aggr do */
	for (i = 0; i < ginfo.lg_nports; i++) {
		portp = &(ginfo.lg_ports[i]);
		if (dladm_phys_info(dh, portp->lp_linkid, &dpa,
		    DLADM_OPT_ACTIVE) != DLADM_STATUS_OK) {
			goto done;
		}

		aggr_port_stat_entry = i_dlstat_single_port_stats(dpa.dp_dev,
		    portp->lp_linkid);

		/* Create dladm_stat_chain_t object for this stat */
		curr = malloc(sizeof (dladm_stat_chain_t));
		if (curr == NULL) {
			free(aggr_port_stat_entry);
			goto done;
		}
		(void) strlcpy(curr->dc_statheader, dpa.dp_dev,
		    sizeof (curr->dc_statheader));
		curr->dc_statentry = aggr_port_stat_entry;
		curr->dc_next = NULL;

		/* Chain this aggr port stat entry */
		/* head of the stat list */
		if (prev == NULL)
			head = curr;
		else
			prev->dc_next = curr;
		prev = curr;
	}

	/*
	 * Prepend the stat list with cumulative aggr stats i.e. summed over all
	 * component ports
	 */
	total_stats = dlstat_aggr_total_stats(head);
	if (total_stats != NULL) {
		total_stats->dc_next = head;
		head = total_stats;
	}

done:
	free(ginfo.lg_ports);
	return (head);
}

/* Link stat specific functions */
void *
dlstat_link_stats(dladm_handle_t dh, datalink_id_t linkid)
{
	link_stat_entry_t	*link_stat_entry;
	dladm_stat_chain_t	*head = NULL;
	char 			linkname[MAXLINKNAMELEN];

	if (dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL, linkname,
	    DLPI_LINKNAME_MAX) != DLADM_STATUS_OK) {
		goto done;
	}

	link_stat_entry = i_dlstat_link_stats(linkname);
	if (link_stat_entry == NULL)
		goto done;

	head = malloc(sizeof (dladm_stat_chain_t));
	if (head == NULL) {
		free(link_stat_entry);
		goto done;
	}

	head->dc_statentry = link_stat_entry;
	(void) strlcpy(head->dc_statheader, "mac_misc_stat",
	    sizeof (head->dc_statheader));
	head->dc_next = NULL;

done:
	return (head);
}

/* Exported functions */
dladm_stat_chain_t *
dladm_link_stat_query(dladm_handle_t dh, datalink_id_t linkid,
    dladm_stat_type_t stattype)
{
	return (dladm_stat_table[stattype].ds_querystat(dh, linkid));
}

dladm_stat_chain_t *
dladm_link_stat_diffchain(dladm_stat_chain_t *op1, dladm_stat_chain_t *op2,
    dladm_stat_type_t stattype)
{
	dladm_stat_chain_t	*op1_curr, *op2_curr;
	dladm_stat_chain_t	*diff_curr;
	dladm_stat_chain_t	*diff_prev = NULL, *diff_head = NULL;

				/* Perform op1 - op2, store result in diff */
	for (op1_curr = op1; op1_curr != NULL; op1_curr = op1_curr->dc_next) {
		for (op2_curr = op2; op2_curr != NULL;
		    op2_curr = op2_curr->dc_next) {
			if (dlstat_match_stats(op1_curr->dc_statentry,
			    op2_curr->dc_statentry, stattype)) {
				break;
			}
		}
		diff_curr = malloc(sizeof (dladm_stat_chain_t));
		if (diff_curr == NULL)
			goto done;

		diff_curr->dc_next = NULL;

		if (op2_curr == NULL) {
			/* prev iteration did not have this stat entry */
			diff_curr->dc_statentry =
			    dlstat_diff_stats(op1_curr->dc_statentry,
			    NULL, stattype);
		} else {
			diff_curr->dc_statentry =
			    dlstat_diff_stats(op1_curr->dc_statentry,
			    op2_curr->dc_statentry, stattype);
		}

		if (diff_curr->dc_statentry == NULL) {
			free(diff_curr);
			goto done;
		}

		if (diff_prev == NULL) /* head of the diff stat list */
			diff_head = diff_curr;
		else
			diff_prev->dc_next = diff_curr;
		diff_prev = diff_curr;
	}
done:
	return (diff_head);
}

void
dladm_link_stat_free(dladm_stat_chain_t *curr)
{
	while (curr != NULL) {
		dladm_stat_chain_t	*tofree = curr;

		curr = curr->dc_next;
		free(tofree->dc_statentry);
		free(tofree);
	}
}

/* Query all link stats */
static name_value_stat_t *
i_dlstat_convert_stats(void *stats, stat_info_t stats_list[], uint_t size)
{
	int			i;
	name_value_stat_t	*head_stat = NULL, *prev_stat = NULL;
	name_value_stat_t	*curr_stat;

	for (i = 0; i < size; i++) {
		uint64_t *val = (void *)
		    ((uchar_t *)stats + stats_list[i].si_offset);

		curr_stat = calloc(1, sizeof (name_value_stat_t));
		if (curr_stat == NULL)
			break;

		(void) strlcpy(curr_stat->nv_statname, stats_list[i].si_name,
		    sizeof (curr_stat->nv_statname));
		curr_stat->nv_statval = *val;
		curr_stat->nv_nextstat = NULL;

		if (head_stat == NULL)	/* First node */
			head_stat = curr_stat;
		else
			prev_stat->nv_nextstat = curr_stat;

		prev_stat = curr_stat;
	}
	return (head_stat);
}

void *
build_nvs_entry(char *statheader, void *statentry, dladm_stat_type_t stattype)
{
	name_value_stat_entry_t	*name_value_stat_entry;
	dladm_stat_desc_t	*stattbl_ptr;
	void			*statfields;

	stattbl_ptr = &dladm_stat_table[stattype];

	/* Allocate memory for query all stat entry */
	name_value_stat_entry = calloc(1, sizeof (name_value_stat_entry_t));
	if (name_value_stat_entry == NULL)
		goto done;

	/* Header for these stat fields */
	(void) strlcpy(name_value_stat_entry->nve_header, statheader,
	    sizeof (name_value_stat_entry->nve_header));

	/* Extract stat fields from the statentry */
	statfields = (uchar_t *)statentry +
	    dladm_stat_table[stattype].ds_offset;

	/* Convert curr_stat to <statname, statval> pair */
	name_value_stat_entry->nve_stats =
	    i_dlstat_convert_stats(statfields,
	    stattbl_ptr->ds_statlist, stattbl_ptr->ds_statsize);
done:
	return (name_value_stat_entry);
}

void *
i_walk_dlstat_chain(dladm_stat_chain_t *stat_head, dladm_stat_type_t stattype)
{
	dladm_stat_chain_t	*curr;
	dladm_stat_chain_t	*nvstat_head = NULL, *nvstat_prev = NULL;
	dladm_stat_chain_t	*nvstat_curr;

	/*
	 * For every stat in the chain, build header and convert all
	 * its stat fields
	 */
	for (curr = stat_head; curr != NULL; curr = curr->dc_next) {
		nvstat_curr = malloc(sizeof (dladm_stat_chain_t));
		if (nvstat_curr == NULL)
			break;

		nvstat_curr->dc_statentry = build_nvs_entry(curr->dc_statheader,
		    curr->dc_statentry, stattype);

		if (nvstat_curr->dc_statentry == NULL) {
			free(nvstat_curr);
			break;
		}

		nvstat_curr->dc_next = NULL;

		if (nvstat_head == NULL)	/* First node */
			nvstat_head = nvstat_curr;
		else
			nvstat_prev->dc_next = nvstat_curr;

		nvstat_prev = nvstat_curr;
	}
done:
	return (nvstat_head);
}

dladm_stat_chain_t *
dladm_link_stat_query_all(dladm_handle_t dh, datalink_id_t linkid,
    dladm_stat_type_t stattype)
{
	dladm_stat_chain_t	*stat_head;
	dladm_stat_chain_t	*nvstat_head = NULL;

	/* Query the requested stat */
	stat_head = dladm_link_stat_query(dh, linkid, stattype);
	if (stat_head == NULL)
		goto done;

	/*
	 * Convert every statfield in every stat-entry of stat chain to
	 * <statname, statval> pair
	 */
	nvstat_head = i_walk_dlstat_chain(stat_head, stattype);

	/* Free stat_head */
	dladm_link_stat_free(stat_head);

done:
	return (nvstat_head);
}

void
dladm_link_stat_query_all_free(dladm_stat_chain_t *curr)
{
	while (curr != NULL) {
		dladm_stat_chain_t	*tofree = curr;
		name_value_stat_entry_t	*nv_entry = curr->dc_statentry;
		name_value_stat_t	*nv_curr = nv_entry->nve_stats;

		while (nv_curr != NULL) {
			name_value_stat_t	*nv_tofree = nv_curr;

			nv_curr = nv_curr->nv_nextstat;
			free(nv_tofree);
		}

		curr = curr->dc_next;
		free(nv_entry);
		free(tofree);
	}
}

/* flow stats specific routines */
flow_stat_t *
dladm_flow_stat_query(const char *flowname)
{
	kstat_ctl_t	*kcp;
	kstat_t		*ksp;
	flow_stat_t	*flow_stat = NULL;

	if ((kcp = kstat_open()) == NULL)
		return (NULL);

	flow_stat = calloc(1, sizeof (flow_stat_t));
	if (flow_stat == NULL)
		goto done;

	ksp = dladm_kstat_lookup(kcp, NULL, -1, flowname, "flow");

	if (ksp != NULL) {
		i_dlstat_get_stats(kcp, ksp, flow_stat, flow_stats_list,
		    FLOW_STAT_SIZE);
	}

done:
	(void) kstat_close(kcp);
	return (flow_stat);
}

flow_stat_t *
dladm_flow_stat_diff(flow_stat_t *op1, flow_stat_t *op2)
{
	flow_stat_t	*diff_stat;

	diff_stat = calloc(1, sizeof (flow_stat_t));
	if (diff_stat == NULL)
		goto done;

	if (op2 == NULL) {
		bcopy(op1, diff_stat, sizeof (flow_stat_t));
	} else {
		i_dlstat_diff_stats(diff_stat, op1, op2, flow_stats_list,
		    FLOW_STAT_SIZE);
	}
done:
	return (diff_stat);
}

void
dladm_flow_stat_free(flow_stat_t *curr)
{
	free(curr);
}

/* Query all flow stats */
name_value_stat_entry_t *
dladm_flow_stat_query_all(const char *flowname)
{
	flow_stat_t		*flow_stat;
	name_value_stat_entry_t	*name_value_stat_entry = NULL;

	/* Query flow stats */
	flow_stat =  dladm_flow_stat_query(flowname);
	if (flow_stat == NULL)
		goto done;

	/* Allocate memory for query all stat entry */
	name_value_stat_entry = calloc(1, sizeof (name_value_stat_entry_t));
	if (name_value_stat_entry == NULL) {
		dladm_flow_stat_free(flow_stat);
		goto done;
	}

	/* Header for these stat fields */
	(void) strncpy(name_value_stat_entry->nve_header, flowname,
	    MAXFLOWNAMELEN);

	/* Convert every statfield in flow_stat to <statname, statval> pair */
	name_value_stat_entry->nve_stats =
	    i_dlstat_convert_stats(flow_stat, flow_stats_list, FLOW_STAT_SIZE);

	/* Free flow_stat */
	dladm_flow_stat_free(flow_stat);

done:
	return (name_value_stat_entry);
}

void
dladm_flow_stat_query_all_free(name_value_stat_entry_t *curr)
{
	name_value_stat_t	*nv_curr = curr->nve_stats;

	while (nv_curr != NULL) {
		name_value_stat_t	*nv_tofree = nv_curr;

		nv_curr = nv_curr->nv_nextstat;
		free(nv_tofree);
	}
}
