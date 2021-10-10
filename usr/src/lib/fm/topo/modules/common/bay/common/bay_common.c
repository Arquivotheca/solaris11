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

/*
 * The routines in this file are what's needed to create a bay topo node.
 */

#include <strings.h>
#include <devid.h>
#include <inttypes.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <sys/fm/protocol.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <bay_impl.h>

static const topo_pgroup_info_t sys_pgroup = {
	TOPO_PGROUP_SYSTEM,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t auth_pgroup = {
	FM_FMRI_AUTHORITY,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

/*
 * Set the label.
 */
int
bay_set_label(topo_mod_t *mod, bay_t *bp, tnode_t *tn)
{
	int	rv;
	int	err;

	if (mod == NULL) {
		return (-1);
	}

	rv = topo_node_label_set(tn, bp->label, &err);
	if (rv != 0) {
		(void) topo_mod_seterrno(mod, err);
		topo_mod_dprintf(mod,
		    "failed to set label (%s) for %s:%d: (%s)\n",
		    bp->label == NULL ? "NULL" : bp->label,
		    bp->hba_nm, bp->phy, topo_strerror(err));
	}

	return (rv);
}

/*
 * Create the authority pgroup and inherit info from the parent.
 */
int
bay_set_auth(topo_mod_t *mod, tnode_t *pnode, tnode_t *tn)
{
	int 		rv;
	int		err;
	nvlist_t	*auth;
	char		*val = NULL;
	char		*prod = NULL;
	char		*psn = NULL;
	char		*csn = NULL;
	char		*server = NULL;

	if (mod == NULL || pnode == NULL || tn == NULL) {
		return (-1);
	}

	rv = topo_pgroup_create(tn, &auth_pgroup, &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		/* not existing and failed to create */
		(void) topo_mod_seterrno(mod, err);
		return (-1);
	}

	/* get auth list from parent */
	auth = topo_mod_auth(mod, pnode);

	/* product-id */
	rv = topo_prop_inherit(tn, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_PRODUCT,
	    &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		/* not in parent node; check auth list */
		val = NULL;
		rv = nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT, &val);
		if (rv != 0 || val == NULL) {
			/* no product info; report the error and drive on */
			topo_mod_dprintf(mod,
			    "bay_set_auth: no product info\n");
		}
		prod = topo_mod_strdup(mod, val);

		if (prod != NULL) {
			rv = topo_prop_set_string(tn, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_PRODUCT, TOPO_PROP_IMMUTABLE, prod,
			    &err);
			if (rv != 0) {
				/* report the error and drive on */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod,
				    "bay_set_auth: failed to set %s: %s\n",
				    FM_FMRI_AUTH_PRODUCT, topo_strerror(err));
			}
			topo_mod_strfree(mod, prod);
		}
	}

	/* product-sn */
	rv = topo_prop_inherit(tn, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_PRODUCT_SN,
	    &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		/* not in parent node; check auth list */
		val = NULL;
		rv = nvlist_lookup_string(auth, FM_FMRI_AUTH_PRODUCT_SN, &val);
		if (rv != 0 || val == NULL) {
			/* no product info; report the error and drive on */
			topo_mod_dprintf(mod,
			    "bay_set_auth: no product-sn info\n");
		}
		psn = topo_mod_strdup(mod, val);

		if (psn != NULL) {
			rv = topo_prop_set_string(tn, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_PRODUCT_SN, TOPO_PROP_IMMUTABLE,
			    psn, &err);
			if (rv != 0) {
				/* report the error and drive on */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod,
				    "bay_set_auth: failed to set %s for %s\n",
				    FM_FMRI_AUTH_PRODUCT_SN,
				    topo_strerror(err));
			}
			topo_mod_strfree(mod, psn);
		}
	}

	/* chassis-id */
	rv = topo_prop_inherit(tn, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_CHASSIS,
	    &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		/* not in parent node; check auth list */
		val = NULL;
		rv = nvlist_lookup_string(auth, FM_FMRI_AUTH_CHASSIS, &val);
		if (rv != 0 || val == NULL) {
			/* no product info; report the error and drive on */
			topo_mod_dprintf(mod,
			    "bay_set_auth: no chassis-id\n");
		}
		csn = topo_mod_strdup(mod, val);

		if (csn != NULL) {
			rv = topo_prop_set_string(tn, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_CHASSIS, TOPO_PROP_IMMUTABLE,
			    csn, &err);
			if (rv != 0) {
				/* report the error and drive on */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod,
				    "bay_set_auth: failed to set %s for %s\n",
				    FM_FMRI_AUTH_CHASSIS, topo_strerror(err));
			}
			topo_mod_strfree(mod, csn);
		}
	}

	/* server-id */
	rv = topo_prop_inherit(tn, FM_FMRI_AUTHORITY, FM_FMRI_AUTH_SERVER,
	    &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		/* not in parent node; check auth list */
		val = NULL;
		rv = nvlist_lookup_string(auth, FM_FMRI_AUTH_SERVER, &val);
		if (rv != 0 || val == NULL) {
			/* no product info; report the error and drive on */
			topo_mod_dprintf(mod,
			    "bay_set_auth: no server info\n");
		}
		server = topo_mod_strdup(mod, val);

		if (server != NULL) {
			rv = topo_prop_set_string(tn, FM_FMRI_AUTHORITY,
			    FM_FMRI_AUTH_SERVER, TOPO_PROP_IMMUTABLE,
			    server, &err);
			if (rv != 0) {
				/* report the error and drive on */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod,
				    "bay_set_auth: failed to set %s for %s\n",
				    FM_FMRI_AUTH_SERVER, topo_strerror(err));
			}
			topo_mod_strfree(mod, server);
		}
	}

	nvlist_free(auth);

	return (0);
}

/*
 * Create the system pgoup and inherit the info from the parent.
 */
int
bay_set_system(topo_mod_t *mod, tnode_t *tn)
{
	int		rv;
	int		err;
	struct utsname	uts;
	char		isa[MAXNAMELEN];

	if (mod == NULL || tn == NULL) {
		topo_mod_dprintf(mod, "bay_set_system: NULL args.\n");
		return (-1);
	}

	rv = topo_pgroup_create(tn, &sys_pgroup, &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		/* not existing and failed to create */
		(void) topo_mod_seterrno(mod, err);
		topo_mod_dprintf(mod,
		    "bay_set_system: failed to create system pgroup: %s.\n",
		    topo_strerror(err));
		return (-1);
	}

	rv = topo_prop_inherit(tn, TOPO_PGROUP_SYSTEM, TOPO_PROP_ISA, &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		isa[0] = '\0';
		rv = sysinfo(SI_ARCHITECTURE, isa, sizeof (isa));
		if (rv == -1) {
			topo_mod_dprintf(mod,
			    "bay_set_system: failed to get ISA: %d\n", errno);
		}

		if (strnlen(isa, MAXNAMELEN) > 0) {
			rv = topo_prop_set_string(tn, TOPO_PGROUP_SYSTEM,
			    TOPO_PROP_ISA, TOPO_PROP_IMMUTABLE, isa, &err);
			if (rv != 0) {
				/* failed to inherit or create prop */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod,
				    "bay_set_system: failed to set isa "
				    "system property: %s\n",
				    topo_strerror(err));
			}
		}
	}

	rv = topo_prop_inherit(tn, TOPO_PGROUP_SYSTEM, TOPO_PROP_MACHINE,
	    &err);
	if (rv != 0 && err != ETOPO_PROP_DEFD) {
		rv = uname(&uts);
		if (rv == -1) {
			(void) topo_mod_seterrno(mod, errno);
			topo_mod_dprintf(mod,
			    "bay_set_system: failed to get uname: %d\n",
			    errno);
		}

		if (strnlen(uts.machine, sizeof (uts.machine)) > 0) {
			rv = topo_prop_set_string(tn, TOPO_PGROUP_SYSTEM,
			    TOPO_PROP_MACHINE, TOPO_PROP_IMMUTABLE,
			    uts.machine, &err);
			if (rv != 0) {
				/* failed to inherit or create prop */
				(void) topo_mod_seterrno(mod, err);
				topo_mod_dprintf(mod,
				    "bay_set_system: failed to set "
				    "system machine property: %s\n",
				    topo_strerror(err));
			}
		}
	}

	return (0);
}

/*
 * Create bay topo node.
 */
int
bay_create_tnode(topo_mod_t *mod, tnode_t *pnode, tnode_t **tnode, bay_t *bayp)
{
	int		rv = 0;
	nvlist_t	*fmri = NULL;
	nvlist_t	*auth = NULL;
	topo_instance_t	instance = bayp->inst;

	char		*f = "bay_create_tnode";

	topo_mod_dprintf(mod, "%s: parent node (%s) instance(%d)\n",
	    f, topo_node_name(pnode), instance);

	/* create FMRI */
	auth = topo_mod_auth(mod, pnode);

	fmri = topo_mod_hcfmri(mod,
	    cmp_str(topo_node_name(pnode), "hc") ? NULL : pnode,
	    FM_HC_SCHEME_VERSION, BAY, instance, NULL, auth, NULL, NULL, NULL);
	if (fmri == NULL) {
		topo_mod_dprintf(mod, "%s: failed to create fmri: %s\n",
		    f, topo_strerror(topo_mod_errno(mod)));
		rv = -1;
		goto out;
	}

	/* bind the node to the parent */
	*tnode = topo_node_bind(mod, pnode, BAY, instance, fmri);
	if (*tnode == NULL) {
		topo_mod_dprintf(mod, "%s: failed to bind node: %s\n",
		    f, topo_strerror(topo_mod_errno(mod)));
		rv = -1;
		goto out;
	}

	/* set the label (bay_set_label) */
	rv = bay_set_label(mod, bayp, *tnode);
	if (rv != 0) {
		/* report the error and drive on */
		topo_mod_dprintf(mod,
		    "%s: failed to set label for %s:%d: (%s)\n",
		    f, bayp->hba_nm, bayp->phy,
		    topo_strerror(topo_mod_errno(mod)));
	}

	/* set authority info (bay_set_auth) */
	rv = bay_set_auth(mod, pnode, *tnode);
	if (rv != 0) {
		/* report the error and drive on */
		topo_mod_dprintf(mod,
		    "%s: failed to set auth for %s:%d: (%s)\n",
		    f, bayp->hba_nm, bayp->phy,
		    topo_strerror(topo_mod_errno(mod)));
	}

	/* set the system info (bay_set_system) */
	rv = bay_set_system(mod, *tnode);
	if (rv != 0) {
		/* report the error and drive on */
		topo_mod_dprintf(mod,
		    "%s: failed to set system for %s:%d: (%s)\n",
		    f, bayp->hba_nm, bayp->phy,
		    topo_strerror(topo_mod_errno(mod)));
	}
out:
	if (auth != NULL) {
		nvlist_free(auth);
	}
	if (fmri != NULL) {
		nvlist_free(fmri);
	}
	return (rv);
}
