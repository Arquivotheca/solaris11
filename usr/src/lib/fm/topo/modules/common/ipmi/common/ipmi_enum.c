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

#include <assert.h>
#include <fm/libtopo.h>
#include <fm/topo_mod.h>
#include <fm/fmd_fmri.h>
#include <sys/fm/protocol.h>
#include <string.h>

#define	TOPO_PGROUP_IPMI 		"ipmi"
#define	TOPO_PROP_IPMI_ENTITY_REF	"entity_ref"
#define	TOPO_PROP_IPMI_ENTITY_PRESENT	"entity_present"

typedef struct ipmi_enum_data {
	topo_mod_t	*ed_mod;
	tnode_t		*ed_pnode;
	const char	*ed_name;
	char		*ed_label;
	uint8_t		ed_entity;
	topo_instance_t	ed_instance;
	boolean_t	ed_hasfru;
} ipmi_enum_data_t;

static int ipmi_presence_state(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int ipmi_enum(topo_mod_t *, tnode_t *, const char *,
    topo_instance_t, topo_instance_t, void *, void *);
static int ipmi_post_process(topo_mod_t *, tnode_t *);

extern int ipmi_fru_label(topo_mod_t *mod, tnode_t *node,
    topo_version_t vers, nvlist_t *in, nvlist_t **out);

extern int ipmi_fru_fmri(topo_mod_t *mod, tnode_t *node,
    topo_version_t vers, nvlist_t *in, nvlist_t **out);

static const topo_method_t ipmi_methods[] = {
	{ TOPO_METH_PRESENCE_STATE, TOPO_METH_PRESENCE_STATE_DESC,
	    TOPO_METH_PRESENCE_STATE_VERSION0, TOPO_STABILITY_INTERNAL,
	    ipmi_presence_state },
	{ "ipmi_fru_label", "Property method", 0,
	    TOPO_STABILITY_INTERNAL, ipmi_fru_label},
	{ "ipmi_fru_fmri", "Property method", 0,
	    TOPO_STABILITY_INTERNAL, ipmi_fru_fmri},
	{ TOPO_METH_SENSOR_FAILURE, TOPO_METH_SENSOR_FAILURE_DESC,
	    TOPO_METH_SENSOR_FAILURE_VERSION, TOPO_STABILITY_INTERNAL,
	    topo_method_sensor_failure },
	{ NULL }
};

const topo_modops_t ipmi_ops = { ipmi_enum, NULL };

const topo_modinfo_t ipmi_info =
	{ "ipmi", FM_FMRI_SCHEME_HC, TOPO_VERSION, &ipmi_ops };

/*
 * Determine if the entity is present.
 */
/*ARGSUSED*/
static int
ipmi_presence_state(topo_mod_t *mod, tnode_t *tn, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	ipmi_handle_t *ihp;
	ipmi_entity_t *ep;
	boolean_t present;
	nvlist_t *nvl;
	int err, i;
	char *name, **names;
	ipmi_sdr_t *sdrp;
	uint_t nelems;

	if ((ihp = topo_mod_ipmi_hold(mod)) == NULL)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_UNKNOWN));

	ep = topo_node_getspecific(tn);
	if (ep == NULL) {
		if (topo_prop_get_string(tn, TOPO_PGROUP_IPMI,
		    TOPO_PROP_IPMI_ENTITY_PRESENT, &name, &err) == 0) {
			/*
			 * Some broken IPMI implementations don't export correct
			 * entities, so referring to an entity isn't sufficient.
			 * For these platforms, we allow the XML to specify a
			 * single SDR record that represents the current present
			 * state.
			 */
			if ((sdrp = ipmi_sdr_lookup(ihp, name)) == NULL ||
			    ipmi_entity_present_sdr(ihp, sdrp, &present) != 0) {
				topo_mod_dprintf(mod,
				    "Failed to get present state of %s (%s)\n",
				    name, ipmi_errmsg(ihp));
				topo_mod_strfree(mod, name);
				topo_mod_ipmi_rele(mod);
				return (-1);
			}

			topo_mod_dprintf(mod,
			    "ipmi_entity_present_sdr(%s) = %d\n", name,
			    present);
			topo_mod_strfree(mod, name);
		} else {
			if (topo_prop_get_string_array(tn, TOPO_PGROUP_IPMI,
			    TOPO_PROP_IPMI_ENTITY_REF, &names, &nelems, &err)
			    != 0) {
				/*
				 * Not all nodes have an entity_ref attribute.
				 * For these cases, return ENOTSUP so that we
				 * fall back to the default hc presence
				 * detection.
				 */
				topo_mod_ipmi_rele(mod);
				return (topo_mod_seterrno(mod,
				    ETOPO_METHOD_NOTSUP));
			}

			for (i = 0; i < nelems; i++)
				if ((ep = ipmi_entity_lookup_sdr(ihp, names[i]))
				    != NULL)
					break;

			for (i = 0; i < nelems; i++)
				topo_mod_strfree(mod, names[i]);
			topo_mod_free(mod, names, (nelems * sizeof (char *)));

			if (ep == NULL) {
				topo_mod_dprintf(mod,
				    "Failed to get present state of %s=%d\n",
				    topo_node_name(tn), topo_node_instance(tn));
				topo_mod_ipmi_rele(mod);
				return (-1);
			}
			topo_node_setspecific(tn, ep);
		}
	}

	if (ep != NULL) {
		if (ipmi_entity_present(ihp, ep, &present) != 0) {
			topo_mod_dprintf(mod,
			    "ipmi_entity_present() failed: %s",
			    ipmi_errmsg(ihp));
			topo_mod_ipmi_rele(mod);
			return (-1);
		}

		topo_mod_dprintf(mod,
		    "ipmi_entity_present(%d, %d) = %d\n", ep->ie_type,
		    ep->ie_instance, present);
	}

	topo_mod_ipmi_rele(mod);

	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));

	/*
	 * As we have no serial number support, return "unknown" instead
	 * of "present". "Unknown" means there's something there but we can't
	 * tell if it's the same device as before.
	 */
	if (nvlist_add_uint32(nvl, TOPO_METH_PRESENCE_STATE_RET, present ?
	    FMD_OBJ_STATE_UNKNOWN : FMD_OBJ_STATE_NOT_PRESENT) != 0) {
		nvlist_free(nvl);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	*out = nvl;

	return (0);
}

/*
 * This determines if the entity has a FRU locator record set, in which case we
 * treat this as a FRU, even if it's part of an association.
 */
/*ARGSUSED*/
static int
ipmi_check_sdr(ipmi_handle_t *ihp, ipmi_entity_t *ep, const char *name,
    ipmi_sdr_t *sdrp, void *data)
{
	ipmi_enum_data_t *edp = data;

	if (sdrp->is_type == IPMI_SDR_TYPE_FRU_LOCATOR)
		edp->ed_hasfru = B_TRUE;

	return (0);
}

/*
 * Main entity enumerator.  If we find a matching entity type, then instantiate
 * a topo node.
 */
static int
ipmi_check_entity(ipmi_handle_t *ihp, ipmi_entity_t *ep, void *data)
{
	ipmi_enum_data_t *edp = data;
	ipmi_enum_data_t cdata;
	tnode_t *pnode = edp->ed_pnode;
	topo_mod_t *mod = edp->ed_mod;
	nvlist_t *auth, *fmri;
	tnode_t *tn;
	topo_pgroup_info_t pgi;
	int err;
	const char *labelname;
	char label[64];
	size_t len;

	if (ep->ie_type != edp->ed_entity)
		return (0);

	/*
	 * The purpose of power and cooling domains is to group psus and fans
	 * together.  Unfortunately, some broken IPMI implementations declare
	 * domains that don't contain other elements.  Since the end goal is to
	 * only enumerate psus and fans, we'll just ignore such elements.
	 */
	if ((ep->ie_type == IPMI_ET_POWER_DOMAIN ||
	    ep->ie_type == IPMI_ET_COOLING_DOMAIN) &&
	    ep->ie_children == 0)
		return (0);

	if ((auth = topo_mod_auth(mod, pnode)) == NULL) {
		topo_mod_dprintf(mod, "topo_mod_auth() failed: %s",
		    topo_mod_errmsg(mod));
		return (1);
	}

	if ((fmri = topo_mod_hcfmri(mod, pnode, FM_HC_SCHEME_VERSION,
	    edp->ed_name, edp->ed_instance, NULL, auth, NULL, NULL,
	    NULL)) == NULL) {
		nvlist_free(auth);
		topo_mod_dprintf(mod, "topo_mod_hcfmri() failed: %s",
		    topo_mod_errmsg(mod));
		return (1);
	}

	nvlist_free(auth);

	if ((tn = topo_node_bind(mod, pnode, edp->ed_name,
	    edp->ed_instance, fmri)) == NULL) {
		nvlist_free(fmri);
		topo_mod_dprintf(mod, "topo_node_bind() failed: %s",
		    topo_mod_errmsg(mod));
		return (1);
	}

	/*
	 * We inherit our label from our parent, appending our label in the
	 * process.  This results in defaults labels of the form "FM 1 FAN 0"
	 * by default when given a hierarchy.
	 */
	if (edp->ed_label != NULL)
		(void) snprintf(label, sizeof (label), "%s ", edp->ed_label);
	else
		label[0] = '\0';

	switch (edp->ed_entity) {
	case IPMI_ET_POWER_DOMAIN:
		labelname = "PM";
		break;

	case IPMI_ET_PSU:
		labelname = "PSU";
		break;

	case IPMI_ET_COOLING_DOMAIN:
		labelname = "FM";
		break;

	case IPMI_ET_FAN:
		labelname = "FAN";
		break;
	}

	len = strlen(label);
	(void) snprintf(label + len, sizeof (label) - len, "%s %d",
	    labelname, edp->ed_instance);

	nvlist_free(fmri);
	edp->ed_instance++;

	if (topo_node_label_set(tn, label, &err) != 0) {
		topo_mod_dprintf(mod, "failed to set label: %s\n",
		    topo_strerror(err));
		return (1);
	}

	/*
	 * Store IPMI entity details as properties on the node
	 */
	pgi.tpi_name = TOPO_PGROUP_IPMI;
	pgi.tpi_namestab = TOPO_STABILITY_PRIVATE;
	pgi.tpi_datastab = TOPO_STABILITY_PRIVATE;
	pgi.tpi_version = TOPO_VERSION;
	if (topo_pgroup_create(tn, &pgi, &err) != 0) {
		if (err != ETOPO_PROP_DEFD) {
			topo_mod_dprintf(mod, "failed to create propgroup "
			    "%s: %s\n", TOPO_PGROUP_IPMI, topo_strerror(err));
			return (1);
		}
	}

	if (topo_method_register(mod, tn, ipmi_methods) != 0) {
		topo_mod_dprintf(mod, "topo_method_register() failed: %s",
		    topo_mod_errmsg(mod));
		return (1);
	}

	/*
	 * If we are a child of a non-chassis node, and there isn't an explicit
	 * FRU locator record, then propagate the parent's FRU.  Otherwise, set
	 * the FRU to be the same as the resource.
	 */
	edp->ed_hasfru = B_FALSE;
	(void) ipmi_entity_iter_sdr(ihp, ep, ipmi_check_sdr, edp);

	if (strcmp(topo_node_name(pnode), CHASSIS) == 0 ||
	    edp->ed_hasfru) {
		if (topo_node_resource(tn, &fmri, &err) != 0) {
			topo_mod_dprintf(mod, "topo_node_resource() failed: %s",
			    topo_strerror(err));
			(void) topo_mod_seterrno(mod, err);
			return (1);
		}
	} else {
		if (topo_node_fru(pnode, &fmri, NULL, &err) != 0) {
			topo_mod_dprintf(mod, "topo_node_fru() failed: %s",
			    topo_strerror(err));
			(void) topo_mod_seterrno(mod, err);
			return (1);
		}
	}

	if (topo_node_fru_set(tn, fmri, 0, &err) != 0) {
		nvlist_free(fmri);
		topo_mod_dprintf(mod, "topo_node_fru_set() failed: %s",
		    topo_strerror(err));
		(void) topo_mod_seterrno(mod, err);
		return (1);
	}

	topo_node_setspecific(tn, ep);

	nvlist_free(fmri);

	/*
	 * Iterate over children, once for recursive domains and once for
	 * psu/fans.
	 */
	if (ep->ie_children != 0 &&
	    (ep->ie_type == IPMI_ET_POWER_DOMAIN ||
	    ep->ie_type == IPMI_ET_COOLING_DOMAIN)) {
		cdata.ed_mod = edp->ed_mod;
		cdata.ed_pnode = tn;
		cdata.ed_instance = 0;
		cdata.ed_name = edp->ed_name;
		cdata.ed_entity = edp->ed_entity;
		cdata.ed_label = label;

		if (ipmi_entity_iter_children(ihp, ep,
		    ipmi_check_entity, &cdata) != 0)
			return (1);

		switch (cdata.ed_entity) {
		case IPMI_ET_POWER_DOMAIN:
			cdata.ed_entity = IPMI_ET_PSU;
			cdata.ed_name = PSU;
			break;

		case IPMI_ET_COOLING_DOMAIN:
			cdata.ed_entity = IPMI_ET_FAN;
			cdata.ed_name = FAN;
			break;
		}

		if (ipmi_entity_iter_children(ihp, ep,
		    ipmi_check_entity, &cdata) != 0)
			return (1);
	}

	return (0);
}

/*
 * libtopo enumeration point.  This simply iterates over entities looking for
 * the appropriate type.
 */
/*ARGSUSED*/
static int
ipmi_enum(topo_mod_t *mod, tnode_t *rnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *arg, void *unused)
{
	ipmi_handle_t *ihp;
	ipmi_enum_data_t data;
	int ret;

	/*
	 * If the node being passed in ISN'T the chassis node, then we're being
	 * asked to post-process a statically defined node.
	 */
	if (strcmp(topo_node_name(rnode), CHASSIS) != 0) {
		if (ipmi_post_process(mod, rnode) != 0) {
			topo_mod_dprintf(mod, "post processing of node %s=%d "
			    "failed!", topo_node_name(rnode),
			    topo_node_instance(rnode));
			return (-1);
		}
		return (0);
	}

	if (strcmp(name, POWERMODULE) == 0) {
		data.ed_entity = IPMI_ET_POWER_DOMAIN;
	} else if (strcmp(name, PSU) == 0) {
		data.ed_entity = IPMI_ET_PSU;
	} else if (strcmp(name, FANMODULE) == 0) {
		data.ed_entity = IPMI_ET_COOLING_DOMAIN;
	} else if (strcmp(name, FAN) == 0) {
		data.ed_entity = IPMI_ET_FAN;
	} else {
		topo_mod_dprintf(mod, "unknown enumeration type '%s'",
		    name);
		return (-1);
	}

	if ((ihp = topo_mod_ipmi_hold(mod)) == NULL)
		return (0);

	data.ed_mod = mod;
	data.ed_pnode = rnode;
	data.ed_name = name;
	data.ed_instance = 0;
	data.ed_label = NULL;

	if ((ret = ipmi_entity_iter(ihp, ipmi_check_entity, &data)) != 0) {
		/*
		 * We don't return failure if IPMI enumeration fails.  This may
		 * be due to the SP being unavailable or an otherwise transient
		 * event.
		 */
		if (ret < 0) {
			topo_mod_dprintf(mod,
			    "failed to enumerate entities: %s",
			    ipmi_errmsg(ihp));
		} else {
			topo_mod_ipmi_rele(mod);
			return (-1);
		}
	}

	topo_mod_ipmi_rele(mod);
	return (0);
}

static int
ipmi_post_process(topo_mod_t *mod, tnode_t *tn)
{
	if (topo_method_register(mod, tn, ipmi_methods) != 0) {
		topo_mod_dprintf(mod, "ipmi_post_process() failed: %s",
		    topo_mod_errmsg(mod));
		return (1);
	}
	return (0);
}

/*ARGSUSED*/
int
_topo_init(topo_mod_t *mod, topo_version_t version)
{
	if (getenv("TOPOIPMIDEBUG") != NULL)
		topo_mod_setdebug(mod);

	if (topo_mod_register(mod, &ipmi_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "%s registration failed: %s\n",
		    DISK, topo_mod_errmsg(mod));
		return (-1); /* mod errno already set */
	}

	topo_mod_dprintf(mod, "IPMI enumerator initialized\n");
	return (0);
}

void
_topo_fini(topo_mod_t *mod)
{
	topo_mod_unregister(mod);
}
