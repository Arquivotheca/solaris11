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
#include <string.h>
#include <sys/mdb_modapi.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ipmgmt_impl.h"

#define	SIN6(a)		((struct sockaddr_in6 *)a)

static const char *
ipmgmt_mdb_atype2str(ipadm_addr_type_t atype)
{
	switch (atype) {
	case IPADM_ADDR_STATIC:
		return ("static");
	case IPADM_ADDR_DHCP:
		return ("dhcp");
	case IPADM_ADDR_IPV6_ADDRCONF:
		return ("addrconf");
	}
	return ("--");
}

/*
 * called once by the debugger when the `aobjmap' walk begins.
 */
static int
aobjmap_walk_init(mdb_walk_state_t *wsp)
{
	GElf_Sym 	sym;
	uintptr_t	addr;

	if (wsp->walk_addr == NULL) {
		if (mdb_lookup_by_name("aobjmap", &sym) == -1) {
			mdb_warn("failed to find 'aobjmap'");
			return (WALK_ERR);
		}
		if (mdb_vread(&addr, sizeof (addr), sym.st_value) == -1) {
			mdb_warn("failed to read 'aobjmap'");
			return (WALK_ERR);
		}
		wsp->walk_addr = addr;
	}

	return (WALK_NEXT);
}

static int
aobjmap_walk_step(mdb_walk_state_t *wsp)
{
	ipmgmt_aobjmap_t amap;
	int		status;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&amap, sizeof (amap), wsp->walk_addr) == -1) {
		mdb_warn("failed to read struct ipmgmt_aobjmap_s at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, NULL, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);
	wsp->walk_addr = (uintptr_t)amap.am_next;
	return (status);
}

/*
 * print one or all ipmgmt_aobjmap_t structures
 */
static int
aobjmap_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	ipmgmt_aobjmap_t	amap;
	char			addrbuf[INET6_ADDRSTRLEN];
	char			lifname[IPMGMT_STRSIZE], *str;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("aobjmap", "aobjmap", argc, argv) == -1) {
			mdb_warn("failed to walk 'aobjmap'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}
	if (argc != 0)
		return (DCMD_USAGE);
	if (mdb_vread(&amap, sizeof (amap), addr) == -1) {
		mdb_warn("failed to read struct ipmgmt_aobjmap_t at %p", addr);
		return (DCMD_ERR);
	}
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %20s %20s %3s %9s %5s %6s %3s %s%</u>\n",
		    "ADDR", "ADDROBJNAME", "LIFNAME", "AF", "TYPE",
		    "NNUM", "FLAGS", "LL", "IFID");
	}
	(void) snprintf(lifname, sizeof (lifname), "%s:%d", amap.am_ifname,
	    amap.am_lnum);
	mdb_printf("%?p %20s %20s %3s %9s %5d %6d %3s ",
	    addr, amap.am_aobjname, lifname,
	    amap.am_family == AF_INET ? "v4" : "v6",
	    ipmgmt_mdb_atype2str(amap.am_atype), amap.am_nextnum,
	    amap.am_flags,
	    amap.am_linklocal ? "Y" : "N");

	(void) strlcpy(addrbuf, "--", sizeof (addrbuf));
	if (amap.am_atype == IPADM_ADDR_IPV6_ADDRCONF) {
		(void) inet_ntop(AF_INET6,
		    &((SIN6(&amap.am_ifid))->sin6_addr.s6_addr),
		    addrbuf, sizeof (addrbuf));
	}

	mdb_printf("%s\n", addrbuf);
	return (DCMD_OK);
}

/* Supported dee-commands */
static const mdb_dcmd_t dcmds[] = {
	{ "aobjmap", "?", "display important fields of ipmgmt_aobjmap_t "
	    "structure", aobjmap_dcmd, NULL },
	{ NULL }
};

/* Supported walkers */
static const mdb_walker_t walkers[] = {
	{ "aobjmap", "walk list of ipmgmt_aobjmap_t structures",
	    aobjmap_walk_init, aobjmap_walk_step, NULL, NULL },
	{ NULL }
};

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
