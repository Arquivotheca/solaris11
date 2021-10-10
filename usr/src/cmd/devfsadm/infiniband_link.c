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

#include <devfsadm.h>
#include <strings.h>
#include <sys/mkdev.h>
#include <sys/sunddi.h>

/*
 * Devlinks under:
 *   /dev/infiniband/ofs/
 *   /dev/infiniband/hca/
 */
#define	IB_DIR	 "infiniband"
#define	OFS_DIR  "ofs"
#define	HCA_DIR  "hca"

/* Use as: devfsadm -c "infiniband" */
#define	IB_CLASS "infiniband"

/* Create devlinks on callback */
static int uverbs_create(di_minor_t minor, di_node_t node);
static int rdma_cm_create(di_minor_t minor, di_node_t node);
static int umad_create(di_minor_t minor, di_node_t node);
static int hca_create(di_minor_t minor, di_node_t node);
static int daplt_create(di_minor_t minor, di_node_t node);

/*
 * Register callbacks for /dev/infiniband/ links
 */
static devfsadm_create_t ib_create_cbt[] = {
	{ IB_CLASS, DDI_PSEUDO, "sol_uverbs",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, uverbs_create
	},
	{ IB_CLASS, DDI_PSEUDO, "sol_ucma",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, rdma_cm_create
	},
	{ IB_CLASS, DDI_PSEUDO, "sol_umad",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, umad_create
	},
	{ IB_CLASS, DDI_PSEUDO, "hermon",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, hca_create
	},
	{ IB_CLASS, DDI_PSEUDO, "tavor",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, hca_create
	},
	{ IB_CLASS, DDI_PSEUDO, "daplt",
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, daplt_create
	}
};
DEVFSADM_CREATE_INIT_V0(ib_create_cbt);

/*
 * Register cleanup
 */
static devfsadm_remove_t ib_remove_cbt[] = {
	{ IB_CLASS, "^" IB_DIR "/" OFS_DIR "/uverbs([0-9]+|:event)$",
	    RM_ALWAYS | RM_PRE | RM_HOT, ILEVEL_0, devfsadm_rm_all
	},
	{ IB_CLASS, "^" IB_DIR "/" OFS_DIR "/rdma_cm$",
	    RM_ALWAYS | RM_PRE | RM_HOT, ILEVEL_0, devfsadm_rm_all
	},
	{ IB_CLASS, "^" IB_DIR "/" OFS_DIR "/(umad|issm)[0-9]+$",
	    RM_ALWAYS | RM_PRE | RM_HOT, ILEVEL_0, devfsadm_rm_all
	},
	{ IB_CLASS, "^" IB_DIR "/" HCA_DIR "/(hermon|tavor)[0-9]+$",
	    RM_ALWAYS | RM_PRE | RM_HOT, ILEVEL_0, devfsadm_rm_all
	},
	{ IB_CLASS, "^daplt$",
	    RM_ALWAYS | RM_PRE, ILEVEL_0, devfsadm_rm_all
	}
};
DEVFSADM_REMOVE_INIT_V0(ib_remove_cbt);

/*
 * /dev/infiniband/ofs/uverbs<n> -> /devices/ib/sol_uverbs@0:uverbs<n>
 * /dev/infiniband/ofs/uverbs:event -> /devices/ib/sol_uverbs@0:event
 * Devlinks not needed for sol_uverbs@0:ucma
 */
static int
uverbs_create(di_minor_t minor, di_node_t node)
{
	char mn[MAXNAMELEN + 1];
	char path[PATH_MAX + 1];

	(void) strcpy(mn, di_minor_name(minor));

	if (strcmp(mn, "ucma") == 0)
		return (DEVFSADM_CONTINUE);

	if (strcmp(mn, "event") == 0)
		(void) snprintf(path, sizeof (path),
		    IB_DIR "/" OFS_DIR "/uverbs:event");
	else
		(void) snprintf(path, sizeof (path),
		    IB_DIR "/" OFS_DIR "/%s", mn);

	(void) devfsadm_mklink(path, node, minor, 0);
	return (DEVFSADM_CONTINUE);
}

/*
 * /dev/infiniband/ofs/rdma_cm -> /devices/pseudo/sol_ucma@0:sol_ucma
 */
static int
rdma_cm_create(di_minor_t minor, di_node_t node)
{
	char path[PATH_MAX + 1];

	if (strcmp(di_minor_name(minor), "sol_ucma") == 0) {
		(void) snprintf(path, sizeof (path),
		    IB_DIR "/" OFS_DIR "/rdma_cm");
		(void) devfsadm_mklink(path, node, minor, 0);
	}
	return (DEVFSADM_CONTINUE);
}

/*
 * /dev/infiniband/ofs/umad<n> -> /devices/ib/sol_umad@0:umad<n>
 * /dev/infiniband/ofs/issm<n> -> /devices/ib/sol_umad@0:issm<n>
 */
static int
umad_create(di_minor_t minor, di_node_t node)
{
	char mn[MAXNAMELEN + 1];
	char path[PATH_MAX + 1];

	(void) strcpy(mn, di_minor_name(minor));

	(void) snprintf(path, sizeof (path),
	    IB_DIR "/" OFS_DIR "/%s", mn);
	(void) devfsadm_mklink(path, node, minor, 0);

	return (DEVFSADM_CONTINUE);
}

/*
 * /dev/infiniband/hermon<n> -> /devices/<hca pci address>:devctl
 * /dev/infiniband/tavor<n> -> /devices/<hca pci address>:devctl
 * Uses the minor (instance) number for <n>
 */
static int
hca_create(di_minor_t minor, di_node_t node)
{
	dev_t dev;
	char path[PATH_MAX + 1];

	dev = di_minor_devt(minor);
	if (strcmp(di_minor_name(minor), "devctl") == 0) {
		(void) snprintf(path, sizeof (path),
		    IB_DIR "/" HCA_DIR "/%s%d",
		    di_driver_name(node), (int)minor(dev));
		(void) devfsadm_mklink(path, node, minor, 0);
	}
	return (DEVFSADM_CONTINUE);
}

/* /dev/daplt */
static int
daplt_create(di_minor_t minor, di_node_t node)
{
	char mn[MAXNAMELEN + 1];

	(void) strcpy(mn, di_minor_name(minor));
	(void) devfsadm_mklink(mn, node, minor, 0);
	return (DEVFSADM_CONTINUE);
}
