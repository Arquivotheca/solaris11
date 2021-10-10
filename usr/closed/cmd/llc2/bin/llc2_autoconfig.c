/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <stropts.h>
#include <libintl.h>
#include <sys/time.h>
#include <locale.h>
#include <sys/llc2.h>
#include <ild.h>
#include <dirent.h>
#include <libdlpi.h>
#include "llc2_conf.h"

/* Structures to represent links found in the system. */
typedef struct link_entry {
	char			linkname[DLPI_LINKNAME_MAX];
	uint_t 			mac_type;
	struct link_entry	*next_entry_p;
} link_entry_t;

/* Head of the link list. */
static link_entry_t	*link_head = NULL;

/* Number of links on the link list.  It is changed in add_link(). */
static int		num_link = 0;

/*
 * We trade space for simplicitiy here...  Array to store which PPA has
 * been used.  It is changed in create_new_conf().
 */
static boolean_t used_ppa[MAXPPA] = { B_FALSE };

/* Also used in llc2_conf.c */
int debug = 0;

static void do_query(void);
static boolean_t add_link(const char *, void *);
static int check_link_compat(const char *, uint_t *);
static int remove_link_entry(char *);
static void print_link_entry(void);
static int find_unused_ppa(int);
static void remove_old_conf(void);
static void create_def_conf(void);
static void create_new_conf(void);

static void
Usage(char *argv[])
{
	(void) fprintf(stderr, gettext("Usage: %s [-f] [-q] [-d]\n"), argv[0]);
}

int
main(int argc, char *argv[])
{
	boolean_t force = B_FALSE;
	boolean_t query = B_FALSE;
	int c;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "fqd")) != EOF) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'd':
			debug++;
			break;
		case 'q':
			query = B_TRUE;
			break;
		default:
			Usage(argv);
			return (0);
		}
	}

	if (query) {
		do_query();
	} else {
		/* Find all the network links in the system. */
		(void) dlpi_walk(add_link, NULL, 0);
		if (debug > 0) {
			(void) fprintf(stderr, "All links found:\n");
			print_link_entry();
		}

		if (force) {
			remove_old_conf();
			create_def_conf();
		} else {
			create_new_conf();
		}
	}

	return (0);
}

/*
 * Print out information about existing configuration files.
 */
static void
do_query(void)
{
	llc2_conf_entry_t *confp;
	char provider[DLPI_LINKNAME_MAX];
	int instance;
	llc2_conf_param_t param;

	if (add_conf() == LLC2_FAIL) {
		(void) fprintf(stderr, gettext("Error in reading default"
		    " configuration directory.\n"));
		return;
	}
	(void) printf(gettext("\n\t\tLLC2 Configuration\n\n"));
	(void) printf("llc2\t Device\t      Device\n");
	(void) printf(" PPA\t   Name\t     Instance\tLoopback?   Enabled?\n");
	(void) printf("====================================================\n");
	for (confp = conf_head; confp != NULL; confp = confp->next_entry_p) {
		if (read_conf_file(confp->fp, provider, &instance,
		    &param) == -1) {
			(void) fprintf(stderr, gettext("Error in reading "
			    "configuration file\n"));
			return;
		}
		(void) printf("%3d\t%8s\t%2d\t%6s%12s\n", confp->ppa, provider,
		    instance, (param.dev_loopback == 0) ? "No" : "Yes",
		    (param.llc2_on == 0) ? "No" : "Yes");
	}
}

/*
 * This function verifies that the link found is compatible with
 * LLC2.  Currently, it checks if the link is CSMA/CD, Ethernet, or
 * FDDI.  If it is, it is compatible with LLC2.  This list may need to change
 * in future.
 *
 * Params:
 *	char *linkname: linkname name to be checked.
 *	uint_t *mtype: it returns the MAC type of the link.
 *
 * Return:
 *	LLC2_FAIL if link is not compatible with LLc2 or error in probing
 *	the link.
 *	LLC2_OK if link is compatible with LLC2.
 */
static int
check_link_compat(const char *linkname, uint_t *mtype)
{
	int retval;
	dlpi_handle_t llc2dh;
	dlpi_info_t llc2_dlinfo;

	/* LLC2 device is compatible with itself.  So ignore it. */
	if (strcmp(linkname, LLC2_NAME) == 0)
		return (LLC2_FAIL);

	if ((retval = dlpi_open(linkname, &llc2dh, 0)) != DLPI_SUCCESS) {
		if (debug > 1) {
			(void) fprintf(stderr, "Cannot open %s:%s\n",
			    linkname, dlpi_strerror(retval));
		}
		return (LLC2_FAIL);
	}

	(void) dlpi_set_timeout(llc2dh, 10);

	if ((retval = dlpi_info(llc2dh, &llc2_dlinfo, 0)) != DLPI_SUCCESS) {
		if (debug > 1) {
			(void) fprintf(stderr, "dlpi_info failed on %s:%s\n",
			    linkname, dlpi_strerror(retval));
		}
		retval = LLC2_FAIL;
		goto done;
	}

	*mtype = llc2_dlinfo.di_mactype;
	switch (*mtype) {
	case DL_CSMACD:
	case DL_TPR:
	case DL_ETHER:
	case DL_FDDI:
		retval = LLC2_OK;
		break;
	default:
		if (debug > 1) {
			(void) fprintf(stderr, "Device does not support"
			    " LLC2, mac_type %s\n", dlpi_mactype(*mtype));
		}
		retval = LLC2_FAIL;
	}

done:
	(void) dlpi_close(llc2dh);
	return (retval);
}

/*
 * Given the linkname, the function removes a link entry from the link list.
 *
 * Params:
 *	char *linkname: the name of link to be removed.
 *
 * Return:
 *	LLC2_FAIL if the link is not found on the list.
 *	LLC2_OK if the link entry is removed successfully.
 */
static int
remove_link_entry(char *linkname)
{
	link_entry_t *linkp, *prev_linkp = NULL;

	for (linkp = link_head; linkp != NULL;
	    prev_linkp = linkp, linkp = linkp->next_entry_p) {
		if (strcmp(linkp->linkname, linkname) == 0) {
			if (prev_linkp == NULL) {
				prev_linkp = link_head;
				link_head = link_head->next_entry_p;
				free(prev_linkp);
			} else {
				prev_linkp->next_entry_p = linkp->next_entry_p;
				free(linkp);
			}
			return (LLC2_OK);
		}
	}
	return (LLC2_FAIL);
}

/*
 * This is the callback function for dlpi_walk().  It adds the link node
 * found to the datalink list.  It calls check_link_compat() to verify
 * the link before adding it to the list.
 */
/* ARGSUSED */
static boolean_t
add_link(const char *link, void *arg)
{
	uint_t mac_type;
	link_entry_t *new_linkp;

	if (check_link_compat(link, &mac_type) == LLC2_FAIL) {
		return (B_FALSE);
	}

	if ((new_linkp = malloc(sizeof (link_entry_t))) == NULL) {
		/* Should we exit() instead?? */
		return (B_FALSE);
	}

	(void) strlcpy(new_linkp->linkname, link, DLPI_LINKNAME_MAX);
	new_linkp->mac_type = mac_type;

	if (++num_link > MAXPPA) {
		(void) fprintf(stderr, gettext("Too many network devices, only"
		    " the first %d devices will be configured.\n"), MAXPPA);
		return (B_TRUE);
	}

	ADD_ENTRY(link_head, new_linkp);

	return (B_FALSE);
}

static void
print_link_entry(void)
{
	link_entry_t *linkp;

	for (linkp = link_head; linkp != NULL; linkp = linkp->next_entry_p) {
		(void) printf("linkname %s, mac_type %s\n", linkp->linkname,
		    dlpi_mactype(linkp->mac_type));
	}
}

/*
 * Find the first unused PPA from the used_ppa array.
 *
 * Params:
 *	int start_ppa: start the search from this PPA.
 *
 * Return:
 *	the first unused PPA number.  If MAXPPA is returned, it means that
 *	there is no unused PPA.
 */
static int
find_unused_ppa(int start_ppa)
{
	int i;

	for (i = start_ppa; i < MAXPPA; i++) {
		if (used_ppa[i] == B_FALSE)
			return (i);
	}
	return (MAXPPA);
}

/*
 * Remove all old configuration files.
 */
static void
remove_old_conf(void)
{
	llc2_conf_entry_t *confp, *prev_confp;
	char pathname[MAXPATHLEN];

	(void) add_conf();
	if (debug > 0) {
		(void) fprintf(stderr, "Existing config files:\n");
		print_conf_entry();
	}

	for (confp = conf_head; confp != NULL;
	    prev_confp = confp, confp = confp->next_entry_p, free(prev_confp)) {
		(void) sprintf(pathname, "%s%s.%d", LLC2_CONF_DIR, LLC2_NAME,
		    confp->ppa);
		if (debug > 0) {
			(void) fprintf(stderr, "Removing %s.\n", pathname);
		}
		if (remove(pathname) < 0) {
			if (debug > 0) {
				(void) fprintf(stderr, "Cannot remove old LLC2"
				    " config file %s %s\n", pathname,
				    strerror(errno));
			}
			/*
			 * If we cannot remove the old config file, mark the
			 * PPA as being used so that new config file will
			 * not reuse it.
			 */
			used_ppa[confp->ppa] = B_TRUE;
		}
		(void) fclose(confp->fp);
	}
	conf_head = NULL;
}

/*
 * Create default configuration files for LLC2 devices using the
 * link list.
 */
static void
create_def_conf(void)
{
	link_entry_t *linkp;
	llc2_conf_param_t param;
	int ppa = 0;

	if (debug > 0) {
		(void) fprintf(stderr, "Creating default configuration"
		    " files.\n");
	}
	param.llc2_on = LLC2_ON_DEF;
	/*
	 * Now assume all devices do not support loopback.  In future,
	 * we may use the mac_type to determine the correct value.
	 */
	param.dev_loopback = 1;
	param.time_intrvl = TIME_INTRVL_DEF;
	param.ack_timer = ACK_TIMER_DEF;
	param.rsp_timer = RSP_TIMER_DEF;
	param.poll_timer = POLL_TIMER_DEF;
	param.rej_timer = REJ_TIMER_DEF;
	param.rem_busy_timer = REM_BUSY_TIMER_DEF;
	param.inact_timer = INACT_TIMER_DEF;
	param.max_retry = MAX_RETRY_DEF;
	param.xmit_win = XMIT_WIN_DEF;
	param.recv_win = RECV_WIN_DEF;

	for (linkp = link_head; linkp != NULL; linkp = linkp->next_entry_p) {
		if (debug > 0) {
			(void) fprintf(stderr, "Creating conf file for %s/n",
			    linkp->linkname);
		}
		ppa = find_unused_ppa(ppa);
		if (ppa == MAXPPA) {
			(void) fprintf(stderr, gettext("Cannot create extra"
			    " configuration files.  All PPA are used up.\n"));
			return;
		}
		used_ppa[ppa] = B_TRUE;
		if (create_conf(linkp->linkname, ppa, &param) == LLC2_FAIL) {
			return;
		}
	}
}

/*
 * Create new LLC2 configuration files for previously unconfigured or
 * newly added links.
 */
static void
create_new_conf(void)
{
	llc2_conf_entry_t *confp;
	link_entry_t *linkp, *tmp_linkp;
	char linkname[DLPI_LINKNAME_MAX], provider[DLPI_LINKNAME_MAX];
	int instance, num_conf;
	int num_new_link = num_link;
	llc2_conf_param_t param;

	/* First read in all existing configuration files. */
	if (add_conf() == LLC2_FAIL) {
		if (debug > 0) {
			(void) fprintf(stderr, "Cannot read existing"
			    " configuration files.\n");
		}
		return;
	}
	if (debug > 0) {
		(void) fprintf(stderr, "Existing config files:\n");
		print_conf_entry();
		(void) fprintf(stderr, "Creating new configuration files.\n");
	}
	/*
	 * If there is no exisitng configuration file, create the default
	 * files.
	 */
	if (conf_head == NULL) {
		create_def_conf();
		return;
	}

	/*
	 * Go thru the whole conf file list and fill in the used_ppa array.
	 * Also delete the corresponding link entry from the datalink list.
	 */
	for (num_conf = 0, confp = conf_head; confp != NULL;
	    confp = confp->next_entry_p, num_conf++) {
		used_ppa[confp->ppa] = B_TRUE;
		if (read_conf_file(confp->fp, provider, &instance, &param) ==
		    LLC2_FAIL) {
			if (debug > 0) {
				(void) fprintf(stderr, "Invalid configuration"
				    " file %s.%d\n", LLC2_NAME, confp->ppa);
			}
		} else {
			(void) dlpi_makelink(linkname, provider, instance);
			if (remove_link_entry(linkname) == LLC2_FAIL) {
				/*
				 * This link does not exist anymore.
				 */
				(void) fprintf(stderr, gettext("Device for"
				    " PPA %d does not exist anymore"),
				    confp->ppa);
			} else {
				num_new_link--;
			}
		}
		(void) fclose(confp->fp);
	}

	if (link_head == NULL) {
		/* Nothing new to add. */
		return;
	}
	/*
	 * For each remaining link entry, create a default configuration
	 * file.  But need to make sure that the number of new links plus
	 * the number of old configured links do not exceed MAXPPA.
	 */
	if (num_new_link + num_conf >= MAXPPA) {
		/* Calculate how many new links we can configure. */
		num_new_link = MAXPPA - num_conf - num_new_link;
		if (num_new_link <= 0) {
			(void) fprintf(stderr, gettext("Too many new devices,"
			    " no PPA to use for configuration.\n"));
			return;
		}
		/* Delete excessive link entries. */
		(void) fprintf(stderr, gettext("Too many new network devices, "
		    "only %d new devices will be configured.\n"), num_new_link);
		for (linkp = link_head; num_new_link > 0;
		    linkp = linkp->next_entry_p, num_new_link--)
			;
		for (; linkp != NULL; tmp_linkp = linkp,
		    linkp = linkp->next_entry_p, free(tmp_linkp))
			;
	}
	create_def_conf();
}
