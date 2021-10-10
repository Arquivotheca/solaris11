/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include <poll.h>
#include <libintl.h>
#include <sys/dlpi.h>
#include <libdlpi.h>
#include <locale.h>
#include <llc2.h>
#include <ild.h>
#include "llc2_conf.h"

/* Also used in llc2_conf.c */
int debug = 0;

static int llc2_plumb_all(void);
static int llc2_plumb_ppa(FILE *fp, int ppa, int llc2_fd);
static int llc2_unplumb_all(void);
static int llc2_unplumb_ppa(int ppa, int llc2_fd);
static int llc2_init_ppa(int ppa, int llc2_fd);
static int llc2_uninit_ppa(int ppa, int llc2_fd);
static int llc2_query(void);
static int get_device_info(int ppa, int llc2_fd);

/* Timeout value of ioctl().  Unit is s. */
#define	IOCTL_TIMEOUT	10

/* Timeout value of poll().  Unit is ms. */
#define	POLL_TIMEOUT	10000

/* Sleep time before unplumbing a PPA.  Unit is s. */
#define	SLEEP_TIME	1

/* Options of the command. */
#define	PLUMB_ALL_OPT	'P'
#define	UNPLUMB_ALL_OPT	'U'
#define	PLUMB_ONE_OPT	'p'
#define	UNPLUMB_ONE_OPT	'u'
#define	DEBUG_OPT	'd'
#define	INIT_OPT	'i'
#define	UNINIT_OPT	'r'
#define	QUERY_OPT	'q'
#define	VERBOSE_OPT	'v'

static void
Usage(char *argv[])
{
	(void) fprintf(stderr, gettext("Usage: %s [-P|-U|-p <ppa>|-u <ppa>|"
	    "-q]\n"), argv[0]);
}

int
main(int argc, char *argv[])
{
	int c, llc2_fd;
	int op = 0;
	int ppa;
	FILE *fp;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "PUdqp:u:i:r:")) != EOF) {
		switch (c) {
		case DEBUG_OPT:
			debug++;
			break;
		case PLUMB_ALL_OPT:
			if (op != 0) {
				goto main_usage;
			}
			op = PLUMB_ALL_OPT;
			break;
		case UNPLUMB_ALL_OPT:
			if (op != 0) {
				goto main_usage;
			}
			op = UNPLUMB_ALL_OPT;
			break;
		case PLUMB_ONE_OPT:
			if (op != 0) {
				goto main_usage;
			}
			ppa = atoi(optarg);
			op = PLUMB_ONE_OPT;
			break;
		case UNPLUMB_ONE_OPT:
			if (op != 0) {
				goto main_usage;
			}
			ppa = atoi(optarg);
			op = UNPLUMB_ONE_OPT;
			break;
		case INIT_OPT:
			if (op != 0) {
				goto main_usage;
			}
			ppa = atoi(optarg);
			op = INIT_OPT;
			break;
		case UNINIT_OPT:
			if (op != 0) {
				goto main_usage;
			}
			ppa = atoi(optarg);
			op = UNINIT_OPT;
			break;
		case QUERY_OPT:
			if (op != 0) {
				goto main_usage;
			}
			op = QUERY_OPT;
			break;
		default:
main_usage:
			Usage(argv);
			return (0);
		}
	}
	switch (op) {
	case PLUMB_ALL_OPT:
		if (add_conf() == LLC2_FAIL) {
			(void) fprintf(stderr, gettext("Error opening LLC2"
			    " configuration files: %s\n"),
			    strerror(errno));
			return (-1);
		}
		if (debug > 0) {
			(void) fprintf(stderr, "\nExisting config"
			    " files found:\n");
			print_conf_entry();
		}
		return (llc2_plumb_all());
		break;
	case UNPLUMB_ALL_OPT:
		return (llc2_unplumb_all());
		break;
	case PLUMB_ONE_OPT:
		if ((fp = open_conf(ppa)) == NULL) {
			(void) fprintf(stderr, gettext("Cannot open"
			    " configuration files for PPA %d\n"), ppa);
			return (-1);
		}
		if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
			(void) fprintf(stderr, gettext("Cannot open LLC2"
			    " device: %s\n"), strerror(errno));
			return (-1);
		}
		if (llc2_plumb_ppa(fp, ppa, llc2_fd) == LLC2_FAIL) {
			(void) fprintf(stderr, gettext("Cannot plumb PPA %d\n"),
			    ppa);
			return (-1);
		}
		/*
		 * Note that after the plumbing, we need to close() llc2_fd.
		 * The reason is that the current LLC2 code distinguishes
		 * between the first open() and subsequent open()s.  For the
		 * first open(), the q_ptr is not initialized.  So doing an
		 * ioctl() on it will panic the system.
		 */
		(void) close(llc2_fd);
		if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
			(void) fprintf(stderr, gettext("Cannot open LLC2"
			    " device: %s\n"), strerror(errno));
			return (-1);
		}
		if (llc2_init_ppa(ppa, llc2_fd) != LLC2_OK) {
			(void) llc2_unplumb_ppa(ppa, llc2_fd);
			return (-1);
		} else {
			return (LLC2_OK);
		}
		break;
	case UNPLUMB_ONE_OPT:
		if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
			(void) fprintf(stderr, gettext("Cannot open LLC2"
			    " device: %s\n"), strerror(errno));
			return (-1);
		}
		(void) (llc2_uninit_ppa(ppa, llc2_fd));
		/*
		 * Need to sleep() before unplumbing LLC2 so that
		 * it will have time to send disconnect indication
		 * to remote ends.
		 */
		(void) sleep(SLEEP_TIME);
		return (llc2_unplumb_ppa(ppa, llc2_fd));
		break;
	case INIT_OPT:
		if (add_conf() == LLC2_FAIL) {
			(void) fprintf(stderr, gettext("Error opening LLC2"
			    " configuration files: %s\n"),
			    strerror(errno));
			return (-1);
		}
		if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
			(void) fprintf(stderr, gettext("Cannot open LLC2"
			    " device: %s\n"), strerror(errno));
			return (-1);
		}
		return (llc2_init_ppa(ppa, llc2_fd));
		break;
	case UNINIT_OPT:
		if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
			(void) fprintf(stderr, gettext("Cannot open LLC2"
			    " device: %s\n"), strerror(errno));
			return (-1);
		}
		return (llc2_uninit_ppa(ppa, llc2_fd));
		break;
	case QUERY_OPT:
	default:	/* By default, it is a query option. */
		return (llc2_query());
		break;
	}
}

/*
 * Initialize LLC2 for a device, given its PPA.
 *
 * Param:
 *	int ppa: the PPA of the device.
 *	int llc2_fd: file descriptor to the LLC2 device.
 *
 * Return:
 *	LLC2_OK if operation successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_init_ppa(int ppa, int llc2_fd)
{
	FILE *fp;
	llc2_conf_param_t param;
	struct strioctl ic;
	int mac = -1;
	int trys = 0;
	char dev_name[MAXPATHLEN];
	int instance;
	struct {
		init_t init;
		ether_init_t ether;
	} init_pkg;
	llc2Init_t init;

	if ((fp = open_conf(ppa)) == NULL) {
		(void) fprintf(stderr, gettext("Cannot open configuration"
		    " files for PPA %d\n"), ppa);
		return (LLC2_FAIL);
	}
	if (read_conf_file(fp, dev_name, &instance, &param) == LLC2_FAIL) {
		(void) fprintf(stderr, gettext("Error reading parameters"
		    " for PPA %d\n"), ppa);
		(void) fclose(fp);
		return (LLC2_FAIL);
	}
	(void) fclose(fp);

	/*
	 * Do the initialization.  There are 2 steps for "historical"
	 * reason.  But this should only be 1.  This needs to be changed
	 * in future.
	 */
	init_pkg.init.mactype = mac;
	init_pkg.init.ppa = ppa;

	ic.ic_cmd = ILD_INIT;
	ic.ic_timout = IOCTL_TIMEOUT;
	ic.ic_len = sizeof (init_pkg);
	ic.ic_dp = (char *)&init_pkg;
	/*
	 * Try several times in case the kernel cannot allocate the
	 * necessary buffer.
	 */
	for (;;) {
		if (ioctl(llc2_fd, I_STR, &ic) < 0) {
			if (++trys > 4) {
				(void) fprintf(stderr,
				    gettext("Timeout initializing"
				    " PPA %d: %s\n"), ppa, strerror(errno));
				return (LLC2_FAIL);
			}
			(void) sleep(1);
			continue;
		}
		break;
	}

	/* 2nd step.  Read in the parameter file to initialize LLC2. */
	ic.ic_cmd = ILD_LLC2;
	ic.ic_timout = IOCTL_TIMEOUT;
	ic.ic_len = sizeof (init);
	ic.ic_dp = (char *)&init;

	init.ppa = ppa;
	init.cmd = LLC2_INIT;

	init.timeinterval = param.time_intrvl;
	if ((init.ackTimerInt = param.ack_timer) == 0) {
		init.ackTimerInt = ACK_TIMER_DEF;
	}
	if ((init.rspTimerInt = param.rsp_timer) == 0) {
		init.rspTimerInt = RSP_TIMER_DEF;
	}
	if ((init.pollTimerInt = param.poll_timer) == 0) {
		init.pollTimerInt = POLL_TIMER_DEF;
	}
	if ((init.rejTimerInt = param.rej_timer) == 0) {
		init.rejTimerInt = REJ_TIMER_DEF;
	}
	if ((init.remBusyTimerInt = param.rem_busy_timer) == 0) {
		init.remBusyTimerInt = REM_BUSY_TIMER_DEF;
	}
	if ((init.inactTimerInt = param.inact_timer) == 0) {
		init.inactTimerInt = INACT_TIMER_DEF;
	}
	if ((init.maxRetry = param.max_retry) == 0) {
		init.maxRetry = MAX_RETRY_DEF;
	}
	if ((init.xmitWindowSz = param.xmit_win) == 0) {
		init.xmitWindowSz = XMIT_WIN_DEF;
	}
	if ((init.rcvWindowSz = param.recv_win) == 0) {
		init.rcvWindowSz = RECV_WIN_DEF;
	}

	if (ioctl(llc2_fd, I_STR, &ic) < 0) {
		(void) fprintf(stderr, gettext("Error initialization PPA %d\n"),
		    ppa);
		return (LLC2_FAIL);
	}
	return (LLC2_OK);
}

/*
 * Plumb LLC2 for a device, given its PPA.
 *
 * Param:
 *	FILE *fp: file pointer to the config file.
 *	int ppa: the PPA of the device.
 *	int llc2_fd: file descriptor to the LLC2 device.
 *
 * Return:
 *	LLC2_OK if operation successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_plumb_ppa(FILE *fp, int ppa, int llc2_fd)
{
	char provider[DLPI_LINKNAME_MAX];
	char prov_path[MAXPATHLEN];
	int dev_instance;
	llc2_conf_param_t param;
	struct strioctl ic;
	ppa_config_t setppa;
	int mac_fd;
	int mux_id;

	if (read_conf_file(fp, provider, &dev_instance, &param) == LLC2_FAIL) {
		if (debug) {
			(void) fprintf(stderr, "Invalid configuration"
			    " file %s.%d\b", LLC2_NAME, ppa);
		}
		(void) fclose(fp);
		return (LLC2_FAIL);
	}
	(void) fclose(fp);

	/*
	 * Check if the device needs to be plumbed.  The LLC2_ON flag
	 * allows system administrators to turn off plumbing of LLC2 on
	 * certain devices, if they choose to.  Their corresponding files
	 * are not removed so that when llc2_autoconfig is run, a new
	 * default file will not be generated for the same device.
	 */
	if (param.llc2_on == 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "Skip plumbing"
			    " PPA %d.\n", ppa);
		}
		return (LLC2_FAIL);
	}

	if (debug > 0) {
		(void) fprintf(stderr, "Plumbing PPA %d: %s(%d)\n", ppa,
		    provider, dev_instance);
	}

	/*
	 * This hack is needed instead of using dlpi_open(..., DLPI_DEVONLY)
	 * as llc2 expects opening of a provider name only.
	 */
	(void) sprintf(prov_path, "%s%s", "/dev/", provider);
	if ((mac_fd = open(prov_path, O_RDWR)) < 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "Error opening device "
			    "%s: %s\n", prov_path, strerror(errno));
		}
		return (LLC2_FAIL);
	}

	if ((mux_id = ioctl(llc2_fd, I_PLINK, mac_fd)) < 0) {
		(void) fprintf(stderr, gettext("Error plumbing %s: %s\n"),
		    prov_path, strerror(errno));
		(void) close(mac_fd);
		return (LLC2_FAIL);
	}
	(void) close(mac_fd);

	setppa.ppa = ppa;
	setppa.cmd = ILD_PPA_CONFIG;
	setppa.index = mux_id;
	setppa.instance = dev_instance;

	ic.ic_cmd = LLC_SETPPA;
	ic.ic_timout = IOCTL_TIMEOUT;
	ic.ic_len = sizeof (ppa_config_t);
	ic.ic_dp = (char *)&setppa;
	if (ioctl(llc2_fd, I_STR, (char *)&ic) < 0) {
		(void) fprintf(stderr, gettext("Error setting PPA(%d) on %s:"
		    " %s\n"), ppa, prov_path, strerror(errno));
		(void) ioctl(llc2_fd, I_PUNLINK, mux_id);
		return (LLC2_FAIL);
	}
	return (LLC2_OK);
}

/*
 * Do the plumbing and initialization of all devices, using the configuration
 * files in the default directory.  It assumes that add_conf() has already
 * been called to open all the configuration files.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_plumb_all(void)
{
	llc2_conf_entry_t *confp;
	llc2_conf_entry_t *prev_confp = NULL;
	int error, llc2_fd;

	/* Nothing to be plumbed... */
	if (conf_head == NULL) {
		return (LLC2_OK);
	}

	/*
	 * There are 2 steps for setting up LLC2 on a given device.
	 * Note that after the plumbing, we need to close() llc2_fd.
	 * The reason is that the current LLC2 code distinguishes between
	 * the first open() and subsequent open()s.  For the first open(),
	 * the q_ptr is not initialized.  So doing an ioctl() on it will
	 * panic the system.
	 */
	if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("Cannot open LLC2 device: %s\n"),
		    strerror(errno));
		return (LLC2_FAIL);
	}

	confp = conf_head;
	while (confp != NULL) {
		if (llc2_plumb_ppa(confp->fp, confp->ppa, llc2_fd) ==
		    LLC2_FAIL) {
			if (debug > 0) {
				(void) fprintf(stderr, "Removed PPA %d\n",
				    confp->ppa);
			}
			RM_CONF_ENTRY(prev_confp, confp);
			if (prev_confp != NULL) {
				confp = prev_confp->next_entry_p;
			} else {
				confp = conf_head;
			}
			continue;
		}
		prev_confp = confp;
		confp = confp->next_entry_p;
	}
	(void) close(llc2_fd);

	/* Reopen /dev/llc2 to finish the initialization. */
	if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("Cannot open LLC2 device: %s\n"),
		    strerror(errno));
		return (LLC2_FAIL);
	}
	for (confp = conf_head; confp != NULL; confp = confp->next_entry_p) {
		if (debug > 0) {
			(void) fprintf(stderr, "Initializing PPA %d\n",
			    confp->ppa);
		}
		if ((error = llc2_init_ppa(confp->ppa, llc2_fd)) == LLC2_FAIL) {
			if (debug > 0) {
				(void) fprintf(stderr, "Error in"
				    " initializing PPA %d\n", confp->ppa);
			}
			(void) llc2_unplumb_ppa(confp->ppa, llc2_fd);
		}
	}
	(void) close(llc2_fd);
	return (error);
}

/*
 * Uninitialize a device, given its PPA.
 *
 * Param:
 *	int ppa: the PPA of the device.
 *	int llc2_fd: file descriptor to the LLC2 device.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_uninit_ppa(int ppa, int llc2_fd)
{
	struct strioctl ic;
	llc2Uninit_t uninit;
	struct {
		uninit_t uninit;
	} uninit_pkg;

	/* There are 2 steps to unitiailize a PPA. */
	ic.ic_cmd = ILD_LLC2;
	ic.ic_timout = IOCTL_TIMEOUT;
	ic.ic_len = sizeof (uninit);
	ic.ic_dp = (char *)&uninit;
	uninit.ppa = ppa;
	uninit.cmd = LLC2_UNINIT;

	/*
	 * Just issue the ioctl.  If it fails, just ignore it.  What else
	 * can we do?
	 */
	(void) ioctl(llc2_fd, I_STR, &ic);

	uninit_pkg.uninit.mactype = 0;
	uninit_pkg.uninit.ppa = ppa;

	ic.ic_cmd = ILD_UNINIT;
	ic.ic_timout = IOCTL_TIMEOUT;
	ic.ic_len = sizeof (uninit_pkg);
	ic.ic_dp = (char *)&uninit_pkg;

	(void) ioctl(llc2_fd, I_STR, &ic);
	return (LLC2_OK);
}

/*
 * Unplumb a device, given its PPA.
 *
 * Param:
 *	int ppa: the PPA of the device.
 *	int llc2_fd: file descriptor to the LLC2 device.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_unplumb_ppa(int ppa, int llc2_fd)
{
	ppa_config_t setppa;
	struct strioctl ic;

	/* No need to initialize setppa.index and setppa.instance. */
	setppa.ppa = ppa;
	setppa.cmd = ILD_PPA_CONFIG;

	ic.ic_cmd = LLC_GETPPA;
	ic.ic_timout = IOCTL_TIMEOUT;
	ic.ic_len = sizeof (ppa_config_t);
	ic.ic_dp = (char *)&setppa;
	if (ioctl(llc2_fd, I_STR, (char *)&ic) < 0) {
		(void) fprintf(stderr, gettext("Error getting info"
		    " from PPA %d: %s\n"), ppa, strerror(errno));
		return (LLC2_FAIL);
	}

	if (ioctl(llc2_fd, I_PUNLINK, setppa.index) < 0) {
		(void) fprintf(stderr, gettext("Error unplumbing PPA %d: %s\n"),
		    ppa, strerror(errno));
		return (LLC2_FAIL);
	}
	return (LLC2_OK);
}

/*
 * Unplumb all devices in the system.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_unplumb_all(void)
{
	int ppa, llc2_fd;
	struct strioctl ic;
	adapter_t brd;

	if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("Cannot open LLC2"
		    " device: %s\n"), strerror(errno));
		return (LLC2_FAIL);
	}
	/* Try all PPAs. */
	for (ppa = 0; ppa < MAXPPA; ppa++) {
		ic.ic_cmd = ILD_GCONFIG;
		ic.ic_timout = IOCTL_TIMEOUT;
		ic.ic_len = sizeof (brd);
		brd.ppa = ppa;
		ic.ic_dp = (char *)&brd;

		if (ioctl(llc2_fd, I_STR, &ic) < 0) {
			/*
			 * ENOENT is returned for "gaps" in the PPA table.
			 * ENXIO is returned for "end of entries."
			 */
			if (errno == ENOENT) {
				continue;
			} else {
				break;
			}
		}
		/*
		 * Note that we don't care about whether llc2_uninit_ppa()
		 * succeeds or not.  We continue the unplumbing of the
		 * underlying device.  This is the way NCR code works now.
		 */
		if (debug > 0) {
			(void) fprintf(stderr, "Unplumbing PPA %d\n", ppa);
		}
		(void) llc2_uninit_ppa(ppa, llc2_fd);
		/*
		 * Need to sleep() before unplumbing LLC2 so that
		 * it will have time to send disconnect indication
		 * to remote ends.
		 */
		(void) sleep(SLEEP_TIME);
		(void) llc2_unplumb_ppa(ppa, llc2_fd);
	}
	(void) close(llc2_fd);
	return (LLC2_OK);
}

/* Title when printing out the result of query. */
static char query_header[] = \
	"PPA State    ID      MACAddr     Type MaxSDU MinSDU Mode     Device";

#define	STATE_UP_STR	"up";
#define	STATE_DOWN_STR	"down";
#define	STATE_OFF_STR	"off";
#define	STATE_BAD_STR	"bad";

#define	STATE_BAD	0x2
#define	STATE_UP	0x4
#define	STATE_OFF	0x8

/*
 * Print out some info of all LLC2 devices.  Note that NCR has removed a
 * lot of info when LLC2 was ported to Solaris.  This function only prints
 * out those meaningful info.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
static int
llc2_query(void)
{
	int ppa, llc2_fd;
	struct strioctl ic;
	adapter_t brd;
	char *state;
	FILE *fp;
	char dev_name[MAXPATHLEN];
	int dev_instance;
	llc2_conf_param_t param;

	if ((llc2_fd = open("/dev/llc2", O_RDWR)) < 0) {
		(void) fprintf(stderr, gettext("Cannot open LLC2"
		    " device: %s\n"), strerror(errno));
		return (LLC2_FAIL);
	}
	(void) printf("%s\n", query_header);
	for (ppa = 0; ppa < MAXPPA; ppa++) {
		ic.ic_cmd = ILD_GCONFIG;
		ic.ic_timout = IOCTL_TIMEOUT;
		ic.ic_len = sizeof (brd);
		brd.ppa = ppa;
		ic.ic_dp = (char *)&brd;

		if (debug > 0) {
			(void) fprintf(stderr, "Checking PPA %d\n", ppa);
		}
		if (ioctl(llc2_fd, I_STR, &ic) < 0) {
			/*
			 * ENOENT is returned for "gaps" in the PPA table.
			 * ENXIO is returned for "end of entries."
			 */
			if (errno == ENOENT) {
				continue;
			} else {
				break;
			}
		}

		if (brd.state & STATE_BAD) {
			state = STATE_BAD_STR;
		} else if (brd.state & STATE_UP) {
			state = STATE_UP_STR;
		} else if (brd.state & STATE_OFF) {
			state = STATE_OFF_STR;
		} else {
			state = STATE_DOWN_STR;
		}

		(void) printf("%3d %5s  %04x "
		    "%02x%02x%02x%02x%02x%02x ",
		    brd.ppa, state, brd.adapterid,
		    brd.bia[0], brd.bia[1], brd.bia[2],
		    brd.bia[3], brd.bia[4], brd.bia[5]);
		if (get_device_info(ppa, llc2_fd) == LLC2_FAIL) {
			(void) printf("      ??     ??     ??   ?? ");
		}
		if ((fp = open_conf(ppa)) == NULL) {
			(void) printf("       ??\n");
		} else {
			if (read_conf_file(fp, dev_name, &dev_instance,
			    &param) == LLC2_FAIL) {
				(void) printf("       ??\n");
			} else {
				(void) printf("%10s%d\n", dev_name,
				    dev_instance);
			}
			(void) fclose(fp);
		}
	}
	(void) close(llc2_fd);
	return (LLC2_OK);
}

#define	CSMACD	"csma/cd"
#define	ETHER	"ethernet"
#define	FDDI	"fddi"
#define	TPR	"tkn-ring"
#define	UNKNW	gettext("unknown")

/*
 * Ask LLC2 for device info for a PPA, and then print them out.  This is called
 * by llc2_query().
 *
 * Param:
 *	int ppa: the PPA.
 *	int llc2_fd: fd of LLC2 device.
 *
 * Return:
 *	LLC2_OK if operation is successful.
 *	LLC2_FAIL if there is an error.
 */
static int
get_device_info(int ppa, int llc2_fd)
{
	struct strbuf ctl;
	union DL_primitives data;
	struct pollfd fds[1];
	int flags;
	char *mac_string;
	int retsize;
	boolean_t error = B_FALSE;

	data.attach_req.dl_primitive = DL_ATTACH_REQ;
	data.attach_req.dl_ppa = ppa;
	ctl.maxlen = DL_ATTACH_REQ_SIZE;
	ctl.len = DL_ATTACH_REQ_SIZE;
	ctl.buf = (char *)&data;
	if (putmsg(llc2_fd, &ctl, NULL, 0) < 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: putmsg(): %s\n",
			    strerror(errno));
		}
		goto bad;
	}

	fds[0].fd = llc2_fd;
	fds[0].events = POLLPRI;
	if (poll(fds, 1, POLL_TIMEOUT) <= 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: PPA (%d) not"
			    " responding\n", ppa);
		}
		goto bad;
	}

	ctl.len = 0;
	ctl.maxlen = sizeof (data);
	flags = 0;
	if (getmsg(llc2_fd, &ctl, NULL, &flags) != 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info:"
			    " getmsg(DL_ATTACH_REQ): %s\n", strerror(errno));
		}
		goto bad;
	}

	if (data.dl_primitive != DL_OK_ACK) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: wrong reply"
			    " primitive: %lx\n", data.dl_primitive);
		}
		goto bad;
	}

	data.info_req.dl_primitive = DL_INFO_REQ;
	ctl.maxlen = DL_INFO_REQ_SIZE;
	ctl.len = DL_INFO_REQ_SIZE;

	if (putmsg(llc2_fd, &ctl, NULL, 0) < 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: putmsg(): %s\n",
			    strerror(errno));
		}
		error = B_TRUE;
		goto detach;
	}

	fds[0].fd = llc2_fd;
	fds[0].events = POLLPRI;

	if (poll(fds, 1, POLL_TIMEOUT) <= 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: PPA (%d) not"
			    " responding\n", ppa);
		}
		error = B_TRUE;
		goto detach;
	}

	ctl.len = 0;
	ctl.maxlen = sizeof (data);
	flags = 0;
	if ((retsize = getmsg(llc2_fd, &ctl, NULL, &flags)) != 0) {
		if (retsize < 0) {
			if (debug > 0) {
				(void) fprintf(stderr, "device_info:"
				    " getmsg(DL_INFO_REQ): %s\n",
				    strerror(errno));
			}
			error = B_TRUE;
			goto detach;
		} else {
			/*
			 * There is a bug in the LLC2 code.  It sends
			 * up more info, broadcast address, ... than
			 * a DL_INFO_ACK.  We don't read them here.
			 * So just discard them.
			 */
			(void) ioctl(llc2_fd, I_FLUSH, FLUSHRW);
		}
	}

	if (data.dl_primitive == DL_INFO_ACK)  {
		switch (data.info_ack.dl_mac_type) {
		case DL_CSMACD:
			mac_string = CSMACD;
			break;
		case DL_ETHER:
			mac_string = ETHER;
			break;
		case DL_FDDI:
			mac_string = FDDI;
			break;
		case DL_TPR:
			mac_string = TPR;
			break;
		default:
			/* This should never happen! */
			mac_string = UNKNW;
			break;
		}
		(void) printf("%7s %6lu %6lu %4lx ", mac_string,
		    data.info_ack.dl_max_sdu, data.info_ack.dl_min_sdu,
		    data.info_ack.dl_service_mode);
	} else {
		error = B_TRUE;
	}
detach:
	data.detach_req.dl_primitive = DL_DETACH_REQ;
	ctl.maxlen = DL_DETACH_REQ_SIZE;
	ctl.len = DL_DETACH_REQ_SIZE;

	if (putmsg(llc2_fd, &ctl, NULL, 0) < 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: putmsg(): %s\n",
			    strerror(errno));
		}
		goto bad;
	}

	fds[0].fd = llc2_fd;
	fds[0].events = POLLPRI;

	if (poll(fds, 1, POLL_TIMEOUT) <= 0) {
		if (debug > 0) {
			(void) fprintf(stderr, "device_info: PPA (%d) not"
			    " responding\n", ppa);
		}
		goto bad;
	}

	ctl.maxlen = sizeof (data);
	ctl.len = 0;
	flags = 0;
	if ((retsize = getmsg(llc2_fd, &ctl, NULL, &flags)) >= 0) {
		if (error) {
			return (LLC2_FAIL);
		} else {
			return (LLC2_OK);
		}
	}
bad:
	/* Flush all the messages in the queue, just in case... */
	(void) ioctl(llc2_fd, I_FLUSH, FLUSHRW);
	return (LLC2_FAIL);
}
