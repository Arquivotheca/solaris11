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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "nfs_mdb.h"
#include <mdb/mdb_ks.h>

#include <nfs/lm.h>
#include <nfs/lm_impl.h>
#include <vm/page.h>
#include <nfs/lm_server.h>
#include <sys/flock_impl.h>

static void print_path(uintptr_t vp);
static int lock_for_sysid(sysid_t, int, const char *);
static int locks_sysid_callback(uintptr_t, const void *, void *);
static int ld_callback(uintptr_t, const void *, void *);

int
nlm_sysid_walk_init(mdb_walk_state_t *wsp)
{
	uintptr_t zoneaddr, lm_addr;

	if (wsp->walk_addr == NULL) {
		if (mdb_readsym(&zoneaddr, sizeof (uintptr_t),
		    "global_zone") == -1) {
			mdb_warn("unable to locate global_zone");
			return (WALK_ERR);
		}
	} else {
		zoneaddr = wsp->walk_addr;
	}

	lm_addr = find_globals(zoneaddr, "lm_zone_key", FALSE);

	wsp->walk_addr = lm_addr + offsetof(lm_globals_t, lm_sysids);

	if (mdb_layered_walk("avl", wsp) == -1) {
		mdb_warn("couldn't walk lm_sysids AVL tree");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

int
nlm_sysid_walk_step(mdb_walk_state_t *wsp)
{
	uintptr_t addr = wsp->walk_addr;
	struct lm_sysid lsysid;

	if (mdb_vread(&lsysid, sizeof (lsysid), addr) != sizeof (lsysid)) {
		mdb_warn("failed to read lm_sysid at %p", addr);
		return (WALK_ERR);
	}
	return (wsp->walk_callback(addr, &lsysid, wsp->walk_cbdata));
}

void
nlm_sysid_help(void)
{
	mdb_printf("<lm_sysid addr>::nlm_sysid [-v]\n\n"
	    "This prints information about lm_sysid at the specified address\n"
	    "If address is not specified, it walks through the entire list\n"
	    "in the global zone\n"
	    "Use -v to get verbose information\n");
}

int
nlm_sysid(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct lm_sysid lmsys;
	char name[MAXNAMELEN];
	uint_t verbose = 0;

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		status = mdb_walk_dcmd("nlm_sysid", "nlm_sysid", argc, argv);
		if (status)
			mdb_warn("Could not walk lm_sysids");
		return (status);
	}

	if (DCMD_HDRSPEC(flags)) {
		/* print the header */
		mdb_printf("%-?s %-20s %-7s ", "lm_sysid", "host", "refcnt");
		if (verbose)
			mdb_printf("%-8s %-9s %-11s %s\n",
			    "sysid", "reclaim", "notify?", "knetconfig");
		else
			mdb_printf("%-4s %-9s %s\n", "sysid", "reclaim",
			    "knetconf");
	}

	if (mdb_vread(&lmsys, sizeof (lmsys), addr) == -1) {
		mdb_warn("Could not read `lm_sysid' at %a\n", addr);
		return (DCMD_ERR);
	}

	mdb_printf("%-?a ", addr); /* address of element */
	/* let's get the host for the sysid */
	if (lmsys.name == NULL ||
	    mdb_readstr(name, MAXNAMELEN, (uintptr_t)lmsys.name) == -1) {
		strncpy(name, "<unknown>", MAXNAMELEN);
	}
	else
		mdb_snprintf(name, MAXNAMELEN, "%s ", name);

	mdb_printf("%-20s ", name); /* print the host name */
	mdb_printf("%-7i ", lmsys.refcnt); /* print reference count */

	/* print the sysid (pid and mask) */
	if (verbose) {
		ASSERT((lmsys.lmsysid & LM_SYSID_CLIENT) == 0);
		mdb_printf("%-5i ", lmsys.sysid & LM_SYSID_MAX);
	}
	else
		mdb_printf("%-4i ", lmsys.sysid);
	/* Check if reclaim flag is set and print */
	mdb_printf("%-9s ", lmsys.in_recovery == TRUE ?	"true" : "false");
	if (verbose) {
		mdb_printf("%-11s ", (lmsys.sm_client ? "client" :
		    lmsys.sm_server ? "server" : "none"));
		nfs_print_netconfig(&lmsys.config);
		nfs_print_netbuf(&lmsys.addr);
	}
	else
		mdb_printf("%a", &((struct lm_sysid *)addr)->config);
	mdb_printf("\n");
	return (DCMD_OK);
}

int
nlm_vnode_walk_init(mdb_walk_state_t *wsp)
{
	lm_globals_t lm;
	uintptr_t zoneaddr, lm_addr;

	if (wsp->walk_addr == NULL) {
		if (mdb_readsym(&zoneaddr, sizeof (uintptr_t),
		    "global_zone") == -1) {
			mdb_warn("unable to locate global_zone");
			return (WALK_ERR);
		}
	} else {
		zoneaddr = wsp->walk_addr;
	}

	lm_addr = find_globals(zoneaddr, "lm_zone_key", FALSE);

	if (mdb_vread(&lm, sizeof (lm_globals_t), lm_addr) == -1) {
		mdb_warn("failed to read lm_gobal %p from zone %p\n",
		    lm_addr, zoneaddr);
		return (WALK_ERR);
	}

	wsp->walk_addr = (uintptr_t)lm.lm_vnodes;

	return (WALK_NEXT);
}

int
nlm_vnode_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	struct lm_vnode lmv;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);
	if (mdb_vread(&lmv, sizeof (lmv), wsp->walk_addr) == -1) {
		mdb_warn("Could not read lm_vnode at %p\n", wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, &lmv, wsp->walk_cbdata);
	wsp->walk_addr = (uintptr_t)lmv.next;
	return (status);
}

/*ARGSUSED*/
void
nlm_vnode_walk_fini(mdb_walk_state_t *wsp)
{
}

void
nlm_vnode_help(void)
{
	mdb_printf("<lm_vnode addr>::nlm_vnode [-v]\n\n"
	    "This prints information about lm_vnode at the specified address\n"
	    "If address is not specified, it walks through the entire list\n"
	    "in the global zone\n"
	    "Use -v to get verbose information\n");
}

int
nlm_vnode(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct lm_vnode lmvnode;
	uint_t verbose = 0;

	if (mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		status = mdb_walk_dcmd("nlm_vnode", "nlm_vnode", argc, argv);
		if (status)
			mdb_warn("Could not ::walk nlm_vnode|::nlm_vnode\n");
		return (status);
	}

	if (DCMD_HDRSPEC(flags)) {
		/* print the header */
		mdb_printf("%-?s %-6s ", "lm_vnode", "refcnt");
		if (verbose)
			mdb_printf("%-?s %-?s    %s\n",
			    "lm_block", "nfs fh", "vnode(path)");
		else
			mdb_printf("%-?s %s\n", "vnode", "lm_block");
	}

	if (mdb_vread(&lmvnode, sizeof (lmvnode), addr) == -1) {
		mdb_warn("Could not read lm_vnode at %p\n", addr);
		return (DCMD_ERR);
	}

	mdb_printf("%-?a ", addr); /* address of element */
	mdb_printf("%-6d ", lmvnode.count); /* address of element */
	if (verbose) {
		mdb_printf("%-?a ", lmvnode.blocked);
		if (lmvnode.fh3.fh3_len != 0)
			mdb_printf("v3:%-?a ", &((struct lm_vnode *)addr)->fh3);
		else if (lmvnode.fh2.fh_len != 0)
			mdb_printf("v2:%-?a ", &((struct lm_vnode *)addr)->fh2);
		else
			mdb_printf("??:%-?s ", "<unknown>");
		print_path((uintptr_t)lmvnode.vp);
	} else {
		/* print the vnode and blocked address */
		mdb_printf("%-?a ", lmvnode.vp);
		mdb_printf("%a", lmvnode.blocked);
	}
	mdb_printf("\n");
	return (DCMD_OK);
}

/*
 * mdp_vnode2path and print the vnode address and decoded path if any
 */
static void
print_path(uintptr_t vp)
{
	char path_buf[MAXPATHLEN];

	/* print vnode path */
	if (mdb_vnode2path(vp, path_buf, sizeof (path_buf)) == 0)
		mdb_printf("%-?a(%s)", vp, path_buf);
	else
		mdb_printf("%-?a(%s)", vp, "??");
}

/*
 * print the knetconfig passed, in as human-readable a manner as
 * possible
 */
int
nfs_print_netconfig(struct knetconfig *kconfig)
{
	char family[KNC_STRSIZE];
	char protocol[KNC_STRSIZE];

	/* here's the nc_semantics */
	mdb_printf("%1d/", kconfig->knc_semantics);

	if (!(kconfig->knc_protofmly &&
	    mdb_readstr(family, KNC_STRSIZE,
	    (uintptr_t)kconfig->knc_protofmly) > 0)) {
		strncpy(family, "<null>", MAXNAMELEN);
	}

	/* the protocol family */
	mdb_printf("%s/", family);

	if (!(kconfig->knc_proto &&
	    mdb_readstr(protocol, KNC_STRSIZE,
	    (uintptr_t)kconfig->knc_proto) > 0)) {
		strncpy(protocol, "<null>", MAXNAMELEN);
	}

	/* and the protocol itself */
	mdb_printf("%s/", protocol);
	return (DCMD_OK);
}

void
nlm_lockson_help(void)
{
	mdb_printf("<zoneaddr>::nlm_lockson [-v] [ $[sysid] hostname]\n"
	    "Print details of locks held on a certain host.\n"
	    "	::nlm_lockson [-v] $[sysid]	<- get locks held for sysid\n"
	    "	::nlm_lockson [-v] hostname	<- get locks held on host\n"
	    "	::nlm_lockson [-v]		<- get locks for all sysids\n"
	    "Defaults to the global zone if <zoneaddr> is blank.\n"
	    "\nFlag -v gives verbose information.");
}

typedef struct {
	uint_t verbose;
	const char *server;
	sysid_t sysid;
} lockson_data_t;

/*ARGSUSED*/
int
nlm_lockson(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int status;
	int last;
	int verbose = 0;
	const char *server = 0;
	sysid_t sysid = 0;
	lockson_data_t pass;

	last = mdb_getopts(argc, argv, 'v', MDB_OPT_SETBITS, TRUE, &verbose,
	    NULL);
	if (last == argc - 1) {
		if (argv[last].a_type == MDB_TYPE_IMMEDIATE) {
			sysid = argv[last].a_un.a_val;
			if (sysid <= 0)
				return (DCMD_USAGE);
		} else if (argv[last].a_type == MDB_TYPE_STRING) {
			server = argv[last].a_un.a_str;
		} else
			return (DCMD_USAGE);
	} else if (last != argc)
		return (DCMD_USAGE);

	pass.verbose = verbose;
	pass.sysid = sysid;
	pass.server = server;
	mdb_printf("%-15s %-?s %5s(x) %-?s %-6s %-12s ", "host",
	    "lock-addr", "sysid", "vnode", "pid", "cmd");
	if (verbose)
		mdb_printf("%-9s %-15s %-7s %s\n",
		    "state", "type(width)", "server-status", "path");
	else
		mdb_printf("%-5s %s\n", "state", "type");
	status = mdb_walk("nlm_sysid", locks_sysid_callback, &pass);
	if (status == WALK_ERR) {
		mdb_warn("couldn't walk nlm_sysid\n");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}

/*ARGSUSED*/
static int
locks_sysid_callback(uintptr_t addr, const void *data, void *passed)
{
	const lockson_data_t *pass = passed;
	sysid_t sysid = pass->sysid;

	struct lm_sysid lmsys;
	char ls_name[MAXNAMELEN];
	int status;

	if (mdb_vread(&lmsys, sizeof (lmsys), addr) == -1) {
		mdb_warn("Could not read `lm_sysid' at %p\n", addr);
		return (DCMD_ERR);
	}

	if (sysid != 0) {
		if (sysid != lmsys.sysid)
			return (WALK_NEXT);
	}

	ls_name[0] = 0;
	if (lmsys.name != NULL) {
		if (mdb_readstr(ls_name, MAXNAMELEN,
		    (uintptr_t)lmsys.name) == -1) {
			mdb_warn("couldn't read server name at %p\n",
			    lmsys.name);
			return (WALK_ERR);
		}
	}
	else
		strcpy(ls_name, "??");

	if (pass->server) {
		if (strcmp(pass->server, ls_name))
			return (WALK_NEXT);
		sysid = lmsys.sysid | LM_SYSID_CLIENT;
	} else if (sysid == 0) {
		/* basically, neither -h nor -s */
		sysid = lmsys.sysid;
	}
	status = lock_for_sysid(sysid, pass->verbose, ls_name);
	return (status);
}

typedef struct {
	int verbose;
	sysid_t sysid;
	const char *host;
} ld_data_t;

static int
lock_for_sysid(sysid_t sysid, int verbose, const char *host)
{
	ld_data_t pass;

	pass.verbose = verbose;
	pass.sysid = sysid;
	pass.host = host;

	return (mdb_pwalk("lock_graph", ld_callback, &pass, NULL));
}

/*ARGSUSED*/
static int
ld_callback(uintptr_t addr, const void *data, void *priv)
{
	const ld_data_t *pass = (ld_data_t *)priv;
	const lock_descriptor_t *ld = data;
	proc_t p;
	char path_buf[MAXPATHLEN];
	const char *server_status[] = { "up", "halting", "down", "unknown" };
	const char *lock_status[] = { "??", "init", "execute", "active",
		"blocked", "granted", "interrupt", "cancel", "done" };

	if ((ld->l_flock.l_sysid & ~LM_SYSID_CLIENT) != pass->sysid)
		return (WALK_NEXT);

	mdb_printf("%-15s %-?p %5i(%c) %-?p %-6d %-12s ",
	    pass->host, addr,
	    ld->l_flock.l_sysid & ~LM_SYSID_CLIENT,
	    ld->l_flock.l_sysid & LM_SYSID_CLIENT ? 'R' : 'L',
	    ld->l_vnode,
	    ld->l_flock.l_pid,
	    ld->l_flock.l_pid == 0 ? "<kernel>" :
	    !(ld->l_flock.l_sysid & LM_SYSID_CLIENT)
	    ? "<remote>" :
	    mdb_pid2proc(ld->l_flock.l_pid, &p) == NULL ?
	    "<defunct>" : p.p_user.u_comm);
	if (pass->verbose) {
		mdb_printf("%-9s %-2s(%5d:%-5d) %-7s ",
		    lock_status[ld->l_status],
		    ld->l_type == F_RDLCK ? "RD" :
		    ld->l_type == F_WRLCK ? "WR" : "??",
		    ld->l_start, ld->l_end,
		    server_status[ld->l_nlm_state]);
		if (mdb_vnode2path((uintptr_t)ld->l_vnode, path_buf,
		    sizeof (path_buf)) == 0)
			mdb_printf("%s\n", path_buf);
		else
			mdb_printf("??\n");
	} else {
		mdb_printf("%-5d %s\n", ld->l_status,
		    ld->l_type == F_RDLCK ? "RD" :
		    ld->l_type == F_WRLCK ? "WR" : "??");
	}
	return (WALK_NEXT);
}
