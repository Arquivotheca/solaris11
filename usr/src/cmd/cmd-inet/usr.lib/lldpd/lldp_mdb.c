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
#include <lldp.h>
#include <sys/list.h>
#include "lldp_impl.h"
#include "dcbx_impl.h"

#define	DCB_FEATURE_LIST_EMPTY(node, lap)				\
	((uintptr_t)((node).list_next) == (uintptr_t)((lap) +		\
	offsetof(lldp_agent_t, la_features) + offsetof(list_t, list_head)))

static char *
lldp_mdb_ftype2str(int ftype)
{
	switch (ftype) {
	case DCBX_TYPE_PFC:
		return ("PFC");
	case DCBX_TYPE_APPLICATION:
		return ("APPN");
	}
	return ("UKWN");
}

static char *
lldp_mdb_mode2str(lldp_admin_status_t mode)
{
	switch (mode) {
	case LLDP_MODE_TXONLY:
		return ("TX");
	case LLDP_MODE_RXONLY:
		return ("RX");
	case LLDP_MODE_RXTX:
		return ("BOTH");
	case LLDP_MODE_DISABLE:
		return ("NONE");
	}
	return ("UKWN");
}

static char *
lldp_mdb_state2str(int state)
{
	switch (state) {
	case LLDP_PORT_DISABLED:
		return ("portDsbd");
	case LLDP_PORT_SHUTDOWN:
		return ("portShut");
	case LLDP_TX_INITIALIZE:
	case LLDP_RX_INITIALIZE:
	case LLDP_TX_TIMER_INITIALIZE:
		return ("init");
	case LLDP_TX_IDLE:
	case LLDP_TX_TIMER_IDLE:
		return ("idle");
	case LLDP_RX_WAIT_FOR_FRAME:
		return ("wait4Frm");
	case LLDP_RX_FRAME:
		return ("rcvdFrame");
	}
	return ("unknown");
}

/*
 * called once by the debugger when the `lldp' walk begins.
 */
static int
lldp_walk_init(mdb_walk_state_t *wsp)
{
	GElf_Sym sym;

	if (mdb_lookup_by_name("lldp_agents", &sym) == -1) {
		mdb_warn("failed to find 'lldp_agents' list");
		return (WALK_ERR);
	}
	wsp->walk_addr = sym.st_value;
	if (mdb_layered_walk("list", wsp) == -1) {
		mdb_warn("failed to walk 'lldp_agents' list");
		return (WALK_ERR);
	}

	return (WALK_NEXT);
}

static int
lldp_walk_step(mdb_walk_state_t *wsp)
{
	lldp_agent_t	la;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&la, sizeof (la), wsp->walk_addr) == -1) {
		mdb_warn("failed to read struct lldp_agent_s at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}
	return (wsp->walk_callback(wsp->walk_addr, NULL, wsp->walk_cbdata));
}

/*
 * print one or all lldp_agent_t structures
 */
static int
lldp_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	lldp_agent_t	la;
	dcbx_feature_t	*dfp = NULL;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("lldp", "lldp", argc, argv) == -1) {
			mdb_warn("failed to walk 'lldp'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}
	if (argc != 0)
		return (DCMD_USAGE);

	if (mdb_vread(&la, sizeof (la), addr) == -1) {
		mdb_warn("failed to read struct lldp_agent_s at %p", addr);
		return (DCMD_ERR);
	}
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %-14s %5s %10s %10s "
		    "%10s %?s %?s %?s %3s%</u>\n",
		    "ADDR", "LINKNAME", "MODE", "TX STATE", "TXT STATE",
		    "RX STATE", "LCL MIB", "REM MIB", "DCBF", "RC");
	}

	if (!DCB_FEATURE_LIST_EMPTY(la.la_features.list_head, addr))
		dfp = list_head(&la.la_features);

	mdb_printf("%?p %-14s %5s %10s %10s %10s %?p %?p %?p %3u\n",
	    addr, la.la_linkname, lldp_mdb_mode2str(la.la_adminStatus),
	    lldp_mdb_state2str(la.la_tx_state),
	    lldp_mdb_state2str(la.la_tx_timer_state),
	    lldp_mdb_state2str(la.la_rx_state), la.la_local_mib,
	    la.la_remote_mib, dfp, la.la_refcnt);

	return (DCMD_OK);
}

/*
 * print one or all dcbx_feature_t structures
 */
static int
dcbf_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	char		linkname[MAXLINKNAMELEN];
	dcbx_feature_t	df;
	lldp_agent_t	la;
	boolean_t	infoTLV;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("dcbf", "dcbf", argc, argv) == -1) {
			mdb_warn("failed to walk 'dcbf'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}
	if (argc != 0)
		return (DCMD_USAGE);

	if (mdb_vread(&df, sizeof (df), addr) == -1) {
		mdb_warn("failed to read struct dcbx_feature_s at %p", addr);
		return (DCMD_ERR);
	}
	if (mdb_vread(&la, sizeof (la), (uintptr_t)df.df_la) == -1) {
		mdb_warn("failed to read struct lldp_agent_s at %p", df.df_la);
		return (DCMD_ERR);
	}
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %-14s %?s %5s %4s %2s "
		    "%3s %3s %4s %5s %4s %</u>\n",
		    "ADDR", "LINKNAME", "LAGENT", "FTYP", "PND", "W",
		    "PW", "PE", "FSM", "PEER", "REF");
	}
	(void) strlcpy(linkname, la.la_linkname, sizeof (linkname));
	infoTLV = (df.df_ftype == DCBX_TYPE_APPLICATION);
	mdb_printf("%?p %-14s %?p %5s %4s %2s %3s %3s %4d %5d %4d\n",
	    addr, linkname, df.df_la,
	    lldp_mdb_ftype2str(df.df_ftype),
	    (infoTLV ? "-" : (df.df_pending ? "y" : "n")),
	    (infoTLV ? "-" : (df.df_willing ? "y" : "n")),
	    (infoTLV ? "-" : (df.df_p_fnopresent ? "-" :
	    (df.df_p_willing ? "y" : "n"))),
	    (df.df_p_fnopresent ? "n" : "y"), df.df_fsm, df.df_npeer,
	    df.df_refcnt);

	return (DCMD_OK);
}

static int
dcbf_walk_init(mdb_walk_state_t *wsp)
{
	if (mdb_layered_walk("lldp", wsp) == -1) {
		mdb_warn("failed to walk 'lldp'");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

static int
dcbf_walk_step(mdb_walk_state_t *wsp)
{
	lldp_agent_t	la;
	dcbx_feature_t	*dfp, df;
	uintptr_t	addr;
	int		status;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	addr = wsp->walk_addr;
	if (mdb_vread(&la, sizeof (la), wsp->walk_addr) == -1) {
		mdb_warn("failed to read struct lldp_agent_s at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}

	/*
	 * walk through all the features for the given LLDP agent and then
	 * call ::dcbf on each address.
	 */
	if (DCB_FEATURE_LIST_EMPTY(la.la_features.list_head, addr))
		return (WALK_NEXT);
	dfp = list_head(&la.la_features);
	while (dfp != NULL) {
		status = wsp->walk_callback((uintptr_t)dfp, NULL,
		    wsp->walk_cbdata);
		if (status != WALK_NEXT)
			return (status);

		/* read the dcbx_feature_t */
		if (mdb_vread(&df, sizeof (df), (uintptr_t)dfp) == -1) {
			mdb_warn("failed to step to next address");
			return (WALK_ERR);
		}

		if (DCB_FEATURE_LIST_EMPTY(df.df_node, addr))
			return (WALK_NEXT);

		dfp = (dcbx_feature_t *)df.df_node.list_next;
	}
	return (WALK_NEXT);
}

/* Supported dee-commands */
static const mdb_dcmd_t dcmds[] = {
	{ "lldp", "?", "display important fields of lldp_agent_t structure",
	    lldp_dcmd, NULL },
	{ "dcbf", "?", "display important fields of dcbx_feature_t structure",
	    dcbf_dcmd, NULL },
	{ NULL }
};

/* Supported walkers */
static const mdb_walker_t walkers[] = {
	{ "lldp", "walk list of lldp_agent_t structures", lldp_walk_init,
	    lldp_walk_step, NULL, NULL },
	{ "dcbf", "walk list of dcbx_feature_t structures", dcbf_walk_init,
	    dcbf_walk_step, NULL, NULL },
	{ NULL }
};

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
