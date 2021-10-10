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
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inet/ip.h>
#include <inet/iptun.h>
#include <inet/tunables.h>
#include <libdladm.h>
#include <libdliptun.h>
#include <libdllink.h>
#include <libinetutil.h>
#include <libipadm.h>
#include <locale.h>
#include <netdb.h>
#include <netinet/in.h>
#include <ofmt.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/list.h>
#include <zone.h>

#define	STR_UNKNOWN_VAL	"?"
#define	LIFC_DEFAULT	(LIFC_NOXMIT | LIFC_TEMPORARY | LIFC_ALLZONES |\
			LIFC_UNDER_IPMP)

typedef void cmdfunc_t(int, char **, const char *);
static cmdfunc_t do_create_ip, do_delete_ip;
static cmdfunc_t do_create_vni, do_delete_vni;
static cmdfunc_t do_enable_if, do_disable_if;
static cmdfunc_t do_create_ipmp, do_delete_ipmp, do_add_ipmp, do_remove_ipmp;
static cmdfunc_t do_show_if;
static cmdfunc_t do_set_prop, do_show_prop, do_set_ifprop;
static cmdfunc_t do_show_ifprop, do_reset_ifprop, do_reset_prop;
static cmdfunc_t do_show_addrprop, do_set_addrprop, do_reset_addrprop;
static cmdfunc_t do_create_addr, do_delete_addr, do_show_addr;
static cmdfunc_t do_enable_addr, do_disable_addr;
static cmdfunc_t do_up_addr, do_down_addr, do_refresh_addr;

typedef struct	cmd {
	char		*c_name;
	cmdfunc_t	*c_fn;
	const char	*c_usage;
} cmd_t;

static cmd_t	cmds[] = {
	/* interface management related sub-commands */
	{ "create-ip",	do_create_ip,	"\tcreate-ip\t[-t] <IP-interface>"},
	{ "delete-ip",	do_delete_ip,	"\tdelete-ip\t<IP-interface>"	},
	{ "create-vni",	do_create_vni,	"\tcreate-vni\t[-t] <VNI-interface>"},
	{ "delete-vni",	do_delete_vni,	"\tdelete-vni\t<VNI-interface>"	},
	{ "create-ipmp", do_create_ipmp,
	    "\tcreate-ipmp\t[-t] [-i <under-interface>[,...]] ... "
	    "<IPMP-interface>"						},
	{ "delete-ipmp", do_delete_ipmp, "\tdelete-ipmp\t[-f] "
	    "<IPMP-interface>"},
	{ "add-ipmp",	do_add_ipmp,
	    "\tadd-ipmp\t[-t] -i <under-interface>[,...] "
	    "[-i interface,[...] ...] <IPMP-interface>"			},
	{ "remove-ipmp", do_remove_ipmp,
	    "\tremove-ipmp\t[-t] -i <under-interface>[,...] "
	    "[-i interface,[...] ...] <IPMP-interface>"			},
	{ "disable-if",	do_disable_if,	"\tdisable-if\t-t <interface>"	},
	{ "enable-if",	do_enable_if,	"\tenable-if\t-t <interface>"	},
	{ "show-if",	do_show_if,
	    "\tshow-if\t\t[[-p] -o <field>,...] [<interface>]\n"	},
	{ "set-ifprop",	do_set_ifprop,
	    "\tset-ifprop\t[-t] -p <prop>=<value[,...]> -m <protocol> "
	    "<interface>" 						},
	{ "reset-ifprop", do_reset_ifprop,
	    "\treset-ifprop\t[-t] -p <prop> -m <protocol> <interface>"	},
	{ "show-ifprop", do_show_ifprop,
	    "\tshow-ifprop\t[[-c] -o <field>,...] [-p <prop>,...]\n"
	    "\t\t\t[-m <protocol>] [<interface>]\n" 			},

	/* address management related sub-commands */
	{ "create-addr", do_create_addr,
	    "\tcreate-addr\t[-t] -T static "
	    "[-d] -a {local|remote}=addr[/prefixlen]\n"
	    "\t\t\t\t<addrobj>\n"
	    "\tcreate-addr\t[-t] -T dhcp "
	    "[-w <seconds> | forever] [-h <hostname>]\n"
	    "\t\t\t\t<addrobj>\n"
	    "\tcreate-addr\t[-t] -T addrconf "
	    "[-i {local|remote}=interface-id]\n"
	    "\t\t\t\t[-p {stateful|stateless}={yes|no}] <addrobj>"	},
	{ "down-addr",	do_down_addr,	"\tdown-addr\t[-t] <addrobj>"	},
	{ "up-addr",	do_up_addr,	"\tup-addr\t\t[-t] <addrobj>"	},
	{ "disable-addr", do_disable_addr, "\tdisable-addr\t-t <addrobj>" },
	{ "enable-addr", do_enable_addr, "\tenable-addr\t-t <addrobj>"	},
	{ "refresh-addr", do_refresh_addr, "\trefresh-addr\t[-i] <addrobj>" },
	{ "delete-addr", do_delete_addr, "\tdelete-addr\t[-r] <addrobj>" },
	{ "show-addr",	do_show_addr,
	    "\tshow-addr\t[[-p] -o <field>,...] [<addrobj>]\n"		},
	{ "set-addrprop", do_set_addrprop,
	    "\tset-addrprop\t[-t] -p <prop>=<value[,...]> <addrobj>"	},
	{ "reset-addrprop", do_reset_addrprop,
	    "\treset-addrprop\t[-t] -p <prop> <addrobj>"		},
	{ "show-addrprop", do_show_addrprop,
	    "\tshow-addrprop\t[[-c] -o <field>,...] [-p <prop>,...] "
	    "[<addrobj>]\n" 						},

	/* protocol properties related sub-commands */
	{ "set-prop",	do_set_prop,
	    "\tset-prop\t[-t] -p <prop>[+|-]=<value[,...]> <protocol>"	},
	{ "reset-prop",	do_reset_prop,
	    "\treset-prop\t[-t] -p <prop> <protocol>"			},
	{ "show-prop",	do_show_prop,
	    "\tshow-prop\t[[-c] -o <field>,...]\n"
	    "\t\t\t[-p <prop>,... <protocol> | <protocol>]"		}
};

static const struct option if_longopts[] = {
	{"interface",	required_argument,	0, 'i'	},
	{"force",	no_argument,		0, 'f'	},
	{"temporary",	no_argument,		0, 't'	},
	{ 0, 0, 0, 0 }
};

static const struct option show_prop_longopts[] = {
	{"parsable",	no_argument,		0, 'c'	},
	{"prop",	required_argument,	0, 'p'	},
	{"output",	required_argument,	0, 'o'	},
	{ 0, 0, 0, 0 }
};

static const struct option show_ifprop_longopts[] = {
	{"module",	required_argument,	0, 'm'	},
	{"parsable",	no_argument,		0, 'c'	},
	{"prop",	required_argument,	0, 'p'	},
	{"output",	required_argument,	0, 'o'	},
	{ 0, 0, 0, 0 }
};

static const struct option set_prop_longopts[] = {
	{"prop",	required_argument,	0, 'p'	},
	{"temporary",	no_argument,		0, 't'	},
	{ 0, 0, 0, 0 }
};

static const struct option set_ifprop_longopts[] = {
	{"module",	required_argument,	0, 'm'	},
	{"prop",	required_argument,	0, 'p'	},
	{"temporary",	no_argument,		0, 't'	},
	{ 0, 0, 0, 0 }
};

static const struct option addr_misc_longopts[] = {
	{"inform",	no_argument,		0, 'i'	},
	{"release",	no_argument,		0, 'r'	},
	{"temporary",	no_argument,		0, 't'	},
	{ 0, 0, 0, 0 }
};

static const struct option addr_longopts[] = {
	{"address",	required_argument,	0, 'a'	},
	{"down",	no_argument,		0, 'd'	},
	{"hostname",	required_argument,	0, 'h'  },
	{"interface-id", required_argument,	0, 'i'	},
	{"prop",	required_argument,	0, 'p'	},
	{"temporary",	no_argument,		0, 't'	},
	{"type",	required_argument,	0, 'T'	},
	{"wait",	required_argument,	0, 'w'	},
	{ 0, 0, 0, 0 }
};

static const struct option show_addr_longopts[] = {
	{"dhcp",	no_argument,		0, 'd'	},
	{"parsable",	no_argument,		0, 'p'	},
	{"output",	required_argument,	0, 'o'	},
	{ 0, 0, 0, 0 }
};

static const struct option show_if_longopts[] = {
	{"parsable",	no_argument,		0, 'p'	},
	{"output",	required_argument,	0, 'o'	},
	{ 0, 0, 0, 0 }
};

/* callback functions to print show-* subcommands output */
static ofmt_cb_t print_prop_cb;
static ofmt_cb_t print_sa_cb;
static ofmt_cb_t print_si_cb;

/* structures for 'ipadm show-*' subcommands */
typedef enum {
	IPADM_PROPFIELD_IFNAME,
	IPADM_PROPFIELD_PROTO,
	IPADM_PROPFIELD_ADDROBJ,
	IPADM_PROPFIELD_PROPERTY,
	IPADM_PROPFIELD_PERM,
	IPADM_PROPFIELD_CURRENT,
	IPADM_PROPFIELD_PERSISTENT,
	IPADM_PROPFIELD_DEFAULT,
	IPADM_PROPFIELD_POSSIBLE
} ipadm_propfield_index_t;

static ofmt_field_t intfprop_fields[] = {
/* name,	field width,	index,			callback */
{ "IFNAME",	12,	IPADM_PROPFIELD_IFNAME,		print_prop_cb},
{ "PROPERTY",	16,	IPADM_PROPFIELD_PROPERTY,	print_prop_cb},
{ "PROTO",	6,	IPADM_PROPFIELD_PROTO,		print_prop_cb},
{ "PERM",	5,	IPADM_PROPFIELD_PERM,		print_prop_cb},
{ "CURRENT",	11,	IPADM_PROPFIELD_CURRENT,	print_prop_cb},
{ "PERSISTENT",	11,	IPADM_PROPFIELD_PERSISTENT,	print_prop_cb},
{ "DEFAULT",	11,	IPADM_PROPFIELD_DEFAULT,	print_prop_cb},
{ "POSSIBLE",	16,	IPADM_PROPFIELD_POSSIBLE,	print_prop_cb},
{ NULL,		0,	0,				NULL}
};


static ofmt_field_t modprop_fields[] = {
/* name,	field width,	index,			callback */
{ "PROTO",	6,	IPADM_PROPFIELD_PROTO,		print_prop_cb},
{ "PROPERTY",	22,	IPADM_PROPFIELD_PROPERTY,	print_prop_cb},
{ "PERM",	5,	IPADM_PROPFIELD_PERM,		print_prop_cb},
{ "CURRENT",	13,	IPADM_PROPFIELD_CURRENT,	print_prop_cb},
{ "PERSISTENT",	13,	IPADM_PROPFIELD_PERSISTENT,	print_prop_cb},
{ "DEFAULT",	13,	IPADM_PROPFIELD_DEFAULT,	print_prop_cb},
{ "POSSIBLE",	15,	IPADM_PROPFIELD_POSSIBLE,	print_prop_cb},
{ NULL,		0,	0,				NULL}
};

static ofmt_field_t addrprop_fields[] = {
/* name,	field width,	index,			callback */
{ "ADDROBJ",	18,	IPADM_PROPFIELD_ADDROBJ,	print_prop_cb},
{ "PROPERTY",	11,	IPADM_PROPFIELD_PROPERTY,	print_prop_cb},
{ "PERM",	5,	IPADM_PROPFIELD_PERM,		print_prop_cb},
{ "CURRENT",	16,	IPADM_PROPFIELD_CURRENT,	print_prop_cb},
{ "PERSISTENT",	16,	IPADM_PROPFIELD_PERSISTENT,	print_prop_cb},
{ "DEFAULT",	16,	IPADM_PROPFIELD_DEFAULT,	print_prop_cb},
{ "POSSIBLE",	15,	IPADM_PROPFIELD_POSSIBLE,	print_prop_cb},
{ NULL,		0,	0,				NULL}
};

typedef struct show_prop_state {
	char		sps_ifname[LIFNAMSIZ];
	char		sps_aobjname[IPADM_AOBJSIZ];
	const char	*sps_pname;
	uint_t		sps_proto;
	char		*sps_propval;
	nvlist_t	*sps_proplist;
	boolean_t	sps_parsable;
	boolean_t	sps_addrprop;
	boolean_t	sps_ifprop;
	boolean_t	sps_modprop;
	ipadm_status_t	sps_status;
	ipadm_status_t	sps_retstatus;
	ofmt_handle_t	sps_ofmt;
} show_prop_state_t;

typedef struct show_addr_state {
	boolean_t	sa_parsable;
	boolean_t	sa_persist;
	ofmt_handle_t	sa_ofmt;
} show_addr_state_t;

typedef struct show_if_state {
	boolean_t	si_parsable;
	ofmt_handle_t	si_ofmt;
} show_if_state_t;

typedef struct show_addr_args_s {
	show_addr_state_t	*sa_state;
	ipadm_addr_info_t	*sa_info;
} show_addr_args_t;

typedef struct show_if_args_s {
	show_if_state_t *si_state;
	ipadm_if_info_t	*si_info;
} show_if_args_t;

typedef enum {
	SA_ADDROBJ,
	SA_TYPE,
	SA_STATE,
	SA_CURRENT,
	SA_PERSISTENT,
	SA_ADDR,
	SA_CIDTYPE,
	SA_CIDVAL,
	SA_BEGIN,
	SA_EXPIRE,
	SA_RENEW
} sa_field_index_t;

typedef enum {
	SI_IFNAME,
	SI_CLASS,
	SI_STATE,
	SI_ACTIVE,
	SI_CURRENT,
	SI_PERSISTENT,
	SI_OVER
} si_field_index_t;

static ofmt_field_t show_addr_fields[] = {
/* name,	field width,	id,		callback */
{ "ADDROBJ",	18,		SA_ADDROBJ,	print_sa_cb},
{ "TYPE",	9,		SA_TYPE,	print_sa_cb},
{ "STATE",	13,		SA_STATE,	print_sa_cb},
{ "CURRENT",	8,		SA_CURRENT,	print_sa_cb},
{ "PERSISTENT",	11,		SA_PERSISTENT,	print_sa_cb},
{ "ADDR",	46,		SA_ADDR,	print_sa_cb},
{ "CID-TYPE",	9,		SA_CIDTYPE,	print_sa_cb},
{ "CID-VALUE",	35,		SA_CIDVAL,	print_sa_cb},
{ "BEGIN",	25,		SA_BEGIN,	print_sa_cb},
{ "EXPIRE",	25,		SA_EXPIRE,	print_sa_cb},
{ "RENEW",	25,		SA_RENEW,	print_sa_cb},
{ NULL,		0,		0,		NULL}
};

static ofmt_field_t show_if_fields[] = {
/* name,	field width,	id,		callback */
{ "IFNAME",	11,		SI_IFNAME,	print_si_cb},
{ "CLASS",	9,		SI_CLASS,	print_si_cb},
{ "STATE",	9,		SI_STATE,	print_si_cb},
{ "ACTIVE",	7,		SI_ACTIVE,	print_si_cb},
{ "CURRENT",	14,		SI_CURRENT,	print_si_cb},
{ "PERSISTENT",	11,		SI_PERSISTENT,	print_si_cb},
{ "OVER",	44,		SI_OVER,	print_si_cb},
{ NULL,		0,		0,		NULL}
};

#define	IPADM_ALL_BITS	((uint_t)-1)
typedef struct intf_mask {
	char		*name;
	uint64_t	bits;
	uint64_t	mask;
} fmask_t;

/*
 * Handle to libipadm. Opened in main() before the sub-command specific
 * function is called and is closed before the program exits.
 */
ipadm_handle_t	iph = NULL;

/*
 * Opaque ipadm address object. Used by all the address management subcommands.
 */
ipadm_addrobj_t	ipaddr = NULL;

static char *progname;

static void	die(const char *, ...);
static void	die_opterr(int, int, const char *);
static void	warn(const char *, ...);
static void	warn_ipadmerr(ipadm_status_t, const char *, ...);
static void 	ipadm_ofmt_check(ofmt_status_t, boolean_t, ofmt_handle_t);
static void 	ipadm_check_propstr(const char *, boolean_t, const char *);
static void 	process_misc_args(int, char **, const char *, int *,
		    uint32_t *);
static void 	process_misc_ipmpargs(int, char **, const char *, int *,
		    uint32_t *, char **);

static void
usage(void)
{
	int	i;
	cmd_t	*cmdp;

	(void) fprintf(stderr,
	    gettext("usage:  ipadm <subcommand> <args> ...\n"));
	for (i = 0; i < sizeof (cmds) / sizeof (cmds[0]); i++) {
		cmdp = &cmds[i];
		if (cmdp->c_usage != NULL)
			(void) fprintf(stderr, "%s\n", gettext(cmdp->c_usage));
	}

	ipadm_destroy_addrobj(ipaddr);
	ipadm_close(iph);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int	i;
	cmd_t	*cmdp;
	ipadm_status_t status;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

	if (argc < 2)
		usage();

	status = ipadm_open(&iph, 0);
	if (status != IPADM_SUCCESS) {
		die("cannot open handle to library - %s",
		    ipadm_status2str(status));
	}

	for (i = 0; i < sizeof (cmds) / sizeof (cmds[0]); i++) {
		cmdp = &cmds[i];
		if (strcmp(argv[1], cmdp->c_name) == 0) {
			cmdp->c_fn(argc - 1, &argv[1], gettext(cmdp->c_usage));
			ipadm_destroy_addrobj(ipaddr);
			ipadm_close(iph);
			exit(0);
		}
	}

	(void) fprintf(stderr, gettext("%s: unknown subcommand '%s'\n"),
	    progname, argv[1]);
	usage();

	return (0);
}

/*
 * Create an IP interface for which no saved configuration exists in the
 * persistent store.
 */
static void
do_create_ip(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE;

	process_misc_args(argc, argv, use, &index, &flags);
	status = ipadm_create_ip(iph, argv[index], AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		die("cannot create interface %s: %s",
		    argv[index], ipadm_status2str(status));
	}
}

/*
 * Create a VNI interface.
 */
static void
do_create_vni(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE;

	process_misc_args(argc, argv, use, &index, &flags);
	status = ipadm_create_vni(iph, argv[index], AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		die("cannot create interface %s: %s",
		    argv[optind], ipadm_status2str(status));
	}
}

enum {
	IPADM_ERROR = -2,
	IPADM_WARNING,
	IPADM_OK
};

/*
 * Function that calls libipadm APIs to add the given underlying interface
 * to `ipmpif'. The boolean `print_warn' says whether the function should
 * exit the program in case of error or just print a warning and return.
 */
static int
add_ipmp(ipadm_handle_t iph, const char *ipmpif, const char *underif,
    uint32_t flags, boolean_t print_warn)
{
	ipadm_status_t		status;
	ipadm_status_t		ustatus;
	ipadm_addr_info_t	*downaddrs = NULL;

retry:
	status = ipadm_add_ipmp(iph, ipmpif, underif, flags);
	switch (status) {
	case IPADM_SUCCESS:
		break;
	case IPADM_IPMPIF_MISSING_AF:
		/*
		 * IPMP interface does not exist for one of the
		 * address families. Create it and retry.
		 */
		status = ipadm_create_ipmp_implicit(iph, ipmpif);
		if (status == IPADM_SUCCESS)
			goto retry;
		goto out;

	case IPADM_UNDERIF_UP_ADDRS:
		/*
		 * Interface has some IFF_UP addresses. Bring those
		 * down and retry. Make sure to bring up the addresses
		 * in `downaddrs' later.
		 */
		if (downaddrs != NULL) {
			/*
			 * downaddrs will have NULL if ipadm_up_addrs() fails.
			 */
			status = ipadm_up_addrs(iph, downaddrs);
			if (status != IPADM_SUCCESS) {
				if (print_warn) {
					warn_ipadmerr(status, "cannot add %s "
					    "to %s: failed to bring down "
					    "addresses on %s", underif, ipmpif,
					    underif);
				} else {
					die("cannot add %s to %s: failed to "
					    "bring down addresses on %s: %s",
					    underif, ipmpif, underif,
					    ipadm_status2str(status));
				}
				return (IPADM_ERROR);
			}
		}
		status = ipadm_down_addrs(iph, underif, &downaddrs);
		if (status == IPADM_SUCCESS)
			goto retry;
		goto out;

	case IPADM_UNDERIF_APP_ADDRS:
		/*
		 * Interface has addresses that are managed by
		 * applications (dhcpagent(1M), in.ndpd(1M)).
		 * We wait for those addresses to be removed
		 * before retrying ipadm_add_ipmp().
		 */
		status = ipadm_wait_app_addrs(iph, underif);
		if (status == IPADM_SUCCESS)
			goto retry;
		goto out;

	default:
		goto out;
	}

	/* Mark test addresses if the interface was added successfully */
	status = ipadm_mark_testaddrs(iph, underif);
	if (status != IPADM_SUCCESS) {
		warn_ipadmerr(status, "cannot convert all data addresses on "
		    "%s to test addresses", underif);
		return (IPADM_WARNING);
	}
out:
	ustatus = ipadm_up_addrs(iph, downaddrs);
	if (ustatus != IPADM_SUCCESS) {
		if (status == IPADM_SUCCESS) {
			warn_ipadmerr(ustatus, "cannot bring up all addresses "
			    "on %s", underif);
			return (IPADM_WARNING);
		}
	}
	if (status == IPADM_SUCCESS)
		return (IPADM_OK);
	if (print_warn)
		warn_ipadmerr(status, "cannot add %s to %s", underif, ipmpif);
	else
		die("cannot add %s to %s: %s", underif, ipmpif,
		    ipadm_status2str(status));
	return (IPADM_ERROR);
}

static int
remove_ipmp(ipadm_handle_t iph, const char *ipmpif, const char *underif,
    uint32_t flags, boolean_t print_warn)
{
	ipadm_status_t	status;

	status = ipadm_remove_ipmp(iph, ipmpif, underif, flags);
	if (status != IPADM_SUCCESS) {
		if (print_warn)
			warn_ipadmerr(status, "cannot remove %s from %s",
			    underif, ipmpif);
		else
			die("cannot remove %s from %s: %s", underif, ipmpif,
			    ipadm_status2str(status));
		return (IPADM_ERROR);
	}

	/* Clear test addresses */
	status = ipadm_clear_testaddrs(iph, underif);
	if (status != IPADM_SUCCESS) {
		warn_ipadmerr(status, "cannot convert all test addresses on "
		    "%s to data addresses", underif);
		return (IPADM_WARNING);
	}
	return (IPADM_OK);
}

/*
 * Adds/removes the comma-separated list of `underifs' to/from IPMP interface
 * `ipmpif'.  Returns success (0) so long as the operation could be performed
 * on at least one underlying interface.
 */
static int
update_ipmp_common(boolean_t add, const char *ipmpif, char *underifs,
    uint32_t flags, boolean_t print_warn)
{
	char		*underif, *lasts;
	int		retval = -1;
	int		res;

	/*
	 * The functions add_ipmp()/remove_ipmp() will handle printing
	 * error messages for each interface added/removed.
	 */
	underif = strtok_r(underifs, ",", &lasts);
	for (; underif != NULL; underif = strtok_r(NULL, ",", &lasts)) {
		if (add)
			res = add_ipmp(iph, ipmpif, underif, flags, print_warn);
		else
			res = remove_ipmp(iph, ipmpif, underif, flags,
			    print_warn);
		if (res == IPADM_OK)
			retval = 0;
	}
	return (retval);
}

/*
 * Create an IPMP interface and add the optionally provided underlying
 * interfaces.
 */
static void
do_create_ipmp(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE;
	char		*underifs = NULL;

	process_misc_ipmpargs(argc, argv, use, &index, &flags, &underifs);
	status = ipadm_create_ipmp(iph, argv[index], AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		die("cannot create interface %s: %s",
		    argv[index], ipadm_status2str(status));
	}
	if (underifs == NULL)
		return;
	if (update_ipmp_common(_B_TRUE, argv[index], underifs, flags,
	    _B_TRUE) != 0)
		warn("cannot add any of the interfaces to %s", argv[index]);
	free(underifs);
}

/*
 * Add the given underlying interfaces to the IPMP interface provided.
 */
static void
do_add_ipmp(int argc, char *argv[], const char *use)
{
	int		index;
	uint32_t	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE;
	char		*underifs = NULL;
	boolean_t	print_warn;

	process_misc_ipmpargs(argc, argv, use, &index, &flags, &underifs);
	if (underifs == NULL)
		die("usage: %s", use);

	print_warn = (strchr(underifs, ',') != NULL);
	if (update_ipmp_common(_B_TRUE, argv[index], underifs, flags,
	    print_warn) != 0)
		die("cannot add any of the interfaces to %s", argv[index]);
	free(underifs);
}

/*
 * Remove the given underlying interfaces from the IPMP interface provided.
 */
static void
do_remove_ipmp(int argc, char *argv[], const char *use)
{
	int		index;
	uint32_t	flags = IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE;
	char		*underifs = NULL;
	boolean_t	print_warn;

	process_misc_ipmpargs(argc, argv, use, &index, &flags, &underifs);
	if (underifs == NULL)
		die("usage: %s", use);

	print_warn = (strchr(underifs, ',') != NULL);
	if (update_ipmp_common(_B_FALSE, argv[index], underifs, flags,
	    print_warn) != 0)
		die("cannot remove any of the interfaces from %s", argv[index]);
	free(underifs);
}

/*
 * Retrieves the disabled dhcp addresses on `ifname' and enables them all.
 */
static void
enable_dhcpaddrs(const char *ifname)
{
	ipadm_status_t	status;
	ipadm_addr_info_t *ainfo, *ainfop;

	status = ipadm_addr_info(iph, ifname, &ainfo, 0, LIFC_DEFAULT);
	if (status != IPADM_SUCCESS) {
		die("cannot get address for interface %s: %s", ifname,
		    ipadm_status2str(status));
	}
	for (ainfop = ainfo; ainfop != NULL; ainfop = IA_NEXT(ainfop)) {
		if (ainfop->ia_atype != IPADM_ADDR_DHCP ||
		    ainfop->ia_state != IPADM_ADDRS_DISABLED)
			continue;
		status = ipadm_enable_addr(iph, ainfop->ia_aobjname,
		    IPADM_OPT_ACTIVE);
		if (status != IPADM_SUCCESS) {
			warn("cannot enable address %s: %s",
			    ainfop->ia_aobjname, ipadm_status2str(status));
			break;
		}
	}
	ipadm_free_addr_info(ainfo);
}

/*
 * Enable an interface based on the persistent configuration for
 * that interface.
 */
static void
do_enable_if(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status, ustatus;
	int		index;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;
	list_t		ifinfo;
	ipadm_if_info_t	*ifp;
	ipadm_ifname_t	*uifp;
	boolean_t	underif_enabled = _B_FALSE;

	process_misc_args(argc, argv, use, &index, &flags);
	if (flags & IPADM_OPT_PERSIST)
		die("persistent operation not supported for enable-if");
	status = ipadm_enable_if(iph, argv[index], flags);
	if (status != IPADM_SUCCESS && status != IPADM_IF_NOT_FULLY_ENABLED &&
	    status != IPADM_IPMPIF_DHCP_NOT_ENABLED) {
		die("cannot enable interface %s: %s",
		    argv[optind], ipadm_status2str(status));
	}
	ustatus = ipadm_if_info(iph, argv[index], &ifinfo, 0, 0);
	if (ustatus != IPADM_SUCCESS) {
		die("cannot get information for interface %s: %s",
		    argv[optind], ipadm_status2str(ustatus));
	}
	ifp = list_head(&ifinfo);
	if (ifp->ifi_class != IPADMIF_CLASS_IPMP) {
		ipadm_free_if_info(&ifinfo);
		if (status == IPADM_IF_NOT_FULLY_ENABLED)
			warn_ipadmerr(status, "");
		return;
	}
	uifp = list_head(&ifp->ifi_punders);
	for (; uifp != NULL; uifp = list_next(&ifp->ifi_punders, uifp)) {
		ustatus = ipadm_enable_if(iph, uifp->ifn_name, flags);
		if (ustatus != IPADM_SUCCESS) {
			warn("failed to enable underlying interface %s: %s ",
			    uifp->ifn_name, ipadm_status2str(ustatus));
			break;
		}
		underif_enabled = _B_TRUE;
	}
	ipadm_free_if_info(&ifinfo);
	/*
	 * When the IPMP interface was enabled, the underlying interfaces
	 * were not added to the group. Hence, it is possible that the dhcp
	 * data addresses, if any, were not enabled. Attempt enabling those
	 * addresses now.
	 */
	if (status == IPADM_IPMPIF_DHCP_NOT_ENABLED && underif_enabled)
		enable_dhcpaddrs(argv[index]);
	else if (status != IPADM_SUCCESS)
		warn_ipadmerr(status, "");
}

/*
 * Remove an IP interface from both active and persistent configuration.
 */
static void
do_delete_ip(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	uint32_t	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	if (argc != 2)
		die("usage: %s", use);

	status = ipadm_delete_ip(iph, argv[1], AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		die("cannot delete interface %s: %s",
		    argv[optind], ipadm_status2str(status));
	}
}

/*
 * Remove a VNI interface from both active and persistent configuration.
 */
static void
do_delete_vni(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	uint32_t	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	if (argc != 2)
		die("usage: %s", use);

	status = ipadm_delete_vni(iph, argv[1], AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		die("cannot delete interface %s: %s",
		    argv[optind], ipadm_status2str(status));
	}
}

/*
 * Remove an IPMP interface from both active and persistent configuration.
 */
static void
do_delete_ipmp(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	uint32_t	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;
	int		option;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":f", if_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'f':
			flags |= IPADM_OPT_FORCE;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (optind != (argc - 1))
		die("usage: %s", use);

	if (!(flags & IPADM_OPT_PERSIST))
		die("use disable-if to temporarily remove an interface object");
	status = ipadm_delete_ipmp(iph, argv[optind], AF_UNSPEC, flags);
	if (status != IPADM_SUCCESS) {
		die("cannot delete IPMP interface %s: %s",
		    argv[optind], ipadm_status2str(status));
	}
}

static void
disable_unders(const char *ipmpif, uint32_t flags)
{
	ipadm_status_t	status;
	list_t		ifinfo;
	ipadm_if_info_t	*ifp;
	ipadm_ifname_t	*uifp;

	status = ipadm_if_info(iph, ipmpif, &ifinfo, 0, 0);
	if (status != IPADM_SUCCESS) {
		die("cannot get information for interface %s: %s", ipmpif,
		    ipadm_status2str(status));
	}
	ifp = list_head(&ifinfo);
	uifp = list_head(&ifp->ifi_unders);
	for (; uifp != NULL; uifp = list_next(&ifp->ifi_unders, uifp)) {
		status = ipadm_disable_if(iph, uifp->ifn_name, flags);
		if (status != IPADM_SUCCESS) {
			die("failed to disable underlying interface %s: %s",
			    uifp->ifn_name, ipadm_status2str(status));
		}
	}
	ipadm_free_if_info(&ifinfo);
}


/*
 * Disable an interface by removing it from active configuration.
 */
static void
do_disable_if(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	process_misc_args(argc, argv, use, &index, &flags);
	if (flags & IPADM_OPT_PERSIST)
		die("persistent operation not supported for disable-if");
retry:
	status = ipadm_disable_if(iph, argv[index], flags);
	if (status == IPADM_GRP_NOTEMPTY) {
		disable_unders(argv[index], flags);
		goto retry;
	}
	if (status != IPADM_SUCCESS) {
		die("cannot disable interface %s: %s",
		    argv[optind], ipadm_status2str(status));
	}
}

/*
 * called in from print_prop_cb() and does the job of printing each
 * individual column in the 'ipadm show-*prop' output.
 */
static void
print_prop(show_prop_state_t *statep, uint_t flags, char *buf, size_t bufsize)
{
	const char		*prop_name = statep->sps_pname;
	char			*ifname = statep->sps_ifname;
	char			*propval = statep->sps_propval;
	uint_t			proto = statep->sps_proto;
	size_t			propsize = MAXPROPVALLEN;
	char			*object;
	ipadm_status_t		status;

	if (statep->sps_ifprop) {
		status = ipadm_get_ifprop(iph, ifname, prop_name, propval,
		    &propsize, proto, flags);
		object = ifname;
	} else if (statep->sps_modprop) {
		status = ipadm_get_prop(iph, prop_name, propval, &propsize,
		    proto, flags);
		object = ipadm_proto2str(proto);
	} else {
		status = ipadm_get_addrprop(iph, prop_name, propval, &propsize,
		    statep->sps_aobjname, flags);
		object = statep->sps_aobjname;
	}

	if (status != IPADM_SUCCESS) {
		if (status == IPADM_PROP_UNKNOWN ||
		    status == IPADM_INVALID_ARG) {
			warn_ipadmerr(status, "cannot get property '%s' for "
			    "'%s'", prop_name, object);
		} else if (status == IPADM_OP_NOTSUP) {
			warn_ipadmerr(status, "'%s'", object);
		} else if (status == IPADM_OBJ_NOTFOUND) {
			if (flags & IPADM_OPT_PERSIST) {
				propval[0] = '\0';
				goto cont;
			} else {
				warn_ipadmerr(status, "no such object '%s'",
				    object);
			}
		} else if (status == IPADM_NOSUCH_IF) {
			/* the interface is probably disabled */
			propval[0] = '\0';
			goto cont;
		}
		statep->sps_status = status;
		statep->sps_retstatus = status;
		return;
	}
cont:
	statep->sps_status = IPADM_SUCCESS;
	(void) snprintf(buf, bufsize, "%s", propval);
}

/*
 * callback function which displays output for set-prop, set-ifprop and
 * set-addrprop subcommands.
 */
static boolean_t
print_prop_cb(ofmt_arg_t *ofarg, char *buf, size_t bufsize)
{
	show_prop_state_t	*statep = ofarg->ofmt_cbarg;
	const char		*propname = statep->sps_pname;
	uint_t			proto = statep->sps_proto;
	boolean_t		cont = _B_TRUE;

	/*
	 * Fail retrieving remaining fields, if you fail
	 * to retrieve a field.
	 */
	if (statep->sps_status != IPADM_SUCCESS)
		return (_B_FALSE);

	switch (ofarg->ofmt_id) {
	case IPADM_PROPFIELD_IFNAME:
		(void) snprintf(buf, bufsize, "%s", statep->sps_ifname);
		break;
	case IPADM_PROPFIELD_PROTO:
		(void) snprintf(buf, bufsize, "%s", ipadm_proto2str(proto));
		break;
	case IPADM_PROPFIELD_ADDROBJ:
		(void) snprintf(buf, bufsize, "%s", statep->sps_aobjname);
		break;
	case IPADM_PROPFIELD_PROPERTY:
		(void) snprintf(buf, bufsize, "%s", propname);
		break;
	case IPADM_PROPFIELD_PERM:
		print_prop(statep, IPADM_OPT_PERM, buf, bufsize);
		break;
	case IPADM_PROPFIELD_CURRENT:
		print_prop(statep, IPADM_OPT_ACTIVE, buf, bufsize);
		break;
	case IPADM_PROPFIELD_PERSISTENT:
		print_prop(statep, IPADM_OPT_PERSIST, buf, bufsize);
		break;
	case IPADM_PROPFIELD_DEFAULT:
		print_prop(statep, IPADM_OPT_DEFAULT, buf, bufsize);
		break;
	case IPADM_PROPFIELD_POSSIBLE:
		print_prop(statep, IPADM_OPT_POSSIBLE, buf, bufsize);
		break;
	}
	if (statep->sps_status != IPADM_SUCCESS)
		cont = _B_FALSE;
	return (cont);
}

/*
 * Callback function called by the property walker (ipadm_walk_prop() or
 * ipadm_walk_proptbl()), for every matched property. This function in turn
 * calls ofmt_print() to print property information.
 */
boolean_t
show_property(void *arg, const char *pname, uint_t proto)
{
	show_prop_state_t	*statep = arg;

	statep->sps_pname = pname;
	statep->sps_proto = proto;
	statep->sps_status = IPADM_SUCCESS;
	ofmt_print(statep->sps_ofmt, arg);

	/*
	 * if an object is not found or operation is not supported then
	 * stop the walker.
	 */
	if (statep->sps_status == IPADM_OBJ_NOTFOUND ||
	    statep->sps_status == IPADM_OP_NOTSUP)
		return (_B_FALSE);
	return (_B_TRUE);
}

/*
 * Properties to be displayed is in `statep->sps_proplist'. If it is NULL,
 * for all the properties for the specified object, relevant information, will
 * be displayed. Otherwise, for the selected property set, display relevant
 * information
 */
static void
show_properties(void *arg, int prop_class)
{
	show_prop_state_t	*statep = arg;
	nvlist_t 		*nvl = statep->sps_proplist;
	uint_t			proto = statep->sps_proto;
	nvpair_t		*curr_nvp;
	char 			*buf, *name;
	ipadm_status_t		status;

	/* allocate sufficient buffer to hold a property value */
	if ((buf = malloc(MAXPROPVALLEN)) == NULL)
		die("insufficient memory");
	statep->sps_propval = buf;

	/* if no properties were specified, display all the properties */
	if (nvl == NULL) {
		(void) ipadm_walk_proptbl(proto, prop_class, show_property,
		    statep);
	} else {
		for (curr_nvp = nvlist_next_nvpair(nvl, NULL); curr_nvp;
		    curr_nvp = nvlist_next_nvpair(nvl, curr_nvp)) {
			name = nvpair_name(curr_nvp);
			status = ipadm_walk_prop(name, proto, prop_class,
			    show_property, statep);
			if (status == IPADM_PROP_UNKNOWN)
				(void) show_property(statep, name, proto);
		}
	}

	free(buf);
}

/*
 * Display information for all or specific interface properties, either for a
 * given interface or for all the interfaces in the system.
 */
static void
do_show_ifprop(int argc, char **argv, const char *use)
{
	int 		option;
	nvlist_t 	*proplist = NULL;
	char		*fields_str = NULL;
	char 		*ifname;
	ofmt_handle_t	ofmt;
	ofmt_status_t	oferr;
	uint_t		ofmtflags = 0;
	uint_t		proto;
	boolean_t	m_arg = _B_FALSE;
	char		*protostr;
	list_t		ifinfo;
	ipadm_if_info_t	*ifp;
	ipadm_status_t	status;
	show_prop_state_t state;

	opterr = 0;
	bzero(&state, sizeof (state));
	state.sps_propval = NULL;
	state.sps_parsable = _B_FALSE;
	state.sps_ifprop = _B_TRUE;
	state.sps_status = state.sps_retstatus = IPADM_SUCCESS;
	while ((option = getopt_long(argc, argv, ":p:m:co:",
	    show_ifprop_longopts, NULL)) != -1) {
		switch (option) {
		case 'p':
			if (ipadm_str2nvlist(optarg, &proplist,
			    IPADM_NORVAL) != 0)
				die("invalid interface properties specified");
			break;
		case 'c':
			state.sps_parsable = _B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		case 'm':
			if (m_arg)
				die("cannot specify more than one -m");
			m_arg = _B_TRUE;
			protostr = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
			break;
		}
	}

	if (optind == argc - 1)
		ifname = argv[optind];
	else if (optind != argc)
		die("usage: %s", use);
	else
		ifname = NULL;

	if (!m_arg)
		protostr = "ip";
	if ((proto = ipadm_str2proto(protostr)) == MOD_PROTO_NONE)
		die("invalid protocol '%s' specified", protostr);

	state.sps_proto = proto;
	state.sps_proplist = proplist;

	if (state.sps_parsable)
		ofmtflags |= OFMT_PARSABLE;
	oferr = ofmt_open(fields_str, intfprop_fields, ofmtflags, 0, &ofmt);
	ipadm_ofmt_check(oferr, state.sps_parsable, ofmt);
	state.sps_ofmt = ofmt;

	/* retrieve interface(s) and print the properties */
	status = ipadm_if_info(iph, ifname, &ifinfo, 0, LIFC_DEFAULT);
	if (ifname != NULL && status == IPADM_NOSUCH_IF)
		die("no such object '%s': %s", ifname,
		    ipadm_status2str(status));
	if (status != IPADM_SUCCESS)
		die("cannot get information for interface(s): %s",
		    ipadm_status2str(status));
	ifp = list_head(&ifinfo);
	for (; ifp != NULL; ifp = list_next(&ifinfo, ifp)) {
		(void) strlcpy(state.sps_ifname, ifp->ifi_name, LIFNAMSIZ);
		state.sps_proto = proto;
		show_properties(&state, IPADMPROP_CLASS_IF);
	}
	ipadm_free_if_info(&ifinfo);

	nvlist_free(proplist);
	ofmt_close(ofmt);

	if (state.sps_retstatus != IPADM_SUCCESS) {
		ipadm_close(iph);
		exit(EXIT_FAILURE);
	}
}

/*
 * set/reset the interface property for a given interface.
 */
static void
set_ifprop(int argc, char **argv, boolean_t reset, const char *use)
{
	int 			option;
	ipadm_status_t 		status = IPADM_SUCCESS;
	boolean_t 		p_arg = _B_FALSE;
	boolean_t		m_arg = _B_FALSE;
	char 			*ifname, *nv, *protostr;
	char			*prop_name, *prop_val;
	uint_t			flags = IPADM_OPT_PERSIST;
	uint_t			proto;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":m:p:t",
	    set_ifprop_longopts, NULL)) != -1) {
		switch (option) {
		case 'p':
			if (p_arg)
				die("-p must be specified once only");
			p_arg = _B_TRUE;

			ipadm_check_propstr(optarg, reset, use);
			nv = optarg;
			break;
		case 'm':
			if (m_arg)
				die("-m must be specified once only");
			m_arg = _B_TRUE;
			protostr = optarg;
			break;
		case 't':
			flags &= ~IPADM_OPT_PERSIST;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}

	if (!m_arg || !p_arg || optind != argc - 1)
		die("usage: %s", use);

	ifname = argv[optind];

	prop_name = nv;
	prop_val = strchr(nv, '=');
	if (prop_val != NULL)
		*prop_val++ = '\0';

	if ((proto = ipadm_str2proto(protostr)) == MOD_PROTO_NONE)
		die("invalid protocol '%s' specified", protostr);

	if (reset)
		flags |= IPADM_OPT_DEFAULT;
	else
		flags |= IPADM_OPT_ACTIVE;
	status = ipadm_set_ifprop(iph, ifname, prop_name, prop_val, proto,
	    flags);

	if (status != IPADM_SUCCESS) {
		if (reset)
			die("reset-ifprop: %s: %s",
			    prop_name, ipadm_status2str(status));
		else
			die("set-ifprop: %s: %s",
			    prop_name, ipadm_status2str(status));
	}
}

static void
do_set_ifprop(int argc, char **argv, const char *use)
{
	set_ifprop(argc, argv, _B_FALSE, use);
}

static void
do_reset_ifprop(int argc, char **argv, const char *use)
{
	set_ifprop(argc, argv, _B_TRUE, use);
}

/*
 * Display information for all or specific protocol properties, either for a
 * given protocol or for supported protocols (IP/IPv4/IPv6/TCP/UDP/SCTP)
 */
static void
do_show_prop(int argc, char **argv, const char *use)
{
	char 			option;
	nvlist_t 		*proplist = NULL;
	char			*fields_str = NULL;
	char 			*protostr;
	show_prop_state_t 	state;
	ofmt_handle_t		ofmt;
	ofmt_status_t		oferr;
	uint_t			ofmtflags = 0;
	uint_t			proto;
	boolean_t		p_arg = _B_FALSE;

	opterr = 0;
	bzero(&state, sizeof (state));
	state.sps_propval = NULL;
	state.sps_parsable = _B_FALSE;
	state.sps_modprop = _B_TRUE;
	state.sps_status = state.sps_retstatus = IPADM_SUCCESS;
	while ((option = getopt_long(argc, argv, ":p:co:", show_prop_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'p':
			if (p_arg)
				die("-p must be specified once only");
			p_arg = _B_TRUE;
			if (ipadm_str2nvlist(optarg, &proplist,
			    IPADM_NORVAL) != 0)
				die("invalid protocol properties specified");
			break;
		case 'c':
			state.sps_parsable = _B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
			break;
		}
	}
	if (optind == argc - 1) {
		protostr =  argv[optind];
		if ((proto = ipadm_str2proto(protostr)) == MOD_PROTO_NONE)
			die("invalid protocol '%s' specified", protostr);
		state.sps_proto = proto;
	} else if (optind != argc) {
		die("usage: %s", use);
	} else {
		if (p_arg)
			die("protocol must be specified when "
			    "property name is used");
		state.sps_proto = MOD_PROTO_NONE;
	}

	state.sps_proplist = proplist;

	if (state.sps_parsable)
		ofmtflags |= OFMT_PARSABLE;
	else
		ofmtflags |= OFMT_WRAP;
	oferr = ofmt_open(fields_str, modprop_fields, ofmtflags, 0, &ofmt);
	ipadm_ofmt_check(oferr, state.sps_parsable, ofmt);
	state.sps_ofmt = ofmt;

	/* handles all the errors */
	show_properties(&state, IPADMPROP_CLASS_MODULE);

	nvlist_free(proplist);
	ofmt_close(ofmt);

	if (state.sps_retstatus != IPADM_SUCCESS) {
		ipadm_close(iph);
		exit(EXIT_FAILURE);
	}
}

/*
 * Checks to see if there are any modifiers, + or -. If there are modifiers
 * then sets IPADM_OPT_APPEND or IPADM_OPT_REMOVE, accordingly.
 */
static void
parse_modifiers(const char *pstr, uint_t *flags, const char *use)
{
	char *p;

	if ((p = strchr(pstr, '=')) == NULL)
		return;

	if (p == pstr)
		die("invalid prop=val specified\n%s", use);

	--p;
	if (*p == '+')
		*flags |= IPADM_OPT_APPEND;
	else if (*p == '-')
		*flags |= IPADM_OPT_REMOVE;
}

/*
 * set/reset the protocol property for a given protocol.
 */
static void
set_prop(int argc, char **argv, boolean_t reset, const char *use)
{
	int 			option;
	ipadm_status_t 		status = IPADM_SUCCESS;
	char 			*protostr, *nv, *prop_name, *prop_val;
	boolean_t 		p_arg = _B_FALSE;
	uint_t 			proto;
	uint_t			flags = IPADM_OPT_PERSIST;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":p:t", set_prop_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'p':
			if (p_arg)
				die("-p must be specified once only");
			p_arg = _B_TRUE;

			ipadm_check_propstr(optarg, reset, use);
			nv = optarg;
			break;
		case 't':
			flags &= ~IPADM_OPT_PERSIST;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}

	if (!p_arg || optind != argc - 1)
		die("usage: %s", use);

	parse_modifiers(nv, &flags, use);
	prop_name = nv;
	prop_val = strchr(nv, '=');
	if (prop_val != NULL) {
		if (flags & (IPADM_OPT_APPEND|IPADM_OPT_REMOVE))
			*(prop_val - 1) = '\0';
		*prop_val++ = '\0';
	}
	protostr = argv[optind];
	if ((proto = ipadm_str2proto(protostr)) == MOD_PROTO_NONE)
		die("invalid protocol '%s' specified", protostr);

	if (reset)
		flags |= IPADM_OPT_DEFAULT;
	else
		flags |= IPADM_OPT_ACTIVE;
	status = ipadm_set_prop(iph, prop_name, prop_val, proto, flags);
done:
	if (status != IPADM_SUCCESS) {
		if (reset)
			die("reset-prop: %s: %s",
			    prop_name, ipadm_status2str(status));
		else
			die("set-prop: %s: %s",
			    prop_name, ipadm_status2str(status));
	}
}

static void
do_set_prop(int argc, char **argv, const char *use)
{
	set_prop(argc, argv, _B_FALSE, use);
}

static void
do_reset_prop(int argc, char **argv, const char *use)
{
	set_prop(argc, argv,  _B_TRUE, use);
}

/* PRINTFLIKE1 */
static void
warn(const char *format, ...)
{
	va_list alist;

	format = gettext(format);
	(void) fprintf(stderr, gettext("%s: warning: "), progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	(void) fprintf(stderr, "\n");
}

/* PRINTFLIKE1 */
static void
die(const char *format, ...)
{
	va_list alist;

	format = gettext(format);
	(void) fprintf(stderr, "%s: ", progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	(void) fprintf(stderr, "\n");

	ipadm_destroy_addrobj(ipaddr);
	ipadm_close(iph);
	exit(EXIT_FAILURE);
}

static void
die_opterr(int opt, int opterr, const char *usage)
{
	switch (opterr) {
	case ':':
		die("option '-%c' requires a value\nusage: %s", opt,
		    gettext(usage));
		break;
	case '?':
	default:
		die("unrecognized option '-%c'\nusage: %s", opt,
		    gettext(usage));
		break;
	}
}

/* PRINTFLIKE2 */
static void
warn_ipadmerr(ipadm_status_t err, const char *format, ...)
{
	va_list alist;

	format = gettext(format);
	(void) fprintf(stderr, gettext("%s: warning: "), progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	(void) fprintf(stderr, ": %s\n", ipadm_status2str(err));
}

static void
process_common_addrargs(const char *use, char *addrarg, const char *aobjname,
    ipadm_addr_type_t addrtype)
{
	int		option;
	char		*val;
	char		*laddr = NULL;
	char		*raddr = NULL;
	char		*save_input_arg = addrarg;
	boolean_t	found_mismatch = _B_FALSE;
	ipadm_status_t	status;
	enum		{ A_LOCAL, A_REMOTE };
	static char	*addr_optstr[] = {
		"local",
		"remote",
		NULL,
	};

	if (addrarg == NULL)
		goto noargs;

	while (*addrarg != '\0') {
		option = getsubopt(&addrarg, addr_optstr, &val);
		switch (option) {
		case A_LOCAL:
			if (laddr != NULL)
				die("multiple local %s provided",
				    (addrtype == IPADM_ADDR_STATIC) ?
				    "addresses" : "interface ids");
			laddr = val;
			break;
		case A_REMOTE:
			if (raddr != NULL)
				die("multiple remote %s provided",
				    (addrtype == IPADM_ADDR_STATIC) ?
				    "addresses" : "interface ids");
			raddr = val;
			break;
		default:
			if (found_mismatch)
				die("invalid %s provided\nusage: %s",
				    (addrtype == IPADM_ADDR_STATIC) ?
				    "address" : "interface id", use);
			found_mismatch = _B_TRUE;
			break;
		}
	}
noargs:
	if (addrtype == IPADM_ADDR_STATIC && raddr != NULL && laddr == NULL)
		die("missing local address\nusage: %s", use);

	/* If only one address is provided, it is assumed a local address. */
	if (laddr == NULL) {
		if (found_mismatch)
			laddr = save_input_arg;
		else if (addrtype == IPADM_ADDR_STATIC)
			die("missing local address\nusage: %s", use);
	}

	/* Initialize the addrobj for static addresses. */
	status = ipadm_create_addrobj(addrtype, aobjname, &ipaddr);
	if (status != IPADM_SUCCESS) {
		die("cannot create address object: %s",
		    ipadm_status2str(status));
	}

	/* Set the local and remote addresses */
	if (addrtype == IPADM_ADDR_STATIC) {
		status = ipadm_set_addr(ipaddr, laddr, AF_UNSPEC);
		if (status != IPADM_SUCCESS) {
			die("cannot set local address: %s",
			    ipadm_status2str(status));
		}
	} else {
		if (laddr != NULL) {
			status = ipadm_set_interface_id(ipaddr, laddr);
			if (status != IPADM_SUCCESS) {
				die("cannot set interface id: %s",
				    ipadm_status2str(status));
			}
		}
	}

	if (raddr != NULL) {
		if (addrtype == IPADM_ADDR_STATIC) {
			status = ipadm_set_dst_addr(ipaddr, raddr, AF_UNSPEC);
			if (status != IPADM_SUCCESS) {
				die("cannot set remote address: %s",
				    ipadm_status2str(status));
			}
		} else {
			status = ipadm_set_dst_interface_id(ipaddr, raddr);
			if (status != IPADM_SUCCESS) {
				die("cannot set remote interface id: %s",
				    ipadm_status2str(status));
			}
		}
	}
}

static void
process_static_addrargs(const char *use, char *addrarg, const char *aobjname)
{
	process_common_addrargs(use, addrarg, aobjname, IPADM_ADDR_STATIC);
}

static void
process_addrconf_intargs(const char *use, char *addrarg, const char *aobjname)
{
	process_common_addrargs(use, addrarg, aobjname,
	    IPADM_ADDR_IPV6_ADDRCONF);
}

static void
process_addrconf_stateargs(const char *use, char *addrarg)
{
	int		option;
	char		*val;
	enum		{ P_STATELESS, P_STATEFUL };
	static char	*addr_optstr[] = {
		"stateless",
		"stateful",
		NULL,
	};
	boolean_t	stateless;
	boolean_t	stateless_arg = _B_FALSE;
	boolean_t	stateful;
	boolean_t	stateful_arg = _B_FALSE;
	ipadm_status_t	status;

	while (*addrarg != '\0') {
		option = getsubopt(&addrarg, addr_optstr, &val);
		switch (option) {
		case P_STATELESS:
			if (stateless_arg)
				die("duplicate option");
			if (strcmp(val, "yes") == 0)
				stateless = _B_TRUE;
			else if (strcmp(val, "no") == 0)
				stateless = _B_FALSE;
			else
				die("invalid argument");
			stateless_arg = _B_TRUE;
			break;
		case P_STATEFUL:
			if (stateful_arg)
				die("duplicate option");
			if (strcmp(val, "yes") == 0)
				stateful = _B_TRUE;
			else if (strcmp(val, "no") == 0)
				stateful = _B_FALSE;
			else
				die("invalid argument");
			stateful_arg = _B_TRUE;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}

	if (!stateless_arg && !stateful_arg)
		die("invalid arguments for option -p");

	/* Set the addrobj fields for addrconf */
	if (stateless_arg) {
		status = ipadm_set_stateless(ipaddr, stateless);
		if (status != IPADM_SUCCESS) {
			die("cannot set stateless option: %s",
			    ipadm_status2str(status));
		}
	}
	if (stateful_arg) {
		status = ipadm_set_stateful(ipaddr, stateful);
		if (status != IPADM_SUCCESS) {
			die("cannot set stateful option: %s",
			    ipadm_status2str(status));
		}
	}
}

/*
 * Creates static, dhcp or addrconf addresses and associates the created
 * addresses with the specified address object name.
 */
static void
do_create_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		option;
	uint32_t	flags =
	    IPADM_OPT_PERSIST|IPADM_OPT_ACTIVE|IPADM_OPT_UP;
	char		*cp;
	char		*atype = NULL;
	char		*static_arg = NULL;
	char		*addrconf_arg = NULL;
	char		*interface_arg = NULL;
	char		*hostname = NULL;
	char		*wait = NULL;
	boolean_t	s_opt = _B_FALSE;	/* static addr options */
	boolean_t	auto_opt = _B_FALSE;	/* Addrconf options */
	boolean_t	dhcp_opt = _B_FALSE;	/* dhcp options */

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":T:a:dh:i:p:w:t",
	    addr_longopts, NULL)) != -1) {
		switch (option) {
		case 'T':
			atype = optarg;
			break;
		case 'a':
			static_arg = optarg;
			s_opt = _B_TRUE;
			break;
		case 'd':
			flags &= ~IPADM_OPT_UP;
			s_opt = _B_TRUE;
			break;
		case 'h':
			hostname = optarg;
			dhcp_opt = _B_TRUE;
			break;
		case 'i':
			interface_arg = optarg;
			auto_opt = _B_TRUE;
			break;
		case 'p':
			addrconf_arg = optarg;
			auto_opt = _B_TRUE;
			break;
		case 'w':
			wait = optarg;
			dhcp_opt = _B_TRUE;
			break;
		case 't':
			flags &= ~IPADM_OPT_PERSIST;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (atype == NULL || optind != (argc - 1)) {
		die("invalid arguments\nusage: %s", use);
	} else if ((cp = strchr(argv[optind], '/')) == NULL ||
	    strlen(++cp) == 0) {
		die("invalid address object name: %s\nusage: %s",
		    argv[optind], use);
	}

	/*
	 * Allocate and initialize the addrobj based on the address type.
	 */
	if (strcmp(atype, "static") == 0) {
		if (static_arg == NULL || auto_opt || dhcp_opt) {
			die("invalid arguments for type %s\nusage: %s",
			    atype, use);
		}
		process_static_addrargs(use, static_arg, argv[optind]);
	} else if (strcmp(atype, "dhcp") == 0) {
		if (auto_opt || s_opt) {
			die("invalid arguments for type %s\nusage: %s",
			    atype, use);
		}

		/* Initialize the addrobj for dhcp addresses. */
		status = ipadm_create_addrobj(IPADM_ADDR_DHCP, argv[optind],
		    &ipaddr);
		if (status != IPADM_SUCCESS) {
			die("cannot create address object: %s",
			    ipadm_status2str(status));
		}
		if (hostname != NULL) {
			status = ipadm_set_reqhost(ipaddr, hostname);
			if (status != IPADM_SUCCESS) {
				die("Error setting DNS dynamically "
				    "requested hostname: %s",
				    ipadm_status2str(status));
			}
		}
		if (wait != NULL) {
			int32_t ipadm_wait;

			if (strcmp(wait, "forever") == 0) {
				ipadm_wait = IPADM_DHCP_WAIT_FOREVER;
			} else {
				char *end;
				long timeout = strtol(wait, &end, 10);

				if (*end != '\0' || timeout < 0)
					die("invalid argument");
				ipadm_wait = (int32_t)timeout;
			}
			status = ipadm_set_wait_time(ipaddr, ipadm_wait);
			if (status != IPADM_SUCCESS) {
				die("cannot set wait time: %s",
				    ipadm_status2str(status));
			}
		}
	} else if (strcmp(atype, "addrconf") == 0) {
		if (dhcp_opt || s_opt) {
			die("invalid arguments for type %s\nusage: %s",
			    atype, use);
		}

		process_addrconf_intargs(use, interface_arg, argv[optind]);
		if (addrconf_arg)
			process_addrconf_stateargs(use, addrconf_arg);
	} else {
		die("invalid address type %s", atype);
	}

	status = ipadm_create_addr(iph, ipaddr, flags);
	if (status == IPADM_DHCP_IPC_TIMEOUT)
		warn_ipadmerr(status, "");
	else if (status != IPADM_SUCCESS)
		die("cannot create address: %s", ipadm_status2str(status));
}

/*
 * Used by interface command handler functions to parse the command line
 * arguments.
 */
static void
process_misc_args(int argc, char *argv[], const char *use, int *index,
    uint32_t *flags)
{
	int		option;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":t", if_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 't':
			*flags &= ~IPADM_OPT_PERSIST;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (optind != (argc - 1))
		die("usage: %s", use);

	*index = optind;
}

/*
 * Used by ipmp command handler functions to parse the command line
 * arguments.
 */
static void
process_misc_ipmpargs(int argc, char *argv[], const char *use, int *index,
    uint32_t *flags, char **underifs)
{
	int		option;
	size_t		psize = 0;
	size_t		nsize;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":ti:", if_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 't':
			*flags &= ~IPADM_OPT_PERSIST;
			break;
		case 'i':
			nsize = psize + strlen(optarg) + 1;
			*underifs = realloc(*underifs, nsize);
			if (*underifs == NULL)
				die("insufficient memory");
			if (psize > 0) {
				(void) strlcat(*underifs, ",", nsize);
				(void) strlcat(*underifs, optarg, nsize);
			} else {
				(void) strncpy(*underifs, optarg, nsize);
			}
			psize = strlen(*underifs) + 1;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (optind != (argc - 1))
		die("usage: %s", use);

	*index = optind;
}

/*
 * Remove an addrobj from both active and persistent configuration.
 */
static void
do_delete_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;
	int		option;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":r", addr_misc_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'r':
			flags |= IPADM_OPT_RELEASE;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (optind != (argc - 1))
		die("usage: %s", use);

	status = ipadm_delete_addr(iph, argv[optind], flags);
	if (status != IPADM_SUCCESS)
		die("cannot delete address: %s", ipadm_status2str(status));
}

/*
 * Enable an IP address based on the persistent configuration for that
 * IP address
 */
static void
do_enable_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	process_misc_args(argc, argv, use, &index, &flags);
	if (flags & IPADM_OPT_PERSIST)
		die("persistent operation not supported for enable-addr");

	status = ipadm_enable_addr(iph, argv[index], flags);
	if (status != IPADM_SUCCESS)
		die("cannot enable address: %s", ipadm_status2str(status));
}

/*
 * Mark the address identified by addrobj 'up'
 */
static void
do_up_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	process_misc_args(argc, argv, use, &index, &flags);
	status = ipadm_up_addr(iph, argv[index], flags);
	if (status == IPADM_ADDROBJ_NOT_CREATED) {
		/*
		 * Show a warning if there was an error in creating
		 * an address object for the migrated data address.
		 */
		warn_ipadmerr(status, "");
	} else if (status != IPADM_SUCCESS) {
		die("cannot mark the address up: %s",
		    ipadm_status2str(status));
	}
}

/*
 * Disable the specified addrobj by removing it from active cofiguration
 */
static void
do_disable_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	process_misc_args(argc, argv, use, &index, &flags);
	if (flags & IPADM_OPT_PERSIST)
		die("persistent operation not supported for disable-addr");

	status = ipadm_disable_addr(iph, argv[index], flags);
	if (status != IPADM_SUCCESS)
		die("cannot disable address: %s", ipadm_status2str(status));
}

/*
 * Mark the address identified by addrobj 'down'
 */
static void
do_down_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		index;
	uint32_t 	flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	process_misc_args(argc, argv, use, &index, &flags);
	status = ipadm_down_addr(iph, argv[index], flags);
	if (status != IPADM_SUCCESS)
		die("cannot mark the address down: %s",
		    ipadm_status2str(status));
}

/*
 * Restart DAD for static address. Extend lease duration for DHCP addresses
 */
static void
do_refresh_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t	status;
	int		option;
	uint32_t	flags = 0;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":i", addr_misc_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'i':
			flags |= IPADM_OPT_INFORM;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (optind != (argc - 1))
		die("usage: %s", use);

	status = ipadm_refresh_addr(iph, argv[optind], flags);
	if (status == IPADM_DHCP_IPC_TIMEOUT)
		warn_ipadmerr(status, "");
	else if (status != IPADM_SUCCESS)
		die("cannot refresh address %s", ipadm_status2str(status));
}

static void
sockaddr2str(const struct sockaddr_storage *ssp, char *buf, uint_t bufsize)
{
	socklen_t socklen;
	struct sockaddr *sp = (struct sockaddr *)ssp;

	switch (ssp->ss_family) {
	case AF_INET:
		socklen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		socklen = sizeof (struct sockaddr_in6);
		break;
	default:
		(void) strlcpy(buf, STR_UNKNOWN_VAL, bufsize);
		return;
	}

	(void) getnameinfo(sp, socklen, buf, bufsize, NULL, 0,
	    (NI_NOFQDN | NI_NUMERICHOST));
}

static void
flags2str(uint64_t flags, fmask_t *tbl, boolean_t is_bits,
    char *buf, uint_t bufsize)
{
	int		i;
	boolean_t	first = _B_TRUE;

	if (is_bits) {
		for (i = 0;  tbl[i].name; i++) {
			if ((flags & tbl[i].mask) == tbl[i].bits)
				(void) strlcat(buf, tbl[i].name, bufsize);
			else
				(void) strlcat(buf, "-", bufsize);
		}
	} else {
		for (i = 0; tbl[i].name; i++) {
			if ((flags & tbl[i].mask) == tbl[i].bits) {
				if (!first)
					(void) strlcat(buf, ",", bufsize);
				(void) strlcat(buf, tbl[i].name, bufsize);
				first = _B_FALSE;
			}
		}
	}
}

/*
 * return true if the address for lifname comes to us from the global zone
 * with 'allowed-ips' constraints.
 */
static boolean_t
is_from_gz(const char *lifname)
{
	list_t			if_info;
	ipadm_if_info_t		*ifp;
	char			phyname[LIFNAMSIZ], *cp;
	boolean_t		ret = _B_FALSE;
	ipadm_status_t		status;
	zoneid_t		zoneid;
	ushort_t		zflags;

	if ((zoneid = getzoneid()) == GLOBAL_ZONEID)
		return (_B_FALSE); /* from-gz only  makes sense in a NGZ */

	if (zone_getattr(zoneid, ZONE_ATTR_FLAGS, &zflags, sizeof (zflags)) < 0)
		return (_B_FALSE);

	if (!(zflags & ZF_NET_EXCL))
		return (_B_TRUE);  /* everything is from the GZ for shared-ip */

	(void) strncpy(phyname, lifname, sizeof (phyname));
	if ((cp = strchr(phyname, ':')) != NULL)
		*cp = '\0';
	status = ipadm_if_info(iph, phyname, &if_info, 0, LIFC_DEFAULT);
	if (status != IPADM_SUCCESS)
		return (ret);

	ifp = list_head(&if_info);
	if (ifp->ifi_cflags & IPADM_IFF_L3PROTECT)
		ret = _B_TRUE;
	ipadm_free_if_info(&if_info);
	return (ret);
}

static boolean_t
print_sa_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	show_addr_args_t	*arg = ofarg->ofmt_cbarg;
	ipadm_addr_info_t	*ainfo = arg->sa_info;
	char			interface[LIFNAMSIZ];
	char			addrbuf[MAXPROPVALLEN];
	char			dstbuf[MAXPROPVALLEN];
	char			prefixlenstr[MAXPROPVALLEN];
	int			prefixlen;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	sa_family_t		af;
	char			*phyname = NULL;
	struct ifaddrs		*ifa = &ainfo->ia_ifa;
	fmask_t cflags_mask[] = {
		{ "U",	IPADM_ADDRF_UP,		IPADM_ADDRF_UP		},
		{ "u",	IPADM_ADDRF_UNNUMBERED,	IPADM_ADDRF_UNNUMBERED	},
		{ "p",	IPADM_ADDRF_PRIVATE,	IPADM_ADDRF_PRIVATE	},
		{ "t",	IPADM_ADDRF_TEMPORARY,	IPADM_ADDRF_TEMPORARY	},
		{ "d",	IPADM_ADDRF_DEPRECATED,	IPADM_ADDRF_DEPRECATED	},
		{ NULL,		0,		0			}
	};
	fmask_t pflags_mask[] = {
		{ "U",	IPADM_ADDRF_UP,		IPADM_ADDRF_UP		},
		{ "p",	IPADM_ADDRF_PRIVATE,	IPADM_ADDRF_PRIVATE	},
		{ "d",	IPADM_ADDRF_DEPRECATED,	IPADM_ADDRF_DEPRECATED	},
		{ NULL,		0,		0			}
	};
	fmask_t type[] = {
		{ "static",	IPADM_ADDR_STATIC,	IPADM_ALL_BITS	},
		{ "addrconf",	IPADM_ADDR_IPV6_ADDRCONF, IPADM_ALL_BITS},
		{ "dhcp",	IPADM_ADDR_DHCP,	IPADM_ALL_BITS	},
		{ NULL,		0,			0		}
	};
	fmask_t addr_state[] = {
		{ "disabled",	IPADM_ADDRS_DISABLED,	IPADM_ALL_BITS},
		{ "duplicate",	IPADM_ADDRS_DUPLICATE,	IPADM_ALL_BITS},
		{ "down",	IPADM_ADDRS_DOWN,	IPADM_ALL_BITS},
		{ "tentative",	IPADM_ADDRS_TENTATIVE,	IPADM_ALL_BITS},
		{ "ok",		IPADM_ADDRS_OK,		IPADM_ALL_BITS},
		{ "inaccessible", IPADM_ADDRS_INACCESSIBLE, IPADM_ALL_BITS},
		{ NULL,		0,			0}
	};

	buf[0] = '\0';
	switch (ofarg->ofmt_id) {
	case SA_ADDROBJ:
		if (ainfo->ia_aobjname[0] == '\0') {
			(void) strncpy(interface, ifa->ifa_name, LIFNAMSIZ);
			phyname = strrchr(interface, ':');
			if (phyname)
				*phyname = '\0';
			(void) snprintf(buf, bufsize, "%s/%s", interface,
			    STR_UNKNOWN_VAL);
		} else {
			(void) snprintf(buf, bufsize, "%s", ainfo->ia_aobjname);
		}
		break;
	case SA_STATE:
		flags2str(ainfo->ia_state, addr_state, _B_FALSE,
		    buf, bufsize);
		break;
	case SA_TYPE:
		if (is_from_gz(ifa->ifa_name))
			(void) snprintf(buf, bufsize, "from-gz");
		else
			flags2str(ainfo->ia_atype, type, _B_FALSE, buf,
			    bufsize);
		break;
	case SA_CURRENT:
		flags2str(ainfo->ia_cflags, cflags_mask, _B_TRUE, buf, bufsize);
		break;
	case SA_PERSISTENT:
		flags2str(ainfo->ia_pflags, pflags_mask, _B_TRUE, buf, bufsize);
		break;
	case SA_ADDR:
		af = ifa->ifa_addr->sa_family;
		/*
		 * If the address is 0.0.0.0 or :: and the origin is DHCP,
		 * print STR_UNKNOWN_VAL.
		 */
		if (ainfo->ia_atype == IPADM_ADDR_DHCP) {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			sin = (struct sockaddr_in *)ifa->ifa_addr;
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			if ((af == AF_INET &&
			    sin->sin_addr.s_addr == INADDR_ANY) ||
			    (af == AF_INET6 &&
			    IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))) {
				(void) snprintf(buf, bufsize, STR_UNKNOWN_VAL);
				break;
			}
		}
		if (ifa->ifa_netmask == NULL)
			prefixlen = 0;
		else
			prefixlen = mask2plen(ifa->ifa_netmask);
		bzero(prefixlenstr, sizeof (prefixlenstr));
		if (prefixlen > 0) {
			(void) snprintf(prefixlenstr, sizeof (prefixlenstr),
			    "/%d", prefixlen);
		}
		bzero(addrbuf, sizeof (addrbuf));
		bzero(dstbuf, sizeof (dstbuf));
		if (ainfo->ia_state == IPADM_ADDRS_DISABLED) {
			/*
			 * Print the hostname fields for static addresses.
			 */
			if (ainfo->ia_atype == IPADM_ADDR_STATIC) {
				(void) snprintf(buf, bufsize, "%s",
				    ainfo->ia_sname);
				if (ainfo->ia_dname[0] != '\0') {
					(void) snprintf(dstbuf, sizeof (dstbuf),
					    "->%s", ainfo->ia_dname);
					(void) strlcat(buf, dstbuf, bufsize);
				} else {
					(void) strlcat(buf, prefixlenstr,
					    bufsize);
				}
				break;
			}

			/*
			 * For addrconf addresses, ifa_addr and ifa_dstaddr
			 * have been populated with their token values (if any).
			 */
			if (ainfo->ia_atype == IPADM_ADDR_IPV6_ADDRCONF) {
				sockaddr2str(
				    /* LINTED E_BAD_PTR_CAST_ALIGN */
				    (struct sockaddr_storage *)ifa->ifa_addr,
				    addrbuf, sizeof (addrbuf));
				if (ifa->ifa_dstaddr != NULL) {
					sockaddr2str(
					    /* LINTED E_BAD_PTR_CAST_ALIGN */
					    (struct sockaddr_storage *)
					    ifa->ifa_dstaddr,
					    dstbuf, sizeof (dstbuf));
					(void) snprintf(buf, bufsize, "%s->%s",
					    addrbuf, dstbuf);
				} else {
					(void) snprintf(buf, bufsize, "%s%s",
					    addrbuf, prefixlenstr);
				}
				break;
			}
		}
		/*
		 * For the non-persistent case, we need to show the
		 * currently configured addresses for source and
		 * destination.
		 */
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		sockaddr2str((struct sockaddr_storage *)ifa->ifa_addr,
		    addrbuf, sizeof (addrbuf));
		if (ifa->ifa_flags & IFF_POINTOPOINT) {
			sockaddr2str(
			    /* LINTED E_BAD_PTR_CAST_ALIGN */
			    (struct sockaddr_storage *)ifa->ifa_dstaddr,
			    dstbuf, sizeof (dstbuf));
			(void) snprintf(buf, bufsize, "%s->%s", addrbuf,
			    dstbuf);
		} else {
			(void) snprintf(buf, bufsize, "%s%s", addrbuf,
			    prefixlenstr);
		}
		break;
	case SA_CIDTYPE:
		if ((ainfo->ia_ifa.ifa_flags & IFF_DHCPRUNNING) &&
		    (ainfo->ia_ifa.ifa_addr->sa_family != AF_INET6 ||
		    strchr(ainfo->ia_ifa.ifa_name, ':') != NULL)) {
			switch (ainfo->ia_clientid_type) {
			case IPADM_CID_DEFAULT:
				(void) strlcat(buf, "default", bufsize);
				break;
			case IPADM_CID_DUID_LLT:
				(void) strlcat(buf, "DUID-LLT", bufsize);
				break;
			case IPADM_CID_DUID_LL:
				(void) strlcat(buf, "DUID-LL", bufsize);
				break;
			case IPADM_CID_DUID_EN:
				(void) strlcat(buf, "DUID-EN", bufsize);
				break;
			default:
				(void) strlcat(buf, "other", bufsize);
				break;
			}
		}
		break;
	case SA_CIDVAL:
		if (ainfo->ia_clientid != NULL)
			(void) strlcat(buf, ainfo->ia_clientid, bufsize);
		break;
	case SA_BEGIN:
		if (ainfo->ia_lease_begin != 0)
			(void) strftime(buf, bufsize, NULL,
			    localtime(&ainfo->ia_lease_begin));
		break;
	case SA_EXPIRE:
		if (ainfo->ia_lease_expire != 0)
			(void) strftime(buf, bufsize, NULL,
			    localtime(&ainfo->ia_lease_expire));
		break;
	case SA_RENEW:
		if (ainfo->ia_lease_renew != 0)
			(void) strftime(buf, bufsize, NULL,
			    localtime(&ainfo->ia_lease_renew));
		break;
	default:
		die("invalid input");
		break;
	}

	return (_B_TRUE);
}

/*
 * Display address information, either for the given address or
 * for all the addresses managed by ipadm.
 */
static void
do_show_addr(int argc, char *argv[], const char *use)
{
	ipadm_status_t		status;
	show_addr_state_t	state;
	char			*def_fields_str = "addrobj,type,state,addr";
	char			*dhcp_fields_str = "addrobj,state,addr,"
	    "cid-type,cid-value,begin,expire,renew";
	char			*fields_str = NULL;
	ipadm_addr_info_t	*ainfo;
	ipadm_addr_info_t	*ptr;
	show_addr_args_t	sargs;
	int			option;
	ofmt_handle_t		ofmt;
	ofmt_status_t		oferr;
	uint_t			ofmtflags = 0;
	char			*aname;
	char			*ifname = NULL;
	char			*cp;
	boolean_t		found = _B_FALSE;
	boolean_t		d_arg = _B_FALSE;
	boolean_t		o_arg = _B_FALSE;

	opterr = 0;
	state.sa_parsable = _B_FALSE;
	state.sa_persist = _B_FALSE;
	while ((option = getopt_long(argc, argv, "dpo:", show_addr_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'd':
			fields_str = dhcp_fields_str;
			d_arg = _B_TRUE;
			break;
		case 'p':
			state.sa_parsable = _B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			o_arg = _B_TRUE;
			break;
		default:
			die_opterr(optopt, option, use);
			break;
		}
	}
	if (state.sa_parsable && fields_str == NULL)
		die("-p requires -o");

	if (d_arg && state.sa_parsable)
		die("-p cannot be used with -d");
	if (d_arg && o_arg)
		die("-d cannot be used with -o");

	if (optind == argc - 1) {
		aname = argv[optind];
		if ((cp = strchr(aname, '/')) == NULL)
			die("invalid address object name provided");
		if (*(cp + 1) == '\0') {
			ifname = aname;
			*cp = '\0';
			aname = NULL;
		}
	} else if (optind == argc) {
		aname = NULL;
	} else {
		die("usage: %s", use);
	}

	if (state.sa_parsable)
		ofmtflags |= OFMT_PARSABLE;
	if (fields_str == NULL)
		fields_str = def_fields_str;
	oferr = ofmt_open(fields_str, show_addr_fields, ofmtflags, 0, &ofmt);

	ipadm_ofmt_check(oferr, state.sa_parsable, ofmt);
	state.sa_ofmt = ofmt;

	status = ipadm_addr_info(iph, ifname, &ainfo, 0, LIFC_DEFAULT);
	/*
	 * Return without printing any error, if no addresses were found,
	 * for the case where all addresses are requested.
	 */
	if (status != IPADM_SUCCESS)
		die("cannot get address information: %s",
		    ipadm_status2str(status));
	if (ainfo == NULL) {
		ofmt_close(ofmt);
		return;
	}

	bzero(&sargs, sizeof (sargs));
	sargs.sa_state = &state;
	for (ptr = ainfo; ptr != NULL; ptr = IA_NEXT(ptr)) {
		sargs.sa_info = ptr;
		if (aname != NULL) {
			if (strcmp(ptr->ia_aobjname, aname) != 0)
				continue;
			found = _B_TRUE;
		}
		if (d_arg &&
		    (!(ptr->ia_ifa.ifa_flags & IFF_DHCPRUNNING) ||
		    (ptr->ia_ifa.ifa_addr->sa_family == AF_INET6 &&
		    strchr(ptr->ia_ifa.ifa_name, ':') == NULL)))
			continue;
		ofmt_print(state.sa_ofmt, &sargs);
	}
	if (ainfo)
		ipadm_free_addr_info(ainfo);
	if (aname != NULL && !found)
		die("address object not found");
}

static boolean_t
print_si_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	show_if_args_t		*arg = ofarg->ofmt_cbarg;
	ipadm_if_info_t		*ifinfo = arg->si_info;
	list_t			*over;
	ipadm_ifname_t		*ptr, *next;
	char			*ifname = ifinfo->ifi_name;
	fmask_t intf_class[] = {
		{ "loopback",	IPADMIF_CLASS_LOOPBACK, IPADM_ALL_BITS},
		{ "ip",		IPADMIF_CLASS_IP,	IPADM_ALL_BITS},
		{ "ipmp",	IPADMIF_CLASS_IPMP,	IPADM_ALL_BITS},
		{ "vni",	IPADMIF_CLASS_VNI,	IPADM_ALL_BITS},
		{ NULL,		0,		0	}
	};
	fmask_t intf_state[] = {
		{ "ok",		IPADM_IFS_OK,		IPADM_ALL_BITS},
		{ "down",	IPADM_IFS_DOWN,		IPADM_ALL_BITS},
		{ "disabled",	IPADM_IFS_DISABLED,	IPADM_ALL_BITS},
		{ "failed",	IPADM_IFS_FAILED,	IPADM_ALL_BITS},
		{ "offline",	IPADM_IFS_OFFLINE,	IPADM_ALL_BITS},
		{ NULL,		0,			0}
	};
	fmask_t intf_pflags[] = {
		{ "4",	IPADM_IFF_IPV4,		IPADM_IFF_IPV4},
		{ "6",	IPADM_IFF_IPV6,		IPADM_IFF_IPV6},
		{ "s",	IPADM_IFF_STANDBY,	IPADM_IFF_STANDBY},
		{ "l",	IPADM_IFF_UNDERIF,	IPADM_IFF_UNDERIF},
		{ NULL,	0,			0}
	};
	fmask_t intf_cflags[] = {
		{ "b",	IPADM_IFF_BROADCAST,	IPADM_IFF_BROADCAST},
		{ "m",	IPADM_IFF_MULTICAST,	IPADM_IFF_MULTICAST},
		{ "4",	IPADM_IFF_IPV4,		IPADM_IFF_IPV4},
		{ "6",	IPADM_IFF_IPV6,		IPADM_IFF_IPV6},
		{ "p",	IPADM_IFF_POINTOPOINT,	IPADM_IFF_POINTOPOINT},
		{ "v",	IPADM_IFF_VIRTUAL,	IPADM_IFF_VIRTUAL},
		{ "s",	IPADM_IFF_STANDBY,	IPADM_IFF_STANDBY},
		{ "l",	IPADM_IFF_UNDERIF,	IPADM_IFF_UNDERIF},
		{ "i",	IPADM_IFF_INACTIVE,	IPADM_IFF_INACTIVE},
		{ "V",	IPADM_IFF_VRRP,		IPADM_IFF_VRRP},
		{ "a",	IPADM_IFF_NOACCEPT,	IPADM_IFF_NOACCEPT},
		{ "Z",	IPADM_IFF_L3PROTECT,	IPADM_IFF_L3PROTECT},
		{ NULL,	0,			0}
	};

	buf[0] = '\0';
	switch (ofarg->ofmt_id) {
	case SI_IFNAME:
		(void) snprintf(buf, bufsize, "%s", ifname);
		break;
	case SI_CLASS:
		flags2str(ifinfo->ifi_class, intf_class, _B_FALSE,
		    buf, bufsize);
		break;
	case SI_STATE:
		flags2str(ifinfo->ifi_state, intf_state, _B_FALSE,
		    buf, bufsize);
		break;
	case SI_ACTIVE:
		if (ifinfo->ifi_active)
			(void) snprintf(buf, bufsize, "yes");
		else
			(void) snprintf(buf, bufsize, "no");
		break;
	case SI_CURRENT:
		flags2str(ifinfo->ifi_cflags, intf_cflags, _B_TRUE,
		    buf, bufsize);
		break;
	case SI_PERSISTENT:
		flags2str(ifinfo->ifi_pflags, intf_pflags, _B_TRUE,
		    buf, bufsize);
		break;
	case SI_OVER:
		if (ifinfo->ifi_state == IPADM_IFS_DISABLED)
			over = &ifinfo->ifi_punders;
		else
			over = &ifinfo->ifi_unders;
		for (ptr = list_head(over); ptr != NULL; ptr = next) {
			next = list_next(over, ptr);
			(void) strlcat(buf, ptr->ifn_name, bufsize);
			if (next != NULL)
				(void) strlcat(buf, " ", bufsize);
		}
		break;
	default:
		die("invalid input");
		break;
	}

	return (_B_TRUE);
}

/*
 * Display interface information, either for the given interface or
 * for all the interfaces in the system.
 */
static void
do_show_if(int argc, char *argv[], const char *use)
{
	ipadm_status_t		status;
	show_if_state_t		state;
	char			*fields_str = NULL;
	char			*def_fields_str =
	    "ifname,class,state,active,over";
	list_t			if_info;
	ipadm_if_info_t		*ifp;
	show_if_args_t		sargs;
	int			option;
	ofmt_handle_t		ofmt;
	ofmt_status_t		oferr;
	uint_t			ofmtflags = 0;
	char			*ifname = NULL;

	opterr = 0;
	state.si_parsable = _B_FALSE;

	while ((option = getopt_long(argc, argv, "po:", show_if_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'p':
			state.si_parsable = _B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
			break;
		}
	}
	if (state.si_parsable && fields_str == NULL)
		die("-p requires -o");

	if (optind == argc - 1)
		ifname = argv[optind];
	else if (optind != argc)
		die("usage: %s", use);
	if (state.si_parsable)
		ofmtflags |= OFMT_PARSABLE;
	if (fields_str == NULL)
		fields_str = def_fields_str;
	oferr = ofmt_open(fields_str, show_if_fields, ofmtflags, 0, &ofmt);
	ipadm_ofmt_check(oferr, state.si_parsable, ofmt);
	state.si_ofmt = ofmt;
	bzero(&sargs, sizeof (sargs));
	sargs.si_state = &state;
	status = ipadm_if_info(iph, ifname, &if_info, 0, LIFC_DEFAULT);
	/*
	 * Return without printing any error, if no addresses were found.
	 */
	if (status != IPADM_SUCCESS) {
		die("cannot get information for interface(s): %s",
		    ipadm_status2str(status));
	}

	ifp = list_head(&if_info);
	for (; ifp != NULL; ifp = list_next(&if_info, ifp)) {
		sargs.si_info = ifp;
		ofmt_print(state.si_ofmt, &sargs);
	}
	ipadm_free_if_info(&if_info);
}

/*
 * set/reset the address property for a given address
 */
static void
set_addrprop(int argc, char **argv, boolean_t reset, const char *use)
{
	int 			option;
	ipadm_status_t 		status = IPADM_SUCCESS;
	boolean_t 		p_arg = _B_FALSE;
	char 			*nv, *aobjname;
	char			*prop_name, *prop_val;
	uint_t			flags = IPADM_OPT_ACTIVE|IPADM_OPT_PERSIST;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":i:p:t", set_ifprop_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'p':
			if (p_arg)
				die("-p must be specified once only");
			p_arg = _B_TRUE;

			ipadm_check_propstr(optarg, reset, use);
			nv = optarg;
			break;
		case 't':
			flags &= ~IPADM_OPT_PERSIST;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}

	if (!p_arg || optind != (argc - 1))
		die("usage: %s", use);

	prop_name = nv;
	prop_val = strchr(nv, '=');
	if (prop_val != NULL)
		*prop_val++ = '\0';
	aobjname = argv[optind];
	if (reset)
		flags |= IPADM_OPT_DEFAULT;
	status = ipadm_set_addrprop(iph, prop_name, prop_val, aobjname, flags);
	if (status != IPADM_SUCCESS) {
		if (reset)
			die("reset-addrprop: %s: %s", prop_name,
			    ipadm_status2str(status));
		else
			die("set-addrprop: %s: %s", prop_name,
			    ipadm_status2str(status));
	}
}

/*
 * Sets a property on an address object.
 */
static void
do_set_addrprop(int argc, char **argv, const char *use)
{
	set_addrprop(argc, argv, _B_FALSE, use);
}

/*
 * Resets a property to its default value on an address object.
 */
static void
do_reset_addrprop(int argc, char **argv, const char *use)
{
	set_addrprop(argc, argv,  _B_TRUE, use);
}

/*
 * Display information for all or specific address properties, either for a
 * given address or for all the addresses in the system.
 */
static void
do_show_addrprop(int argc, char *argv[], const char *use)
{
	int 			option;
	nvlist_t 		*proplist = NULL;
	char			*fields_str = NULL;
	show_prop_state_t 	state;
	ofmt_handle_t		ofmt;
	ofmt_status_t		oferr;
	uint_t			ofmtflags = 0;
	char			*aobjname;
	char			*ifname = NULL;
	char			*cp;

	opterr = 0;
	bzero(&state, sizeof (state));
	state.sps_propval = NULL;
	state.sps_parsable = _B_FALSE;
	state.sps_addrprop = _B_TRUE;
	state.sps_proto = MOD_PROTO_NONE;
	state.sps_status = state.sps_retstatus = IPADM_SUCCESS;
	while ((option = getopt_long(argc, argv, ":p:i:cPo:",
	    show_prop_longopts, NULL)) != -1) {
		switch (option) {
		case 'p':
			if (ipadm_str2nvlist(optarg, &proplist,
			    IPADM_NORVAL) != 0)
				die("invalid interface properties specified");
			break;
		case 'c':
			state.sps_parsable = _B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
			break;
		}
	}
	if (optind == argc - 1) {
		aobjname = argv[optind];
		cp = strchr(aobjname, '/');
		if (cp == NULL)
			die("invalid address object name provided");
		if (*(cp + 1) == '\0') {
			ifname = aobjname;
			*cp = '\0';
			aobjname = NULL;
		}
	} else if (optind == argc) {
		aobjname = NULL;
	} else {
		die("usage: %s", use);
	}
	state.sps_proplist = proplist;
	if (state.sps_parsable)
		ofmtflags |= OFMT_PARSABLE;
	oferr = ofmt_open(fields_str, addrprop_fields, ofmtflags, 0, &ofmt);
	ipadm_ofmt_check(oferr, state.sps_parsable, ofmt);
	state.sps_ofmt = ofmt;

	if (aobjname != NULL) {
		(void) strlcpy(state.sps_aobjname, aobjname,
		    sizeof (state.sps_aobjname));
		show_properties(&state, IPADMPROP_CLASS_ADDR);
	} else {
		ipadm_addr_info_t	*ainfop = NULL;
		ipadm_addr_info_t	*ptr;
		ipadm_status_t		status;

		status = ipadm_addr_info(iph, ifname, &ainfop, 0, LIFC_DEFAULT);
		/*
		 * Return without printing any error, if no addresses were
		 * found.
		 */
		if (status == IPADM_OBJ_NOTFOUND)
			return;
		if (status != IPADM_SUCCESS) {
			die("cannot get address information: %s",
			    ipadm_status2str(status));
		}
		for (ptr = ainfop; ptr; ptr = IA_NEXT(ptr)) {
			aobjname = ptr->ia_aobjname;
			if (aobjname[0] == '\0' ||
			    ptr->ia_atype == IPADM_ADDR_IPV6_ADDRCONF) {
				continue;
			}
			(void) strlcpy(state.sps_aobjname, aobjname,
			    sizeof (state.sps_aobjname));
			show_properties(&state, IPADMPROP_CLASS_ADDR);
		}
		ipadm_free_addr_info(ainfop);
	}
	nvlist_free(proplist);
	ofmt_close(ofmt);
	if (state.sps_retstatus != IPADM_SUCCESS) {
		ipadm_close(iph);
		exit(EXIT_FAILURE);
	}
}

static void
ipadm_ofmt_check(ofmt_status_t oferr, boolean_t parsable,
    ofmt_handle_t ofmt)
{
	char buf[OFMT_BUFSIZE];

	if (oferr == OFMT_SUCCESS)
		return;
	(void) ofmt_strerror(ofmt, oferr, buf, sizeof (buf));
	/*
	 * All errors are considered fatal in parsable mode.
	 * NOMEM errors are always fatal, regardless of mode.
	 * For other errors, we print diagnostics in human-readable
	 * mode and processs what we can.
	 */
	if (parsable || oferr == OFMT_ENOFIELDS) {
		ofmt_close(ofmt);
		die(buf);
	} else {
		warn(buf);
	}
}

/*
 * check if the `pstr' adheres to following syntax
 *	- prop=<value[,...]>	(for set)
 *	- prop			(for reset)
 */
static void
ipadm_check_propstr(const char *pstr, boolean_t reset, const char *use)
{
	char	*nv;

	nv = strchr(pstr, '=');
	if (reset) {
		if (nv != NULL)
			die("incorrect syntax used for -p.\n%s", use);
	} else {
		if (nv == NULL || *++nv == '\0')
			die("please specify the value to be set.\n%s", use);
		nv = strchr(nv, '=');
		/* cannot have multiple 'prop=val' for single -p */
		if (nv != NULL)
			die("cannot specify more than one prop=val at "
			    "a time.\n%s", use);
	}
}
