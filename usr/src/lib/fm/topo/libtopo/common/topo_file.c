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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <topo_error.h>
#include <topo_tree.h>
#include <topo_subr.h>
#include <topo_file.h>

/*
 * topo_file.c
 *
 *	This file hides the details of any file manipulation to
 *	establish topology for a given enumerator.
 */

#define	TOPO_DEFAULT_FILE	"maps/%s-%s-topology.xml"
#define	TOPO_COMMON_FILE	"maps/%s-topology.xml"

static void
topo_file_unload(topo_file_t *tfp)
{

	if (tfp == NULL)
		return;

	if (tfp->tf_filenm != NULL)
		topo_mod_strfree(tfp->tf_mod, tfp->tf_filenm);

	if (tfp->tf_tmap != NULL)
		tf_info_free(tfp->tf_mod, tfp->tf_tmap);

	topo_mod_free(tfp->tf_mod, tfp, sizeof (topo_file_t));
}

int
topo_file_load(topo_mod_t *mod, tnode_t *node, const char *name,
    const char *scheme, boolean_t pmap)
{
	topo_file_t *tfp;
	char fp[MAXNAMELEN];
	boolean_t load_snapshot;

	if (mod->tm_hdl->th_snap_dir == NULL)
		load_snapshot = B_FALSE;
	else
		load_snapshot = B_TRUE;

	topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP, "%s: mod=0x%p, node=0x%p, "
	    "name=%s, scheme=%s, pmap=%d, load_snapshot=%s\n",
	    __func__, (void *)mod, (void *)node,
	    (name == NULL) ? "NULL" : name, scheme, pmap,
	    (load_snapshot == B_TRUE) ? "TRUE" : "FALSE");

	if ((tfp = topo_mod_zalloc(mod, sizeof (topo_file_t))) == NULL)
		return (topo_mod_seterrno(mod, ETOPO_NOMEM));

	tfp->tf_mod = mod;

	/*
	 * If we're loading a topology snapshot, then "name" will be NULL
	 * and the directory name for the snapshot files will be in the
	 * topo handle struct. Otherwise,"name" will either be a map file
	 * prefix or NULL.
	 */
	if (load_snapshot == B_TRUE) {
		if (name != NULL) {
			topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
			    "%s: snapshot directory name=%s should not be "
			    " specified\n", __func__, name);
			return (topo_mod_seterrno(mod, ETOPO_MOD_NOENT));
		}
		(void) snprintf(fp, MAXNAMELEN, "%s/%s/%s-topology.xml",
		    mod->tm_rootdir, mod->tm_hdl->th_snap_dir, scheme);
		tfp->tf_filenm = topo_mod_strdup(mod, fp);
		topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
		    "%s: topo snapshot xml file is %s\n",
		    __func__, tfp->tf_filenm);
	} else {
		if (name != NULL)
			(void) snprintf(fp, MAXNAMELEN, TOPO_DEFAULT_FILE,
			    name, scheme);
		else
			(void) snprintf(fp, MAXNAMELEN, TOPO_COMMON_FILE,
			    scheme);
		if ((tfp->tf_filenm =
		    topo_search_path(mod, fp)) == NULL) {
			topo_file_unload(tfp);
			return (topo_mod_seterrno(mod, ETOPO_MOD_NOENT));
		}
		topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
		    "%s: topo xml file is %s\n", __func__, tfp->tf_filenm);
	}

	if ((tfp->tf_tmap = topo_xml_read(mod, tfp->tf_filenm, scheme))
	    == NULL) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
		    "%s: failed to load topology file %s: %s\n",
		    __func__, tfp->tf_filenm, topo_strerror(ETOPO_MOD_XRD));
		topo_file_unload(tfp);
		return (topo_mod_seterrno(mod, ETOPO_MOD_XRD));
	}

	if (pmap == B_TRUE)
		tfp->tf_tmap->tf_flags |= TF_PROPMAP;

	if (topo_xml_enum(mod, tfp->tf_tmap, node) < 0) {
		topo_dprintf(mod->tm_hdl, TOPO_DBG_ERR,
		    "%s: failed to enumerate topology: %s\n",
		    __func__, topo_strerror(ETOPO_MOD_XENUM));
		topo_file_unload(tfp);
		return (topo_mod_seterrno(mod, ETOPO_MOD_XENUM));
	}

	topo_file_unload(tfp);

	return (0);
}
