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
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <devfsadm.h>
#include <syslog.h>

static char *modname = "SUNW_port_link";

/*
 * devfsadm_print message id
 */
#define	PORT_MID	"SUNW_port_link"

#define	DIALOUT_SUFFIX	",cu"
#define	DEVNAME_SEPR	'/'
#define	MN_SEPR		','
#define	MN_NULLCHAR	'\0'

/*
 * enumeration regular expressions, port and onboard port devices
 * On x86, /dev/term|cua/[a..z] namespace is split into 2:
 * a-d are assigned based on minor name. e-z are
 * assigned via enumeration.
 */
static devfsadm_enumerate_t port_rules[] =
	{"^(term|cua)$/^([0-9]+)$", 1, MATCH_MINOR, "1"};

#ifdef __i386
static devfsadm_enumerate_t obport_rules[] =
	{"^(term|cua)$/^([e-z])$", 1, MATCH_MINOR, "1"};
static char start_id[] = "e";
#else
static devfsadm_enumerate_t obport_rules[] =
	{"^(term|cua)$/^([a-z])$", 1, MATCH_MINOR, "1"};
static char start_id[] = "a";
#endif

static int serial_port_create(di_minor_t minor, di_node_t node);
static int onbrd_port_create(di_minor_t minor, di_node_t node);
static int dialout_create(di_minor_t minor, di_node_t node);
static int onbrd_dialout_create(di_minor_t minor, di_node_t node);
static int rsc_port_create(di_minor_t minor, di_node_t node);
static int lom_port_create(di_minor_t minor, di_node_t node);
static int pcmcia_port_create(di_minor_t minor, di_node_t node);
static int pcmcia_dialout_create(di_minor_t minor, di_node_t node);
static int is_dialout(char *dname);

/*
 * devfs create callback register
 */
static devfsadm_create_t ports_cbt[] = {
	{"pseudo", "ddi_pseudo", "su",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_1, rsc_port_create},
	{"port", "ddi_serial:lomcon", "su",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_1, lom_port_create},
	{"port", "ddi_serial", "pcser",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_1, pcmcia_port_create},
	{"port", "ddi_serial:dialout", "pcser",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_1, pcmcia_dialout_create},
	{"port", "ddi_serial", NULL,
	    TYPE_EXACT, ILEVEL_0, serial_port_create},
	{"port", "ddi_serial:mb", NULL,
	    TYPE_EXACT, ILEVEL_0, onbrd_port_create},
	{"port", "ddi_serial:dialout", NULL,
	    TYPE_EXACT, ILEVEL_0, dialout_create},
	{"port", "ddi_serial:dialout,mb", NULL,
	    TYPE_EXACT, ILEVEL_0, onbrd_dialout_create},
};
DEVFSADM_CREATE_INIT_V0(ports_cbt);

/*
 * devfs cleanup register
 * no cleanup rules for PCMCIA port devices
 */
static devfsadm_remove_t ports_remove_cbt[] = {
	{"port", "^term/[0-9]+$", RM_PRE | RM_ALWAYS | RM_HOT, ILEVEL_0,
	    devfsadm_rm_all},
	{"port", "^cua/[0-9]+$", RM_PRE | RM_ALWAYS | RM_HOT, ILEVEL_0,
	    devfsadm_rm_all},
	{"port", "^(term|cua)/[a-z]$",
	    RM_PRE | RM_ALWAYS, ILEVEL_0, devfsadm_rm_all},
};
DEVFSADM_REMOVE_INIT_V0(ports_remove_cbt);

int
minor_init()
{
	return (DEVFSADM_SUCCESS);
}

int
minor_fini()
{
	return (DEVFSADM_SUCCESS);
}

/*
 * Called for all serial devices that are NOT onboard
 * Creates links of the form "/dev/term/[0..n]"
 * Schedules an update the sacadm (portmon).
 */
static int
serial_port_create(di_minor_t minor, di_node_t node)
{
	char l_path[MAXPATHLEN], p_path[MAXPATHLEN];
	char *devfspath, *buf, *minor_name;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_CONTINUE);
	}

	if ((minor_name = di_minor_name(minor)) == NULL) {
		devfsadm_errprint("%s: NULL minor name\n\t%s\n", modname,
		    devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * verify dialout ports do not come in on this nodetype
	 */
	if (is_dialout(minor_name)) {
		devfsadm_errprint("%s: dialout device\n\t%s:%s\n",
		    modname, devfspath, minor_name);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * add the minor name to the physical path so we can
	 * enum the port# and create the link.
	 */
	(void) strcpy(p_path, devfspath);
	(void) strcat(p_path, ":");
	(void) strcat(p_path, minor_name);
	di_devfs_path_free(devfspath);

	if (devfsadm_enumerate_int(p_path, 0, &buf, port_rules, 1)) {
		devfsadm_errprint("%s:serial_port_create:"
		    " enumerate_int() failed\n\t%s\n",
		    modname, p_path);
		return (DEVFSADM_CONTINUE);
	}

	(void) strcpy(l_path, "term/");
	(void) strcat(l_path, buf);
	(void) devfsadm_mklink(l_path, node, minor, 0);

	/*
	 * This is probably a USB serial port coming into the system
	 * because someone just plugged one in.  Log an indication of
	 * this to syslog just in case someone wants to know what the
	 * name of the new serial device is ..
	 */
	(void) syslog(LOG_INFO, "serial device /dev/%s present", l_path);

	free(buf);
	return (DEVFSADM_CONTINUE);
}

/*
 * Called for all dialout devices that are NOT onboard
 * Creates links of the form "/dev/cua/[0..n]"
 */
static int
dialout_create(di_minor_t minor, di_node_t node)
{
	char l_path[MAXPATHLEN], p_path[MAXPATHLEN];
	char  *devfspath, *buf, *mn;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_CONTINUE);
	}

	if ((mn = di_minor_name(minor)) == NULL) {
		devfsadm_errprint("%s: NULL minorname\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	if (!is_dialout(mn)) {
		devfsadm_errprint("%s: invalid minor name\n\t%s:%s\n",
		    modname, devfspath, mn);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	(void) strcpy(p_path, devfspath);
	(void) strcat(p_path, ":");
	(void) strcat(p_path, mn);
	di_devfs_path_free(devfspath);

	if (devfsadm_enumerate_int(p_path, 0, &buf, port_rules, 1)) {
		devfsadm_errprint("%s:dialout_create:"
		    " enumerate_int() failed\n\t%s\n",
		    modname, p_path);
		return (DEVFSADM_CONTINUE);
	}
	(void) strcpy(l_path, "cua/");
	(void) strcat(l_path, buf);

	/*
	 *  add the minor name to the physical path so we can create
	 *  the link.
	 */
	(void) devfsadm_mklink(l_path, node, minor, 0);

	free(buf);
	return (DEVFSADM_CONTINUE);
}

#ifdef __i386

static int
portcmp(char *devfs_path, char *phys_path)
{
	char *p1, *p2;
	int rv;

	p2 = NULL;

	p1 = strrchr(devfs_path, ':');
	if (p1 == NULL)
		return (1);

	p1 = strchr(p1, ',');
	if (p1)
		*p1 = '\0';

	p2 = strrchr(phys_path, ':');
	if (p2 == NULL) {
		rv = -1;
		goto out;
	}

	p2 = strchr(p2, ',');
	if (p2)
		*p2 = '\0';

	rv = strcmp(devfs_path, phys_path);

out:
	if (p1)
		*p1 = ',';
	if (p2)
		*p2 = ',';

	return (rv);
}

/*
 * If the minor name begins with [a-d] and the
 * links in /dev/term/<char> and /dev/cua/<char>
 * don't point at a different minor, then we can
 * create compatibility links for this minor.
 * Returns:
 *	port id if a compatibility link can be created.
 *	NULL otherwise
 */
static char *
check_compat_ports(di_node_t node, char *phys_path, char *minor)
{
	char portid = *minor;
	char port[PATH_MAX];
	char *devfs_path;

	if (portid < 'a' || portid >  'd')
		return (NULL);

	(void) snprintf(port, sizeof (port), "term/%c", portid);
	if (devfsadm_read_link(node, port, &devfs_path) == DEVFSADM_SUCCESS &&
	    portcmp(devfs_path, phys_path) != 0) {
		free(devfs_path);
		return (NULL);
	}

	free(devfs_path);

	(void) snprintf(port, sizeof (port), "cua/%c", portid);
	if (devfsadm_read_link(node, port, &devfs_path) == DEVFSADM_SUCCESS &&
	    portcmp(devfs_path, phys_path) != 0) {
		free(devfs_path);
		return (NULL);
	}

	free(devfs_path);

	/*
	 * Neither link exists or both links point at "phys_path"
	 * We can safely create compatibility links.
	 */
	port[0] = portid;
	port[1] = '\0';

	return (s_strdup(port));
}

#endif

/*
 * Called for all Onboard serial devices
 * Creates links of the form "/dev/term/[a..z]"
 */
static int
onbrd_port_create(di_minor_t minor, di_node_t node)
{
	char l_path[MAXPATHLEN], p_path[MAXPATHLEN];
	char  *devfspath, *buf, *minor_name;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_CONTINUE);
	}

	if ((minor_name = di_minor_name(minor)) == NULL) {
		devfsadm_errprint("%s: NULL minor name\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * verify dialout ports do not come in on this nodetype
	 */
	if (is_dialout(minor_name)) {
		devfsadm_errprint("%s: dialout device\n\t%s:%s\n", modname,
		    devfspath, minor_name);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	(void) strcpy(p_path, devfspath);
	(void) strcat(p_path, ":");
	(void) strcat(p_path, minor_name);
	di_devfs_path_free(devfspath);


	buf = NULL;

#ifdef __i386
	buf = check_compat_ports(node, p_path, minor_name);
#endif

	/*
	 * devfsadm_enumerate_char_start() is a private interface for use by the
	 * ports module only
	 */
	if (!buf && devfsadm_enumerate_char_start(p_path, 0, &buf, obport_rules,
	    1, start_id)) {
		devfsadm_errprint("%s: devfsadm_enumerate_char_start() failed"
		    "\n\t%s\n", modname, p_path);
		return (DEVFSADM_CONTINUE);
	}

	(void) strcpy(l_path, "term/");
	(void) strcat(l_path, buf);
	(void) devfsadm_mklink(l_path, node, minor, 0);
	free(buf);
	return (DEVFSADM_CONTINUE);
}

/*
 * Onboard dialout devices
 * Creates links of the form "/dev/cua/[a..z]"
 */
static int
onbrd_dialout_create(di_minor_t minor, di_node_t node)
{
	char l_path[MAXPATHLEN], p_path[MAXPATHLEN];
	char  *devfspath, *buf, *mn;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_CONTINUE);
	}

	if ((mn = di_minor_name(minor)) == NULL) {
		devfsadm_errprint("%s: NULL minor name\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * verify this is a dialout port
	 */
	if (!is_dialout(mn)) {
		devfsadm_errprint("%s: not a dialout device\n\t%s:%s\n",
		    modname, devfspath, mn);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	(void) strcpy(p_path, devfspath);
	(void) strcat(p_path, ":");
	(void) strcat(p_path, mn);
	di_devfs_path_free(devfspath);

	buf = NULL;

#ifdef __i386
	buf = check_compat_ports(node, p_path, mn);
#endif

	/*
	 * devfsadm_enumerate_char_start() is a private interface
	 * for use by the ports module only.
	 */
	if (!buf && devfsadm_enumerate_char_start(p_path, 0, &buf, obport_rules,
	    1, start_id)) {
		devfsadm_errprint("%s: devfsadm_enumerate_char_start() failed"
		    "\n\t%s\n", modname, p_path);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * create the logical link
	 */
	(void) strcpy(l_path, "cua/");
	(void) strcat(l_path, buf);
	(void) devfsadm_mklink(l_path, node, minor, 0);
	free(buf);
	return (DEVFSADM_CONTINUE);
}


/*
 * Remote System Controller (RSC) serial ports
 * Creates links of the form "/dev/rsc-control" | "/dev/term/rsc-console".
 */
static int
rsc_port_create(di_minor_t minor, di_node_t node)
{
	char  *devfspath;
	char  *minor_name;


	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_CONTINUE);
	}

	if ((minor_name = di_minor_name(minor)) == NULL) {
		devfsadm_errprint("%s: NULL minor name\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * if this is the RSC console serial port (i.e. the minor name == ssp),
	 * create /dev/term/rsc-console link and then we are done with this
	 * node.
	 */
	if (strcmp(minor_name, "ssp") == 0) {
		(void) devfsadm_mklink("term/rsc-console", node, minor, 0);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_TERMINATE);

	/*
	 * else if this is the RSC control serial port (i.e. the minor name ==
	 * sspctl), create /dev/rsc-control link and then we are done with this
	 * node.
	 */
	} else if (strcmp(minor_name, "sspctl") == 0) {
		(void) devfsadm_mklink("rsc-control", node, minor, 0);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_TERMINATE);
	}

	/* This is not an RSC node, continue... */
	di_devfs_path_free(devfspath);
	return (DEVFSADM_CONTINUE);
}

/*
 * Lights Out Management (LOM) serial ports
 * Creates links of the form "/dev/term/lom-console".
 */
static int
lom_port_create(di_minor_t minor, di_node_t node)
{
	char  *devfspath;
	char  *minor_name;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_CONTINUE);
	}

	if ((minor_name = di_minor_name(minor)) == NULL) {
		devfsadm_errprint("%s: NULL minor name\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_CONTINUE);
	}

	/*
	 * if this is the LOM console serial port (i.e. the minor
	 * name == lom-console ), create /dev/term/lom-console link and
	 * then we are done with this node.
	 */
	if (strcmp(minor_name, "lom-console") == 0) {
		(void) devfsadm_mklink("term/lom-console", node, minor, 0);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_TERMINATE);
	}

	/* This is not a LOM node, continue... */
	di_devfs_path_free(devfspath);
	return (DEVFSADM_CONTINUE);
}

/*
 * PCMCIA serial ports
 * Creates links of the form "/dev/term/pcN", where N is the PCMCIA
 * socket # the device is plugged into.
 */
#define	PCMCIA_MAX_SOCKETS	64
#define	PCMCIA_SOCKETNO(x)	((x) & (PCMCIA_MAX_SOCKETS - 1))

static int
pcmcia_port_create(di_minor_t minor, di_node_t node)
{
	char l_path[MAXPATHLEN];
	char  *devfspath;
	int socket, *intp;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_TERMINATE);
	}

	if (di_prop_lookup_ints(DDI_DEV_T_ANY, node, "socket", &intp) <= 0) {
		devfsadm_errprint("%s: failed pcmcia socket lookup\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_TERMINATE);
	}

	socket = PCMCIA_SOCKETNO(*intp);

	di_devfs_path_free(devfspath);

	(void) sprintf(l_path, "term/pc%d", socket);
	(void) devfsadm_mklink(l_path, node, minor, 0);

	return (DEVFSADM_TERMINATE);
}

/*
 * PCMCIA dialout serial ports
 * Creates links of the form "/dev/cua/pcN", where N is the PCMCIA
 * socket number the device is plugged into.
 */
static int
pcmcia_dialout_create(di_minor_t minor, di_node_t node)
{
	char l_path[MAXPATHLEN];
	char  *devfspath;
	int socket, *intp;

	devfspath = di_devfs_path(node);
	if (devfspath == NULL) {
		devfsadm_errprint("%s: di_devfs_path() failed\n", modname);
		return (DEVFSADM_TERMINATE);
	}

	if (di_prop_lookup_ints(DDI_DEV_T_ANY, node, "socket", &intp) <= 0) {
		devfsadm_errprint("%s: failed socket lookup\n\t%s\n",
		    modname, devfspath);
		di_devfs_path_free(devfspath);
		return (DEVFSADM_TERMINATE);
	}

	socket = PCMCIA_SOCKETNO(*intp);

	di_devfs_path_free(devfspath);
	(void) sprintf(l_path, "cua/pc%d", socket);
	(void) devfsadm_mklink(l_path, node, minor, 0);

	return (DEVFSADM_TERMINATE);
}

/*
 * check if the minor name is suffixed with ",cu"
 */
static int
is_dialout(char *name)
{
	char *s_chr;

	if ((name == NULL) || (s_chr = strrchr(name, MN_SEPR)) == NULL)
		return (0);

	if (strcmp(s_chr, DIALOUT_SUFFIX) == 0) {
		return (1);
	} else {
		return (0);
	}
}
