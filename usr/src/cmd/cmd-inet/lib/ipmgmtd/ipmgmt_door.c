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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Main door handler functions used by ipmgmtd to process the different door
 * call requests, issued by the library libipadm.so.
 */

#include <alloca.h>
#include <pwd.h>
#include <auth_attr.h>
#include <secdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>
#include <libnvpair.h>
#include <libsocket_priv.h>
#include <libinetutil.h>
#include "ipmgmt_impl.h"

/* Handler declaration for each door command */
typedef void ipmgmt_door_handler_t(void *argp);

static ipmgmt_door_handler_t	ipmgmt_getaddr_handler,
				ipmgmt_getprop_handler,
				ipmgmt_getif_handler,
				ipmgmt_initif_handler,
				ipmgmt_aobjop_handler,
				ipmgmt_resetaddr_handler,
				ipmgmt_setif_handler,
				ipmgmt_resetif_handler,
				ipmgmt_resetprop_handler,
				ipmgmt_setaddr_handler,
				ipmgmt_setprop_handler,
				ipmgmt_updateipmp_handler,
				ipmgmt_moveaddr_handler;

typedef struct ipmgmt_door_info_s {
	uint_t			idi_cmd;
	boolean_t		idi_set;
	ipmgmt_door_handler_t	*idi_handler;
} ipmgmt_door_info_t;

/* maps door commands to door handler functions */
static ipmgmt_door_info_t i_ipmgmt_door_info_tbl[] = {
	{ IPMGMT_CMD_SETPROP,		B_TRUE,  ipmgmt_setprop_handler },
	{ IPMGMT_CMD_SETIF,		B_TRUE,  ipmgmt_setif_handler },
	{ IPMGMT_CMD_SETADDR,		B_TRUE,  ipmgmt_setaddr_handler },
	{ IPMGMT_CMD_GETPROP,		B_FALSE, ipmgmt_getprop_handler },
	{ IPMGMT_CMD_GETIF,		B_FALSE, ipmgmt_getif_handler },
	{ IPMGMT_CMD_GETADDR,		B_FALSE, ipmgmt_getaddr_handler },
	{ IPMGMT_CMD_RESETIF,		B_TRUE,  ipmgmt_resetif_handler },
	{ IPMGMT_CMD_RESETADDR,		B_TRUE,  ipmgmt_resetaddr_handler },
	{ IPMGMT_CMD_RESETPROP,		B_TRUE,  ipmgmt_resetprop_handler },
	{ IPMGMT_CMD_INITIF,		B_TRUE,  ipmgmt_initif_handler },
	{ IPMGMT_CMD_UPDATE_IPMP,	B_TRUE,	 ipmgmt_updateipmp_handler },
	{ IPMGMT_CMD_MOVE_ADDROBJ,	B_TRUE,	 ipmgmt_moveaddr_handler },
	{ IPMGMT_CMD_ADDROBJ_LOOKUPADD,	B_TRUE,  ipmgmt_aobjop_handler },
	{ IPMGMT_CMD_ADDROBJ_SETLIFNUM,	B_TRUE,  ipmgmt_aobjop_handler },
	{ IPMGMT_CMD_ADDROBJ_ADD,	B_TRUE,  ipmgmt_aobjop_handler },
	{ IPMGMT_CMD_AOBJNAME2ADDROBJ,	B_FALSE, ipmgmt_aobjop_handler },
	{ IPMGMT_CMD_LIF2ADDROBJ,	B_FALSE, ipmgmt_aobjop_handler },
	{ 0, 0, NULL },
};

/*
 * The main server procedure function that gets invoked for any of the incoming
 * door commands. Inside this function we identify the incoming command and
 * invoke the right door handler function.
 */
/* ARGSUSED */
void
ipmgmt_handler(void *cookie, char *argp, size_t argsz, door_desc_t *dp,
    uint_t n_desc)
{
	ipmgmt_door_info_t	*infop = NULL;
	ipmgmt_retval_t		retval;
	int			i;
	uint_t			err;
	ucred_t			*cred = NULL;

	for (i = 0; i_ipmgmt_door_info_tbl[i].idi_cmd != 0; i++) {
		if (i_ipmgmt_door_info_tbl[i].idi_cmd ==
		    ((ipmgmt_arg_t *)(void *)argp)->ia_cmd) {
			infop = &i_ipmgmt_door_info_tbl[i];
			break;
		}
	}

	if (infop == NULL) {
		ipmgmt_log(LOG_ERR, "Invalid door command specified");
		err = EINVAL;
		goto fail;
	}

	/* check for solaris.network.interface.config authorization */
	if (infop->idi_set) {
		uid_t		uid;
		struct passwd	pwd;
		char		buf[1024];

		if (door_ucred(&cred) != 0) {
			err = errno;
			ipmgmt_log(LOG_ERR, "Could not get user credentials.");
			goto fail;
		}
		uid = ucred_getruid(cred);
		if ((int)uid < 0) {
			err = errno;
			ipmgmt_log(LOG_ERR, "Could not get user id.");
			goto fail;
		}
		if (getpwuid_r(uid, &pwd, buf, sizeof (buf)) ==
		    NULL) {
			err = errno;
			ipmgmt_log(LOG_ERR, "Could not get password entry.");
			goto fail;
		}
		if (chkauthattr(NETWORK_INTERFACE_CONFIG_AUTH,
		    pwd.pw_name) != 1) {
			err = EPERM;
			ipmgmt_log(LOG_ERR, "Not authorized for operation.");
			goto fail;
		}
		ucred_free(cred);
	}

	/* individual handlers take care of calling door_return */
	infop->idi_handler((void *)argp);
	return;
fail:
	ucred_free(cred);
	retval.ir_err = err;
	(void) door_return((char *)&retval, sizeof (retval), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_GETPROP. It retrieves the persisted
 * property value for the given property.
 */
static void
ipmgmt_getprop_handler(void *argp)
{
	ipmgmt_prop_arg_t	*pargp = argp;
	ipmgmt_getprop_rval_t	rval, *rvalp = &rval;

	assert(pargp->ia_cmd == IPMGMT_CMD_GETPROP);

	rvalp->ir_err = ipmgmt_db_walk(ipmgmt_db_getprop, pargp, IPADM_DB_READ);
	if (rvalp->ir_err == 0)
		(void) strlcpy(rvalp->ir_pval, pargp->ia_pval,
		    sizeof (rvalp->ir_pval));
	(void) door_return((char *)rvalp, sizeof (*rvalp), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_SETPROP. It persists the property value
 * for the given property in the DB.
 */
static void
ipmgmt_setprop_handler(void *argp)
{
	ipmgmt_prop_arg_t	*pargp = argp;
	ipmgmt_retval_t		rval;
	ipadm_dbwrite_cbarg_t	cb;
	nvlist_t		*nvl = NULL;
	int			err;

	assert(pargp->ia_cmd == IPMGMT_CMD_SETPROP);

	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0)
		goto fail;
	if (pargp->ia_module[0] != '\0' &&
	    (err = nvlist_add_string(nvl, IPADM_NVP_PROTONAME,
	    pargp->ia_module)) != 0) {
		goto fail;
	}
	if (pargp->ia_ifname[0] != '\0' &&
	    (err = nvlist_add_string(nvl, IPADM_NVP_IFNAME,
	    pargp->ia_ifname)) != 0)
		goto fail;
	if (pargp->ia_aobjname[0] != '\0' &&
	    (err = nvlist_add_string(nvl, IPADM_NVP_AOBJNAME,
	    pargp->ia_aobjname)) != 0)
		goto fail;
	if ((err = nvlist_add_string(nvl, pargp->ia_pname,
	    pargp->ia_pval)) != 0)
		goto fail;

	cb.dbw_nvl = nvl;
	cb.dbw_flags = pargp->ia_flags;
	err = ipmgmt_db_walk(ipmgmt_db_update, &cb, IPADM_DB_WRITE);
fail:
	nvlist_free(nvl);
	rval.ir_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Helper function for ipmgmt_setaddr_handler().
 * It converts the nvlist_t, `nvl', to aobjmap node `nodep'.
 */
static int
i_ipmgmt_nvl2aobjnode(nvlist_t *nvl, ipmgmt_aobjmap_t *nodep)
{
	char			*aobjname = NULL, *ifname = NULL;
	int32_t			lnum;
	nvlist_t		*nvladdr;
	struct sockaddr_storage	addr;
	uint32_t		plen;
	uint_t			n;
	sa_family_t		af = AF_UNSPEC;
	ipadm_addr_type_t	addrtype = IPADM_ADDR_NONE;
	int			err = 0;

	/*
	 * Retrieve all the information needed to build '*nodep' from
	 * nvlist_t nvl.
	 */
	if ((err = nvlist_lookup_string(nvl, IPADM_NVP_AOBJNAME,
	    &aobjname)) != 0 ||
	    (err = nvlist_lookup_string(nvl, IPADM_NVP_IFNAME, &ifname)) != 0 ||
	    (err = nvlist_lookup_int32(nvl, IPADM_NVP_LIFNUM, &lnum)) != 0) {
		return (err);
	}
	if (nvlist_exists(nvl, IPADM_NVP_IPV4ADDR)) {
		af = AF_INET;
		addrtype = IPADM_ADDR_STATIC;
	} else if (nvlist_exists(nvl, IPADM_NVP_DHCP)) {
		af = AF_INET;
		addrtype = IPADM_ADDR_DHCP;
	} else if (nvlist_exists(nvl, IPADM_NVP_IPV6ADDR)) {
		af = AF_INET6;
		addrtype = IPADM_ADDR_STATIC;
	} else if (nvlist_lookup_nvlist(nvl, IPADM_NVP_INTFID, &nvladdr) == 0) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&addr;
		uint8_t	*addr6;

		af = AF_INET6;
		addrtype = IPADM_ADDR_IPV6_ADDRCONF;
		if (nvlist_lookup_uint32(nvladdr, IPADM_NVP_PREFIXLEN,
		    &plen) != 0)
			return (EINVAL);
		if (plen != 0) {
			if (nvlist_lookup_uint8_array(nvladdr,
			    IPADM_NVP_IPNUMADDR, &addr6, &n) != 0)
				return (EINVAL);
			bcopy(addr6, &sin6->sin6_addr, n);
		} else {
			bzero(&sin6->sin6_addr, sizeof (sin6->sin6_addr));
		}
	}

	/*
	 * populate the `*nodep' with retrieved values.
	 */
	(void) strlcpy(nodep->am_ifname, ifname, sizeof (nodep->am_ifname));
	(void) strlcpy(nodep->am_aobjname, aobjname,
	    sizeof (nodep->am_aobjname));
	nodep->am_lnum = lnum;
	nodep->am_family = af;
	nodep->am_atype = addrtype;
	if (addrtype == IPADM_ADDR_IPV6_ADDRCONF) {
		nodep->am_linklocal = B_TRUE;
		nodep->am_ifid = addr;
		nodep->am_ifidplen = plen;
	}
	nodep->am_nextnum = 0;
	nodep->am_next = NULL;

	/*
	 * Do not store logical interface number in persistent store as it
	 * takes different value on reboot. So remove it from `nvl'.
	 */
	if (nvlist_exists(nvl, IPADM_NVP_LIFNUM))
		(void) nvlist_remove(nvl, IPADM_NVP_LIFNUM, DATA_TYPE_INT32);

	return (0);
}

/*
 * Handles the door command IPMGMT_CMD_SETADDR. It adds a new address object
 * node to the list `aobjmap' and then persists the address information in the
 * DB.
 */
static void
ipmgmt_setaddr_handler(void *argp)
{
	ipmgmt_setaddr_arg_t	*sargp = argp;
	ipmgmt_retval_t		rval;
	ipmgmt_aobjmap_t	node;
	nvlist_t		*nvl = NULL;
	char			*nvlbuf;
	size_t			nvlsize = sargp->ia_nvlsize;
	uint32_t		flags = sargp->ia_flags;
	int			err = 0;

	nvlbuf = (char *)argp + sizeof (ipmgmt_setaddr_arg_t);
	if ((err = nvlist_unpack(nvlbuf, nvlsize, &nvl, NV_ENCODE_NATIVE)) != 0)
		goto ret;
	if (flags & (IPMGMT_ACTIVE|IPMGMT_MODIFY)) {
		if ((err = i_ipmgmt_nvl2aobjnode(nvl, &node)) != 0)
			goto ret;
		if (flags & IPMGMT_MODIFY)
			node.am_flags = (IPMGMT_ACTIVE|IPMGMT_PERSIST);
		else
			node.am_flags = flags;
		if ((err = ipmgmt_aobjmap_op(&node, ADDROBJ_ADD)) != 0)
			goto ret;
	}
	if (flags & IPMGMT_PERSIST) {
		ipadm_dbwrite_cbarg_t	cb;

		cb.dbw_nvl = nvl;
		cb.dbw_flags = 0;
		err = ipmgmt_db_walk(ipmgmt_db_add, &cb, IPADM_DB_WRITE);
	}
ret:
	nvlist_free(nvl);
	rval.ir_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Handles the door commands that modify the `aobjmap' structure.
 *
 * IPMGMT_CMD_ADDROBJ_LOOKUPADD - places a stub address object in `aobjmap'
 *	after ensuring that the namespace is not taken. If required, also
 *	generates an `aobjname' for address object for the library to use.
 * IPMGMT_CMD_ADDROBJ_ADD - add/update address object in `aobjmap'
 * IPMGMT_CMD_LIF2ADDROBJ - given a logical interface, return address object
 *	associated with that logical interface.
 * IPMGMT_CMD_AOBJNAME2ADDROBJ - given an address object name return logical
 *	interface associated with that address object.
 */
static void
ipmgmt_aobjop_handler(void *argp)
{
	ipmgmt_aobjop_arg_t	*largp = argp;
	ipmgmt_retval_t		rval;
	ipmgmt_aobjop_rval_t	aobjrval;
	void			*rvalp;
	size_t			rsize;
	ipmgmt_aobjmap_t	node;
	int			err = 0;
	char			*ifname = largp->ia_ifname;
	char			*aobjname = largp->ia_aobjname;
	int32_t			lnum = largp->ia_lnum;
	sa_family_t		af = largp->ia_family;
	ipadm_addr_type_t	atype = largp->ia_atype;
	ipmgmt_aobjmap_t	*head;

	switch (largp->ia_cmd) {
	case IPMGMT_CMD_ADDROBJ_LOOKUPADD:
		rsize = sizeof (ipmgmt_aobjop_rval_t);
		rvalp = &aobjrval;
		bzero(&node, sizeof (node));
		(void) strlcpy(node.am_aobjname, aobjname,
		    sizeof (node.am_aobjname));
		(void) strlcpy(node.am_ifname, ifname,
		    sizeof (node.am_ifname));
		node.am_family = af;
		node.am_atype = atype;
		/*
		 * Set am_linklocal for addrconf object. This is required
		 * by ADDROBJ_ADD later to update the node, instead of
		 * adding a new node.
		 */
		if (atype == IPADM_ADDR_IPV6_ADDRCONF)
			node.am_linklocal = B_TRUE;

		/* no logical number is associated with this addrobj yet */
		node.am_lnum = -1;
		/* The address object is not persisted yet. */
		node.am_flags = IPMGMT_ACTIVE;
		err = ipmgmt_aobjmap_op(&node, ADDROBJ_LOOKUPADD);
		if (err == 0) {
			(void) strlcpy(aobjrval.ir_aobjname, node.am_aobjname,
			    sizeof (aobjrval.ir_aobjname));
		}
		break;
	case IPMGMT_CMD_ADDROBJ_SETLIFNUM:
		rsize = sizeof (ipmgmt_retval_t);
		rvalp = &rval;
		bzero(&node, sizeof (node));
		(void) strlcpy(node.am_aobjname, aobjname,
		    sizeof (node.am_aobjname));
		(void) strlcpy(node.am_ifname, ifname,
		    sizeof (node.am_ifname));
		node.am_family = af;
		node.am_lnum = lnum;
		err = ipmgmt_aobjmap_op(&node, ADDROBJ_SETLIFNUM);
		break;
	case IPMGMT_CMD_ADDROBJ_ADD:
		rsize = sizeof (ipmgmt_retval_t);
		rvalp = &rval;
		if (aobjname[0] == '\0' || ifname[0] == '\0' || lnum == -1 ||
		    af == AF_UNSPEC) {
			err = EINVAL;
			break;
		}
		bzero(&node, sizeof (node));
		(void) strlcpy(node.am_aobjname, aobjname,
		    sizeof (node.am_aobjname));
		(void) strlcpy(node.am_ifname, ifname,
		    sizeof (node.am_ifname));
		node.am_atype = atype;
		node.am_lnum = lnum;
		node.am_family = af;
		/* The address object is not persisted. */
		node.am_flags = IPMGMT_ACTIVE;
		err = ipmgmt_aobjmap_op(&node, ADDROBJ_ADD);
		break;
	case IPMGMT_CMD_AOBJNAME2ADDROBJ:
		rsize = sizeof (ipmgmt_aobjop_rval_t);
		rvalp = &aobjrval;
		bzero(&aobjrval, sizeof (aobjrval));
		if (aobjname[0] == '\0') {
			err = EINVAL;
			break;
		}
		(void) pthread_rwlock_rdlock(&aobjmap.aobjmap_rwlock);
		head = aobjmap.aobjmap_head;
		for (; head; head = head->am_next) {
			if (strcmp(head->am_aobjname, aobjname) != 0)
				continue;
			/*
			 * For an auto-configured interface, return
			 * the lifnum that has the link-local on it.
			 * Other logical interfaces were created for
			 * prefixes and dhcpv6 addresses and do not
			 * have am_ifid set.
			 */
			if (head->am_atype != IPADM_ADDR_IPV6_ADDRCONF ||
			    head->am_linklocal) {
				break;
			}
		}
		if (head == NULL) {
			err = ENOENT;
			(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);
			break;
		}
		(void) strlcpy(aobjrval.ir_ifname, head->am_ifname,
		    sizeof (aobjrval.ir_ifname));
		aobjrval.ir_lnum = head->am_lnum;
		aobjrval.ir_family = head->am_family;
		aobjrval.ir_flags = head->am_flags;
		aobjrval.ir_atype = head->am_atype;
		if (head->am_atype == IPADM_ADDR_IPV6_ADDRCONF &&
		    head->am_linklocal) {
			aobjrval.ir_ifid = head->am_ifid;
			aobjrval.ir_ifidplen = head->am_ifidplen;
		}

		(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);
		break;
	case IPMGMT_CMD_LIF2ADDROBJ:
		rsize = sizeof (ipmgmt_aobjop_rval_t);
		rvalp = &aobjrval;
		bzero(&aobjrval, sizeof (aobjrval));
		if (ifname[0] == '\0') {
			err = EINVAL;
			break;
		}
		(void) pthread_rwlock_rdlock(&aobjmap.aobjmap_rwlock);
		head = aobjmap.aobjmap_head;
		for (; head; head = head->am_next) {
			if (strcmp(head->am_ifname, ifname) == 0 &&
			    head->am_lnum == lnum &&
			    head->am_family == af) {
				break;
			}
		}
		if (head == NULL) {
			err = ENOENT;
			(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);
			break;
		}
		(void) strlcpy(aobjrval.ir_aobjname, head->am_aobjname,
		    sizeof (aobjrval.ir_aobjname));
		aobjrval.ir_atype = head->am_atype;
		aobjrval.ir_flags = head->am_flags;
		(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);
		break;
	default:
		rsize = sizeof (ipmgmt_retval_t);
		rvalp = &rval;
		err = EINVAL;
	}
	((ipmgmt_retval_t *)rvalp)->ir_err = err;
	(void) door_return((char *)rvalp, rsize, NULL, 0);
}

/*
 * Given an interface name and family, deletes all the address objects
 * associated with it.
 */
void
i_ipmgmt_delif_aobjs(char *ifname, sa_family_t af, uint32_t flags)
{
	ipmgmt_aobjmap_t	*head, *next, *prev;
	ipadm_db_op_t		db_op;

	prev = NULL;

	(void) pthread_rwlock_wrlock(&aobjmap.aobjmap_rwlock);
	head = aobjmap.aobjmap_head;
	for (; head; head = next) {
		next = head->am_next;
		if (strcmp(head->am_ifname, ifname) != 0 ||
		    head->am_family != af) {
			prev = head;
			continue;
		}

		if (head->am_flags == (IPMGMT_ACTIVE|IPMGMT_PERSIST) &&
		    flags == IPMGMT_ACTIVE) {
			/*
			 * If the addres is present in both active and
			 * persistent store, and if we are performing
			 * a temporary delete, we update the node to
			 * indicate that the address is only present in
			 * persistent store and we proceed. Otherwise
			 * we always delete the node from aobjmap.
			 */
			head->am_flags &= ~IPMGMT_ACTIVE;
			head->am_lnum = -1;
			db_op = IPADM_DB_WRITE;
		} else {
			db_op = IPADM_DB_DELETE;
			if (prev == NULL)
				aobjmap.aobjmap_head = next;
			else
				prev->am_next = next;
		}
		(void) ipmgmt_persist_aobjmap(head, db_op);
		if (db_op == IPADM_DB_DELETE)
			free(head);
	}
	(void) pthread_rwlock_unlock(&aobjmap.aobjmap_rwlock);
}

/*
 * Handles the door command IPMGMT_CMD_SETIF. It persists the interface
 * information in the DB.
 */
static void
ipmgmt_setif_handler(void *argp)
{
	ipmgmt_retval_t		rval;

	rval.ir_err = ipmgmt_persist_if(argp);
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_RESETIF. For the given interface,
 * deletes all the persisted interface configuration. It also deletes, from
 * `aobjmap', all the address objects configured on the given interface.
 */
static void
ipmgmt_resetif_handler(void *argp)
{
	ipmgmt_resetif_arg_t	*rargp = argp;
	ipmgmt_retval_t		rval;
	ipmgmt_if_cbarg_t	cbarg;
	uint32_t		flags = rargp->ia_flags;
	int			err = 0;

	cbarg.cb_family = rargp->ia_family;
	cbarg.cb_ifname = rargp->ia_ifname;
	cbarg.cb_hasv4 = B_FALSE;
	cbarg.cb_hasv6 = B_FALSE;
	if (flags & IPMGMT_PERSIST) {
		/*
		 * Find which address families of the interface exist in
		 * the persistent config. We need this information to
		 * determine whether the line with the given interface
		 * as the underlying interface, if any, should be deleted.
		 */
		(void) ipmgmt_db_walk(ipmgmt_db_searchif, &cbarg,
		    IPADM_DB_READ);
		if (cbarg.cb_hasv4 || cbarg.cb_hasv6)
			err = ipmgmt_db_walk(ipmgmt_db_resetif, &cbarg,
			    IPADM_DB_DELETE);
		else
			err = ENOENT;
	}

	if (flags & IPMGMT_ACTIVE)
		i_ipmgmt_delif_aobjs(rargp->ia_ifname, rargp->ia_family,
		    flags);

	rval.ir_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_RESETADDR. For the given addrobj
 * deletes all the persisted addrobj configuration. It also deletes the
 * corresponding node, from `aobjmap'.
 */
static void
ipmgmt_resetaddr_handler(void *argp)
{
	ipmgmt_addr_arg_t	*rargp = argp;
	ipmgmt_retval_t		rval;
	ipmgmt_aobjmap_t	node;
	uint32_t		flags = rargp->ia_flags;
	int			err = 0;
	ipmgmt_resetaddr_cbarg_t cbarg;

	cbarg.cb_aobjname = rargp->ia_aobjname;

	if (flags & IPMGMT_PERSIST)
		err = ipmgmt_db_walk(ipmgmt_db_resetaddr, &cbarg,
		    IPADM_DB_DELETE);

	if (flags & IPMGMT_ACTIVE) {
		bzero(&node, sizeof (node));
		(void) strlcpy(node.am_aobjname, rargp->ia_aobjname,
		    sizeof (node.am_aobjname));

		/*
		 * am_lnum is used only for IPv6 autoconf case, since there
		 * can be multiple nodes with the same aobjname.
		 */
		node.am_lnum = rargp->ia_lnum;
		node.am_flags = flags;
		(void) ipmgmt_aobjmap_op(&node, ADDROBJ_DELETE);
	}

	rval.ir_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_GETADDR. It retrieves the persisted
 * address for a given `gargp->ia_aobjname'. If it is not defined then it
 * retrieves all the addresses configured on `gargp->ia_ifname'. The
 * "ipadm show-addr addrobj" or "ipadm show-addr <ifname>/\*" will call this
 * handler through library.
 */
static void
ipmgmt_getaddr_handler(void *argp)
{
	size_t			buflen, onvlsize;
	char			*buf, *onvlbuf;
	ipmgmt_getaddr_arg_t	*gargp = argp;
	ipmgmt_getaddr_cbarg_t	cbarg;
	ipmgmt_get_rval_t 	rval, *rvalp = &rval;
	int			err = 0;

	cbarg.cb_ifname = gargp->ia_ifname;
	cbarg.cb_aobjname = gargp->ia_aobjname;
	cbarg.cb_ocnt = 0;
	if (nvlist_alloc(&cbarg.cb_onvl, NV_UNIQUE_NAME, 0) != 0)
		goto fail;
	err = ipmgmt_db_walk(ipmgmt_db_getaddr, &cbarg, IPADM_DB_READ);
	if (err == ENOENT && cbarg.cb_ocnt > 0) {
		/*
		 * If there is atleast one entry in the nvlist,
		 * do not return error.
		 */
		err = 0;
	}
	if (err != 0)
		goto fail;

	if ((err = nvlist_size(cbarg.cb_onvl, &onvlsize,
	    NV_ENCODE_NATIVE)) != 0) {
		goto fail;
	}
	buflen = onvlsize + sizeof (ipmgmt_get_rval_t);
	/*
	 * We cannot use malloc() here because door_return never returns, and
	 * memory allocated by malloc() would get leaked. Use alloca() instead.
	 */
	buf = alloca(buflen);
	onvlbuf = buf + sizeof (ipmgmt_get_rval_t);
	if ((err = nvlist_pack(cbarg.cb_onvl, &onvlbuf, &onvlsize,
	    NV_ENCODE_NATIVE, 0)) != 0) {
		goto fail;
	}
	nvlist_free(cbarg.cb_onvl);
	rvalp = (ipmgmt_get_rval_t *)(void *)buf;
	rvalp->ir_err = 0;
	rvalp->ir_nvlsize = onvlsize;

	(void) door_return(buf, buflen, NULL, 0);
	return;
fail:
	nvlist_free(cbarg.cb_onvl);
	rvalp->ir_err = err;
	(void) door_return((char *)rvalp, sizeof (*rvalp), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_RESETPROP. It deletes the property line
 * from the DB.
 */
static void
ipmgmt_resetprop_handler(void *argp)
{
	ipmgmt_prop_arg_t	*pargp = argp;
	ipmgmt_retval_t		rval;

	assert(pargp->ia_cmd == IPMGMT_CMD_RESETPROP);

	rval.ir_err = ipmgmt_db_walk(ipmgmt_db_resetprop, pargp,
	    IPADM_DB_DELETE);
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_GETIF. It retrieves the name of all the
 * persisted interfaces and the IP protocols (IPv4 or IPv6) they support.
 */
static void
ipmgmt_getif_handler(void *argp)
{
	ipmgmt_getif_arg_t	*getif = argp;
	ipmgmt_get_rval_t	*rvalp;
	ipmgmt_retval_t		rval;
	ipmgmt_getif_cbarg_t	cbarg;
	int			err = 0;
	size_t			buflen, nvlsize;
	char			*buf, *onvlbuf;

	assert(getif->ia_cmd == IPMGMT_CMD_GETIF);

	bzero(&cbarg, sizeof (cbarg));
	cbarg.cb_ifname = getif->ia_ifname;
	cbarg.cb_ocnt = 0;
	if ((err = nvlist_alloc(&cbarg.cb_onvl, NV_UNIQUE_NAME, 0)) != 0)
		goto fail;
	err = ipmgmt_db_walk(ipmgmt_db_getif, &cbarg, IPADM_DB_READ);
	if (err == ENOENT && cbarg.cb_ocnt > 0) {
		/*
		 * If there is atleast one entry in the nvlist,
		 * do not return error.
		 */
		err = 0;
	}
	if (err != 0)
		goto fail;

	if ((err = nvlist_size(cbarg.cb_onvl, &nvlsize,
	    NV_ENCODE_NATIVE)) != 0) {
		goto fail;
	}
	buflen = nvlsize + sizeof (ipmgmt_get_rval_t);
	/*
	 * We cannot use malloc() here because door_return never returns, and
	 * memory allocated by malloc() would get leaked. Use alloca() instead.
	 */
	buf = alloca(buflen);
	onvlbuf = buf + sizeof (ipmgmt_get_rval_t);
	if ((err = nvlist_pack(cbarg.cb_onvl, &onvlbuf, &nvlsize,
	    NV_ENCODE_NATIVE, 0)) != 0) {
		goto fail;
	}
	nvlist_free(cbarg.cb_onvl);
	rvalp = (ipmgmt_get_rval_t *)(void *)buf;
	rvalp->ir_err = 0;
	rvalp->ir_nvlsize = nvlsize;

	(void) door_return(buf, buflen, NULL, 0);
	return;

fail:
	nvlist_free(cbarg.cb_onvl);
	rval.ir_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/*
 * Handles the door command IPMGMT_CMD_INITIF. It retrieves all the persisted
 * interface configuration (interface properties and addresses), for all those
 * interfaces that need to be initialized.
 */
static void
ipmgmt_initif_handler(void *argp)
{
	ipmgmt_getif_arg_t	*initif = argp;
	size_t			buflen, nvlsize;
	char			*buf = NULL, *onvlbuf;
	ipmgmt_get_rval_t	rval, *rvalp = &rval;
	ipmgmt_initif_cbarg_t	cbarg;
	int			err;

	assert(initif->ia_cmd == IPMGMT_CMD_INITIF);

	bzero(&cbarg, sizeof (cbarg));
	cbarg.cb_ifname = initif->ia_ifname;
	if (nvlist_alloc(&cbarg.cb_onvl, NV_UNIQUE_NAME, 0) != 0)
		goto fail;

	err = ipmgmt_db_walk(ipmgmt_db_initif, &cbarg, IPADM_DB_READ);
	if (err == ENOENT && cbarg.cb_ocnt > 0) {
		/*
		 * If there is atleast one entry in the nvlist,
		 * do not return error.
		 */
		err = 0;
	}
	if (err != 0)
		goto fail;

	if ((err = nvlist_size(cbarg.cb_onvl, &nvlsize, NV_ENCODE_NATIVE)) != 0)
		goto fail;
	buflen = nvlsize + sizeof (ipmgmt_get_rval_t);
	/*
	 * We cannot use malloc() here because door_return never returns, and
	 * memory allocated by malloc() would get leaked. Use alloca() instead.
	 */
	buf = alloca(buflen);
	onvlbuf = buf + sizeof (ipmgmt_get_rval_t);
	if ((err = nvlist_pack(cbarg.cb_onvl, &onvlbuf, &nvlsize,
	    NV_ENCODE_NATIVE, 0)) != 0) {
		goto fail;
	}
	nvlist_free(cbarg.cb_onvl);
	rvalp = (ipmgmt_get_rval_t *)(void *)buf;
	rvalp->ir_err = 0;
	rvalp->ir_nvlsize = nvlsize;

	(void) door_return(buf, buflen, NULL, 0);
	return;
fail:
	nvlist_free(cbarg.cb_onvl);
	rvalp->ir_err = err;
	(void) door_return((char *)rvalp, sizeof (*rvalp), NULL, 0);
}

int
ipmgmt_persist_if(ipmgmt_setif_arg_t *sargp)
{
	ipadm_dbwrite_cbarg_t	cb;
	uint32_t		flags = sargp->ia_flags;
	nvlist_t		*nvl = NULL;
	int			err = 0;
	char			strval[IPMGMT_STRSIZE];

	if (!(flags & IPMGMT_PERSIST) || sargp->ia_family == AF_UNSPEC ||
	    sargp->ia_ifname[0] == '\0') {
		err = EINVAL;
		goto ret;
	}
	if ((err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0)
		goto ret;
	if ((err = nvlist_add_string(nvl, IPADM_NVP_IFNAME,
	    sargp->ia_ifname)) != 0)
		goto ret;
	(void) snprintf(strval, IPMGMT_STRSIZE, "%d", sargp->ia_family);
	if ((err = nvlist_add_string(nvl, IPADM_NVP_FAMILY, strval)) != 0)
		goto ret;
	(void) snprintf(strval, IPMGMT_STRSIZE, "%d", sargp->ia_class);
	if ((err = nvlist_add_string(nvl, IPADM_NVP_IFCLASS, strval)) != 0)
		goto ret;
	cb.dbw_nvl = nvl;
	cb.dbw_flags = 0;
	err = ipmgmt_db_walk(ipmgmt_db_add, &cb, IPADM_DB_WRITE);
ret:
	nvlist_free(nvl);
	return (err);
}

/*
 * Handles the door command IPMGMT_CMD_UPDATE_IPMP. It either adds an
 * underlying interface or removes an underlying interface to the given
 * IPMP interface.
 */
static void
ipmgmt_updateipmp_handler(void *argp)
{
	ipadm_dbwrite_cbarg_t	cb;
	ipmgmt_ipmp_arg_t	*sargp = argp;
	ipmgmt_retval_t		rval;
	nvlist_t		*nvl = NULL;
	uint32_t		flags = sargp->ia_flags;
	int			err = 0;

	if (!(flags & IPMGMT_PERSIST) || sargp->ia_ipmpif[0] == '\0' ||
	    sargp->ia_underif[0] == '\0' ||
	    (flags & (IPMGMT_APPEND|IPMGMT_REMOVE)) == 0) {
		err = EINVAL;
		goto ret;
	}
	if (flags & IPMGMT_APPEND) {
		ipmgmt_if_cbarg_t	ifcb;

		ifcb.cb_ifname = sargp->ia_underif;
		ifcb.cb_isunder = B_FALSE;
		/*
		 * Find if the interface is already persisted as an
		 * underlying interface. It should not be persisted again.
		 */
		(void) ipmgmt_db_walk(ipmgmt_db_is_underif, &ifcb,
		    IPADM_DB_READ);
		if (ifcb.cb_isunder ||
		    (err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0)) != 0 ||
		    (err = nvlist_add_string(nvl, IPADM_NVP_IFNAME,
		    sargp->ia_ipmpif)) != 0 ||
		    (err = nvlist_add_string(nvl, IPADM_NVP_UNDERIF,
		    sargp->ia_underif)) != 0)
			goto ret;

		cb.dbw_nvl = nvl;
		cb.dbw_flags = flags;
		err = ipmgmt_db_walk(ipmgmt_db_add, &cb, IPADM_DB_WRITE);
	} else {
		ipmgmt_prop_arg_t	parg;

		bzero(&parg, sizeof (parg));
		(void) strlcpy(parg.ia_pname, IPADM_NVP_UNDERIF,
		    sizeof (parg.ia_pname));
		(void) strlcpy(parg.ia_ifname, sargp->ia_ipmpif,
		    sizeof (parg.ia_ifname));
		(void) strlcpy(parg.ia_pval, sargp->ia_underif,
		    sizeof (parg.ia_pval));
		err = ipmgmt_db_walk(ipmgmt_db_resetprop, &parg,
		    IPADM_DB_DELETE);
	}
ret:
	nvlist_free(nvl);
	rval.ir_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

void
ipmgmt_fill_node(ipmgmt_aobjmap_t *node, const char *ifname, uint32_t lnum,
    sa_family_t af, const char *aobjname, ipadm_addr_type_t atype,
    uint32_t flags)
{
	bzero(node, sizeof (*node));
	(void) strlcpy(node->am_aobjname, aobjname, sizeof (node->am_aobjname));
	(void) strlcpy(node->am_ifname, ifname, sizeof (node->am_ifname));
	node->am_family = af;
	node->am_atype = atype;
	node->am_lnum = lnum;
	node->am_flags = flags;
}

/*
 * Handles the door command IPMGMT_CMD_MOVE_ADDROBJ. This is used to handle
 * the special case of migrating data address objects from an underlying
 * interface to its IPMP interface.
 */
static void
ipmgmt_moveaddr_handler(void *argp)
{
	ipmgmt_migrateaddr_arg_t	*margp = argp;
	ipmgmt_retval_t		rval;
	nvlist_t		*nvl = NULL;
	nvpair_t		*nvp;
	struct ifaddrs		*ifa = NULL, *ifap;
	size_t			nvlsize = margp->ia_nvlsize;
	char			*nvlbuf;
	char			*ipmpif = margp->ia_ipmpif;
	char			*underif = margp->ia_underif;
	ipmgmt_aobjmap_t	*node1;
	char			*cp;
	char			tifname[LIFNAMSIZ];
	int			err, ret_err;
	uint8_t			*addr6;
	struct sockaddr_storage	addr;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	uint_t			n;
	uint32_t		nextnum;
	char			taobjname[IPADM_AOBJSIZ];

	nvlbuf = (char *)argp + sizeof (ipmgmt_migrateaddr_arg_t);
	ret_err = nvlist_unpack(nvlbuf, nvlsize, &nvl, NV_ENCODE_NATIVE);
	if (ret_err != 0)
		goto ret;
	if (getallifaddrs(AF_UNSPEC, &ifa, 0, 0) != 0) {
		ret_err = errno;
		goto ret;
	}
	nvp = nvlist_next_nvpair(nvl, NULL);
	for (; nvp != NULL; nvp = nvlist_next_nvpair(nvl, nvp)) {
		sa_family_t		af;
		ipmgmt_aobjmap_t	node2;
		nvlist_t		*nvladdr;
		uint32_t		o_lnum, n_lnum = 0;

		sin = (struct sockaddr_in *)&addr;
		if ((ret_err = nvpair_value_nvlist(nvp, &nvladdr)) != 0)
			goto ret;
		/*
		 * Parse the logical interface number on `underif' that
		 * data address was migrated from. We need this to find
		 * the underif's aobjmap node to be removed.
		 */
		ret_err = nvlist_lookup_uint32(nvladdr, IPADM_NVP_LIFNUM,
		    &o_lnum);
		if (ret_err != 0)
			goto ret;
		/*
		 * Obtain the migrated address from the nvlist. This will
		 * be used to compare against the list of addresses in `ifa'
		 * to find the logical interface number on `ipmpif' it was
		 * migrated to.
		 */
		if ((err = nvlist_lookup_uint32(nvladdr, IPADM_NVP_IPV4ADDR,
		    &sin->sin_addr.s_addr)) == 0) {
			sin->sin_family = AF_INET;
			af = AF_INET;
		} else if ((err = nvlist_lookup_uint8_array(nvladdr,
		    IPADM_NVP_IPV6ADDR, &addr6, &n)) == 0) {
			sin6 = (struct sockaddr_in6 *)&addr;
			bcopy(addr6, &sin6->sin6_addr, n);
			sin6->sin6_family = AF_INET6;
			af = AF_INET6;
		} else {
			ret_err = EINVAL;
			goto ret;
		}
		/* Remove the aobjmap node for the underif. */
		node1 = ipmgmt_aobjmap_search(underif, o_lnum, af);
		(void) ipmgmt_aobjmap_op(node1, ADDROBJ_DELETE);
		/*
		 * Search the list of addresses in `ifa' for the data
		 * address that was migrated to `ipmpif' and once found,
		 * create a new aobjmap node on `ipmpif' using the lifnum
		 * just found.
		 */
		for (ifap = ifa; ifap != NULL; ifap = ifap->ifa_next) {
			(void) strlcpy(tifname, ifap->ifa_name, LIFNAMSIZ);
			if ((cp = strchr(tifname, ':')) != NULL) {
				*cp = '\0';
				n_lnum = atoi(++cp);
			}
			if (strcmp(tifname, ipmpif) != 0 ||
			    /* LINTED E_BAD_PTR_CAST_ALIGN */
			    !sockaddrcmp((struct sockaddr_storage *)
			    ifap->ifa_addr, &addr))
				continue;
			if (ipmgmt_aobjmap_search(ipmpif, n_lnum, af) != NULL)
				continue;
			/*
			 * Make a new aobjmap node for the data address.
			 * As the first step, we need to generate an
			 * address object name, based on the next available
			 * number for `ipmpif'. Next, we make a new aobjmap
			 * with this aobjname and all the available info,
			 * before adding it to the `aobjmap'.
			 */
			nextnum = ipmgmt_get_nextnum(ipmpif);
			err = i_ipmgmt_get_priv_aobjname(taobjname,
			    IPADM_AOBJSIZ, ipmpif, nextnum);
			if (err != 0) {
				ret_err = ENOMEM;
				break;
			}
			ipmgmt_fill_node(&node2, ipmpif, n_lnum, af,
			    taobjname, IPADM_ADDR_STATIC, node1->am_flags);
			node2.am_nextnum = ++nextnum;
			if ((err = i_ipmgmt_add_amnode(&node2)) != 0)
				ret_err = ENOMEM;
			break;
		}
	}
ret:
	freeifaddrs(ifa);
	nvlist_free(nvl);
	rval.ir_err = ret_err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}
