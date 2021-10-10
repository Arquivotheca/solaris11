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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#include <sys/types.h>
#include <getopt.h>
#include <ofmt.h>
#include <lldp.h>
#include <libdllink.h>
#include <liblldp.h>
#include <lldp.h>
#include <sys/vlan.h>

static void	die(const char *, ...);
static void	die_optdup(int);
static void	die_opterr(int, int, const char *);
static void	die_dlerr(lldp_status_t, const char *, ...);
static void	warn(const char *, ...);
static void	warn_dlerr(lldp_status_t, const char *, ...);
static void	do_show_agent_stats(const char *, const char *, boolean_t);

/*
 * callback functions for printing output and error diagnostics.
 */
static ofmt_cb_t print_lldp_prop_cb;
static ofmt_cb_t print_info_cb, print_lldp_stats_cb;

typedef void cmdfunc_t(int, char **, const char *);

static cmdfunc_t do_set_agent_prop, do_reset_agent_prop, do_show_agent_prop;
static cmdfunc_t do_set_agent_tlvprop, do_reset_agent_tlvprop,
		do_show_agent_tlvprop;
static cmdfunc_t do_set_global_tlvprop, do_reset_global_tlvprop,
		do_show_global_tlvprop;
static cmdfunc_t do_show_agent_info;

static char *progname;

typedef struct	cmd {
	char		*c_name;
	char		*c_alias;
	cmdfunc_t	*c_fn;
	const char	*c_usage;
} cmd_t;

/*
 * structures for 'lldpadm show-agentprop'
 */
typedef enum {
	LLDP_PROP_AGENT,
	LLDP_PROP_TLVNAME,
	LLDP_PROP_PROPERTY,
	LLDP_PROP_PERM,
	LLDP_PROP_VALUE,
	LLDP_PROP_DEFAULT,
	LLDP_PROP_POSSIBLE
} lldp_prop_field_index_t;

static ofmt_field_t lldp_agentprop_fields[] = {
/* name,	field width,  index */
{ "AGENT",	13,	LLDP_PROP_AGENT,	print_lldp_prop_cb},
{ "PROPERTY",	11,	LLDP_PROP_PROPERTY,	print_lldp_prop_cb},
{ "PERM",	5,	LLDP_PROP_PERM,		print_lldp_prop_cb},
{ "VALUE",	15,	LLDP_PROP_VALUE,	print_lldp_prop_cb},
{ "DEFAULT",	15,	LLDP_PROP_DEFAULT,	print_lldp_prop_cb},
{ "POSSIBLE",	20,	LLDP_PROP_POSSIBLE,	print_lldp_prop_cb},
{ NULL,		0,	0,			NULL}};

static ofmt_field_t lldp_agent_tlvprop_fields[] = {
/* name,	field width,  index */
{ "AGENT",	13,	LLDP_PROP_AGENT,	print_lldp_prop_cb},
{ "TLVNAME",	8,	LLDP_PROP_TLVNAME,	print_lldp_prop_cb},
{ "PROPERTY",	12,	LLDP_PROP_PROPERTY,	print_lldp_prop_cb},
{ "PERM",	5,	LLDP_PROP_PERM,		print_lldp_prop_cb},
{ "VALUE",	15,	LLDP_PROP_VALUE,	print_lldp_prop_cb},
{ "DEFAULT",	15,	LLDP_PROP_DEFAULT,	print_lldp_prop_cb},
{ "POSSIBLE",	20,	LLDP_PROP_POSSIBLE,	print_lldp_prop_cb},
{ NULL,		0,	0,			NULL}};

static ofmt_field_t lldp_global_tlvprop_fields[] = {
/* name,	field width,  index */
{ "TLVNAME",	10,	LLDP_PROP_TLVNAME,	print_lldp_prop_cb},
{ "PROPERTY",	12,	LLDP_PROP_PROPERTY,	print_lldp_prop_cb},
{ "PERM",	5,	LLDP_PROP_PERM,		print_lldp_prop_cb},
{ "VALUE",	15,	LLDP_PROP_VALUE,	print_lldp_prop_cb},
{ "DEFAULT",	15,	LLDP_PROP_DEFAULT,	print_lldp_prop_cb},
{ "POSSIBLE",	20,	LLDP_PROP_POSSIBLE,	print_lldp_prop_cb},
{ NULL,		0,	0,			NULL}};

typedef struct show_prop_state {
	char		ls_laname[MAXLINKNAMELEN];
	const char	*ls_tlvname;
	const char	*ls_pname;
	lldp_propclass_t ls_pclass;
	nvlist_t	*ls_proplist;
	boolean_t	ls_parsable;
	boolean_t	ls_gtlvprop;
	boolean_t	ls_agentprop;
	boolean_t	ls_atlvprop;
	lldp_status_t	ls_status;
	lldp_status_t	ls_retstatus;
	ofmt_handle_t	ls_ofmt;
} show_prop_state_t;

static const struct option setprop_longopts[] = {
	{"agent",	required_argument,	0, 'a'	},
	{"prop",	required_argument,	0, 'p'  },
	{ 0, 		0, 			0, 0 }
};

static const struct option show_agentprop_longopts[] = {
	{"output",	required_argument,	0, 'o'  },
	{"prop",	required_argument,	0, 'p'  },
	{"parsable",	no_argument,		0, 'c'  },
	{ 0, 		0, 			0, 0 }
};

static const struct option show_globaltlv_longopts[] = {
	{"output",	required_argument,	0, 'o'  },
	{"prop",	required_argument,	0, 'p'  },
	{"parsable",	no_argument,		0, 'c'  },
	{ 0, 		0, 			0, 0 }
};

static const struct option show_agenttlv_longopts[] = {
	{"agent",	required_argument,	0, 'a'	},
	{"output",	required_argument,	0, 'o'  },
	{"prop",	required_argument,	0, 'p'  },
	{"parsable",	no_argument,		0, 'c'  },
	{ 0, 		0, 			0, 0 }
};

static const struct option info_longopts[] = {
	{"output",	required_argument,	0, 'o'	},
	{"parsable",	no_argument,		0, 'c'	},
	{"local",	no_argument,		0, 'l'  },
	{"remote",	no_argument,		0, 'r'  },
	{"verbose",	no_argument,		0, 'v'  },
	{ "statistics",	no_argument,		0, 's'	},
	{ 0, 		0, 			0, 0 }
};

static cmd_t	cmds[] = {
	{ "set-agentprop", "set-ap",	do_set_agent_prop,
	    "\tset-agentprop\t-p <prop>[+|-]=<value>[,...] <lldp_agent>" },
	{ "reset-agentprop", "reset-ap",	do_reset_agent_prop,
	    "\treset-agentprop\t-p <prop>[,...] <lldp_agent>" },
	{ "show-agentprop", "show-ap",	do_show_agent_prop,
	    "\tshow-agentprop\t[[-c] -o <field>,...] -p <prop>[,...] "
	    "[<lldp_agent>]" },
	{ "set-tlvprop", "set-tp",	do_set_global_tlvprop,
	    "\tset-tlvprop\t\t-p <prop>[+|-]=<value>[,...] <tlv_name>" },
	{ "reset-tlvprop", "reset-tp",	do_reset_global_tlvprop,
	    "\treset-tlvprop\t-p <prop>[,...] <tlv_name>" },
	{ "show-tlvprop", "show-tp",	do_show_global_tlvprop,
	    "\tshow-tlvprop\t[[-c] -o <field>,...] -p <prop>[,...] "
	    "[<tlv_name>]" },
	{ "set-agenttlvprop", "set-atp",	do_set_agent_tlvprop,
	    "\tset-agenttlvprop\t-p <prop>[+|-]=<value>[,...] "
	    "\n\t\t\t-a <lldp_agentl> <tlv_name>" },
	{ "reset-agenttlvprop", "reset-atp",	do_reset_agent_tlvprop,
	    "\treset-agenttlvprop\t-p <prop>[,...] -a <lldp_agent> "
	    "\n\t\t\t<tlv_name>" },
	{ "show-agenttlvprop",	"show-atp", do_show_agent_tlvprop,
	    "\tshow-agenttlvprop\t[[-c] -o <field>,...] -p <prop>[,...] "
	    "\n\t\t\t[-a <lldp_agent>] [<tlv_name>]" },
	/* l - Local, r - Remote, v - Verbose */
	{ "show-agent",	"show-agent", do_show_agent_info,
	    "\tshow-agent\t [-s] [-v] [-l|-r] [[-c] -o <field>,...] "
	    "\n\t\t\t[<lldp_agent>]" },
};

/*
 * Detailed fields for LLDP
 */
typedef enum {
	LLDP_FIELD_AGENT,
	LLDP_FIELD_CID,
	LLDP_FIELD_CID_SUBTYPE,
	LLDP_FIELD_PORTID,
	LLDP_FIELD_PID_SUBTYPE,
	LLDP_FIELD_PORTDESC,
	LLDP_FIELD_TTL,
	LLDP_FIELD_INFO_VALID,
	LLDP_FIELD_NEXT_TX,
	LLDP_FIELD_SYSNAME,
	LLDP_FIELD_SYSDESC,
	LLDP_FIELD_SUPCAPAB,
	LLDP_FIELD_ENABCAPAB,
	LLDP_FIELD_MGMTADDR,
	LLDP_FIELD_MAXFRAMESZ,
	LLDP_FIELD_PVID,
	LLDP_FIELD_VLAN_INFO,
	LLDP_FIELD_VNIC_INFO,
	LLDP_FIELD_AGGR_INFO,
	LLDP_FIELD_WILLING,
	LLDP_FIELD_PFC_CAP,
	LLDP_FIELD_PFC_MBC,
	LLDP_FIELD_PFC_ENABLE,
	LLDP_FIELD_APPLN_INFO
} lldp_fields_index_t;

/* Summary fields for local LLDP information */
static ofmt_field_t lldp_remote_summary_fields[] = {
/* name,	field width,	field id,		callback */
{ "AGENT",		20,	LLDP_FIELD_AGENT,	print_info_cb },
{ "SYSNAME",		20,	LLDP_FIELD_SYSNAME,	print_info_cb },
{ "CHASSISID",		20,	LLDP_FIELD_CID,		print_info_cb },
{ "PORTID",		20,	LLDP_FIELD_PORTID,	print_info_cb },
{ NULL,			0, 	0, 			NULL}
};

/* Summary fields for remote LLDP information */
static ofmt_field_t lldp_local_summary_fields[] = {
/* name,	field width,	field id,		callback */
{ "AGENT",		20,	LLDP_FIELD_AGENT,	print_info_cb },
{ "CHASSISID",		20,	LLDP_FIELD_CID,		print_info_cb },
{ "PORTID",		20,	LLDP_FIELD_PORTID,	print_info_cb },
{ NULL,			0, 	0, 			NULL}
};

#define	LLDP_DETAILED_FIELDS	\
{ "Agent",			20, LLDP_FIELD_AGENT, print_info_cb },    \
{ "Chassis ID Subtype",		20, LLDP_FIELD_CID_SUBTYPE, print_info_cb }, \
{ "Chassis ID",			20, LLDP_FIELD_CID,	print_info_cb },     \
{ "Port ID Subtype",		20, LLDP_FIELD_PID_SUBTYPE, print_info_cb }, \
{ "Port ID",			20, LLDP_FIELD_PORTID,	print_info_cb },   \
{ "Port Description",		20, LLDP_FIELD_PORTDESC, print_info_cb },  \
{ "Time to Live",		20, LLDP_FIELD_TTL,	print_info_cb },   \
{ "System Name",		20, LLDP_FIELD_SYSNAME,	print_info_cb },   \
{ "System Description",		20, LLDP_FIELD_SYSDESC,	print_info_cb },   \
{ "Supported Capabilities",	20, LLDP_FIELD_SUPCAPAB, print_info_cb },  \
{ "Enabled Capabilities", 	20, LLDP_FIELD_ENABCAPAB, print_info_cb }, \
{ "Management Address", 	20, LLDP_FIELD_MGMTADDR, print_info_cb },  \
{ "Maximum Frame Size",		20, LLDP_FIELD_MAXFRAMESZ, print_info_cb }, \
{ "Port VLAN ID", 		20, LLDP_FIELD_PVID,	print_info_cb },   \
{ "VLAN Name/ID",		20, LLDP_FIELD_VLAN_INFO, print_info_cb }, \
{ "VNIC PortID/VLAN ID", 	20, LLDP_FIELD_VNIC_INFO, print_info_cb }, \
{ "Aggregation Information",	20, LLDP_FIELD_AGGR_INFO, print_info_cb },  \
{ "PFC Willing",		20, LLDP_FIELD_WILLING, print_info_cb },  \
{ "PFC Cap",			20, LLDP_FIELD_PFC_CAP, print_info_cb },  \
{ "PFC MBC",			20, LLDP_FIELD_PFC_MBC, print_info_cb },  \
{ "PFC Enable",			20, LLDP_FIELD_PFC_ENABLE, print_info_cb },  \
{ "Application(s)(ID/Sel/Pri)",	20, LLDP_FIELD_APPLN_INFO, print_info_cb }  \

static ofmt_field_t lldp_local_detailed_fields[] = {
LLDP_DETAILED_FIELDS,
{ "Next Packet Transmission",	20,	LLDP_FIELD_NEXT_TX,	print_info_cb },
{ NULL,				0, 	0, 			NULL}
};

static ofmt_field_t lldp_remote_detailed_fields[] = {
LLDP_DETAILED_FIELDS,
{ "Information Valid Until",	20,	LLDP_FIELD_INFO_VALID,	print_info_cb },
{ NULL,				0, 	0, 			NULL}
};

/*
 * structures for 'show-agentstats'.
 */
typedef enum {
	LLDP_S_AGENT,
	LLDP_S_FRAMES_INTOTAL,
	LLDP_S_FRAMES_OUTTOTAL,
	LLDP_S_FRAMES_INERR,
	LLDP_S_FRAMES_IDISCARD,
	LLDP_S_FRAMES_LENERR,
	LLDP_S_TLV_DISCARD,
	LLDP_S_TLV_UNRECOGNIZED,
	LLDP_S_AGEOUT
} lldp_stats_fields_index_t;

static ofmt_field_t lldp_stats_fields[] = {
/* name,	field width,	offset,		callback */
{ "AGENT",	16,	LLDP_S_AGENT,		print_lldp_stats_cb },
{ "IFRAMES",	8,	LLDP_S_FRAMES_INTOTAL,	print_lldp_stats_cb },
{ "IERR",	8,	LLDP_S_FRAMES_INERR,	print_lldp_stats_cb },
{ "IDISCARD",	9,	LLDP_S_FRAMES_IDISCARD,	print_lldp_stats_cb },
{ "OFRAMES",	8,	LLDP_S_FRAMES_OUTTOTAL,	print_lldp_stats_cb },
{ "OLENERR",	8,	LLDP_S_FRAMES_LENERR,	print_lldp_stats_cb },
{ "TLVDISCARD",	11,	LLDP_S_TLV_DISCARD,	print_lldp_stats_cb },
{ "TLVUNRECOG",	11,	LLDP_S_TLV_UNRECOGNIZED, print_lldp_stats_cb },
{ "AGEOUT",	7,	LLDP_S_AGEOUT,		print_lldp_stats_cb },
{ NULL,		0, 	0, 			NULL}
};

typedef struct show_lldp_cbarg_s {
	char			slc_laname[MAXLINKNAMELEN];
	nvlist_t		*slc_nvl;
	lldp_stats_t		*slc_stats;
	boolean_t		slc_verbose;
	boolean_t		slc_neighbor;
	lldp_status_t		slc_status;
	ofmt_handle_t		slc_ofmt;
} show_lldp_cbarg_t;

/*
 * Handle to libdladm.  Opened in main() before the sub-command
 * specific function is called.
 */
static dladm_handle_t handle = NULL;

static void
lldp_ofmt_check(ofmt_status_t oferr, boolean_t parsable,
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
	if (parsable || oferr == OFMT_ENOMEM || oferr == OFMT_ENOFIELDS) {
		ofmt_close(ofmt);
		die(buf);
	} else {
		warn(buf);
	}
}

static void
usage(void)
{
	int	i;
	cmd_t	*cmdp;
	(void) fprintf(stderr,
	    gettext("usage:  lldpadm <subcommand> <args> ...\n"));
	for (i = 0; i < sizeof (cmds) / sizeof (cmds[0]); i++) {
		cmdp = &cmds[i];
		if (cmdp->c_usage != NULL)
			(void) fprintf(stderr, "%s\n", gettext(cmdp->c_usage));
	}

	/* close dladm handle if it was opened */
	if (handle != NULL)
		dladm_close(handle);

	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int	i;
	cmd_t	*cmdp;
	char	dlerr[DLADM_STRSIZE];
	dladm_status_t status;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = argv[0];

	if (argc < 2)
		usage();

	for (i = 0; i < sizeof (cmds) / sizeof (cmds[0]); i++) {
		cmdp = &cmds[i];
		if (strcmp(argv[1], cmdp->c_name) == 0 ||
		    strcmp(argv[1], cmdp->c_alias) == 0) {
			/* Open the libdladm handle */
			if ((status = dladm_open(&handle)) != DLADM_STATUS_OK) {
				die("could not open /dev/dld: %s",
				    dladm_status2str(status, dlerr));
			}

			cmdp->c_fn(argc - 1, &argv[1], cmdp->c_usage);

			dladm_close(handle);
			return (EXIT_SUCCESS);
		}
	}

	(void) fprintf(stderr, gettext("%s: unknown subcommand '%s'\n"),
	    progname, argv[1]);
	usage();
	return (EXIT_FAILURE);
}

static void
print_lldp_prop(show_prop_state_t *statep, char *buf,
    size_t bufsize, uint_t valtype)
{
	lldp_status_t	status;

	if (statep->ls_agentprop) {
		status = lldp_get_agentprop(statep->ls_laname,
		    statep->ls_pname, buf, &bufsize, valtype);
	} else if (statep->ls_gtlvprop) {
		status = lldp_get_global_tlvprop(statep->ls_tlvname,
		    statep->ls_pname, buf, &bufsize, valtype);
	} else if (statep->ls_atlvprop) {
		status = lldp_get_agent_tlvprop(statep->ls_laname,
		    statep->ls_tlvname, statep->ls_pname, buf, &bufsize,
		    valtype);
	}
	if (status != LLDP_STATUS_OK) {
		statep->ls_status = status;
		statep->ls_retstatus = status;
		if (status == LLDP_STATUS_PROPUNKNOWN) {
			warn_dlerr(status, "cannot get property '%s'",
			    statep->ls_pname);
		} else if (status == LLDP_STATUS_NOTSUP ||
		    status == LLDP_STATUS_DISABLED ||
		    status == LLDP_STATUS_NOTFOUND) {
			buf = "--";
			statep->ls_status = LLDP_STATUS_OK;
		} else if (status == LLDP_STATUS_LINKINVAL) {
			die_dlerr(status, "%s", statep->ls_laname);
		}
	}
}

static boolean_t
print_lldp_prop_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	show_prop_state_t	*statep = ofarg->ofmt_cbarg;

	/*
	 * Fail retrieving remaining fields, if you fail
	 * to retrieve a field.
	 */
	if (statep->ls_status != LLDP_STATUS_OK)
		return (B_FALSE);

	switch (ofarg->ofmt_id) {
	case LLDP_PROP_AGENT:
		(void) snprintf(buf, bufsize, "%s", statep->ls_laname);
		break;
	case LLDP_PROP_TLVNAME:
		(void) snprintf(buf, bufsize, "%s", statep->ls_tlvname);
		break;
	case LLDP_PROP_PROPERTY:
		(void) snprintf(buf, bufsize, "%s", statep->ls_pname);
		break;
	case LLDP_PROP_VALUE:
		print_lldp_prop(statep, buf, bufsize, LLDP_OPT_ACTIVE);
		break;
	case LLDP_PROP_PERM:
		print_lldp_prop(statep, buf, bufsize, LLDP_OPT_PERM);
		break;
	case LLDP_PROP_DEFAULT:
		print_lldp_prop(statep, buf, bufsize, LLDP_OPT_DEFAULT);
		break;
	case LLDP_PROP_POSSIBLE:
		print_lldp_prop(statep, buf, bufsize, LLDP_OPT_POSSIBLE);
		break;
	default:
		die("invalid input");
		break;
	}
	return ((statep->ls_status == LLDP_STATUS_OK) ?
	    B_TRUE : B_FALSE);
}

static boolean_t
show_oneprop(const char *tlvname, const char *pname, void *arg)
{
	show_prop_state_t	*statep = arg;

	statep->ls_pname = pname;
	if (!statep->ls_agentprop)
		statep->ls_tlvname = tlvname;
	statep->ls_status = LLDP_STATUS_OK;
	ofmt_print(statep->ls_ofmt, arg);

	return (B_TRUE);
}

static int
show_properties(dladm_handle_t dh, datalink_id_t linkid, void *arg)
{
	show_prop_state_t	*statep = arg;
	nvlist_t		*nvl = statep->ls_proplist;
	nvpair_t		*nvp;
	char			*pname;

	if (linkid != DATALINK_INVALID_LINKID) {
		if (dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL,
		    statep->ls_laname, MAXLINKNAMELEN) != DLADM_STATUS_OK) {
			statep->ls_retstatus = LLDP_STATUS_NOTFOUND;
			return (DLADM_WALK_CONTINUE);
		}
	}

	/* if no properties were specified, display all the properties */
	if (nvl == NULL) {
		(void) lldp_walk_prop(show_oneprop, arg,
		    statep->ls_pclass);
	} else {
		for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(nvl, nvp)) {
			pname = nvpair_name(nvp);
			(void) show_oneprop(statep->ls_tlvname, pname, arg);
		}
	}
	return (DLADM_WALK_CONTINUE);
}

static void
do_show_agent_prop(int argc, char **argv, const char *use)
{
	show_prop_state_t state;
	int		option;
	char		propstr[LLDP_STRSIZE];
	nvlist_t	*proplist = NULL;
	char		*fields_str = NULL;
	ofmt_handle_t	ofmt;
	ofmt_status_t	oferr;
	uint_t		ofmtflags = 0;
	boolean_t	agent = B_FALSE;
	boolean_t	p_arg = B_FALSE;

	bzero(propstr, LLDP_STRSIZE);
	bzero(&state, sizeof (state));
	state.ls_pclass = LLDP_PROPCLASS_AGENT;
	state.ls_agentprop = B_TRUE;
	state.ls_status = state.ls_retstatus = LLDP_STATUS_OK;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":p:co:",
	    show_agentprop_longopts, NULL)) != -1) {
		switch (option) {
		case 'p':
			if (propstr[0] != '\0')
				(void) strlcat(propstr, ",", LLDP_STRSIZE);
			if (strlcat(propstr, optarg, LLDP_STRSIZE) >=
			    LLDP_STRSIZE) {
				die("property list too long '%s'", propstr);
			}
			p_arg = B_TRUE;
			break;
		case 'c':
			state.ls_parsable = B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (optind == (argc - 1)) {
		if (strlcpy(state.ls_laname, argv[optind], MAXLINKNAMELEN) >=
		    MAXLINKNAMELEN) {
			die("agent name too long");
		}
		/* check if the link is valid */
		if (dladm_name2info(handle, state.ls_laname, NULL, NULL, NULL,
		    NULL) != DLADM_STATUS_OK) {
			die("agent '%s' is not valid", state.ls_laname);
		}
		agent = B_TRUE;
	} else if (optind != argc) {
		die("Usage:\n%s", use);
	}
	if (p_arg && lldp_str2nvlist(propstr, &proplist, B_FALSE) != 0)
		die("invalid properties specified");
	state.ls_proplist = proplist;
	if (state.ls_parsable)
		ofmtflags |= OFMT_PARSABLE;
	else
		ofmtflags |= OFMT_WRAP;

	oferr = ofmt_open(fields_str, lldp_agentprop_fields, ofmtflags,
	    0, &ofmt);
	lldp_ofmt_check(oferr, state.ls_parsable, ofmt);
	state.ls_ofmt = ofmt;

	if (!agent) {
		/* lldp agents are supported only on physical ports */
		(void) dladm_walk_datalink_id(show_properties,
		    handle, &state, DATALINK_CLASS_PHYS | DATALINK_CLASS_SIMNET,
		    DL_ETHER, DLADM_OPT_ACTIVE);
	} else {
		(void) show_properties(handle, DATALINK_INVALID_LINKID,
		    &state);
	}
	ofmt_close(ofmt);
	nvlist_free(proplist);

	if (state.ls_retstatus != LLDP_STATUS_OK) {
		dladm_close(handle);
		exit(EXIT_FAILURE);
	}
}

static void
do_show_global_tlvprop(int argc, char **argv, const char *use)
{
	show_prop_state_t state;
	int		option;
	char		propstr[LLDP_STRSIZE];
	nvlist_t	*proplist = NULL;
	char		*fields_str = NULL;
	ofmt_handle_t	ofmt;
	ofmt_status_t	oferr;
	uint_t		ofmtflags = 0;
	boolean_t	p_arg = B_FALSE;

	bzero(propstr, LLDP_STRSIZE);
	bzero(&state, sizeof (state));
	state.ls_pclass = LLDP_PROPCLASS_GLOBAL_TLVS;
	state.ls_gtlvprop = B_TRUE;
	state.ls_status = state.ls_retstatus = LLDP_STATUS_OK;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":p:co:",
	    show_globaltlv_longopts, NULL)) != -1) {
		switch (option) {
		case 'p':
			if (propstr[0] != '\0')
				(void) strlcat(propstr, ",", LLDP_STRSIZE);
			if (strlcat(propstr, optarg, LLDP_STRSIZE) >=
			    LLDP_STRSIZE) {
				die("property list too long '%s'", propstr);
			}
			p_arg = B_TRUE;
			break;
		case 'c':
			state.ls_parsable = B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}

	if (optind == (argc - 1)) {
		state.ls_tlvname = argv[optind];
		state.ls_pclass = lldp_tlvname2pclass(state.ls_tlvname);
		if (state.ls_pclass == LLDP_PROPCLASS_NONE)
			die("tlvname %s not supported", state.ls_tlvname);
	} else if (optind != argc) {
		die("Usage:\n%s", use);
	} else if (p_arg) {
		die("TLV name must be specified when property name is used");
	}
	if (p_arg && lldp_str2nvlist(propstr, &proplist, B_FALSE) != 0)
		die("invalid properties specified");
	state.ls_proplist = proplist;
	if (state.ls_parsable)
		ofmtflags |= OFMT_PARSABLE;
	else
		ofmtflags |= OFMT_WRAP;

	oferr = ofmt_open(fields_str, lldp_global_tlvprop_fields, ofmtflags,
	    0, &ofmt);
	lldp_ofmt_check(oferr, state.ls_parsable, ofmt);
	state.ls_ofmt = ofmt;
	(void) show_properties(handle, DATALINK_INVALID_LINKID, &state);
	ofmt_close(ofmt);
	nvlist_free(proplist);

	if (state.ls_retstatus != LLDP_STATUS_OK) {
		dladm_close(handle);
		exit(EXIT_FAILURE);
	}
}

static void
do_show_agent_tlvprop(int argc, char **argv, const char *use)
{
	show_prop_state_t state;
	int		option;
	char		propstr[LLDP_STRSIZE];
	nvlist_t	*proplist = NULL;
	char		*fields_str = NULL;
	ofmt_handle_t	ofmt;
	ofmt_status_t	oferr;
	uint_t		ofmtflags = 0;
	boolean_t	agent = B_FALSE;
	boolean_t	p_arg = B_FALSE;

	bzero(propstr, LLDP_STRSIZE);
	bzero(&state, sizeof (state));
	state.ls_pclass = LLDP_PROPCLASS_AGENT_TLVS;
	state.ls_atlvprop = B_TRUE;
	state.ls_status = state.ls_retstatus = LLDP_STATUS_OK;

	opterr = 0;
	while ((option = getopt_long(argc, argv, ":p:co:a:",
	    show_agenttlv_longopts, NULL)) != -1) {
		switch (option) {
		case 'a':
			if (agent)
				die_optdup(option);
			if (strlcpy(state.ls_laname, optarg, MAXLINKNAMELEN) >=
			    MAXLINKNAMELEN) {
				die("agent name too long");
			}
			/* check if the link is valid */
			if (dladm_name2info(handle, state.ls_laname, NULL, NULL,
			    NULL, NULL) != DLADM_STATUS_OK) {
				die("agent '%s' is not valid", state.ls_laname);
			}
			agent = B_TRUE;
			break;
		case 'p':
			if (propstr[0] != '\0')
				(void) strlcat(propstr, ",", LLDP_STRSIZE);
			if (strlcat(propstr, optarg, LLDP_STRSIZE) >=
			    LLDP_STRSIZE) {
				die("property list too long '%s'", propstr);
			}
			p_arg = B_TRUE;
			break;
		case 'c':
			state.ls_parsable = B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		default:
			die_opterr(optopt, option, use);
			break;
		}
	}

	if (optind == (argc - 1)) {
		state.ls_tlvname = argv[optind];
		state.ls_pclass = lldp_tlvname2pclass(state.ls_tlvname);
		if (state.ls_pclass == LLDP_PROPCLASS_NONE)
			die("tlvname %s not supported", state.ls_tlvname);
	} else if (optind != argc) {
		die("Usage:\n%s", use);
	} else if (p_arg) {
		die("TLV name must be specified when property name is used");
	}
	if (p_arg && lldp_str2nvlist(propstr, &proplist, B_FALSE) != 0)
		die("invalid properties specified");
	state.ls_proplist = proplist;
	if (state.ls_parsable)
		ofmtflags |= OFMT_PARSABLE;
	else
		ofmtflags |= OFMT_WRAP;

	oferr = ofmt_open(fields_str, lldp_agent_tlvprop_fields, ofmtflags,
	    0, &ofmt);
	lldp_ofmt_check(oferr, state.ls_parsable, ofmt);
	state.ls_ofmt = ofmt;

	if (!agent) {
		/* lldp agents are supported only on physical ports */
		(void) dladm_walk_datalink_id(show_properties, handle,
		    &state, DATALINK_CLASS_PHYS|DATALINK_CLASS_SIMNET, DL_ETHER,
		    DLADM_OPT_ACTIVE);
	} else {
		(void) show_properties(handle, DATALINK_INVALID_LINKID,
		    &state);
	}
	ofmt_close(ofmt);
	nvlist_free(proplist);

	if (state.ls_retstatus != LLDP_STATUS_OK) {
		dladm_close(handle);
		exit(EXIT_FAILURE);
	}
}

static void
set_lldp_prop(int argc, char **argv,  boolean_t agentprop, boolean_t global,
    boolean_t reset, const char *use)
{
	int			option;
	lldp_status_t		status = LLDP_STATUS_OK;
	char			*pval, *endp;
	char			*laname, *tlvname, *pname;
	char			propstr[LLDP_STRSIZE];
	nvlist_t		*proplist = NULL;
	nvpair_t		*nvp;
	uint_t			flags;

	opterr = 0;
	bzero(propstr, sizeof (propstr));
	laname = tlvname = pname = NULL;
	while ((option = getopt_long(argc, argv, ":p:a:", setprop_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'a':
			if (global || agentprop)
				die("Usage:\n%s", use);
			if (laname != NULL)
				die_optdup(option);
			laname = optarg;
			break;
		case 'p':
			if (propstr[0] != '\0')
				(void) strlcat(propstr, ",", LLDP_STRSIZE);

			if (strlcat(propstr, optarg, LLDP_STRSIZE) >=
			    LLDP_STRSIZE) {
				die("property list too long '%s'", propstr);
			}
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}

	/* required last argument */
	if (optind != (argc - 1))
		die("Usage:\n%s", use);
	if (agentprop) {
		laname = argv[optind];
	} else {
		if (!global && laname == NULL)
			die("-a is mandatory.\n%s", use);
		tlvname = argv[optind];
		if (lldp_tlvname2pclass(tlvname) == LLDP_PROPCLASS_NONE)
			die("tlvname %s not supported", tlvname);
	}
	if (lldp_str2nvlist(propstr, &proplist, !reset) != 0)
		die("invalid properties specified");
	if (proplist == NULL)
		die("property must be specified");

	for (nvp = nvlist_next_nvpair(proplist, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(proplist, nvp)) {
		pname = nvpair_name(nvp);
		if (reset) {
			pval = NULL;
			flags = LLDP_OPT_DEFAULT;
		} else {
			if (nvpair_value_string(nvp, &pval) != 0) {
				warn("failed to set property '%s'", pname);
				continue;
			}
			/* check for an qualifiers, '+' or '-' */
			endp = pname + strlen(pname) - 1;
			if (*endp == '+') {
				*endp = '\0';
				flags = LLDP_OPT_APPEND;
			} else if (*endp == '-') {
				*endp = '\0';
				flags = LLDP_OPT_REMOVE;
			} else {
				flags = LLDP_OPT_ACTIVE;
			}
		}
		/* now set the property */
		if (agentprop) {
			status = lldp_set_agentprop(laname, pname, pval, flags);
		} else if (global) {
			status = lldp_set_global_tlvprop(tlvname, pname, pval,
			    flags);
		} else {
			status = lldp_set_agent_tlvprop(laname, tlvname, pname,
			    pval, flags);
		}
		if (status != LLDP_STATUS_OK) {
			if (reset) {
				warn_dlerr(status, "cannot reset property "
				    "'%s'", pname);
			} else {
				warn_dlerr(status, "cannot set property "
				    "'%s'", pname);
			}
		}
	}
	nvlist_free(proplist);
	if (status != LLDP_STATUS_OK) {
		dladm_close(handle);
		exit(EXIT_FAILURE);
	}
}

static void
do_set_agent_prop(int argc, char **argv, const char *use)
{
	set_lldp_prop(argc, argv, B_TRUE, B_FALSE, B_FALSE, use);
}

static void
do_reset_agent_prop(int argc, char **argv, const char *use)
{
	set_lldp_prop(argc, argv, B_TRUE, B_FALSE, B_TRUE, use);
}

static void
do_set_global_tlvprop(int argc, char **argv, const char *use)
{
	set_lldp_prop(argc, argv, B_FALSE, B_TRUE, B_FALSE, use);
}

static void
do_reset_global_tlvprop(int argc, char **argv, const char *use)
{
	set_lldp_prop(argc, argv, B_FALSE,  B_TRUE, B_TRUE, use);
}

static void
do_set_agent_tlvprop(int argc, char **argv, const char *use)
{
	set_lldp_prop(argc, argv, B_FALSE, B_FALSE, B_FALSE, use);
}

static void
do_reset_agent_tlvprop(int argc, char **argv, const char *use)
{
	set_lldp_prop(argc, argv, B_FALSE, B_FALSE, B_TRUE, use);
}

static boolean_t
print_info_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	show_lldp_cbarg_t	*cbarg = ofarg->ofmt_cbarg;
	nvlist_t		*nvl = cbarg->slc_nvl;
	char			*str;
	lldp_chassisid_t	ci;
	lldp_portid_t		pi;
	lldp_syscapab_t		sc;
	lldp_aggr_t		aggr;
	lldp_vlan_info_t	*vlan, *tvlan;
	lldp_vnic_info_t	*vnic, *tvnic;
	lldp_mgmtaddr_t		*mpp, *mp;
	lldp_pfc_t		pfc;
	lldp_appln_t		*appln, *app;
	uint_t			nappln;
	uint8_t			subtype;
	uint16_t		ttl, pvid, time, fsz;
	int			vcnt, mcnt, cnt, len;
	char			pidstr[LLDP_MAX_PORTIDSTRLEN];
	char			addrstr[LLDP_STRSIZE];

	switch (ofarg->ofmt_id) {
	case LLDP_FIELD_AGENT:
		(void) snprintf(buf, bufsize, "%s", cbarg->slc_laname);
		break;
	case LLDP_FIELD_SYSNAME:
		if (lldp_nvlist2sysname(nvl, &str) == 0)
			(void) snprintf(buf, bufsize, "%s", str);
		break;
	case LLDP_FIELD_CID_SUBTYPE:
		if (lldp_nvlist2chassisid(nvl, &ci) != 0)
			break;
		subtype = ci.lc_subtype;
		(void) snprintf(buf, bufsize, "%s(%d)",
		    lldp_chassis_subtype2str(subtype), subtype);
		break;
	case LLDP_FIELD_CID:
		if (lldp_nvlist2chassisid(nvl, &ci) == 0)
			(void) lldp_chassisID2str(&ci, buf, bufsize);
		break;
	case LLDP_FIELD_PID_SUBTYPE:
		if (lldp_nvlist2portid(nvl, &pi) != 0)
			break;
		subtype = pi.lp_subtype;
		(void) snprintf(buf, bufsize, "%s(%d)",
		    lldp_port_subtype2str(subtype), subtype);
		break;
	case LLDP_FIELD_PORTID:
		if (lldp_nvlist2portid(nvl, &pi) == 0)
			(void) lldp_portID2str(&pi, buf, bufsize);
		break;
	case LLDP_FIELD_PORTDESC:
		if (lldp_nvlist2portdescr(nvl, &str) == 0)
			(void) snprintf(buf, bufsize, "%s", str);
		break;
	case LLDP_FIELD_SYSDESC:
		if (lldp_nvlist2sysdescr(nvl, &str) == 0)
			(void) snprintf(buf, bufsize, "%s", str);
		break;
	case LLDP_FIELD_SUPCAPAB:
		if (lldp_nvlist2syscapab(nvl, &sc) == 0)
			lldp_syscapab2str(sc.ls_sup_syscapab, buf, bufsize);
		break;
	case LLDP_FIELD_ENABCAPAB:
		if (lldp_nvlist2syscapab(nvl, &sc) == 0)
			lldp_syscapab2str(sc.ls_enab_syscapab, buf, bufsize);
		break;
	case LLDP_FIELD_MGMTADDR:
		if (lldp_nvlist2mgmtaddr(nvl, NULL, &mpp, &mcnt) != 0)
			break;
		mp = mpp;
		for (cnt = 0; cnt < mcnt; cnt++) {
			addrstr[0] = '\0';
			lldp_mgmtaddr2str(mp, addrstr, sizeof (addrstr));
			if (cnt > 0)
				(void) strlcat(buf, ",", bufsize);
			(void) strlcat(buf, addrstr, bufsize);
			mp++;
		}
		break;
	case LLDP_FIELD_TTL:
		if (lldp_nvlist2ttl(nvl, &ttl) == 0)
			(void) snprintf(buf, bufsize, "%d (seconds)", ttl);
		break;
	case LLDP_FIELD_INFO_VALID:
		if (lldp_nvlist2infovalid(nvl, &time) == 0)
			(void) snprintf(buf, bufsize, "%d (seconds)", time);
		break;
	case LLDP_FIELD_NEXT_TX:
		if (lldp_nvlist2nexttx(nvl, &time) == 0)
			(void) snprintf(buf, bufsize, "%d (seconds)", time);
		break;
	case LLDP_FIELD_MAXFRAMESZ:
		if (lldp_nvlist2maxfsz(nvl, &fsz) == 0)
			(void) snprintf(buf, bufsize, "%u", fsz);
		break;
	case LLDP_FIELD_PVID:
		if (lldp_nvlist2pvid(nvl, &pvid) == 0)
			(void) snprintf(buf, bufsize, "%u", pvid);
		break;
	case LLDP_FIELD_AGGR_INFO:
		if (lldp_nvlist2aggr(nvl, &aggr) != 0)
			break;
		if (aggr.la_status & LLDP_AGGR_MEMBER) {
			(void) snprintf(buf, bufsize, "Aggregated, ID : %d",
			    aggr.la_id);
		} else if (aggr.la_status & LLDP_AGGR_CAPABLE) {
			(void) snprintf(buf, bufsize,
			    "Capable, Not Aggregated");
		}
		break;
	case LLDP_FIELD_VLAN_INFO:
		if (lldp_nvlist2vlan(nvl, &vlan, &vcnt) != 0)
			break;
		len = 0;
		tvlan = vlan;
		for (cnt = 0; cnt < vcnt; cnt++) {
			if (cnt > 0) {
				len += snprintf(buf + len, bufsize,
				    ",%s/%u", vlan->lvi_name,
				    vlan->lvi_vid);
			} else {
				len += snprintf(buf + len, bufsize,
				    "%s/%u", vlan->lvi_name,
				    vlan->lvi_vid);
			}
			vlan++;
		}
		free(tvlan);
		break;
	case LLDP_FIELD_VNIC_INFO:
		if (lldp_nvlist2vnic(nvl, &vnic, &vcnt) != 0)
			break;
		len = 0;
		tvnic = vnic;
		for (cnt = 0; cnt < vcnt; cnt++) {
			if (lldp_portID2str(&vnic->lvni_portid,
			    pidstr, LLDP_MAX_PORTIDSTRLEN) == NULL) {
				cbarg->slc_status = LLDP_STATUS_BADVAL;
				return (B_FALSE);
			}
			if (cnt > 0) {
				if (vnic->lvni_vid != VLAN_ID_NONE) {
					len += snprintf(buf + len,
					    bufsize, ",%s/%u", pidstr,
					    vnic->lvni_vid);
				} else {
					len += snprintf(buf + len,
					    bufsize, ",%s", pidstr);
				}
			} else {
				if (vnic->lvni_vid != VLAN_ID_NONE) {
					len += snprintf(buf + len,
					    bufsize, "%s/%u", pidstr,
					    vnic->lvni_vid);
				} else {
					len += snprintf(buf + len,
					    bufsize, "%s", pidstr);
				}
			}
			vnic++;
		}
		free(tvnic);
		break;
	case LLDP_FIELD_WILLING:
		if (lldp_nvlist2pfc(nvl, &pfc) == 0) {
			(void) snprintf(buf, bufsize, "%s",
			    pfc.lp_willing ? "On" : "Off");
		}
		break;
	case LLDP_FIELD_PFC_CAP:
		if (lldp_nvlist2pfc(nvl, &pfc) == 0)
			(void) snprintf(buf, bufsize, "%u", pfc.lp_cap);
		break;
	case LLDP_FIELD_PFC_MBC:
		if (lldp_nvlist2pfc(nvl, &pfc) == 0) {
			(void) snprintf(buf, bufsize, "%s",
			    pfc.lp_mbc ? "True" : "False");
		}
		break;
	case LLDP_FIELD_PFC_ENABLE:
		if (lldp_nvlist2pfc(nvl, &pfc) == 0)
			(void) snprintf(buf, bufsize, "%u", pfc.lp_enable);
		break;
	case LLDP_FIELD_APPLN_INFO:
		if (lldp_nvlist2appln(nvl, &appln, &nappln) != 0)
			break;
		len = 0;
		app = appln;
		for (cnt = 0; cnt < nappln; cnt++) {
			if (cnt > 0) {
				len += snprintf(buf + len, bufsize,
				    "; %x/%u/%u", app->la_id,
				    app->la_sel, app->la_pri);
			} else {
				len += snprintf(buf + len, bufsize,
				    "%x/%u/%u", app->la_id,
				    app->la_sel, app->la_pri);
			}
			app++;
		}
		free(appln);
		break;
	default:
		die("invalid input");
	}
	return (B_TRUE);
}

static int
show_agent_info(dladm_handle_t dh, datalink_id_t linkid, void *arg)
{
	show_lldp_cbarg_t	*cbarg = arg;
	lldp_status_t		status;
	nvlist_t		*nvl = NULL, *tnvl;
	nvpair_t		*nvp;

	if (linkid != DATALINK_INVALID_LINKID &&
	    dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL,
	    cbarg->slc_laname, MAXLINKNAMELEN) != DLADM_STATUS_OK) {
		return (DLADM_WALK_CONTINUE);
	}
	status = lldp_get_agent_info(cbarg->slc_laname, cbarg->slc_neighbor,
	    &nvl);
	if (status != LLDP_STATUS_OK && status != LLDP_STATUS_DISABLED &&
	    status != LLDP_STATUS_NOTFOUND) {
		cbarg->slc_status = status;
		return (DLADM_WALK_TERMINATE);
	} else if (status == LLDP_STATUS_NOTFOUND ||
	    status == LLDP_STATUS_DISABLED) {
		return (DLADM_WALK_CONTINUE);
	}

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (nvpair_value_nvlist(nvp, &tnvl) != 0)
			continue;
		cbarg->slc_nvl = tnvl;
		ofmt_print(cbarg->slc_ofmt, cbarg);
		if (cbarg->slc_status != LLDP_STATUS_OK)
			break;
	}
	nvlist_free(nvl);
	return ((cbarg->slc_status != LLDP_STATUS_OK ? DLADM_WALK_TERMINATE :
	    DLADM_WALK_CONTINUE));
}

static void
do_show_agent_info(int argc, char *argv[], const char *use)
{
	int		option;
	ofmt_handle_t	ofmt;
	ofmt_status_t	oferr;
	uint_t		ofmtflags = 0;
	show_lldp_cbarg_t cbarg;
	char		*fields_str = NULL;
	ofmt_field_t 	*fields = NULL;
	boolean_t	link = B_FALSE;
	boolean_t	r_arg = B_FALSE;
	boolean_t	l_arg = B_FALSE;
	boolean_t	s_arg = B_FALSE;
	boolean_t	parsable = B_FALSE;

	bzero(&cbarg, sizeof (cbarg));
	cbarg.slc_status = LLDP_STATUS_OK;
	opterr = 0;
	while ((option = getopt_long(argc, argv, ":co:vlrs", info_longopts,
	    NULL)) != -1) {
		switch (option) {
		case 'c':
			parsable = B_TRUE;
			break;
		case 'o':
			fields_str = optarg;
			break;
		case 'r':
			cbarg.slc_neighbor = B_TRUE;
			r_arg = B_TRUE;
			break;
		case 'l':
			cbarg.slc_neighbor = B_FALSE;
			l_arg = B_TRUE;
			break;
		case 'v':
			cbarg.slc_verbose = B_TRUE;
			break;
		case 's':
			s_arg = B_TRUE;
			break;
		default:
			die_opterr(optopt, option, use);
		}
	}
	if (s_arg && (l_arg || r_arg || cbarg.slc_verbose))
		die("-s and -l/-r/-v options are not compatible");
	if (r_arg && l_arg)
		die("-l and -r options are not compatible");
	if (cbarg.slc_verbose && parsable)
		die("-v and -c options are not compatible");

	if (optind == argc - 1) {
		if (strlcpy(cbarg.slc_laname, argv[optind], MAXLINKNAMELEN) >=
		    MAXLINKNAMELEN) {
			die("link name too long");
		}
		/* check if the link is valid */
		if (dladm_name2info(handle, cbarg.slc_laname, NULL, NULL, NULL,
		    NULL) != DLADM_STATUS_OK) {
			die("link '%s' is not valid", cbarg.slc_laname);
		}
		link = B_TRUE;
	} else if (optind != argc) {
		die("Usage: %s\n", use);
	}

	if (s_arg) {
		do_show_agent_stats((link ? argv[optind] : NULL), fields_str,
		    parsable);
		return;
	}

	if (cbarg.slc_verbose) {
		ofmtflags |= OFMT_MULTILINE;
		fields = (cbarg.slc_neighbor ? lldp_remote_detailed_fields :
		    lldp_local_detailed_fields);
	} else {
		fields = (cbarg.slc_neighbor ? lldp_remote_summary_fields :
		    lldp_local_summary_fields);
	}
	if (parsable)
		ofmtflags |= OFMT_PARSABLE;

	oferr = ofmt_open(fields_str, fields, ofmtflags, 0, &ofmt);
	lldp_ofmt_check(oferr, parsable, ofmt);

	cbarg.slc_ofmt = ofmt;
	if (!link) {
		(void) dladm_walk_datalink_id(show_agent_info, handle,
		    &cbarg, DATALINK_CLASS_PHYS|DATALINK_CLASS_SIMNET, DL_ETHER,
		    DLADM_OPT_ACTIVE);
	} else {
		(void) show_agent_info(handle, DATALINK_INVALID_LINKID, &cbarg);
	}
	if (cbarg.slc_status != LLDP_STATUS_OK) {
		die_dlerr(cbarg.slc_status,
		    "failed to show information for %s",
		    cbarg.slc_neighbor ? "neighbors" : "local");
	}
	ofmt_close(ofmt);
}

static boolean_t
print_lldp_stats_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	show_lldp_cbarg_t	*cbarg = ofarg->ofmt_cbarg;
	lldp_stats_t		*stat = cbarg->slc_stats;

	switch (ofarg->ofmt_id) {
	case LLDP_S_AGENT:
		(void) snprintf(buf, bufsize, "%s", cbarg->slc_laname);
		break;
	case LLDP_S_FRAMES_INTOTAL:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_FramesInTotal);
		break;
	case LLDP_S_FRAMES_OUTTOTAL:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_FramesOutTotal);
		break;
	case LLDP_S_FRAMES_INERR:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_FramesInErrorsTotal);
		break;
	case LLDP_S_FRAMES_IDISCARD:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_FramesDiscardedTotal);
		break;
	case LLDP_S_FRAMES_LENERR:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_lldpduLengthErrors);
		break;
	case LLDP_S_TLV_DISCARD:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_TLVSDiscardedTotal);
		break;
	case LLDP_S_TLV_UNRECOGNIZED:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_TLVSUnrecognizedTotal);
		break;
	case LLDP_S_AGEOUT:
		(void) snprintf(buf, bufsize, "%u",
		    stat->ls_stats_AgeoutsTotal);
		break;
	default:
		die("invalid input");
	}
	return (B_TRUE);
}

static int
show_agent_stats(dladm_handle_t dh, datalink_id_t linkid, void *arg)
{
	show_lldp_cbarg_t	*cbarg = arg;
	lldp_status_t		status;
	lldp_stats_t		stat;

	if (linkid != DATALINK_INVALID_LINKID &&
	    dladm_datalink_id2info(dh, linkid, NULL, NULL, NULL,
	    cbarg->slc_laname, MAXLINKNAMELEN) != DLADM_STATUS_OK) {
		return (DLADM_WALK_CONTINUE);
	}

	status = lldp_get_agent_stats(cbarg->slc_laname, &stat, 0);
	if (status != LLDP_STATUS_OK && status != LLDP_STATUS_NOTFOUND &&
	    status != LLDP_STATUS_DISABLED) {
		cbarg->slc_status = status;
		return (DLADM_WALK_TERMINATE);
	} else if (status == LLDP_STATUS_NOTFOUND ||
	    status == LLDP_STATUS_DISABLED) {
		return (DLADM_WALK_CONTINUE);
	}

	cbarg->slc_stats = &stat;
	ofmt_print(cbarg->slc_ofmt, cbarg);

	return ((cbarg->slc_status != LLDP_STATUS_OK ? DLADM_WALK_TERMINATE :
	    DLADM_WALK_CONTINUE));
}

static void
do_show_agent_stats(const char *linkname, const char *fields_str,
    boolean_t parsable)
{
	ofmt_handle_t	ofmt;
	ofmt_status_t	oferr;
	uint_t		ofmtflags = OFMT_RIGHTJUST;
	show_lldp_cbarg_t cbarg;

	bzero(&cbarg, sizeof (cbarg));
	cbarg.slc_status = LLDP_STATUS_OK;
	if (linkname != NULL)
		(void) strlcpy(cbarg.slc_laname, linkname, MAXLINKNAMELEN);
	if (parsable)
		ofmtflags |= OFMT_PARSABLE;

	oferr = ofmt_open(fields_str, lldp_stats_fields, ofmtflags, 0, &ofmt);
	lldp_ofmt_check(oferr, parsable, ofmt);

	cbarg.slc_ofmt = ofmt;
	if (linkname == NULL) {
		(void) dladm_walk_datalink_id(show_agent_stats, handle,
		    &cbarg, DATALINK_CLASS_PHYS|DATALINK_CLASS_SIMNET, DL_ETHER,
		    DLADM_OPT_ACTIVE);
	} else {
		(void) show_agent_stats(handle, DATALINK_INVALID_LINKID,
		    &cbarg);
	}

	if (cbarg.slc_status != LLDP_STATUS_OK)
		die_dlerr(cbarg.slc_status, "failed to show lldp stats");
	ofmt_close(ofmt);
}

/* PRINTFLIKE1 */
static void
warn(const char *format, ...)
{
	va_list alist;

	format = gettext(format);
	(void) fprintf(stderr, "%s: warning: ", progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	(void) putc('\n', stderr);
}

/* PRINTFLIKE2 */
static void
warn_dlerr(lldp_status_t err, const char *format, ...)
{
	va_list alist;
	char	errmsg[LLDP_STRSIZE];

	format = gettext(format);
	(void) fprintf(stderr, gettext("%s: warning: "), progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);
	(void) fprintf(stderr, ": %s\n", lldp_status2str(err, errmsg));
}

/*
 * Also closes the dladm handle if it is not NULL.
 */
/* PRINTFLIKE2 */
static void
die_dlerr(lldp_status_t err, const char *format, ...)
{
	va_list alist;
	char	errmsg[LLDP_STRSIZE];

	format = gettext(format);
	(void) fprintf(stderr, "%s: ", progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);
	(void) fprintf(stderr, ": %s\n", lldp_status2str(err, errmsg));

	/* close dladm handle if it was opened */
	if (handle != NULL)
		dladm_close(handle);

	exit(EXIT_FAILURE);
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

	(void) putc('\n', stderr);

	/* close dladm handle if it was opened */
	if (handle != NULL)
		dladm_close(handle);

	exit(EXIT_FAILURE);
}

static void
die_optdup(int opt)
{
	die("the option -%c cannot be specified more than once", opt);
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
