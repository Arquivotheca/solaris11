/*
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <fcntl.h>
#include <strings.h>
#include <sys/ioctl.h>
#include "ipf.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_pool.h"

static int poolfd = -1;

/*
 * Function setup_ioctl() prepares arguments for ioctl() operation.  It also
 * opens ioctl() device, if it is not opened already. Arguments are as follows:
 * 	pn	- pointer to ip_pool_node_t buffer for ioctl data
 *	op	- pointer to iplookupop_t buffer for ioctl data container
 *	unit	- always 0 right now
 *	name	- pool node name
 *	node	- pointer to node match pattern for look up operation.
 */
static int
setup_ioctl(ip_pool_node_t *pn, iplookupop_t *op, int unit,
    const char *name, ip_pool_node_t *node)
{
	if ((poolfd == -1) && ((opts & OPT_DONOTHING) == 0)) {
		poolfd = open(IPLOOKUP_NAME, O_RDWR);

		if (poolfd == -1)
			return (-1);
	}

	op->iplo_unit = unit;
	op->iplo_type = IPLT_POOL;
	op->iplo_arg = 0;
	strlcpy(op->iplo_name, name, sizeof (op->iplo_name));
	op->iplo_struct = pn;
	op->iplo_size = sizeof (pn);

	bzero((char *)pn, sizeof (ip_pool_node_t));
	bcopy((char *)&node->ipn_addr, (char *)&pn->ipn_addr,
	    sizeof (pn->ipn_addr));
	bcopy((char *)&node->ipn_mask, (char *)&pn->ipn_mask,
	    sizeof (pn->ipn_mask));
	pn->ipn_info = node->ipn_info;
	strlcpy(pn->ipn_name, node->ipn_name, sizeof (pn->ipn_name));

	return (0);
}

/*
 * Function reset_poolnode() resets statistics (hits, bytecount) for given pool
 * node (record in ippool).
 *	unit	- always 0
 *	name	- name of ippool
 *	node	- parameters for lookup operation, node name, IP address
 *	iocfunc	- pointer to ioctl function.
 *
 * Function prototype sticks with existing load_poolnode, remove_poolnode.
 */
int
reset_poolnode(int unit, const char *name,
	ip_pool_node_t *node, ioctlfunc_t iocfunc)
{
	ip_pool_node_t pn;
	iplookupop_t op;

	if (setup_ioctl(&pn, &op, unit, name, node) == -1)
		return (-1);

	if ((*iocfunc)(poolfd, SIOCLOOKUPRESETNODE, &op)) {
		if ((opts & OPT_DONOTHING) == 0) {
			perror("remove_pool:SIOCLOOKUPRESETNODE");
			return (-1);
		}
	}

	return (0);
}

/*
 * Function show_poolnode() shows statistics for given ippool node (record in
 * ippool).
 *	unit	- always 0
 *	name	- name of ippool
 *	node	- parameters for lookup operation, node name, IP address
 *	iocfunc	- pointer to ioctl function.
 *
 * Function prototype sticks with existing load_poolnode, remove_poolnode.
 */
int
show_poolnode(int unit, const char *name,
	ip_pool_node_t *node, ioctlfunc_t iocfunc)
{
	ip_pool_node_t pn;
	iplookupop_t op;

	if (setup_ioctl(&pn, &op, unit, name, node) == -1)
		return (-1);

	if ((*iocfunc)(poolfd, SIOCLOOKUPNODE, &op)) {

		if ((opts & OPT_DONOTHING) == 0) {
			perror("remove_pool:SIOCLOOKUPNODE");
			return (-1);
		}
	}

	(void) printpoolnode(&pn, opts);

	return (0);
}
