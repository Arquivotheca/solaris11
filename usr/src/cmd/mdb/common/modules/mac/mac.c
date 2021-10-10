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

#include <sys/mdb_modapi.h>
#include <sys/types.h>
#include <inet/ip.h>
#include <inet/ip6.h>

#include <sys/mac.h>
#include <sys/mac_provider.h>
#include <sys/mac_client.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_flow_impl.h>
#include <sys/mac_stat.h>
#include <sys/zone.h>

#define	STRSIZE	64
/* same max cache name length as defined in mac module */
#define	FLOW_CACHE_NAME_LEN	32

#define	LAYERED_WALKER_FOR_FLOW	"flow_entry_cache"
#define	LAYERED_WALKER_FOR_RING	"mac_ring_cache"

/* arguments passed to mac_flow dee-command */
#define	MAC_FLOW_NONE	0x01
#define	MAC_FLOW_ATTR	0x02
#define	MAC_FLOW_PROP	0x04
#define	MAC_FLOW_RX	0x08
#define	MAC_FLOW_TX	0x10
#define	MAC_FLOW_USER	0x20
#define	MAC_FLOW_STATS	0x40
#define	MAC_FLOW_MISC	0x80
#define	MAC_FLOW_ZONE	0x100

/* Callback data struct used when walking through zones */
typedef struct flow_cbdata_s {
	const char	*zonename;
	zoneid_t	zoneid[MAX_ZONEID];
	int		zonenum;
} flow_cbdata_t;

/* ARGSUSED */
static int
mac_get_zoneid_cb(uintptr_t addr, const void *flow_arg, void *flow_cb_arg)
{
	flow_cbdata_t	*flow_cb = flow_cb_arg;
	zone_t		zone;
	char		zone_name[ZONENAME_MAX];

	if (mdb_vread(&zone, sizeof (zone_t), addr) == -1) {
		mdb_warn("can't read zone at %p", addr);
		return (WALK_ERR);
	}

	(void) mdb_readstr(zone_name, ZONENAME_MAX, (uintptr_t)zone.zone_name);
	if ((flow_cb->zonename != NULL) &&
	    (strcmp(flow_cb->zonename, zone_name) != 0))
		return (WALK_NEXT);

	/* Skip shared zone */
	if (!(zone.zone_flags & ZF_NET_EXCL) &&
	    (strcmp(zone_name, "global") != 0))
		return (WALK_NEXT);

	flow_cb->zoneid[flow_cb->zonenum++] = zone.zone_id;
	return (flow_cb->zonename == NULL ? WALK_NEXT : WALK_DONE);
}

static char *
mac_flow_proto2str(uint8_t protocol)
{
	switch (protocol) {
	case IPPROTO_TCP:
		return ("tcp");
	case IPPROTO_UDP:
		return ("udp");
	case IPPROTO_SCTP:
		return ("sctp");
	case IPPROTO_ICMP:
		return ("icmp");
	case IPPROTO_ICMPV6:
		return ("icmpv6");
	default:
		return ("--");
	}
}

static char *
mac_flow_priority2str(mac_priority_level_t prio)
{
	switch (prio) {
	case MPL_LOW:
		return ("low");
	case MPL_MEDIUM:
		return ("medium");
	case MPL_HIGH:
		return ("high");
	case MPL_RESET:
		return ("reset");
	default:
		return ("--");
	}
}

/*
 *  Convert bandwidth in bps to a string in mpbs.
 */
static char *
mac_flow_bw2str(uint64_t bw, char *buf, ssize_t len)
{
	int kbps, mbps;

	kbps = (bw % 1000000)/1000;
	mbps = bw/1000000;
	if ((mbps == 0) && (kbps != 0))
		mdb_snprintf(buf, len, "0.%03u", kbps);
	else
		mdb_snprintf(buf, len, "%5u", mbps);
	return (buf);
}

static void
mac_flow_print_header(uint_t args)
{
	switch (args) {
	case MAC_FLOW_NONE:
		mdb_printf("%?s %-20s %4s %?s %?s %-16s\n",
		    "", "", "LINK", "", "", "MIP");
		mdb_printf("%<u>%?s %-20s %4s %?s %?s %-16s%-4s%</u>\n",
		    "ADDR", "FLOW NAME", "ID", "MCIP", "MIP", "NAME", "ZID");
		break;
	case MAC_FLOW_ATTR:
		mdb_printf("%<u>%?s %-32s %-7s %6s "
		    "%-9s %s%</u>\n",
		    "ADDR", "FLOW NAME", "PROTO", "PORT",
		    "DSFLD:MSK", "IPADDR");
		break;
	case MAC_FLOW_PROP:
		mdb_printf("%<u>%?s %-32s %8s %9s%</u>\n",
		    "ADDR", "FLOW NAME", "MAXBW(M)", "PRIORITY");
		break;
	case MAC_FLOW_MISC:
		mdb_printf("%<u>%?s %-24s %10s %10s "
		    "%20s %4s%</u>\n",
		    "ADDR", "FLOW NAME", "TYPE", "FLAGS",
		    "MATCH_FN", "ZONE");
		break;
	case MAC_FLOW_RX:
		mdb_printf("%?s %-24s %s\n", "", "", "RX");
		mdb_printf("%<u>%?s %-24s %3s %</u>\n",
		    "ADDR", "FLOW NAME", "CNT");
		break;
	case MAC_FLOW_TX:
		mdb_printf("%<u>%?s %-32s %</u>\n",
		    "ADDR", "FLOW NAME");
		break;
	case MAC_FLOW_STATS:
		mdb_printf("%<u>%?s %-32s %16s %16s%</u>\n",
		    "ADDR", "FLOW NAME", "RBYTES", "OBYTES");
		break;
	}
}

/*
 * Display selected fields of the flow_entry_t structure
 */
static int
mac_flow_dcmd_output(uintptr_t addr, uint_t flags, uint_t args,
    const char *zonename)
{
	static const mdb_bitmask_t flow_type_bits[] = {
		{"P", FLOW_PRIMARY_MAC, FLOW_PRIMARY_MAC},
		{"V", FLOW_VNIC_MAC, FLOW_VNIC_MAC},
		{"M", FLOW_MCAST, FLOW_MCAST},
		{"O", FLOW_OTHER, FLOW_OTHER},
		{"U", FLOW_USER, FLOW_USER},
		{"V", FLOW_VNIC, FLOW_VNIC},
		{"NS", FLOW_NO_STATS, FLOW_NO_STATS},
		{ NULL, 0, 0 }
	};
#define	FLOW_MAX_TYPE	(sizeof (flow_type_bits) / sizeof (mdb_bitmask_t))

	static const mdb_bitmask_t flow_flag_bits[] = {
		{"Q", FE_QUIESCE, FE_QUIESCE},
		{"W", FE_WAITER, FE_WAITER},
		{"T", FE_FLOW_TAB, FE_FLOW_TAB},
		{"G", FE_G_FLOW_HASH, FE_G_FLOW_HASH},
		{"I", FE_INCIPIENT, FE_INCIPIENT},
		{"C", FE_CONDEMNED, FE_CONDEMNED},
		{"NU", FE_UF_NO_DATAPATH, FE_UF_NO_DATAPATH},
		{"NC", FE_MC_NO_DATAPATH, FE_MC_NO_DATAPATH},
		{ NULL, 0, 0 }
	};
#define	FLOW_MAX_FLAGS	(sizeof (flow_flag_bits) / sizeof (mdb_bitmask_t))
	flow_entry_t		fe;
	mac_client_impl_t	mcip;
	mac_impl_t		mip;
	flow_cbdata_t		cbdata;

	if (mdb_vread(&fe, sizeof (fe), addr) == -1) {
		mdb_warn("failed to read struct flow_entry_s at %p", addr);
		return (DCMD_ERR);
	}
	if (args & MAC_FLOW_USER) {
		args &= ~MAC_FLOW_USER;
		if (fe.fe_type & FLOW_MCAST) {
			if (DCMD_HDRSPEC(flags))
				mac_flow_print_header(args);
			return (DCMD_OK);
		}
	}
	if (DCMD_HDRSPEC(flags))
		mac_flow_print_header(args);
	bzero(&mcip, sizeof (mcip));
	bzero(&mip, sizeof (mip));
	if (fe.fe_mcip != NULL && mdb_vread(&mcip, sizeof (mcip),
	    (uintptr_t)fe.fe_mcip) == sizeof (mcip)) {
		(void) mdb_vread(&mip, sizeof (mip), (uintptr_t)mcip.mci_mip);
	}

	if (zonename != NULL) {
		cbdata.zonename = zonename;
		cbdata.zonenum = 0;
		if (mdb_walk("zone", (mdb_walk_cb_t)mac_get_zoneid_cb, &cbdata)
		    == -1) {
			mdb_warn("failed to walk zone");
			return (WALK_ERR);
		}
		if (cbdata.zoneid[0] != fe.fe_flow_desc.fd_zoneid)
			return (DCMD_OK);
	}

	switch (args) {
	case MAC_FLOW_NONE: {
		mdb_printf("%?p %-20s %4d %?p "
		    "%?p %-16s%-4d\n",
		    addr, fe.fe_flow_name, fe.fe_link_id, fe.fe_mcip,
		    mcip.mci_mip, mip.mi_name, fe.fe_flow_desc.fd_zoneid);
		break;
	}
	case MAC_FLOW_ATTR: {
		struct 	in_addr	in4;
		uintptr_t	desc_addr;
		flow_desc_t	fdesc;

		desc_addr = addr + OFFSETOF(flow_entry_t, fe_flow_desc);
		if (mdb_vread(&fdesc, sizeof (fdesc), desc_addr) == -1) {
			mdb_warn("failed to read struct flow_description at %p",
			    desc_addr);
			return (DCMD_ERR);
		}
		mdb_printf("%?p %-32s "
		    "%-7s %6d "
		    "%4d:%-4d ",
		    addr, fe.fe_flow_name,
		    mac_flow_proto2str(fdesc.fd_protocol), fdesc.fd_local_port,
		    fdesc.fd_dsfield, fdesc.fd_dsfield_mask);
		if (fdesc.fd_ipversion == IPV4_VERSION) {
			IN6_V4MAPPED_TO_INADDR(&fdesc.fd_local_addr, &in4);
			mdb_printf("%I", in4.s_addr);
		} else if (fdesc.fd_ipversion == IPV6_VERSION) {
			mdb_printf("%N", &fdesc.fd_local_addr);
		} else {
			mdb_printf("%s", "--");
		}
		mdb_printf("\n");
		break;
	}
	case MAC_FLOW_PROP: {
		uintptr_t	prop_addr;
		char		bwstr[STRSIZE];
		mac_resource_props_t	fprop;

		prop_addr = addr + OFFSETOF(flow_entry_t, fe_resource_props);
		if (mdb_vread(&fprop, sizeof (fprop), prop_addr) == -1) {
			mdb_warn("failed to read struct mac_resoource_props "
			    "at %p", prop_addr);
			return (DCMD_ERR);
		}
		mdb_printf("%?p %-32s "
		    "%8s %9s\n",
		    addr, fe.fe_flow_name,
		    mac_flow_bw2str(fprop.mrp_maxbw, bwstr, STRSIZE),
		    mac_flow_priority2str(fprop.mrp_priority));
		break;
	}
	case MAC_FLOW_MISC: {
		char		flow_flags[2 * FLOW_MAX_FLAGS];
		char		flow_type[2 * FLOW_MAX_TYPE];
		GElf_Sym 	sym;
		char		func_name[MDB_SYM_NAMLEN] = "";
		uintptr_t	func, match_addr;

		match_addr = addr + OFFSETOF(flow_entry_t, fe_match);
		(void) mdb_vread(&func, sizeof (func), match_addr);
		(void) mdb_lookup_by_addr(func, MDB_SYM_EXACT, func_name,
		    MDB_SYM_NAMLEN, &sym);
		mdb_snprintf(flow_flags, 2 * FLOW_MAX_FLAGS, "%hb",
		    fe.fe_flags, flow_flag_bits);
		mdb_snprintf(flow_type, 2 * FLOW_MAX_TYPE, "%hb",
		    fe.fe_type, flow_type_bits);
		mdb_printf("%?p %-24s %10s %10s %20s\n",
		    addr, fe.fe_flow_name, flow_type, flow_flags, func_name);
		break;
	}
	case MAC_FLOW_RX: {
		mdb_printf("%?p %-24s\n", addr, fe.fe_flow_name);
		break;
	}
	case MAC_FLOW_TX: {
		mdb_printf("%?p %-32s\n", addr, fe.fe_flow_name);
		break;
	}
	case MAC_FLOW_STATS: {
		uint64_t  		totibytes = 0;
		uint64_t  		totobytes = 0;

		/* XXX revisit */
		mdb_printf("%?p %-32s %16llu %16llu\n",
		    addr, fe.fe_flow_name, totibytes, totobytes);

		break;
	}
	}
	return (DCMD_OK);
}

/*
 * Parse the arguments passed to the dcmd and print all or one flow_entry_t
 * structures
 */
static int
mac_flow_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t		args = 0;
	const char	*zonename = NULL;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("mac_flow", "mac_flow", argc, argv) == -1) {
			mdb_warn("failed to walk 'mac_flow'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}
	if ((mdb_getopts(argc, argv,
	    'a', MDB_OPT_SETBITS, MAC_FLOW_ATTR, &args,
	    'p', MDB_OPT_SETBITS, MAC_FLOW_PROP, &args,
	    'm', MDB_OPT_SETBITS, MAC_FLOW_MISC, &args,
	    'r', MDB_OPT_SETBITS, MAC_FLOW_RX, &args,
	    't', MDB_OPT_SETBITS, MAC_FLOW_TX, &args,
	    's', MDB_OPT_SETBITS, MAC_FLOW_STATS, &args,
	    'u', MDB_OPT_SETBITS, MAC_FLOW_USER, &args,
	    'z', MDB_OPT_STR, &zonename, NULL) != argc)) {
		return (DCMD_USAGE);
	}

	if (argc > 2 || (argc == 2 && !(args & MAC_FLOW_USER) &&
	    zonename == NULL))
		return (DCMD_USAGE);
	/*
	 * If no arguments was specified or just "-u" was specified then
	 * we default to printing basic information of flows.
	 */
	if (args == 0 || args == MAC_FLOW_USER)
		args |= MAC_FLOW_NONE;
	return (mac_flow_dcmd_output(addr, flags, args, zonename));
}

static void
mac_flow_help(void)
{
	mdb_printf("If an address is specified, then flow_entry structure at "
	    "that address is printed. Otherwise all the flows in the system "
	    "are printed.\n");
	mdb_printf("Options:\n"
	    "\t-u\tdisplay user defined link & vnic flows.\n"
	    "\t-a\tdisplay flow attributes\n"
	    "\t-p\tdisplay flow properties\n"
	    "\t-r\tdisplay rx side information\n"
	    "\t-t\tdisplay tx side information\n"
	    "\t-s\tdisplay flow statistics\n"
	    "\t-m\tdisplay miscellaneous flow information\n\n");
	mdb_printf("%<u>Interpreting Flow type and Flow flags output.%</u>\n");
	mdb_printf("Flow Types:\n");
	mdb_printf("\t  P --> FLOW_PRIMARY_MAC\n");
	mdb_printf("\t  V --> FLOW_VNIC_MAC\n");
	mdb_printf("\t  M --> FLOW_MCAST\n");
	mdb_printf("\t  O --> FLOW_OTHER\n");
	mdb_printf("\t  U --> FLOW_USER\n");
	mdb_printf("\t NS --> FLOW_NO_STATS\n\n");
	mdb_printf("Flow Flags:\n");
	mdb_printf("\t  Q --> FE_QUIESCE\n");
	mdb_printf("\t  W --> FE_WAITER\n");
	mdb_printf("\t  T --> FE_FLOW_TAB\n");
	mdb_printf("\t  G --> FE_G_FLOW_HASH\n");
	mdb_printf("\t  I --> FE_INCIPIENT\n");
	mdb_printf("\t  C --> FE_CONDEMNED\n");
	mdb_printf("\t NU --> FE_UF_NO_DATAPATH\n");
	mdb_printf("\t NC --> FE_MC_NO_DATAPATH\n");
}

static int
get_zoneid_list(zoneid_t *zid_list)
{
	flow_cbdata_t	cbdata;
	int		i;

	cbdata.zonename = NULL;
	cbdata.zonenum = 0;
	if (mdb_walk("zone", (mdb_walk_cb_t)mac_get_zoneid_cb, &cbdata) == -1) {
		mdb_warn("failed to walk zone");
		return (0);
	}

	for (i = 0; i < cbdata.zonenum; i++)
		zid_list[i] = cbdata.zoneid[i];
	return (cbdata.zonenum);
}

/*
 * called once by the debugger when the mac_flow walk begins.
 */
static int
mac_flow_walk_init(mdb_walk_state_t *wsp)
{
	char		flow_tbl_name[FLOW_CACHE_NAME_LEN];
	zoneid_t	zid_list[MAX_ZONEID];
	int		num, i;

	num = get_zoneid_list(zid_list);
	for (i = 0; i < num; i++) {
		mdb_snprintf(flow_tbl_name, FLOW_CACHE_NAME_LEN, "%s_%d",
		    LAYERED_WALKER_FOR_FLOW, zid_list[i]);

		/*
		 * If flow table is empty, layered_walk() will return -1.
		 * In this case, we simply go to the next one.
		 */
		if (mdb_layered_walk(flow_tbl_name, wsp) == -1)
			return (WALK_NEXT);
	}

	return (WALK_NEXT);
}

/*
 * Common walker step funciton for flow_entry_t, and mac_ring_t.
 *
 * Steps through each flow_entry_t and calls the callback function. If the
 * user executed ::walk mac_flow, it just prints the address or if the user
 * executed ::mac_flow it displays selected fields of flow_entry_t structure
 * by calling "mac_flow_dcmd"
 */
static int
mac_common_walk_step(mdb_walk_state_t *wsp)
{
	int status;

	if (wsp->walk_addr == NULL)
		return (WALK_DONE);

	status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
	    wsp->walk_cbdata);

	return (status);
}

#if 0
/*
 * we print the CPUs assigned to a link.
 * 'len' is used for formatting the output and represents the number of
 * spaces between CPU list and Fanout CPU list in the output.
 */
static boolean_t
mac_print_cpu(int *i, uint32_t cnt, uint32_t *cpu_list, int *len)
{
	int		num = 0;

	if (*i == 0)
		mdb_printf("(");
	else
		mdb_printf(" ");
	while (*i < cnt) {
		/* We print 6 CPU's at a time to keep display within 80 cols */
		if (((num + 1) % 7) == 0) {
			if (len != NULL)
				*len = 2;
			return (B_FALSE);
		}
		mdb_printf("%02x%c", cpu_list[*i], ((*i == cnt - 1)?')':','));
		++*i;
		++num;
	}
	if (len != NULL)
		*len = (7 - num) * 3;
	return (B_TRUE);
}
#endif

static char *
mac_ring_state2str(mac_ring_state_t state)
{
	switch (state) {
	case MR_FREE:
		return ("free");
	case MR_NEWLY_ADDED:
		return ("new");
	case MR_INUSE:
		return ("inuse");
	}
	return ("--");
}

static int
mac_ring_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mac_ring_t		ring;
	mac_group_t		group;
	mac_impl_t		mip;

	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("mac_ring", "mac_ring", argc, argv) == -1) {
			mdb_warn("failed to walk 'mac_ring'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}
	if (mdb_vread(&ring, sizeof (ring), addr) == -1) {
		mdb_warn("failed to read struct mac_ring_s at %p", addr);
		return (DCMD_ERR);
	}
	bzero(&mip, sizeof (mip));
	if (mdb_vread(&mip, sizeof (mip), (uintptr_t)ring.mr_mip) == -1) {
		mdb_warn("failed to read struct mac_impl_t at %p", ring.mr_mip);
		return (DCMD_ERR);
	}
	(void) mdb_vread(&group, sizeof (group), (uintptr_t)ring.mr_gh);
	if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %5s %4s %?s "
		    "%?s %s %</u>\n",
		    "ADDR", "TYPE", "STATE", "FLAG", "GROUP",
		    "MIP", "MI NAME");
	}
	mdb_printf("%?p %-4s "
	    "%5s %04x "
	    "%?p %?p %s\n",
	    addr, ((ring.mr_type == 1)? "RX" : "TX"),
	    mac_ring_state2str(ring.mr_state), ring.mr_flag,
	    ring.mr_gh, group.mrg_mh, mip.mi_name);
	return (DCMD_OK);
}

static int
mac_ring_walk_init(mdb_walk_state_t *wsp)
{
	if (mdb_layered_walk(LAYERED_WALKER_FOR_RING, wsp) == -1) {
		mdb_warn("failed to walk `mac_ring`");
		return (WALK_ERR);
	}
	return (WALK_NEXT);
}

static void
mac_ring_help(void)
{
	mdb_printf("If an address is specified, then mac_ring_t "
	    "structure at that address is printed. Otherwise all the "
	    "hardware rings in the system are printed.\n");
}

/* Supported dee-commands */
static const mdb_dcmd_t dcmds[] = {
	{"mac_flow", "?[-u] [-aprtsm]", "display Flow Entry structures",
	    mac_flow_dcmd, mac_flow_help},
	{"mac_ring", "?", "display MAC ring (hardware) structures",
	    mac_ring_dcmd, mac_ring_help},
	{ NULL }
};

/* Supported walkers */
static const mdb_walker_t walkers[] = {
	{"mac_flow", "walk list of flow entry structures", mac_flow_walk_init,
	    mac_common_walk_step, NULL, NULL},
	{"mac_ring", "walk list of mac ring structures", mac_ring_walk_init,
	    mac_common_walk_step, NULL, NULL},
	{ NULL }
};

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
