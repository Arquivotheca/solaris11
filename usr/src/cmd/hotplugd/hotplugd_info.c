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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libdevinfo.h>
#include <libhotplug.h>
#include <libhotplug_impl.h>
#include <sys/sunddi.h>
#include <sys/ddi_hp.h>
#include "hotplugd_impl.h"

/*
 * Definition of hp_node_t list.
 */
typedef struct {
	hp_node_t	head;
	hp_node_t	prev;
} hp_node_list_t;

/*
 * Definition of di_hp_t index array.
 */
typedef struct {
	int		type;
	int		connection;
	int		depends;
	di_hp_t		hp;
} hp_index_t;

/*
 * Definition of di_hp_t dependency table.
 */
typedef struct {
	int		depends;
	uint_t		first_index;
	uint_t		last_index;
	boolean_t	traversed;
} hp_depends_t;

/*
 * Local functions.
 */
static int		copy_devinfo(const char *, const char *, uint_t,
			    hp_node_t *);
static int		copy_devices(hp_node_t, di_node_t, uint_t, hp_node_t *);
static int		copy_hotplug(hp_node_t, di_node_t, const char *, uint_t,
			    hp_node_t *);
static char		*base_path(const char *);
static int		search_cb(di_node_t, void *);
static int		check_search(di_node_t, uint_t);
static hp_node_t	new_device_node(hp_node_t, di_node_t);
static hp_node_t	new_hotplug_node(hp_node_t, di_hp_t);
static void		node_list_add(hp_node_list_t *, hp_node_t);
static int		compare_index(const void *, const void *);
static int		compare_depends(const void *, const void *);
static int		count_depends(int, hp_index_t *);
static void		build_depends(int, int, hp_index_t *, hp_depends_t *);
static hp_depends_t	*search_depends(hp_depends_t *, int, int);
static int		copy_hotplug_traverse(hp_node_t, int, int, hp_index_t *,
			    int, hp_depends_t *, uint_t, hp_node_t *);
static int		copy_iov_usage(hp_node_t, void *argp);

/*
 * getinfo()
 *
 *	Build a hotplug information snapshot.  The path, connection,
 *	and flags indicate what information should be included.
 */
int
getinfo(const char *path, const char *connection, uint_t flags, hp_node_t *retp)
{
	hp_node_t	root = NULL;
	hp_node_t	child;
	char		*basepath;
	int		rv;

	if ((path == NULL) || (retp == NULL))
		return (EINVAL);

	dprintf("getinfo: path=%s, connection=%s, flags=0x%x\n", path,
	    (connection == NULL) ? "NULL" : connection, flags);

	/* Allocate the base path */
	if ((basepath = base_path(path)) == NULL)
		return (ENOMEM);

	/* Copy in device and hotplug nodes from libdevinfo */
	if ((rv = copy_devinfo(basepath, connection, flags, &root)) != 0) {
		hp_fini(root);
		free(basepath);
		return (rv);
	}

	/* Check if there were no connections */
	if (root == NULL) {
		dprintf("getinfo: no hotplug connections.\n");
		free(basepath);
		return (ENOENT);
	}

	/* Special case: exclude root nexus from snapshot */
	if (strcmp(basepath, "/") == 0) {
		child = root->hp_child;
		if (root->hp_name != NULL)
			free(root->hp_name);
		free(root);
		root = child;
		for (child = root; child; child = child->hp_sibling)
			child->hp_parent = NULL;
	}

	/* Store a pointer to the base path in each root node */
	for (child = root; child != NULL; child = child->hp_sibling)
		child->hp_basepath = basepath;

	/* Copy in usage information from RCM */
	if (flags & HPINFOUSAGE) {
		if ((rv = copy_usage(root)) != 0) {
			(void) hp_fini(root);
			return (rv);
		}

		/* XXX: Insert IOV usage */
		rv = 0;
		(void) hp_traverse(root, &rv, copy_iov_usage);
		if (rv != 0) {
			(void) hp_fini(root);
			return (rv);
		}
	}

	*retp = root;
	return (0);
}

/*
 * copy_devinfo()
 *
 *	Copy information about device and hotplug nodes from libdevinfo.
 *
 *	When path is set to "/", the results need to be limited only to
 *	branches that contain hotplug information.  An initial search
 * 	is performed to mark which branches contain hotplug nodes.
 */
static int
copy_devinfo(const char *path, const char *connection, uint_t flags,
    hp_node_t *rootp)
{
	hp_node_t	hp_root = NULL;
	di_node_t	di_root;
	int		rv;

	/* Get libdevinfo snapshot */
	if ((di_root = di_init(path, DINFOSUBTREE | DINFOHP)) == DI_NODE_NIL)
		return (errno);

	/* Do initial search pass, if required */
	if (strcmp(path, "/") == 0) {
		flags |= HPINFOSEARCH;
		(void) di_walk_node(di_root, DI_WALK_CLDFIRST, NULL, search_cb);
	}

	/*
	 * If a connection is specified, just copy immediate hotplug info.
	 * Else, copy the device tree normally.
	 */
	if (connection != NULL)
		rv = copy_hotplug(NULL, di_root, connection, flags, &hp_root);
	else
		rv = copy_devices(NULL, di_root, flags, &hp_root);

	/* Destroy devinfo snapshot */
	di_fini(di_root);

	*rootp = (rv == 0) ? hp_root : NULL;
	return (rv);
}

/*
 * copy_devices()
 *
 *	Copy a full branch of device nodes.  Used by copy_devinfo() and
 *	copy_hotplug().
 */
static int
copy_devices(hp_node_t parent, di_node_t dev, uint_t flags, hp_node_t *rootp)
{
	hp_node_list_t	children;
	hp_node_t	self, branch;
	di_node_t	child;
	int		rv = 0;

	/* Initialize results */
	*rootp = NULL;

	/* Enforce search semantics */
	if (check_search(dev, flags) == 0)
		return (0);

	/* Allocate new node for current device */
	if ((self = new_device_node(parent, dev)) == NULL)
		return (ENOMEM);

	/*
	 * If the device has hotplug nodes, then use copy_hotplug()
	 * instead to build the branch associated with current device.
	 */
	if (di_hp_next(dev, DI_HP_NIL) != DI_HP_NIL) {
		if ((rv = copy_hotplug(self, dev, NULL, flags,
		    &self->hp_child)) != 0) {
			free(self);
			return (rv);
		}
		*rootp = self;
		return (0);
	}

	/*
	 * The device does not have hotplug nodes.  Use normal
	 * approach of iterating through its child device nodes.
	 */
	(void) memset(&children, 0, sizeof (hp_node_list_t));
	for (child = di_child_node(dev); child != DI_NODE_NIL;
	    child = di_sibling_node(child)) {
		branch = NULL;
		if ((rv = copy_devices(self, child, flags, &branch)) != 0) {
			(void) hp_fini(children.head);
			free(self);
			return (rv);
		}
		if (branch != NULL)
			node_list_add(&children, branch);
	}
	self->hp_child = children.head;

	/* Done */
	*rootp = self;
	return (0);
}

/*
 * copy_hotplug()
 *
 *	Copy a full branch of hotplug nodes.  Used by copy_devinfo()
 *	and copy_devices().
 *
 *	The first steps are to assemble an index array and a dependency
 *	table.  The index array contains all hotplug nodes sorted in a way
 *	that simplifies the other steps.  The dependency table refers to
 *	the index array to identify sets of nodes that depend upon others.
 *
 *	The next step is to select the start of the copy.  If a connection
 *	was specified, it will be located in the index array as the only
 *	root of the copy operation.  Otherwise, all root nodes are selected.
 *
 *	The copy operation is then implemented by iterating through the
 *	selected roots, using copy_hotplug_traverse() to copy each branch.
 *	The index array and dependency table are used to then traverse the
 *	layers of dependency in proper order.
 */
static int
copy_hotplug(hp_node_t parent, di_node_t dev, const char *connection,
    uint_t flags, hp_node_t *retp)
{
	di_hp_t		hp;
	hp_index_t	*nodes = NULL;
	hp_depends_t	*deps = NULL;
	int		i, rv, first, last, num_nodes, num_deps;

	/* Count number of nodes */
	num_nodes = 0;
	for (hp = DI_HP_NIL; (hp = di_hp_next(dev, hp)) != DI_HP_NIL; )
		num_nodes++;

	/* Stop if no nodes found */
	if (num_nodes == 0)
		return (ENXIO);

	/* Create index array */
	nodes = (hp_index_t *)malloc(num_nodes * sizeof (hp_index_t));
	if (nodes == NULL) {
		log_err("Out of memory for hotplug index array.\n");
		return (ENOMEM);
	}
	for (hp = DI_HP_NIL, i = 0; i < num_nodes; i++) {
		hp = di_hp_next(dev, hp);
		nodes[i].type = di_hp_type(hp);
		nodes[i].connection = di_hp_connection(hp);
		nodes[i].depends = di_hp_depends_on(hp);
		nodes[i].hp = hp;
	}
	(void) qsort(nodes, num_nodes, sizeof (hp_index_t), compare_index);

	/* Create dependency table */
	num_deps = count_depends(num_nodes, nodes);
	if (num_deps > 0) {
		deps = (hp_depends_t *)calloc(num_deps, sizeof (hp_depends_t));
		if (deps == NULL) {
			rv = ENOMEM;
			log_err("Out of memory for hotplug depends table.\n");
			goto cleanup;
		}
		build_depends(num_nodes, num_deps, nodes, deps);
	}

	/* Select nodes to traverse */
	if (connection == NULL) {
		for (first = 0, last = -1, i = 0; i < num_nodes; i++, last++)
			if (nodes[i].depends != DDI_HP_CN_NUM_NONE)
				break;
	} else {
		for (i = 0; i < num_nodes; i++)
			if (strcmp(connection, di_hp_name(nodes[i].hp)) == 0) {
				first = last = i;
				break;
			}
		if (i >= num_nodes) {
			rv = ENXIO;
			goto cleanup;
		}
	}

	/* Perform the copy traversal */
	rv = copy_hotplug_traverse(parent, first, last, nodes, num_deps, deps,
	    flags, retp);

cleanup:
	if (nodes != NULL)
		free(nodes);
	if (deps != NULL)
		free(deps);
	return (rv);
}

/*
 * base_path()
 *
 *	Normalize the base path of a hotplug information snapshot.
 *	The caller must free the string that is allocated.
 */
static char *
base_path(const char *path)
{
	char	*base_path;
	size_t	devices_len;

	devices_len = strlen(S_DEVICES);

	if (strncmp(path, S_DEVICES, devices_len) == 0)
		base_path = strdup(&path[devices_len]);
	else
		base_path = strdup(path);

	return (base_path);
}

/*
 * search_cb()
 *
 *	Callback function used by di_walk_node() to search for branches
 *	of the libdevinfo snapshot that contain hotplug nodes.
 */
/*ARGSUSED*/
static int
search_cb(di_node_t node, void *arg)
{
	di_node_t	parent;
	uint_t		flags;

	(void) di_node_private_set(node, (void *)(uintptr_t)0);

	if (di_hp_next(node, DI_HP_NIL) == DI_HP_NIL)
		return (DI_WALK_CONTINUE);

	for (parent = node; parent != DI_NODE_NIL;
	    parent = di_parent_node(parent)) {
		flags = (uint_t)(uintptr_t)di_node_private_get(parent);
		flags |= HPINFOSEARCH;
		(void) di_node_private_set(parent, (void *)(uintptr_t)flags);
	}

	return (DI_WALK_CONTINUE);
}

/*
 * check_search()
 *
 *	Check if a device node was marked by an initial search pass.
 */
static int
check_search(di_node_t dev, uint_t flags)
{
	uint_t	dev_flags;

	if (flags & HPINFOSEARCH) {
		dev_flags = (uint_t)(uintptr_t)di_node_private_get(dev);
		if ((dev_flags & HPINFOSEARCH) == 0)
			return (0);
	}

	return (1);
}

/*
 * node_list_add()
 *
 *	Utility function to append one node to a list of hotplug nodes.
 */
static void
node_list_add(hp_node_list_t *listp, hp_node_t node)
{
	if (listp->prev != NULL)
		listp->prev->hp_sibling = node;
	else
		listp->head = node;

	listp->prev = node;
}

/*
 * new_device_node()
 *
 *	Build a new hotplug node based on a specified devinfo node.
 */
static hp_node_t
new_device_node(hp_node_t parent, di_node_t dev)
{
	hp_node_t	node;
	int		instance;
	char		*driver_name, *node_name, *bus_addr;
	char		name[MAXPATHLEN];

	node = (hp_node_t)calloc(1, sizeof (struct hp_node));

	if (node != NULL) {
		node->hp_parent = parent;
		node->hp_type = HP_NODE_DEVICE;

		node_name = di_node_name(dev);
		bus_addr = di_bus_addr(dev);
		if (bus_addr && (strlen(bus_addr) > 0)) {
			if (snprintf(name, sizeof (name), "%s@%s", node_name,
			    bus_addr) >= sizeof (name)) {
				log_err("Path too long for device node.\n");
				free(node);
				return (NULL);
			}
			node->hp_name = strdup(name);
		} else {
			node->hp_name = strdup(node_name);
		}

		if (node->hp_name == NULL) {
			log_err("Out of memory for device node.\n");
			free(node);
			return (NULL);
		}

		if (((driver_name = di_driver_name(dev)) != NULL) &&
		    ((node->hp_driver = strdup(driver_name)) == NULL)) {
			log_err("Out of memory for device node.\n");
			free(node->hp_name);
			free(node);
			return (NULL);
		}

		if ((instance = di_instance(dev)) != -1)
			node->hp_instance = instance;
	}

	return (node);
}

/*
 * new_hotplug_node()
 *
 *	Build a new hotplug node based on a specified devinfo hotplug node.
 */
static hp_node_t
new_hotplug_node(hp_node_t parent, di_hp_t hp)
{
	hp_node_t	node;
	char		*s;

	node = (hp_node_t)calloc(1, sizeof (struct hp_node));

	if (node != NULL) {
		node->hp_parent = parent;
		node->hp_state = di_hp_state(hp);
		node->hp_state_priv = unpack_state_priv(di_hp_state_priv(hp),
		    di_hp_state_priv_size(hp));
		node->hp_last_change = di_hp_last_change(hp);

		if ((s = di_hp_name(hp)) != NULL)
			node->hp_name = strdup(s);
		if ((s = di_hp_description(hp)) != NULL)
			node->hp_description = strdup(s);
		if ((di_hp_type(hp) & DDI_HP_CN_TYPE_CONNECTOR_MASK) != 0)
			node->hp_type = HP_NODE_CONNECTOR;
		else
			node->hp_type = HP_NODE_PORT;

		if ((node->hp_name == NULL) || (node->hp_description == NULL)) {
			log_err("Out of memory for hotplug node.\n");
			free(node);
			return (NULL);
		}
	}

	return (node);
}

/*
 * compare_index()
 *
 *	Compare two hp_index_t structures (e.g. for sorting).
 *
 *	The first order of sorting is by 'depends_on' value.  This will
 *	place roots at the beginning (their 'depends_on' value is -1).
 *	It will also place peer items into contiguous groups.
 *
 *	The second order of sorting within a set of peers is by type.
 *	Virtual ports are sorted before physical connectors.
 *
 *	The third order of sorting within a set of peers of equal type
 *	is by connection number.
 *
 *	The following is an example of hotplug items in sorted order:
 *
 *		{ depends_on = -1, type = PORT, connection = 0 }
 *		{ depends_on = -1, type = PORT, connection = 1 }
 *		{ depends_on = -1, type = CONNECTOR, connection = 256 }
 *		{ depends_on = -1, type = CONNECTOR, connection = 512 }
 *		{ depends_on = 256, type = PORT, connection = 2 }
 *		{ depends_on = 256, type = PORT, connection = 3 }
 *		{ depends_on = 512, type = PORT, connection = 4 }
 *		{ depends_on = 512, type = PORT, connection = 5 }
 */
static int
compare_index(const void *a, const void *b)
{
	hp_index_t	*hp_a = (hp_index_t *)a;
	hp_index_t	*hp_b = (hp_index_t *)b;

	/* First sort by dependency */
	if (hp_a->depends < hp_b->depends)
		return (-1);
	if (hp_a->depends > hp_b->depends)
		return (1);

	/* Then sort by type (ports before connectors) */
	if (hp_a->type < hp_b->type)
		return (-1);
	if (hp_a->type > hp_b->type)
		return (1);

	/* Finally, sort by connection number */
	if (hp_a->connection < hp_b->connection)
		return (-1);
	if (hp_a->connection > hp_b->connection)
		return (1);

	return (0);
}

/*
 * compare_depends()
 *
 *	Compare two hp_depends_t structures (e.g. for sorting).
 *
 *	Items are sorted by the connection numbers that each set
 *	of items depends upon.
 */
static int
compare_depends(const void *a, const void *b)
{
	hp_depends_t	*list_a = (hp_depends_t *)a;
	hp_depends_t	*list_b = (hp_depends_t *)b;

	/* First sort by 'depends' value */
	if (list_a->depends < list_b->depends)
		return (-1);
	if (list_a->depends > list_b->depends)
		return (1);

	/* Then sort by 'first_index' */
	if (list_a->first_index < list_b->first_index)
		return (-1);
	if (list_a->first_index > list_b->first_index)
		return (1);

	return (0);
}

/*
 * count_depends()
 *
 *	Count the total number of dependency lists within an index array.
 */
static int
count_depends(int num_nodes, hp_index_t *nodes)
{
	int		i;
	int		prev = -1;
	int		num_deps = 0;
	boolean_t	vf_boundary;

	/* Count by detecting boundaries in the index array */
	for (i = 0; i < num_nodes; i++) {
		if (nodes[i].depends != DDI_HP_CN_NUM_NONE) {

			/* First dependent node is a boundary */
			if (prev == -1) {
				prev = i;
				num_deps++;
				continue;
			}

			/*
			 * Change in dependency, or transition from
			 * VF to non-VF nodes is also a boundary.
			 */
			vf_boundary = B_FALSE;
			if ((nodes[i].type != nodes[prev].type) &&
			    ((nodes[i].type == DDI_HP_CN_TYPE_PORT_IOV_VF) ||
			    (nodes[prev].type == DDI_HP_CN_TYPE_PORT_IOV_VF)))
				vf_boundary = B_TRUE;
			if (vf_boundary ||
			    (nodes[i].depends != nodes[prev].depends)) {
				prev = i;
				num_deps++;
			}
		}
	}

	return (num_deps);
}

/*
 * build_depends()
 *
 *	Initialize a dependency table based on the specified index array.
 *
 *	The index array is sorted using compare_index(), which arranges
 *	root nodes at the start of the array and then groups nodes with
 *	the same parent in contiguous spans.  A linear scan of the array
 *	can then identify these sets of dependent nodes.
 *
 *	The resulting table is an array of structures containing the sets
 *	of dependent nodes.  Each entry specifies the connection number of
 *	the parent, and the first and last indices in the index array of
 *	the members of the set.  The table is sorted by parent connection
 *	number so that a quick binary search can locate the dependents of
 *	any node in the index array.
 */
static void
build_depends(int n_nodes, int n_deps, hp_index_t *nodes, hp_depends_t *deps)
{
	int		i, j, prev;
	boolean_t	vf_boundary;

	/* Skip root nodes in the beginning of the index array */
	for (i = 0; nodes[i].depends == DDI_HP_CN_NUM_NONE; i++)
		;

	/* Gather first/last indices of each dependency list */
	for (j = 0; j < n_deps; j++) {
		deps[j].depends = nodes[i].depends;
		deps[j].first_index = i;
		for (prev = i; i < n_nodes; i++) {

			/* Detect boundary between VF and non-VF nodes */
			vf_boundary = B_FALSE;
			if ((nodes[i].type != nodes[prev].type) &&
			    ((nodes[i].type == DDI_HP_CN_TYPE_PORT_IOV_VF) ||
			    (nodes[prev].type == DDI_HP_CN_TYPE_PORT_IOV_VF)))
				vf_boundary = B_TRUE;

			/*
			 * Set boundary is when dependency changes, or a
			 * boundary between VF and non-VF nodes is detected.
			 */
			if (vf_boundary ||
			    (nodes[i].depends != nodes[prev].depends))
				break;
		}
		deps[j].last_index = i - 1;
	}

	/* Sort the lists by connection number */
	(void) qsort(deps, n_deps, sizeof (hp_depends_t), compare_depends);
}

/*
 * search_depends()
 *
 *	Search a table of dependency sets for the next set of nodes
 *	that depends on the specified connection number.  Only sets
 *	which have not been traversed yet are returned, and only in
 *	their proper sorted order.
 *
 *	The 'traversed' flag is toggled before returning a match.
 *
 *	Returns NULL if no such set exists.
 */
static hp_depends_t *
search_depends(hp_depends_t *deps, int num_deps, int keyval)
{
	int		i, min, max;

	/* Binary search for a matching set */
	min = 0;
	max = num_deps - 1;
	while (min <= max) {

		i = (min + max) / 2;

		/*
		 * If a match is found, then check 'traversed' flag.
		 * If current match already traversed, then search
		 * forward for more possible matches.  Else, search
		 * backward for more possible matches.
		 */
		if (deps[i].depends == keyval) {
			if (deps[i].traversed) {
				while (++i < num_deps) {
					if ((deps[i].depends == keyval) &&
					    (deps[i].traversed == B_FALSE)) {
						deps[i].traversed = B_TRUE;
						return (&deps[i]);
					}
				}
				return (NULL);
			} else {
				while (i > 0) {
					if ((deps[i - 1].depends != keyval) ||
					    (deps[i - 1].traversed == B_TRUE))
						break;
					i--;
				}
				deps[i].traversed = B_TRUE;
				return (&deps[i]);
			}
		}

		if (keyval > deps[i].depends)
			min = i + 1;
		else
			max = i - 1;
	}

	/* No match */
	return (NULL);
}

/*
 * copy_hotplug_traverse()
 *
 *	Called from copy_hotplug() to recursively copy hotplug
 *	information nodes through multiple layers of dependency.
 */
static int
copy_hotplug_traverse(hp_node_t parent, int first, int last, hp_index_t *nodes,
    int num_deps, hp_depends_t *deps_table, uint_t flags, hp_node_t *retp)
{
	hp_node_t	node, branch;
	hp_node_list_t	node_list, children_list;
	hp_depends_t	*deps;
	di_node_t	child_dev;
	uint_t		child_flags;
	int		i, rv;

	/* Stop implementing the HPINFOSEARCH flag */
	child_flags = flags & ~(HPINFOSEARCH);

	/* Initialize empty node list */
	(void) memset(&node_list, 0, sizeof (hp_node_list_t));

	/* Iterate through current dependency layer */
	for (i = first; i <= last; i++) {

		/* Create each node */
		if ((node = new_hotplug_node(parent, nodes[i].hp)) == NULL) {
			(void) hp_fini(node_list.head);
			*retp = NULL;
			return (ENOMEM);
		}

		/* Append node to current list */
		node_list_add(&node_list, node);

		/* Initialize a list of child branches */
		(void) memset(&children_list, 0, sizeof (hp_node_list_t));

		/* Add branch of child devices */
		if ((child_dev = di_hp_child(nodes[i].hp)) != DI_NODE_NIL) {
			branch = NULL;
			if ((rv = copy_devices(node, child_dev, child_flags,
			    &branch)) != 0) {
				(void) hp_fini(node_list.head);
				*retp = NULL;
				return (rv);
			}
			if (branch != NULL)
				node_list_add(&children_list, branch);
		}

		/* Traverse dependents */
		if ((num_deps > 0) &&
		    ((nodes[i].type & DDI_HP_CN_TYPE_CONNECTOR_MASK) ||
		    (nodes[i].type == DDI_HP_CN_TYPE_PORT_IOV_PF))) {

			/* Search for a set of dependents */
			deps = search_depends(deps_table, num_deps,
			    nodes[i].connection);

			/* Traverse the set of dependents */
			if (deps != NULL) {
				branch = NULL;
				if ((rv = copy_hotplug_traverse(node,
				    deps->first_index, deps->last_index, nodes,
				    num_deps, deps_table, flags,
				    &branch)) != 0) {
					(void) hp_fini(children_list.head);
					(void) hp_fini(node_list.head);
					*retp = NULL;
					return (rv);
				}
				if (branch != NULL)
					node_list_add(&children_list, branch);
			}
		}

		/* Add traversed children to current node */
		node->hp_child = children_list.head;
	}

	*retp = node_list.head;
	return (0);
}

/*
 * copy_iov_usage()
 *
 *	XXX: Temporary routine to insert IOV dependency information.
 */
static int
copy_iov_usage(hp_node_t node, void *argp)
{
	hp_node_t	usage;
	hp_node_t	target;
	hp_node_t	prev;
	hp_node_t	next;
	boolean_t	pf_flag = B_FALSE;
	char		*desc;
	char		usage_str[MAXPATHLEN];

	/* Only analyze virtual ports */
	if (hp_type(node) != HP_NODE_PORT)
		return (HP_WALK_CONTINUE);

	/* Get node description */
	if ((desc = hp_description(node)) == NULL)
		return (HP_WALK_CONTINUE);

	/* Identify node type, and only process IOV nodes */
	if (strcmp(desc, DDI_HP_CN_TYPE_STR_PORT_IOV_PF) == 0)
		pf_flag = B_TRUE;
	else if (strcmp(desc, DDI_HP_CN_TYPE_STR_PORT_IOV_VF) != 0)
		return (HP_WALK_CONTINUE);

	/*
	 * Only add usage to PF nodes.  If the current node is a PF, then
	 * add usage about being a PF.  If the current node is a VF, then
	 * add usage about the dependent VF to its parent PF.  If parent
	 * is not in the snapshot (e.g. root of snapshot is a VF), then
	 * no usage will be added.
	 */
	target = (pf_flag) ? node : hp_parent(node);
	if (target == NULL)
		/* PF is not in snapshot */
		return (HP_WALK_CONTINUE);

	/* Allocate new usage node */
	if ((usage = (hp_node_t)calloc(1, sizeof (struct hp_node))) == NULL) {
		log_err("Cannot allocate hotplug usage node.\n");
		*(int *)argp = ENOMEM;
		return (HP_WALK_TERMINATE);
	}

	/* Initialize the usage, depending on current node type */
	if (pf_flag)
		(void) snprintf(usage_str, sizeof (usage_str),
		    "IOV physical function");
	else
		(void) snprintf(usage_str, sizeof (usage_str),
		    "IOV virtual function '%s'", hp_name(node));
	usage->hp_type = HP_NODE_USAGE;
	usage->hp_name = strdup(hp_name(node));
	usage->hp_usage = strdup(usage_str);
	if ((usage->hp_name == NULL) || (usage->hp_usage == NULL)) {
		log_err("Cannot initialize hotplug usage node.\n");
		if (usage->hp_name != NULL)
			free(usage->hp_name);
		free(usage);
		*(int *)argp = ENOMEM;
		return (HP_WALK_TERMINATE);
	}

	/* Append the usage node to the selected target */
	target = target->hp_child;
	if (target == NULL)
		return (HP_WALK_CONTINUE);

	prev = NULL;
	next = target->hp_child;

	if (pf_flag) {
		/* Insert PF's usage info to the head of the list */
		usage->hp_sibling = target->hp_child;
		target->hp_child = usage;
	} else {
		/* Insert VF's usage info to the end of the list */
		while (next != NULL) {
			prev = next;
			next = next->hp_sibling;
		}
		usage->hp_parent = target;
		usage->hp_sibling = next;
		if (prev == NULL)
			target->hp_child = usage;
		else
			prev->hp_sibling = usage;
	}

	return (HP_WALK_CONTINUE);
}
