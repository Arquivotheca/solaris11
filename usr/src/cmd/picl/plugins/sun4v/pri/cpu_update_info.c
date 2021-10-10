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

#include "priplugin.h"

#define	MDESC_PATH	"/devices/pseudo/mdesc@0:mdesc"

#define	SUN4V_CPU_REGSIZE	4
#define	CFGHDL_TO_CPUID(x)	(x  & ~(0xful << 28))


static uint64_t		*mdesc_bufp = NULL;
static md_t		*mdescp = NULL;
static mde_cookie_t	rootnode;


static md_t *mdesc_devinit(void);
static void mdesc_free(void *, size_t);
static void mdesc_devfini(md_t *);

static void update_clock_frequency(picl_nodehdl_t, mde_cookie_t);
static int update_cpu_prop(picl_nodehdl_t, void *);

/*
 * Update all CPU nodes in the PICL tree based on information
 * from the current MD.
 * Currently this just updates the 'clock-frequency' property
 * which may have changed since a PRI update is received
 * after the successful migration of a guest domain.
 */
void
cpu_update_info(void)
{
	picl_nodehdl_t	picl_root_node;
	int		status;

	mdescp = mdesc_devinit();
	if (mdescp == NULL) {
		pri_debug(LOG_NOTICE, "cpu_update_info: "
		    "mdescp is NULL\n");
	}

	rootnode = md_root_node(mdescp);

	status = ptree_get_root(&picl_root_node);
	if (status != PICL_SUCCESS) {
		pri_debug(LOG_NOTICE, "cpu_update_info: "
		    "can't get picl tree root node: %s\n",
		    picl_strerror(status));
		return;
	}

	status = ptree_walk_tree_by_class(picl_root_node, PICL_CLASS_CPU,
	    NULL, update_cpu_prop);

	if (status != PICL_SUCCESS) {
		pri_debug(LOG_NOTICE, "cpu_update_info: "
		    "can't update CPU properties: \n",
		    picl_strerror(status));
	}

	pri_debug(LOG_NOTICE, "cpu_update_info: update successful\n");

	mdesc_devfini(mdescp);
	mdescp = NULL;
}

static md_t *
mdesc_devinit(void)
{
	int fd;
	md_t *mdescp;
	size_t size;

	/*
	 * We haven't finished using the previous MD/PRI info.
	 */
	if (mdesc_bufp != NULL)
		return (NULL);

	do {
		if ((fd = open(MDESC_PATH, O_RDONLY, 0)) < 0)
			break;

		if (ioctl(fd, MDESCIOCGSZ, &size) < 0)
			break;
		if ((mdesc_bufp = (uint64_t *)malloc(size)) == NULL) {
			(void) close(fd);
			break;
		}

		/*
		 * A partial read is as bad as a failed read.
		 */
		if (read(fd, mdesc_bufp, size) != size) {
			free(mdesc_bufp);
			mdesc_bufp = NULL;
		}

		(void) close(fd);
	/*LINTED: E_CONSTANT_CONDITION */
	} while (0);

	if (mdesc_bufp) {
		mdescp = md_init_intern(mdesc_bufp, malloc, mdesc_free);
		if (mdescp == NULL) {
			free(mdesc_bufp);
			mdesc_bufp = NULL;
		}
	} else
		mdescp = NULL;

	return (mdescp);
}

/*ARGSUSED*/
static void
mdesc_free(void *bufp, size_t size)
{
	if (bufp)
		free(bufp);
}

static void
mdesc_devfini(md_t *mdp)
{
	if (mdp)
		(void) md_fini(mdp);

	if (mdesc_bufp)
		free(mdesc_bufp);
	mdesc_bufp = NULL;
}

static void
update_clock_frequency(picl_nodehdl_t pnode, mde_cookie_t mnode)
{
	int		status;
	uint64_t	uint64_value;
	uint32_t	uint32_value;
	uint32_t	pnode_propval;

	/*
	 * Get clock-frequency value from the specified MD node
	 */
	if (md_get_prop_val(mdescp, mnode, "clock-frequency",
	    &uint64_value)) {
		return;
	}
	uint32_value = (uint32_t)(uint64_value & UINT32_MAX);

	/*
	 * Get clock-frequency value (if the property exists)
	 * from the PICL tree node.
	 */
	status = ptree_get_propval_by_name(pnode, "clock-frequency",
	    &pnode_propval, sizeof (pnode_propval));

	if (status != PICL_SUCCESS) {
		/*
		 * Do nothing if we can't get the property.
		 */
		return;
	}

	/*
	 * Compare the values and update the PICL node
	 * if they differ.
	 */
	if (pnode_propval != uint32_value) {
		(void) ptree_update_propval_by_name(pnode,
		    "clock-frequency", &uint32_value,
		    sizeof (uint32_t));
	}
}

static int
update_cpu_prop(picl_nodehdl_t node, void *args)
{
	mde_cookie_t *cpulistp;
	int x, num_nodes;
	int ncpus;
	int status;
	int reg_prop[SUN4V_CPU_REGSIZE], cpuid;
	uint64_t int64_value;

	/*
	 * Get the CPU id for the CPU this PICL node represents.
	 */
	status = ptree_get_propval_by_name(node, OBP_REG, reg_prop,
	    sizeof (reg_prop));
	if (status != PICL_SUCCESS) {
		return (PICL_WALK_TERMINATE);
	}
	cpuid = CFGHDL_TO_CPUID(reg_prop[0]);

	/*
	 * Allocate space on the stack for MD data.
	 */
	num_nodes = md_node_count(mdescp);

	cpulistp = (mde_cookie_t *)alloca(sizeof (mde_cookie_t) * num_nodes);
	if (cpulistp == NULL) {
		return (PICL_WALK_TERMINATE);
	}

	/*
	 * Starting at the root node, scan the "fwd" dag for
	 * all the cpus in this description.
	 */
	ncpus = md_scan_dag(mdescp, rootnode, md_find_name(mdescp, "cpu"),
	    md_find_name(mdescp, "fwd"), cpulistp);

	if (ncpus < 0) {
		return (PICL_WALK_TERMINATE);
	}

	/*
	 * Update the clock-frequency of the PICL node with the values
	 * from the current MD.
	 */
	for (x = 0; x < ncpus; x++) {
		if (md_get_prop_val(mdescp, cpulistp[x], "id", &int64_value)) {
			continue;
		}

		if (int64_value != cpuid)
			continue;

		update_clock_frequency(node, cpulistp[x]);
	}

	return (PICL_WALK_CONTINUE);
}
