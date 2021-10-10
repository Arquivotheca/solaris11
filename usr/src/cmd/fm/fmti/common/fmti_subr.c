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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/fm/protocol.h>
#include <sys/scsi/scsi_address.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <stddef.h>
#include <thread.h>
#include <time.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

#include <fmti_impl.h>

/*
 * Global defines.
 */
extern char		*ofile_name;
extern di_node_t	devtree;
extern int		nbays;
extern int		hba_node_cnt;
extern boolean_t	fmti_debug;
extern topo_hdl_t	*topo_hp;
extern char		*topo_uuid;

#define	RD_BUF_SZ	4096

/*
 * Activity LED control.
 */
volatile boolean_t do_activity;

/*
 * Callback structure for product identity info.
 */
typedef struct prod_cb_s {
	char	*prod;
	char	*ch_sn;
	char	*server;
} prod_cb_t;

/*
 * Various routines used by the fmti tool.
 */

/*
 * SIGUSR1 signal to turn off activity LED.
 */
static void
activity_handler(int sig)
{
	if (sig == SIGUSR1 || sig == SIGINT) {
		do_activity = B_FALSE;
		if (sig == SIGINT) {
			(void) printf("interrupted!\n");
			exit(1);
		}
	}
}

/*
 * Activity LED thread.
 */
static void *
activity_led(void *arg)
{
	int			fd;
	char			*path = (char *)arg;
	char			device_path[MAXPATHLEN];
	char			buf[RD_BUF_SZ];
	ssize_t			rd;
	struct sigaction	act;

	char			*f = "activity_led";

	/* only prepend "/devices" to a raw device path */
	if (strstr(path, ":a,raw") != NULL) {
		(void) snprintf(device_path, MAXPATHLEN, "%s%s", "/devices",
		    path);
	} else {
		/* prepend "/devices" and append ":a,raw" to the oc-path */
		(void) snprintf(device_path, MAXPATHLEN, "%s%s%s", "/devices",
		    path, ":a,raw");
		dprintf("%s: device path (%s)\n", f, device_path);
		di_devfs_path_free(path);
	}

	/* open the raw device to make some activity */
	fd = open(device_path, O_RDONLY);
	if (fd == -1) {
		dprintf("%s: failed to open (%s)\n", f, device_path);
		goto out;
	}

	/* set up a sig handler to stop the loop */
	act.sa_handler = activity_handler;
	(void) sigaction(SIGUSR1, &act, NULL);
	(void) sigaction(SIGINT, &act, NULL);

	dprintf("%s: initiate activity LED.\n", f);
	do_activity = B_TRUE;

	/* make some activity */
	while (do_activity) {
		/* read 4k bytes */
		rd = read(fd, buf, RD_BUF_SZ);
		if (rd == -1) {
			/* set back to 0 */
			dprintf("%s: reset read pointer to 0\n", f);
			(void) lseek(fd, 0,  SEEK_SET);
		}
	}
	(void) close(fd);
	dprintf("%s: LED activity done.\n", f);
out:
	return (NULL);
}

/*
 * Create occupant device path to disk; which will be used to generate disk
 * traffic to light it's activity LED.
 */
static char *
oc_client(di_node_t cnode, di_path_t pnode)
{
	int		rv;
	int		lun;
	char		*ipath = NULL;
	char		*ocpath = NULL;
	char		*target_port = malloc(MAXNAMELEN);
	char		*path_addr = NULL;
	char		wwn_path[MAXPATHLEN];
	boolean_t	got_w;
	di_node_t	client_node = DI_NODE_NIL;
	di_node_t	iport_node = DI_NODE_NIL;
	di_path_t	pi_node = DI_PATH_NIL;

	char		*f = "oc_client";

	/* 'target-port' prop */
	rv = get_str_prop(cnode, DI_PATH_NIL, SCSI_ADDR_PROP_TARGET_PORT,
	    target_port);
	if (rv != 0) {
		dprintf("%s: failed to get target-port\n", f);
		goto out;
	}

	/* 'lun' proo */
	lun = get_int_prop(cnode, DI_PATH_NIL, "lun");
	if (lun < 0 || lun > 255) {
		dprintf("%s: invalid lun (%d)\n", f, lun);
		goto out;
	}

	/* HBA iport node */
	ipath = di_devfs_path(cnode);
	if (ipath == NULL) {
		dprintf("%s: failed to get iport devfs_path\n", f);
		goto out;
	}
	iport_node = di_lookup_node(devtree, ipath);
	if (iport_node == DI_PATH_NIL) {
		dprintf("%s: failed to lookup iport node\n", f);
		goto out;
	}

	/* find the matching client, this is our path di_path_phci_next_path */
	for (pi_node = di_path_phci_next_path(iport_node, DI_PATH_NIL);
	    pi_node; pi_node = di_path_phci_next_path(iport_node, pi_node)) {
		/* check for client node */
		client_node = di_path_client_node(pi_node);
		if (client_node == DI_NODE_NIL) {
			continue;
		}

		/* path addr string */
		path_addr = di_path_bus_addr(pi_node);
		if (path_addr == NULL) {
			dprintf("%s: failed to get path_addr path_bus_addr\n",
			    f);
			goto out;
		}

		/* leading 'w' is not always consistent */
		got_w = path_addr[0] == 'w' ? B_TRUE : B_FALSE;
		if (got_w) {
			(void) snprintf(wwn_path, MAXPATHLEN, "%s,%x",
			    target_port, lun);
		} else {
			(void) snprintf(wwn_path, MAXPATHLEN, "w%s,%x",
			    target_port, lun);
		}
		if (strcmp(path_addr, wwn_path) == 0) {
			/*
			 * Found our node; occupant path is the client
			 * devfs path.
			 */
			ocpath = di_devfs_path(client_node);
			if (ocpath != NULL) {
				/* found our path */
				break;
			}
		}
	}

	if (pnode != DI_PATH_NIL && ocpath == NULL) {
		ocpath = di_path_devfs_path(pnode);
	}
out:
	/* cleanup */
	if (ipath != NULL) {
		di_devfs_path_free(ipath);
	}
	if (target_port != NULL) {
		free(target_port);
	}

	/* return the path */
	dprintf("%s: ocpath (%s)\n", f, ocpath);
	return (ocpath);
}

/*
 * Cause activity to the disk inserted into the bay@phy (inst). Need to find
 * the 'sd' node that matches the oc_path of bay@phy. Use 'sd,raw:a' to create
 * read activity.
 */
static int
activity(di_node_t cn, di_path_t pn, thread_t *tid)
{
	int		rv;
	di_node_t	client_node = DI_NODE_NIL;
	di_node_t	cnode = DI_NODE_NIL;
	char		*mpxd = malloc(MAXNAMELEN);
	char		*oc_path = NULL;

	char		*f = "activity";

	/*
	 * If mpxio is disabled ("mpxio-disable=yes") then the oc-path
	 * is either di_devfs_path(cnode) or the "raw" child of an iport.
	 */
	rv = get_str_prop(cn, pn, "mpxio-disable", mpxd);
	if (rv == 0 && cmp_str(mpxd, "yes")) {
		oc_path = di_devfs_path(cn);
		/*
		 * For HBAs that support iport and MPxIO is disabled, a child
		 * node is the raw device.
		 */
		if (strstr(oc_path, "iport@") != NULL) {
			/* look for ":a,raw" child */
			di_devfs_path_free(oc_path);
			oc_path = NULL;
			cnode = di_child_node(cn);
			while (cnode != DI_NODE_NIL) {
				oc_path = di_devfs_path(cnode);
				if (cmp_str(di_node_name(cnode), "disk") ||
				    cmp_str(di_node_name(cnode), "sd") ||
				    (strstr(oc_path, ":a,raw") != NULL)) {
					break;
				}

				if (oc_path != NULL) {
					/* free path */
					di_devfs_path_free(oc_path);
					oc_path = NULL;
				}

				/* next child */
				cnode = di_sibling_node(cnode);
			}
		}
	} else {
		/* generate the occupant path for the child node */
		oc_path = oc_client(cn, pn);
	}
	free(mpxd);

	/* sometimes 'mpxio-disable' just can't be trusted */
	if (oc_path == NULL) {
		if (cn != DI_NODE_NIL) {
			oc_path = di_devfs_path(cn);
		} else if (pn != DI_PATH_NIL) {
			client_node = di_path_client_node(pn);
			if (client_node != DI_NODE_NIL) {
				oc_path = di_devfs_path(client_node);
			} else {
				oc_path = di_path_devfs_path(pn);
			}
		}
	}

	if (oc_path != NULL) {
		/* create thread to blink the activity LED */
		(void) thr_create(NULL, 0, activity_led, (void *)oc_path,
		    0, tid);
		dprintf("%s: created thread id %d\n", f, *tid);
		rv = 0;
	} else {
		dprintf("%s: oc_path NULL\n", f);
		rv = -1;
	}
	return (rv);
}

/*
 * Find the child node that matches the phy. Call make activity to create
 * disk traffic.
 */
static int
blink_activity(di_node_t dnode, int phy, thread_t *tid)
{
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;

	/* look for phy in devinfo child nodes */
	cnode = di_child_node(dnode);
	while (cnode != DI_NODE_NIL) {
		if (get_phy(cnode, DI_PATH_NIL) == phy) {
			goto found;
		}
		cnode = di_sibling_node(cnode);
	}

	/* look for phy in pathinfo nodes */
	while ((pnode = di_path_phci_next_path(dnode, pnode)) != DI_PATH_NIL) {
		if (get_phy(DI_NODE_NIL, pnode) == phy) {
			goto found;
		}
	}

	if (cnode == DI_NODE_NIL && pnode == DI_PATH_NIL) {
		dprintf("blink_activity: failed to find child phy\n");
		return (-1);
	}
found:
	return (activity(cnode, pnode, tid));
}

/*
 * Blink the locate LED associated with the HBA:PHY. If the driver doen not
 * support SGPIO ioctl, attempt to create read traffic to the disk to blink
 * it's activity LED.
 */
int
blink_locate(di_node_t dnode, int phy, int flag, thread_t *tid)
{
	int			rv = 0;
	int			fd;
	char			dev_nm[MAXPATHLEN];
	struct dc_led_ctl	led;

	char			*f = "blink_locate";

	dprintf("%s: %s\n", f, flag == LED_ON ? "ON" : "OFF");
	/*
	 * Build the device file for ioctl:
	 * "/devices" + di_devfs_path() + ":devctl"
	 */
	(void) snprintf(dev_nm, MAXPATHLEN, "%s%s%s", "/devices",
	    di_devfs_path(dnode), ":devctl");

	dprintf("%s: device path : %s\n", f, dev_nm);
	dprintf("%s: PHY         : %d\n", f, phy);

	fd = open(dev_nm, O_RDWR);
	if (fd == -1) {
		/* could be a child node of HBA, try parent */
		(void) snprintf(dev_nm, MAXPATHLEN, "%s%s%s", "/devices",
		    di_devfs_path(di_parent_node(dnode)), ":devctl");
		fd = open(dev_nm, O_RDWR);
		if (fd == -1) {
			(void) printf("%s: failed to open (%s)\n",
			    f, dev_nm);
			return (-1);
		}
	}

	/* fill in led control strucure */
	led.led_number = phy;
	led.led_ctl_active = DCL_CNTRL_ON;
	led.led_type = DCL_TYPE_DEVICE_FAIL; /* locate */
	led.led_state = flag == LED_OFF ? DCL_STATE_OFF : DCL_STATE_FAST_BLNK;

	/* submit the request */
	rv = ioctl(fd, DEVCTL_SET_LED, &led);
	if (rv == -1 && led.led_state == DCL_STATE_FAST_BLNK) {
		/* fast blink may not be supported; try ON */
		led.led_state = DCL_STATE_ON;
		rv = ioctl(fd, DEVCTL_SET_LED, &led);
	}
	if (rv == -1) {
		dprintf("%s: SGPIO ioctl not supported.\n", f);
		if (flag == LED_ON) {
			/*
			 * The driver doesn't support SGPIO led ioctl,
			 * drive some work that will at the least light
			 * the activity LED on the disk.
			 */
			rv = blink_activity(dnode, phy, tid);
		} else {
			/* stop the disk LED activity thread */
			dprintf("%s: sending SIGUSR1 to tid %d\n", f, *tid);
			(void) thr_kill(*tid, SIGUSR1);
			(void) thr_join(*tid, NULL, NULL);
		}
	}

	(void) close(fd);
	dprintf("%s: done.\n", f);
	return (rv);
}

/*
 * DEBUG printing.
 */
void
dprintf(const char *format, ...)
{
	if (fmti_debug) {
		va_list alist;

		va_start(alist, format);
		(void) vfprintf(stderr, format, alist);
		va_end(alist);
	}
}

/*
 * Print the char count times.
 */
void
fill_line(char c, int cnt)
{
	int	i;

	for (i = 0; i < cnt; i++) {
		(void) printf("%c", c);
	}
	(void) printf("\n");
}

/*
 * Get the chassis label from the user. If the HBA is connected to drive
 * bays that are internal to the system the expected input is either '\n'
 * or "internal"; otherwise an recognizable 'nickname' should be input.
 */
char *
get_ch_label(void)
{
	int	i;
	char	*vs = NULL;
	char	s[MAXNAMELEN];

	(void) printf("Enter the label for the chassis that houses the disk\n"
	    "bays/drives of this HBA. Hit <return> if the bays are\n"
	    "\'internal\' to the system, otherwise please enter a\n"
	    "descriptive \'nickname\' [internal]\t\t\t: ");

	(void) fgets(s, MAXNAMELEN, stdin);
	(void) fflush(stdin);

	/* replace newline from fgets with NULL */
	for (i = 0; i < MAXNAMELEN; i++) {
		if (s[i] == '\n') {
			s[i] = '\0';
			break;
		}
	}

	/* 'internal' is default */
	vs = verify_ans(s);
	if (s[0] == '\0' || cmp_str(vs, "internal")) {
		(void) snprintf(s, MAXNAMELEN, "%s", INTERNAL);
	}
	free(vs);
	return (strdup(s));
}

/*
 * Get the external chassis product name from the user.
 */
char *
get_extch_prod(void)
{
	int	i;
	char	s[MAXNAMELEN];

	(void) printf("\nEnter the Product name for this external "
	    "chassis\t: ");

	(void) fgets(s, MAXNAMELEN, stdin);
	(void) fflush(stdin);

	/* replace newline from fgets with \0 */
	for (i = 0; i < MAXNAMELEN; i++) {
		if (s[i] == '\n') {
			s[i] = '\0';
			break;
		}
	}
	return (strdup(s));
}

/*
 * Get the external chassis serial number from the user.
 */
char *
get_extch_sn(void)
{
	int	i;
	char	s[MAXNAMELEN];

	(void) printf("\nEnter the serial number for this external "
	    "chassis\t: ");

	(void) fgets(s, MAXNAMELEN, stdin);
	(void) fflush(stdin);

	/* replace newline from fgets with \0 */
	for (i = 0; i < MAXNAMELEN; i++) {
		if (s[i] == '\n') {
			s[i] = '\0';
			break;
		}
	}
	return (strdup(s));
}

/*
 * Get the slot label for the slot which the HBA PCIE card is installed.
 */
char *
get_hba_label(di_node_t dnode)
{
	int		rv;
	int		err;
	topo_walk_t	*twp;
	tw_pcie_cbs_t	cbs;
	char		*label = NULL;

	char		*f = "get_hba_label";

	/*
	 * Get the pci slot id from the HBA pcibus topo label.
	 * The pcibus enum has already done all the twizzle
	 * required to figure out the correct label.
	 */

	/* make sure we have a good topo handle */
	if (topo_hp == NULL) {
		goto out;
	}

	/* fill in call back structure */
	cbs.devfs_path = di_devfs_path(dnode);
	cbs.label = malloc(MAXNAMELEN);
	cbs.mod = NULL;
	cbs.hdl = topo_hp;

	/* init walk */
	twp = topo_walk_init(topo_hp, FM_FMRI_SCHEME_HC, th_hba_l,
	    &cbs, &err);
	if (twp == NULL) {
		dprintf("%s: topo_walk_init() failed\n", f);
		goto out;
	}

	/* walk */
	rv = topo_walk_step(twp, TOPO_WALK_CHILD);
	if (rv == TOPO_WALK_ERR) {
		dprintf("%s: failed to walk topology\n", f);
	}
	topo_walk_fini(twp);
	di_devfs_path_free(cbs.devfs_path);

	/* Copy the label for return */
	if (strlen(cbs.label) > 0) {
		dprintf("%s: label from topo walk (%s)\n", f, cbs.label);
		label = cbs.label;
	} else {
		free(cbs.label);
	}
out:
	return (label);
}

/*
 * Get label for a given disk bay:
 *  - blink the locator LED
 *  - ask the user for the label for that disk bay
 */
char *
get_bay_label(di_node_t dnode, int phy)
{
	thread_t	tid;
	int		bl_ok;
	char		s[MAXNAMELEN];

	bl_ok = blink_locate(dnode, phy, LED_ON, &tid); /* on */

	pr_find_bay();
	(void) printf("\nEnter the label for that bay\t\t\t\t: ");

	(void) fgets(s, MAXNAMELEN, stdin);
	(void) fflush(stdin);
	(void) printf("\n");

	if (bl_ok == 0) {
		(void) blink_locate(dnode, phy, LED_OFF, &tid); /* off */
	}

	return (strdup(s));
}

/*
 * Topo walk callback to find product name, chassis s/n, and server name.
 */
/* ARGSUSED */
int
th_prod_sn(topo_hdl_t *thp, tnode_t *tnp, void *arg)
{
	int		rv;
	int		err;
	char		*prod = NULL;
	char		*sn = NULL;
	char		*server = NULL;
	prod_cb_t	*cbp = (prod_cb_t *)arg;

	/* looking for FM_FMRI_AUTH_PRODUCT and FM_FMRI_AUTH_CHASSIS */
	(void) topo_prop_get_string(tnp, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_PRODUCT, &prod, &err);

	(void) topo_prop_get_string(tnp, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_CHASSIS, &sn, &err);

	/* get FM_FMRI_AUTH_SERVER */
	(void) topo_prop_get_string(tnp, FM_FMRI_AUTHORITY,
	    FM_FMRI_AUTH_SERVER, &server, &err);

	if (prod == NULL || sn == NULL || server == NULL) {
		/* try again */
		rv = TOPO_WALK_NEXT;
		goto out;
	}

	/* pass the values back */
	cbp->prod = prod;
	cbp->ch_sn = sn;
	cbp->server = server;

	/* we're done */
	rv = TOPO_WALK_TERMINATE;
out:
	return (rv);
}


/*
 * Get product identity information from topo snapshot.
 */
int
get_product(char *prod, char *ch_sn, char *server)
{
	int		rv;
	int		err;
	topo_walk_t	*twp;
	prod_cb_t	cb;

	char		*f = "get_product";

	/* make sure the topo handle is good */
	if (topo_hp == NULL) {
		dprintf("%s: no topo handle\n", f);
		return (-1);
	}

	/* init walk */
	twp = topo_walk_init(topo_hp, FM_FMRI_SCHEME_HC, th_prod_sn,
	    &cb, &err);
	if (twp == NULL) {
		dprintf("%s: topo_walk_init() failed\n", f);
		return (-1);
	}

	/* walk */
	rv = topo_walk_step(twp, TOPO_WALK_CHILD);
	if (rv == TOPO_WALK_ERR) {
		dprintf("%s: failed to walk topology\n", f);
	}
	topo_walk_fini(twp);

	/* copy out strings */
	if (cb.prod != NULL) {
		bcopy(cb.prod, prod, strlen(cb.prod) + 1);
		topo_hdl_strfree(topo_hp, cb.prod);
	}
	if (cb.ch_sn != NULL) {
		bcopy(cb.ch_sn, ch_sn, strlen(cb.ch_sn) + 1);
		topo_hdl_strfree(topo_hp, cb.ch_sn);
	}
	if (cb.server != NULL) {
		bcopy(cb.server, server, strlen(cb.server) + 1);
		topo_hdl_strfree(topo_hp, cb.server);
	}

	dprintf("%s: product name(%s) chassis S/N(%s) server(%s)\n", f,
	    prod, ch_sn, server);

	return (0);
}

/*
 * Parse the input string to validate any incarnation of "yes", "no",
 * or "internal".
 */
char *
verify_ans(char *s)
{
	int		i;
	int		iter = 3;	/* campare up to 3 strings */
	int		len = strlen(s);
	const char	*strings[] = {
	    "internal",
	    "no",
	    "yes"
	};

	/* yes/no/internal */
	if (len > 8 || len < 2) {
		goto bad;
	}

	for (i = 0; i < iter; i++) {
		if (strlen(strings[i]) == len) {
			if (strncasecmp(s, strings[i], len) == 0) {
				return (strdup(strings[i]));
			}
		}
	}
bad:
	return (strdup("bad"));
}

/*
 * Create configuration file, write out product and system name then a
 * descriptive header.
 */
int
wr_hdr(char *prod_name, char *server)
{
	time_t		timer;
	struct tm	*tm_struct;
	int		year;
	FILE		*f;
	char		copy_str[MAXNAMELEN];
	const char	*header =
	    "# Product : HBA : HBA Instance : Chassis Name : Chassis S/N : "
	    "PHY : Label\n"
	    "# ------------------------------------------------------------"
	    "-----------\n";

	/* zero out the file */
	f = fopen(ofile_name, "w+");
	if (f == NULL) {
		return (-1);
	}

	/* copyright year */
	timer = time(NULL);
	tm_struct = localtime(&timer);
	year = 1900 + tm_struct->tm_year;

	/* write out copyright */
	(void) snprintf(copy_str, MAXPATHLEN, "%s%d%s", "#\n# Copyright (c) ",
	    year, ", Oracle and/or its affiliates. All rights reserved.\n");
	(void) fputs(copy_str, f);

	/* write the header */
	(void) fputs("#\n# Product Name: ", f);
	(void) fputs(prod_name, f);
	(void) fputs("\n# Server Name: ", f);
	(void) fputs(server, f);
	(void) fputs("\n#\n", f);
	(void) fputs(header, f);

	(void) fclose(f);
	return (0);
}

/*
 * Write out line to config file:
 * Product : HBA name : HBA Instance : Chassis Name : Chassis S/N : PHY : Label
 */
void
wr_config(bay_t *bay, char *product)
{
	int	cnt = 0;
	FILE	*F;
	char	*oline = malloc(MAXPATHLEN);

	char	*f = "wr_config";

	dprintf("%s: "
	    "Product: %s   Driver: %s   Instance: %d   Ch Name: %s   "
	    "Ch S/N: %s   PHY: %d   Label: %s\n", f,
	    product, bay->hba_nm, bay->hba_inst, bay->ch_label,
	    bay->ch_serial, bay->phy, bay->label);

	/* create output line */
	(void) snprintf(oline, MAXPATHLEN, "%s:%s:%d:%s:%s:%d:%s",
	    product, bay->hba_nm, bay->hba_inst, bay->ch_label,
	    bay->ch_serial != NULL ? bay->ch_serial : "NULL",
	    bay->phy, bay->label);
	dprintf("wr_config: oline (%s)\n", oline);

	/*
	 * Open file for writing/append to end
	 */
	F = fopen(ofile_name, "aw");
	if (F == NULL) {
		(void) printf("wr_config: failed to open %s\n", ofile_name);
		free(oline);
		return;
	}

	/* write out config line */
	cnt = fputs(oline, F);
	dprintf("%s: wrote %d bytes to %s\n\n", f, cnt, ofile_name);

	/* cleanup */
	free(oline);
	(void) fclose(F);
}

/*
 * Printing routines.
 */

/*
 * Print out tool header.
 */
void
fmti_pr_hdr(void)
{
	fill_line('*', 80);
	(void) printf("*\t\tFM Direct Attached Disk Topology Discovery "
	    "Tool\t\t       *\n");
	fill_line('*', 80);
}

/*
 * Print out explaining what we are.
 */
void
fmti_pr_hdr1(char *prod_name)
{
	(void) printf("\nSystem: %s\n\n", prod_name);
	(void) printf("The fmti utility will derive labels for direct "
	    "attached SAS disk bays by\nmanipulating bay/disk LEDs while "
	    "prompting for individual bay labels.\n");
	fill_line('-', 80);
	fill_line('-', 80);
}

/*
 * Print out a short description of blinking LEDs.
 */
static void
hba_pr_desc_led(void)
{
	(void) printf("\nNOTE:"
	    "\n\tInitial blinking of a particular bay/disk LED will"
	    "\n\tbe used to locate this specific HBA. There will be"
	    "\n\tfurther blinking of LEDs in the next steps to locate"
	    "\n\tspecific bays/disks attached to this HBA.\n");
}

/*
 * Print out the HBA description.
 */
void
hba_pr_hdr(char *hba_name, di_node_t dnode)
{
	char	*hba_label = get_hba_label(dnode);

	(void) printf("\n%s HBA", hba_name);
	if (hba_label != NULL) {
		if (cmp_str(hba_label, "MB")) {
			(void) printf(" installed on the Motherboard\n");
			fill_line('*', (strlen(hba_name) + 33));
		} else {
			(void) printf(" installed in slot %s:\n", hba_label);
			fill_line('*', (strlen(hba_name) +
			    strlen(hba_label) + 24));
		}
		free(hba_label);
	} else {
		(void) printf("\n");
		fill_line('*', (strlen(hba_name) + 4));
		(void) printf("\nWARNING:"
		    "\n\tUnable to determine which PCIe slot this HBA is"
		    "\n\tinstalled. The consequence is the inability to"
		    "\n\tdetermine the relational position of the bays"
		    "\n\tconnected to this HBA.\n");
	}

	/* print out a short description of blinking leds */
	hba_pr_desc_led();
}

/*
 * Print out a note about empty bays.
 */
void
pr_ebay_note(void)
{
	(void) printf("NOTE:"
	    "\n\tEmpty bays have been discovered. For best results"
	    "\n\tit is STRONGLY recommended that all bays be populated.\n");
	(void) printf(
	    "\n\tWill loop though all remaining PHYs of this HBA to"
	    "\n\tattempt to locate the empty bays by illuminating"
	    "\n\ttheir locator LED. When prompted, enter \'yes\' when"
	    "\n\tan empty bay's LED is blinking, otherwise answer "
	    "\'no\'.\n\n");
}

/*
 * Print out to look for blinking LED.
 */
void
pr_find_bay(void)
{
	(void) printf("\nPlease find the disk/bay with the "
	    "blinking LED.\n\n");
}

/*
 * Print out summary.
 */
void
pr_summary(char *prod_name)
{
	(void) printf("\nThere %s %d SAS HBA%s",
	    hba_node_cnt > 1 ? "are" : "is",
	    hba_node_cnt,
	    hba_node_cnt > 1 ? "s " : " ");

	if (nbays > 0)
		(void) printf("with %d total bays ", nbays);

	(void) printf("on this %s system.\n\n", prod_name);
	(void) printf("Configuration written to file:\n%s\n\n", ofile_name);
}
