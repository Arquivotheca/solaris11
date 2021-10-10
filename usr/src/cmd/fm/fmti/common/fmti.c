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

#include <libsysevent.h>
#include <string.h>
#include <fmti_impl.h>


/* running count of hbas/bays */
int nbays = 0;
int hba_node_cnt = 0;

/* devinfo node(s) */
di_node_t hba_nodes[MAX_HBAS];
di_node_t devtree = DI_NODE_NIL;

/* configuration file name */
char *ofile_name = NULL;

/* fmti debug printing */
boolean_t fmti_debug = B_FALSE;

/* topo snapshot handle */
topo_hdl_t *topo_hp = NULL;
char *topo_uuid = NULL;

/*
 * fmti: FM Topo Interactive.
 *
 * Interactive tool to gather FMA topology information that is impossible to be
 * derived any other way. Initially the tool will gather direct attached SAS
 * disk labels that are either not obtained via PRI/PRMS-1 or added to a system
 * with an add on HBA card.
 */

static void
usage(FILE *fp)
{
	(void) fprintf(fp,
	    "Usage: fmti -d|h\n");

	(void) fprintf(fp,
	    "\t-d  direct attached disk labels\n"
	    "\t-h  print this help output\n");
}

/*
 * Fill in the bay structure and write out to the config file.
 */
static void
wr_bay(di_node_t hba_dnode, int phy, char *drv_name, int instance,
    char *ch_l, char *ch_sn, char *ch_prod)
{
	bay_t	*bayp = malloc(sizeof (bay_t));

	if (bayp == NULL) {
		(void) printf("Unable to allocate memory for bay.\n");
		return;
	}

	/* fill in the bay struct */
	bayp->phy = phy;
	bayp->hba_inst = instance;
	bayp->hba_nm = drv_name;
	bayp->label = get_bay_label(hba_dnode, phy);
	bayp->ch_label = ch_l;
	bayp->ch_serial = ch_sn;

	/* write it out */
	wr_config(bayp, ch_prod);
	nbays++;

	/* cleanup */
	free(bayp->label);
	free(bayp);
}

/*
 * Identify empty bays and write them out to the config file.
 */
static int
wr_empty_bays(di_node_t hba_dnode, char *ch_l, int att_phys_pm,
    int phy_mask, char *ch_sn, char *ch_prod, int rembays)
{
	int		i;
	int		j;
	int		val;
	int		hba_num_phys;
	int		cnt = 0;
	int		bl_ok;
	int		instance = di_instance(hba_dnode);
	thread_t	tid;
	char		*drv_name = di_driver_name(hba_dnode);

	/* grab the number of phys of this hba */
	hba_num_phys = get_int_prop(hba_dnode, DI_PATH_NIL, "num-phys");
	if (hba_num_phys == -1) {
		hba_num_phys = get_int_prop(hba_dnode, DI_PATH_NIL,
		    "num-phys-hba");
		if (hba_num_phys == -1) {
			hba_num_phys = DFLT_NUM_PHYS;
		}
	}

	/* no phy mask - have to ask */
	if (att_phys_pm == -1) {
		pr_ebay_note();
	}

	/* loop through all PHYs */
	for (i = 0; i < hba_num_phys; i++) {
		if (cnt == rembays) {
			/* all done */
			break;
		}

		val = (1 << i);
		if (phy_mask & val) {
			/* not empty, disk inserted - skip */
			continue;
		}

		/* ask if no attached phys mask */
		if (att_phys_pm == -1) {
			char ans[MAXNAMELEN];
			char *vans = NULL;

			/* turn on the LED */
			bl_ok = blink_locate(hba_dnode, i, LED_ON, &tid);
			do {
				if (vans != NULL) {
					(void) printf("Invalid response: "
					    "%s\nPlease enter yes or no.\n",
					    ans);
					free(vans);
				}

				(void) printf("Is there an empty bay "
				    "w/blinking LED [yes/no]\t\t: ");
				(void) fgets(ans, MAXNAMELEN, stdin);
				(void) fflush(stdin);
				/* remove the newline from gets() */
				for (j = 0; j < strlen(ans); j++) {
					if (ans[j] == '\n') {
						ans[j] = '\0';
					}
				}
				vans = verify_ans(ans);
			} while (cmp_str(vans, "bad"));
			if (cmp_str(vans, "no")) {
				/* turn off the LED and try the next PHY */
				if (bl_ok == 0) {
					(void) blink_locate(hba_dnode, i,
					    LED_OFF, &tid);
				}
				free(vans);
				continue;
			}
			/* turn off the LED */
			if (bl_ok == 0) {
				(void) blink_locate(hba_dnode, i, LED_OFF,
				    &tid);
			}
			free(vans);
		} else {
			/* check phy mask */
			if (!(att_phys_pm & val)) {
				/* phy not in mask */
				continue;
			}
		}

		/* write out bay config line */
		wr_bay(hba_dnode, i, drv_name, instance, ch_l, ch_sn, ch_prod);
		cnt++;
	}

	return (cnt);
}

/*
 * Identify the chassis the HBA is connect to, how many direct attached SAS
 * bays it connect to, and create a line for each bay in the config file.
 */
static void
wr_hba(di_node_t hba_dnode, char *prod_name, char *ich_sn)
{
	int		i;
	int		n;
	int		done;
	int		cnt = 0;
	int		bl_ok;
	int		hba_inst;
	int		phy = 0x0;
	int		att_phys_pm = 0x0;
	int		phy_mask = 0x0;
	thread_t	tid;
	boolean_t	valid = B_FALSE;
	char		*ch_label = NULL;
	char		*ch_sn = NULL;
	char		*ch_prod = NULL;
	di_node_t	cnode = DI_NODE_NIL;
	di_path_t	pnode = DI_PATH_NIL;
	char		*hba_nm = di_driver_name(hba_dnode);

	char		*f = "wr_hba";

	/* print out hba info */
	hba_pr_hdr(hba_nm, hba_dnode);

	/* first node */
	cnode = di_child_node(hba_dnode);
	pnode = di_path_phci_next_path(hba_dnode, DI_PATH_NIL);
	phy = get_phy(cnode, pnode);
	hba_inst = di_instance(hba_dnode);

	/* chassis this hba is installed in */
	bl_ok = blink_locate(hba_dnode, phy, LED_ON, &tid);

	pr_find_bay();
	ch_label = get_ch_label();
	dprintf("%s: chassis name %s\n", f, ch_label);

	/* get S/N if external chassis */
	if (cmp_str(ch_label, INTERNAL)) {
		ch_prod = prod_name;
		ch_sn = strdup((const char *)ich_sn);
	} else {
		ch_prod = get_extch_prod();
		ch_sn = get_extch_sn();
	}
	dprintf("%s: chassis serial number (%s)\n", f, ch_sn);

	/* look for 'attached-phys-pm' prop */
	att_phys_pm = get_int_prop(hba_dnode, DI_PATH_NIL,
	    "attached-phys-pm");
	if (att_phys_pm == -1) {
		/* property not available; must ask */
		do {
			/* number of direct attached bays */
			(void) printf("\nEnter the total number of bays "
			    "attached to this\n%s HBA\t\t\t\t\t\t%s: ",
			    hba_nm, strlen(hba_nm) < 4 ? "\t" : "");
			(void) scanf("%d", &n);
			(void) fflush(stdin);
			if (n < 0 || n > MAX_BAYS) {
				(void) printf("\nTotal number of bays must "
				    "be between 0 and %d. You entered %d "
				    "whcih is invalid.\n", MAX_BAYS, n);
			} else {
				dprintf("\n%s: %s:%d HBA contains %d direct "
				    "attached bays.\n", f, hba_nm,
				    hba_inst, n);
				valid = B_TRUE;
			}
		} while (!valid);
		(void) printf("\n");
	} else {
		/* count the number of bays */
		n = 0;
		for (i = 0; i < DFLT_NUM_PHYS; i++) {
			if (att_phys_pm & (1 << i)) {
				n++;
			}
		}
	}
	/* turn off the LED */
	if (bl_ok == 0) {
		(void) blink_locate(hba_dnode, phy, LED_OFF, &tid);
	}
	dprintf("%s: attached phys(%d)  mask(0x%x)\n", f, n, att_phys_pm);

	/* generate label for each bay */
	for (i = 0, done = 0; i < n; i++) {
		if (!sas_direct(hba_dnode)) {
			break;
		}
		/* create phy-mask for use w/empty bays */
		phy = get_phy(cnode, pnode);
		if (phy == -1) {
			continue; /* no disk here */
		}
		phy_mask = (phy_mask | (0x1 << phy));

		/* write out a bay config line */
		wr_bay(hba_dnode, phy, hba_nm, hba_inst, ch_label, ch_sn,
		    ch_prod);
		done++;

		/* first nodes's been set, get the next */
		if (cnode != DI_NODE_NIL) {
			cnode = di_sibling_node(cnode);
		}
		if (pnode != DI_PATH_NIL) {
			pnode = di_path_phci_next_path(hba_dnode, pnode);
		}
	}

	/* empty bays count too */
	if (done != n) {
		/* there are empty bays */
		cnt = wr_empty_bays(hba_dnode, ch_label, att_phys_pm,
		    phy_mask, ch_sn, ch_prod, n - done);

		if ((done  + cnt) != n) {
			dprintf("%s: only wrote out %d of %d bays "
			    "for %s HBA.\n", f, (done  + cnt), n, hba_nm);
			return;
		}
	}
	dprintf("%s: wrote out %d bays for %s HBA.\n", f, n, hba_nm);

	/* cleanup */
	free(ch_sn);
	free(ch_label);
}

/*
 * Main
 *
 * Return values:
 *	0: normal termination
 *	1: abnormal termination
 *	2: usage error
 */
int
main(int argc, char **argv)
{
	int		i;
	int		c;
	int		rv;
	int		d_flg = 0x0;
	int		err;
	sysevent_id_t	eid;
	char		*prod_name = NULL;
	char		*ch_sn = NULL;
	char		*server = NULL;

	char		*f = "fmti";

	/* deal with arguments */
	if (argc < 2) {
		usage(stderr);
		return (2);
	}
	while (optind < argc) {
		while ((c = getopt(argc, argv, "dh")) != -1) {
			switch (c) {
			case 'd':
				d_flg++;
				break;
			case 'h':
				usage(stderr);
				return (0);
			default:
				usage(stderr);
				return (1);
			}
		}
	}

	/* debug printing */
	fmti_debug = getenv("FMTI_DEBUG") != NULL;

	/* grab devinfo snapshot */
	devtree = di_init("/", DINFOCPYALL | DINFOPATH);
	if (devtree == DI_NODE_NIL) {
		dprintf("%s: di_init failed.\n", f);
		return (1);
	}

	/* find our HBA nodes */
	(void) di_walk_node(devtree, DI_WALK_CLDFIRST,
	    (void *)hba_nodes, gather_sas_hba);
	if (hba_node_cnt == 0) {
		(void) printf("\n%s: No direct attached SAS HBAs found.\n", f);
		di_fini(devtree);
		return (0);
	}

	/* allocate space */
	prod_name = malloc(MAXNAMELEN);
	ch_sn = malloc(MAXNAMELEN);
	server = malloc(MAXNAMELEN);
	ofile_name = malloc(MAXNAMELEN);
	if (prod_name == NULL || ch_sn == NULL || server == NULL ||
	    ofile_name == NULL) {
		dprintf("%s: no memory.\n", f);
		errno = ENOMEM;
		rv = 1;
		goto out;
	}

	/* print header */
	fmti_pr_hdr();

	/* topo snapshot */
	(void) printf("\nGathering topology.. (this may take a few "
	    "minutes..)\n");
	topo_hp = topo_open(TOPO_VERSION, "/", &err);
	if (topo_hp == NULL) {
		dprintf("%s: failed to get topo handle\n", f);
		rv = 1;
		goto out;
	}
	topo_uuid = topo_snap_hold(topo_hp, NULL, &err);
	if (topo_uuid == NULL) {
		dprintf("%s: failed to get topo snapshot\n", f);
		rv = 1;
		goto out;
	}

	/* get product and S/N */
	*prod_name	= '\0';
	*ch_sn		= '\0';
	*server		= '\0';
	rv = get_product(prod_name, ch_sn, server);
	if (rv != 0) {
		dprintf("%s: Failed to get product name and S/N.\n", f);
		rv = -1;
		goto out;
	}

	/* create output file name */
	gen_ofile_name(prod_name, ch_sn, ofile_name);
	dprintf("%s: output file (%s)\n", f, ofile_name);

	/* print out explaination and product name */
	fmti_pr_hdr1(prod_name);

	/* create/truncate/open the config file, write header */
	rv = wr_hdr(prod_name, server);
	if (rv != 0) {
		dprintf("%s: failed to create and write header to: %s.\n",
		    f, ofile_name);
		rv = 1;
		goto out;
	}

	/* go through the HBA nodes */
	for (i = 0; i < hba_node_cnt; i++) {
		/* verify child is direct attached disk bay */
		if (!sas_direct(hba_nodes[i])) {
			continue;
		}

		/* fill in config file */
		wr_hba(hba_nodes[i], prod_name, ch_sn);
	}

	/* print summary */
	pr_summary(prod_name);

	/* signal fmd to take a new topo snapshot */
	(void) sysevent_post_event(EC_CRO, ESC_CRO_TOPOREFRESH,
	    SUNW_VENDOR, "fmd", NULL, &eid);
out:
	/* clean up */
	if (topo_uuid != NULL) {
		topo_hdl_strfree(topo_hp, topo_uuid);
	}
	if (topo_hp != NULL) {
		topo_snap_release(topo_hp);
		topo_close(topo_hp);
	}
	if (ch_sn != NULL) {
		free(ch_sn);
	}
	if (prod_name != NULL) {
		free(prod_name);
	}
	if (server != NULL) {
		free(server);
	}
	if (ofile_name != NULL) {
		free(ofile_name);
	}
	di_fini(devtree);

	/* that's all folks */
	return (rv);
}
