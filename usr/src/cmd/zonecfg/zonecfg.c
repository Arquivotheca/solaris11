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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * zonecfg is a lex/yacc based command interpreter used to manage zone
 * configurations.  The lexer (see zonecfg_lex.l) builds up tokens, which
 * the grammar (see zonecfg_grammar.y) builds up into commands, some of
 * which takes resources and/or properties as arguments.  See the block
 * comments near the end of zonecfg_grammar.y for how the data structures
 * which keep track of these resources and properties are built up.
 *
 * The resource/property data structures are inserted into a command
 * structure (see zonecfg.h), which also keeps track of command names,
 * miscellaneous arguments, and function handlers.  The grammar selects
 * the appropriate function handler, each of which takes a pointer to a
 * command structure as its sole argument, and invokes it.  The grammar
 * itself is "entered" (a la the Matrix) by yyparse(), which is called
 * from read_input(), our main driving function.  That in turn is called
 * by one of do_interactive(), cmd_file() or one_command_at_a_time(), each
 * of which is called from main() depending on how the program was invoked.
 *
 * The rest of this module consists of the various function handlers and
 * their helper functions.  Some of these functions, particularly the
 * X_to_str() functions, which maps command, resource and property numbers
 * to strings, are used quite liberally, as doing so results in a better
 * program w/rt I18N, reducing the need for translation notes.
 */

#include <sys/mntent.h>
#include <sys/varargs.h>
#include <sys/sysmacros.h>

#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zone.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <locale.h>
#include <libintl.h>
#include <alloca.h>
#include <signal.h>
#include <wait.h>
#include <libtecla.h>
#include <libzfs.h>
#include <sys/brand.h>
#include <libbrand.h>
#include <sys/systeminfo.h>
#include <libdladm.h>
#include <libdllink.h>
#include <libdlvnic.h>
#include <libinetutil.h>
#include <pwd.h>
#include <inet/ip.h>
#include <sys/mac_flow.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fnmatch.h>

#include <libzonecfg.h>
#include "zonecfg.h"

#if !defined(TEXT_DOMAIN)		/* should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it wasn't */
#endif

#define	EXEC_PREFIX	"exec "
#define	EXEC_LEN	(strlen(EXEC_PREFIX))
#define	BLANK_TEMPLATE	SYS_ZONE_PREFIX	"blank"

struct help {
	uint_t	cmd_num;
	char	*cmd_name;
	uint_t	flags;
	char	*short_usage;
};

extern int yyparse(void);
extern int lex_lineno;

#define	MAX_LINE_LEN	1024
#define	MAX_CMD_HIST	1024
#define	MAX_CMD_LEN	1024

#define	ONE_MB		1048576

/*
 * Each SHELP_ should be a simple string.
 */

#define	SHELP_ADD	"add <resource-type>\n\t(global scope)\n" \
	"add <property-name> <property-value>\n\t(resource scope)"
#define	SHELP_CANCEL	"cancel"
#define	SHELP_CLEAR	"clear <property-name>"
#define	SHELP_COMMIT	"commit"
#define	SHELP_CREATE	"create [-F] [ -a <path> | -b | -t <template> ]"
#define	SHELP_DELETE	"delete [-F]"
#define	SHELP_END	"end"
#define	SHELP_EXIT	"exit [-F]"
#define	SHELP_EXPORT	"export [-f output-file]"
#define	SHELP_HELP	"help [commands] [syntax] [usage] [<command-name>]"
#define	SHELP_INFO	"info [<resource-type> [property-name=property-value]*]"
#define	SHELP_REMOVE	"remove [-F] <resource-type> " \
	"[ <property-name>=<property-value> ]*\n" \
	"\t(global scope)\n" \
	"remove <property-name> <property-value>\n" \
	"\t(resource scope)"
#define	SHELP_REVERT	"revert [-F]"
#define	SHELP_SELECT	"select <resource-type> { <property-name>=" \
	"<property-value> }"
#define	SHELP_SET	"set <property-name>=<property-value>"
#define	SHELP_VERIFY	"verify [-v]"
#define	SHELP_AI_REPORT	"auto-install-report -f config_file [-o outfile]"

static struct help helptab[] = {
	{ CMD_ADD,	"add",		HELP_RES_PROPS,	SHELP_ADD, },
	{ CMD_CANCEL,	"cancel",	0,		SHELP_CANCEL, },
	{ CMD_CLEAR,	"clear",	HELP_PROPS,	SHELP_CLEAR, },
	{ CMD_COMMIT,	"commit",	0,		SHELP_COMMIT, },
	{ CMD_CREATE,	"create",	0,		SHELP_CREATE, },
	{ CMD_DELETE,	"delete",	0,		SHELP_DELETE, },
	{ CMD_END,	"end",		0,		SHELP_END, },
	{ CMD_EXIT,	"exit",		0,		SHELP_EXIT, },
	{ CMD_EXPORT,	"export",	0,		SHELP_EXPORT, },
	{ CMD_HELP,	"help",		0,		SHELP_HELP },
	{ CMD_INFO,	"info",		HELP_RES_PROPS,	SHELP_INFO, },
	{ CMD_REMOVE,	"remove",	HELP_RES_PROPS,	SHELP_REMOVE, },
	{ CMD_REVERT,	"revert",	0,		SHELP_REVERT, },
	{ CMD_SELECT,	"select",	HELP_RES_PROPS,	SHELP_SELECT, },
	{ CMD_SET,	"set",		HELP_PROPS,	SHELP_SET, },
	{ CMD_VERIFY,	"verify",	0,		SHELP_VERIFY, },
	{ 0 },
};

#define	MAX_RT_STRLEN	16

/* These *must* match the order of the RT_ define's from zonecfg.h */
char *res_types[] = {
	"unknown",
	"zonename",
	"zonepath",
	"autoboot",
	"pool",
	"fs",
	"net",
	"device",
	"rctl",
	"attr",
	"dataset",
	"limitpriv",
	"bootargs",
	"brand",
	"dedicated-cpu",
	"capped-memory",
	ALIAS_MAXLWPS,
	ALIAS_MAXSHMMEM,
	ALIAS_MAXSHMIDS,
	ALIAS_MAXMSGIDS,
	ALIAS_MAXSEMIDS,
	ALIAS_SHARES,
	"scheduling-class",
	"ip-type",
	"capped-cpu",
	"hostid",
	"admin",
	"fs-allowed",
	ALIAS_MAXPROCS,
	"anet",
	"file-mac-profile",
	NULL
};

/* These *must* match the order of the PT_ define's from zonecfg.h */
char *prop_types[] = {
	"unknown",
	"zonename",
	"zonepath",
	"autoboot",
	"pool",
	"dir",
	"special",
	"type",
	"options",
	"address",
	"physical",
	"name",
	"value",
	"match",
	"priv",
	"limit",
	"action",
	"raw",
	"limitpriv",
	"bootargs",
	"brand",
	"ncpus",
	"importance",
	"swap",
	"locked",
	ALIAS_SHARES,
	ALIAS_MAXLWPS,
	ALIAS_MAXSHMMEM,
	ALIAS_MAXSHMIDS,
	ALIAS_MAXMSGIDS,
	ALIAS_MAXSEMIDS,
	ALIAS_MAXLOCKEDMEM,
	ALIAS_MAXSWAP,
	"scheduling-class",
	"ip-type",
	"defrouter",
	"hostid",
	"user",
	"auths",
	"fs-allowed",
	ALIAS_MAXPROCS,
	ZONE_ALLOW_PARTITION,
	ZONE_ALLOW_RAW_IO,
	"allowed-address",
	"linkname",
	"lower-link",
	"allowed-dhcp-cids",
	"mac-address",
	"auto-mac-address",
	"mac-prefix",
	"mac-slot",
	"maxbw",
	"priority",
	"vlan-id",
	"rxrings",
	"txrings",
	"mtu",
	"link-protection",
	"file-mac-profile",
	"configure-allowed-address",
	"alias",
	"rxfanout",
	NULL
};

/* These *must* match the order of the PROP_VAL_ define's from zonecfg.h */
static char *prop_val_types[] = {
	"simple",
	"complex",
	"list",
};

/*
 * The various _cmds[] lists below are for command tab-completion.
 */

/*
 * remove has a space afterwards because it has qualifiers; the other commands
 * that have qualifiers (add, select, etc.) don't need a space here because
 * they have their own _cmds[] lists below.
 */
static const char *global_scope_cmds[] = {
	"add",
	"clear",
	"commit",
	"create",
	"delete",
	"exit",
	"export",
	"help",
	"info",
	"remove ",
	"revert",
	"select",
	"set",
	"verify",
	NULL
};

static const char *add_cmds[] = {
	"add fs",
	"add net",
	"add anet",
	"add device",
	"add rctl",
	"add attr",
	"add dataset",
	"add dedicated-cpu",
	"add capped-cpu",
	"add capped-memory",
	"add admin",
	NULL
};

static const char *clear_cmds[] = {
	"clear autoboot",
	"clear file-mac-profile",
	"clear pool",
	"clear limitpriv",
	"clear bootargs",
	"clear scheduling-class",
	"clear ip-type",
	"clear " ALIAS_MAXLWPS,
	"clear " ALIAS_MAXSHMMEM,
	"clear " ALIAS_MAXSHMIDS,
	"clear " ALIAS_MAXMSGIDS,
	"clear " ALIAS_MAXSEMIDS,
	"clear " ALIAS_SHARES,
	"clear " ALIAS_MAXPROCS,
	NULL
};

static const char *remove_cmds[] = {
	"remove fs ",
	"remove net ",
	"remove anet ",
	"remove device ",
	"remove rctl ",
	"remove attr ",
	"remove dataset ",
	"remove dedicated-cpu ",
	"remove capped-cpu ",
	"remove capped-memory ",
	"remove admin ",
	NULL
};

static const char *select_cmds[] = {
	"select fs ",
	"select net ",
	"select anet ",
	"select device ",
	"select rctl ",
	"select attr ",
	"select dataset ",
	"select dedicated-cpu",
	"select capped-cpu",
	"select capped-memory",
	"select admin",
	NULL
};

static const char *set_cmds[] = {
	"set zonename=",
	"set zonepath=",
	"set brand=",
	"set autoboot=",
	"set file-mac-profile=",
	"set pool=",
	"set limitpriv=",
	"set bootargs=",
	"set scheduling-class=",
	"set ip-type=",
	"set " ALIAS_MAXLWPS "=",
	"set " ALIAS_MAXSHMMEM "=",
	"set " ALIAS_MAXSHMIDS "=",
	"set " ALIAS_MAXMSGIDS "=",
	"set " ALIAS_MAXSEMIDS "=",
	"set " ALIAS_SHARES "=",
	"set hostid=",
	"set fs-allowed=",
	"set " ALIAS_MAXPROCS "=",
	NULL
};

static const char *info_cmds[] = {
	"info fs ",
	"info net ",
	"info anet ",
	"info device ",
	"info rctl ",
	"info attr ",
	"info dataset ",
	"info capped-memory",
	"info dedicated-cpu",
	"info capped-cpu",
	"info zonename",
	"info zonepath",
	"info autoboot",
	"info pool",
	"info limitpriv",
	"info bootargs",
	"info brand",
	"info scheduling-class",
	"info ip-type",
	"info max-lwps",
	"info max-shm-memory",
	"info max-shm-ids",
	"info max-msg-ids",
	"info max-sem-ids",
	"info cpu-shares",
	"info hostid",
	"info admin",
	"info fs-allowed",
	"info max-processes",
	"info file-mac-profile",
	NULL
};

static const char *fs_res_scope_cmds[] = {
	"add options ",
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"remove options ",
	"set dir=",
	"set raw=",
	"set special=",
	"set type=",
	"clear raw",
	NULL
};

static const char *net_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set address=",
	"set allowed-address=",
	"set configure-allowed-address=",
	"set physical=",
	"set defrouter=",
	NULL
};

static const char *anet_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set linkname=",
	"set lower-link=",
	"set allowed-address=",
	"set configure-allowed-address=",
	"set defrouter=",
	"set allowed-dhcp-cids=",
	"set link-protection=",
	"set mac-address=",
	"set mac-prefix=",
	"set mac-slot=",
	"set maxbw=",
	"set priority=",
	"set vlan-id=",
	"set rxrings=",
	"set txrings=",
	"set mtu=",
	"set rxfanout=",
	NULL
};

static const char *device_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set match=",
	"set " ZONE_ALLOW_PARTITION "=",
	"set " ZONE_ALLOW_RAW_IO "=",
	NULL
};

static const char *attr_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set name=",
	"set type=",
	"set value=",
	NULL
};

static const char *rctl_res_scope_cmds[] = {
	"add value ",
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"remove value ",
	"set name=",
	NULL
};

static const char *dataset_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set name=",
	"set alias=",
	NULL
};

static const char *pset_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set ncpus=",
	"set importance=",
	"clear importance",
	NULL
};

static const char *pcap_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set ncpus=",
	NULL
};

static const char *mcap_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set physical=",
	"set swap=",
	"set locked=",
	"clear physical",
	"clear swap",
	"clear locked",
	NULL
};

static const char *admin_res_scope_cmds[] = {
	"cancel",
	"end",
	"exit",
	"help",
	"info",
	"set user=",
	"set auths=",
	NULL
};

/* Global variables */

/* set early in main(), never modified thereafter, used all over the place */
static char *execname;

/* set in main(), used all over the place */
static zone_dochandle_t handle;
static dladm_handle_t dld_handle = NULL;

/* used all over the place */
static char zone[ZONENAME_MAX];
static char revert_zone[ZONENAME_MAX];

/* global brand operations */
static brand_handle_t brand;

/* set in modifying functions, checked in read_input() */
static boolean_t need_to_commit = B_FALSE;
boolean_t saw_error;

/* set in yacc parser, checked in read_input() */
boolean_t newline_terminated;

/* set in main(), checked in lex error handler */
boolean_t cmd_file_mode;

/* set in exit_func(), checked in read_input() */
static boolean_t time_to_exit = B_FALSE, force_exit = B_FALSE;

/* used in short_usage() and zerr() */
static char *cmd_file_name = NULL;

/* set and used in ai_report_func() */
static char *input_file_name = NULL;

/* checked in read_input() and other places */
static boolean_t ok_to_prompt = B_FALSE;

/* set and checked in initialize() */
static boolean_t got_handle = B_FALSE;

/* initialized in do_interactive(), checked in initialize() */
static boolean_t interactive_mode;

/* set if configuring the global zone */
static boolean_t global_zone = B_FALSE;

/* set in main(), checked in multiple places */
static boolean_t read_only_mode;

/* was ip-type explicitly set? */
static boolean_t ip_type_set = B_FALSE;

/* scope is outer/global or inner/resource */
static boolean_t global_scope = B_TRUE;
static int resource_scope;	/* should be in the RT_ list from zonecfg.h */
static int end_op = -1;		/* operation on end is either add or modify */

int num_prop_vals;		/* for grammar */

/*
 * These are for keeping track of resources as they are specified as part of
 * the multi-step process.  They should be initialized by add_resource() or
 * select_func() and filled in by add_property() or set_func().
 */
static struct zone_fstab	old_fstab, in_progress_fstab;
static struct zone_nettab	old_nettab, in_progress_nettab, *ipn;
static struct zone_anettab	old_anettab, in_progress_anettab, *ipa;
static struct zone_devtab	old_devtab, in_progress_devtab;
static struct zone_rctltab	old_rctltab, in_progress_rctltab;
static struct zone_attrtab	old_attrtab, in_progress_attrtab;
static struct zone_dstab	old_dstab, in_progress_dstab;
static struct zone_psettab	old_psettab, in_progress_psettab;
static struct zone_mcaptab	old_mcaptab, in_progress_mcaptab;
static struct zone_admintab	old_admintab, in_progress_admintab;

static GetLine *gl;	/* The gl_get_line() resource object */

static void bytes_to_units(char *str, char *buf, int bufsize);
static int str_to_bool(int prop, const char *str, boolean_t *out);
static boolean_t warn_device_conflict_with_dsalias(zone_dochandle_t handle,
    struct zone_devtab *devtab);
int ai_report_func(int argc, char *argv[]);
#define	AI_REPORT	"auto-install-report"

/* Functions begin here */

static boolean_t
initial_match(const char *line1, const char *line2, int word_end)
{
	if (word_end <= 0)
		return (B_TRUE);
	return (strncmp(line1, line2, word_end) == 0);
}

static int
add_stuff(WordCompletion *cpl, const char *line1, const char **list,
    int word_end)
{
	int i, err;

	for (i = 0; list[i] != NULL; i++) {
		if (initial_match(line1, list[i], word_end)) {
			err = cpl_add_completion(cpl, line1, 0, word_end,
			    list[i] + word_end, "", "");
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

static
/* ARGSUSED */
CPL_MATCH_FN(cmd_cpl_fn)
{
	if (global_scope) {
		/*
		 * The MAX/MIN tests below are to make sure we have at least
		 * enough characters to distinguish from other prefixes (MAX)
		 * but only check MIN(what we have, what we're checking).
		 */
		if (strncmp(line, "add ", MAX(MIN(word_end, 4), 1)) == 0)
			return (add_stuff(cpl, line, add_cmds, word_end));
		if (strncmp(line, "clear ", MAX(MIN(word_end, 6), 2)) == 0)
			return (add_stuff(cpl, line, clear_cmds, word_end));
		if (strncmp(line, "select ", MAX(MIN(word_end, 7), 3)) == 0)
			return (add_stuff(cpl, line, select_cmds, word_end));
		if (strncmp(line, "set ", MAX(MIN(word_end, 4), 3)) == 0)
			return (add_stuff(cpl, line, set_cmds, word_end));
		if (strncmp(line, "remove ", MAX(MIN(word_end, 7), 1)) == 0)
			return (add_stuff(cpl, line, remove_cmds, word_end));
		if (strncmp(line, "info ", MAX(MIN(word_end, 5), 1)) == 0)
			return (add_stuff(cpl, line, info_cmds, word_end));
		return (add_stuff(cpl, line, global_scope_cmds, word_end));
	}
	switch (resource_scope) {
	case RT_FS:
		return (add_stuff(cpl, line, fs_res_scope_cmds, word_end));
	case RT_NET:
		return (add_stuff(cpl, line, net_res_scope_cmds, word_end));
	case RT_ANET:
		return (add_stuff(cpl, line, anet_res_scope_cmds, word_end));
	case RT_DEVICE:
		return (add_stuff(cpl, line, device_res_scope_cmds, word_end));
	case RT_RCTL:
		return (add_stuff(cpl, line, rctl_res_scope_cmds, word_end));
	case RT_ATTR:
		return (add_stuff(cpl, line, attr_res_scope_cmds, word_end));
	case RT_DATASET:
		return (add_stuff(cpl, line, dataset_res_scope_cmds, word_end));
	case RT_DCPU:
		return (add_stuff(cpl, line, pset_res_scope_cmds, word_end));
	case RT_PCAP:
		return (add_stuff(cpl, line, pcap_res_scope_cmds, word_end));
	case RT_MCAP:
		return (add_stuff(cpl, line, mcap_res_scope_cmds, word_end));
	case RT_ADMIN:
		return (add_stuff(cpl, line, admin_res_scope_cmds, word_end));
	}
	return (0);
}

/*
 * For the main CMD_func() functions below, several of them call getopt()
 * then check optind against argc to make sure an extra parameter was not
 * passed in.  The reason this is not caught in the grammar is that the
 * grammar just checks for a miscellaneous TOKEN, which is *expected* to
 * be "-F" (for example), but could be anything.  So (for example) this
 * check will prevent "create bogus".
 */

cmd_t *
alloc_cmd(void)
{
	return (calloc(1, sizeof (cmd_t)));
}

void
free_cmd(cmd_t *cmd)
{
	int i;

	for (i = 0; i < MAX_EQ_PROP_PAIRS; i++)
		if (cmd->cmd_property_ptr[i] != NULL) {
			property_value_ptr_t pp = cmd->cmd_property_ptr[i];

			switch (pp->pv_type) {
			case PROP_VAL_SIMPLE:
				free(pp->pv_simple);
				break;
			case PROP_VAL_COMPLEX:
				free_complex(pp->pv_complex);
				break;
			case PROP_VAL_LIST:
				free_list(pp->pv_list);
				break;
			}
		}
	for (i = 0; i < cmd->cmd_argc; i++)
		free(cmd->cmd_argv[i]);
	free(cmd);
}

complex_property_ptr_t
alloc_complex(void)
{
	return (calloc(1, sizeof (complex_property_t)));
}

void
free_complex(complex_property_ptr_t complex)
{
	if (complex == NULL)
		return;
	free_complex(complex->cp_next);
	if (complex->cp_value != NULL)
		free(complex->cp_value);
	free(complex);
}

list_property_ptr_t
alloc_list(void)
{
	return (calloc(1, sizeof (list_property_t)));
}

void
free_list(list_property_ptr_t list)
{
	if (list == NULL)
		return;
	if (list->lp_simple != NULL)
		free(list->lp_simple);
	free_complex(list->lp_complex);
	free_list(list->lp_next);
	free(list);
}

void
free_outer_list(list_property_ptr_t list)
{
	if (list == NULL)
		return;
	free_outer_list(list->lp_next);
	free(list);
}

static struct zone_rctlvaltab *
alloc_rctlvaltab(void)
{
	return (calloc(1, sizeof (struct zone_rctlvaltab)));
}

static char *
rt_to_str(int res_type)
{
	assert(res_type >= RT_MIN && res_type <= RT_MAX);
	return (res_types[res_type]);
}

static char *
pt_to_str(int prop_type)
{
	assert(prop_type >= PT_MIN && prop_type <= PT_MAX);
	return (prop_types[prop_type]);
}

static char *
pvt_to_str(int pv_type)
{
	assert(pv_type >= PROP_VAL_MIN && pv_type <= PROP_VAL_MAX);
	return (prop_val_types[pv_type]);
}

static char *
cmd_to_str(int cmd_num)
{
	assert(cmd_num >= CMD_MIN && cmd_num <= CMD_MAX);
	return (helptab[cmd_num].cmd_name);
}

/* PRINTFLIKE1 */
static void
zerr(const char *fmt, ...)
{
	va_list alist;
	static int last_lineno;

	/* lex_lineno has already been incremented in the lexer; compensate */
	if (cmd_file_mode && lex_lineno > last_lineno) {
		if (strcmp(cmd_file_name, "-") == 0)
			(void) fprintf(stderr, gettext("On line %d:\n"),
			    lex_lineno - 1);
		else
			(void) fprintf(stderr, gettext("On line %d of %s:\n"),
			    lex_lineno - 1, cmd_file_name);
		last_lineno = lex_lineno;
	}
	va_start(alist, fmt);
	(void) vfprintf(stderr, fmt, alist);
	(void) fprintf(stderr, "\n");
	va_end(alist);
}

/*
 * This is a separate function rather than a set of define's because of the
 * gettext() wrapping.
 */

/*
 * TRANSLATION_NOTE
 * Each string below should have \t follow \n whenever needed; the
 * initial \t and the terminal \n will be provided by the calling function.
 */

static char *
long_help(int cmd_num)
{
	static char line[1024];	/* arbitrary large amount */

	assert(cmd_num >= CMD_MIN && cmd_num <= CMD_MAX);
	switch (cmd_num) {
		case CMD_HELP:
			return (gettext("Prints help message."));
		case CMD_CREATE:
			(void) snprintf(line, sizeof (line),
			    gettext("Creates a configuration for the "
			    "specified zone.  %s should be\n\tused to "
			    "begin configuring a new zone.  If overwriting an "
			    "existing\n\tconfiguration, the -F flag can be "
			    "used to force the action.  If\n\t-t template is "
			    "given, creates a configuration identical to the\n"
			    "\tspecified template, except that the zone name "
			    "is changed from\n\ttemplate to zonename.  '%s -a' "
			    "creates a configuration from a\n\tdetached "
			    "zonepath.  '%s -b' results in a blank "
			    "configuration.\n\t'%s' with no arguments applies "
			    "the Sun default settings."),
			    cmd_to_str(CMD_CREATE), cmd_to_str(CMD_CREATE),
			    cmd_to_str(CMD_CREATE), cmd_to_str(CMD_CREATE));
			return (line);
		case CMD_EXIT:
			return (gettext("Exits the program.  The -F flag can "
			    "be used to force the action."));
		case CMD_EXPORT:
			return (gettext("Prints configuration to standard "
			    "output, or to output-file if\n\tspecified, in "
			    "a form suitable for use in a command-file."));
		case CMD_ADD:
			return (gettext("Add specified resource to "
			    "configuration."));
		case CMD_DELETE:
			return (gettext("Deletes the specified zone.  The -F "
			    "flag can be used to force the\n\taction."));
		case CMD_REMOVE:
			return (gettext("Remove specified resource from "
			    "configuration.  The -F flag can be used\n\tto "
			    "force the action."));
		case CMD_SELECT:
			(void) snprintf(line, sizeof (line),
			    gettext("Selects a resource to modify.  "
			    "Resource modification is completed\n\twith the "
			    "command \"%s\".  The property name/value pairs "
			    "must uniquely\n\tidentify a resource.  Note that "
			    "the curly braces ('{', '}') mean one\n\tor more "
			    "of whatever is between them."),
			    cmd_to_str(CMD_END));
			return (line);
		case CMD_SET:
			return (gettext("Sets property values."));
		case CMD_CLEAR:
			return (gettext("Clears property values."));
		case CMD_INFO:
			return (gettext("Displays information about the "
			    "current configuration.  If resource\n\ttype is "
			    "specified, displays only information about "
			    "resources of\n\tthe relevant type.  If resource "
			    "id is specified, displays only\n\tinformation "
			    "about that resource."));
		case CMD_VERIFY:
			return (gettext("Verifies current configuration "
			    "for correctness (some resource types\n\thave "
			    "required properties).  The -v flag can be used "
			    "to warn about\n\tconfigurations that, while "
			    "correct, may be problematic in rare cases."));
		case CMD_COMMIT:
			(void) snprintf(line, sizeof (line),
			    gettext("Commits current configuration.  "
			    "Configuration must be committed to\n\tbe used by "
			    "%s.  Until the configuration is committed, "
			    "changes \n\tcan be removed with the %s "
			    "command.  This operation is\n\tattempted "
			    "automatically upon completion of a %s "
			    "session."), "zoneadm", cmd_to_str(CMD_REVERT),
			    "zonecfg");
			return (line);
		case CMD_REVERT:
			return (gettext("Reverts configuration back to the "
			    "last committed state.  The -F flag\n\tcan be "
			    "used to force the action."));
		case CMD_CANCEL:
			return (gettext("Cancels resource/property "
			    "specification."));
		case CMD_END:
			return (gettext("Ends resource/property "
			    "specification."));
	}
	/* NOTREACHED */
	return (NULL);
}

/*
 * Called with verbose TRUE when help is explicitly requested, FALSE for
 * unexpected errors.
 */

void
usage(boolean_t verbose, uint_t flags)
{
	FILE *fp = verbose ? stdout : stderr;
	int i;

	if (flags & HELP_META) {
		(void) fprintf(fp, gettext("More help is available for the "
		    "following:\n"));
		(void) fprintf(fp, "\n\tcommands ('%s commands')\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\tsyntax ('%s syntax')\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\tusage ('%s usage')\n\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, gettext("You may also obtain help on any "
		    "command by typing '%s <command-name>.'\n"),
		    cmd_to_str(CMD_HELP));
	}
	if (flags & HELP_RES_SCOPE) {
		switch (resource_scope) {
		case RT_FS:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to configure a file-system.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_DIR), gettext("<path>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_SPECIAL), gettext("<path>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_RAW), gettext("<raw-device>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_TYPE), gettext("<file-system type>"));
			(void) fprintf(fp, "\t%s %s %s\n", cmd_to_str(CMD_ADD),
			    pt_to_str(PT_OPTIONS),
			    gettext("<file-system options>"));
			(void) fprintf(fp, "\t%s %s %s\n",
			    cmd_to_str(CMD_REMOVE), pt_to_str(PT_OPTIONS),
			    gettext("<file-system options>"));
			(void) fprintf(fp, gettext("Consult the file-system "
			    "specific manual page, such as mount_ufs(1M), "
			    "for\ndetails about file-system options.  Note "
			    "that any file-system options with an\nembedded "
			    "'=' character must be enclosed in double quotes, "
			    /*CSTYLED*/
			    "such as \"%s=5\".\n"), MNTOPT_RETRY);
			break;
		case RT_NET:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to configure a network interface.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_ADDRESS), gettext("<IP-address>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_ALLOWED_ADDRESS),
			    gettext("<IP-address>,..."));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_CONFIGURE_ALLOWED_ADDRESS),
			    gettext("true|false"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_PHYSICAL), gettext("<interface>"));
			(void) fprintf(fp, gettext("See ifconfig(1M) for "
			    "details of the <interface> string.\n"));
			(void) fprintf(fp, gettext("%s %s is valid "
			    "if the %s property is set to %s, otherwise it "
			    "must not be set.\n"),
			    cmd_to_str(CMD_SET), pt_to_str(PT_ADDRESS),
			    pt_to_str(PT_IPTYPE), gettext("shared"));
			(void) fprintf(fp, gettext("%s %s is valid "
			    "if the %s property is set to %s, otherwise it "
			    "must not be set.\n"),
			    cmd_to_str(CMD_SET),
			    pt_to_str(PT_CONFIGURE_ALLOWED_ADDRESS),
			    pt_to_str(PT_IPTYPE), gettext("exclusive"));
			(void) fprintf(fp, gettext("%s %s is valid "
			    "if the %s property is set to %s, otherwise it "
			    "must not be set.\n"),
			    cmd_to_str(CMD_SET),
			    pt_to_str(PT_ALLOWED_ADDRESS),
			    pt_to_str(PT_IPTYPE), gettext("exclusive"));
			(void) fprintf(fp, gettext("\t%s %s=%s\n%s %s "
			    "is valid if the %s or %s property is set, "
			    "otherwise it must not be set\n"),
			    cmd_to_str(CMD_SET),
			    pt_to_str(PT_DEFROUTER), gettext("<IP-address>"),
			    cmd_to_str(CMD_SET), pt_to_str(PT_DEFROUTER),
			    gettext(pt_to_str(PT_ADDRESS)),
			    gettext(pt_to_str(PT_ALLOWED_ADDRESS)));
			break;
		case RT_ANET:
			(void) fprintf(fp, gettext("The '%s' resource scope "
			    "is used to configure a virtual datalink that "
			    "will automatically be added to the zone.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_LINKNAME), gettext("<datalink name>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_LOWER_LINK),
			    gettext("<datalink name>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_ALLOWED_ADDRESS),
			    gettext("<IP-address>,..."));
			(void) fprintf(fp, gettext("\t%s %s=%s\n%s %s "
			    "is valid if the %s property is set, "
			    "otherwise it must not be set\n"),
			    cmd_to_str(CMD_SET),
			    pt_to_str(PT_DEFROUTER),
			    gettext("<IP-address>,..."),
			    cmd_to_str(CMD_SET), pt_to_str(PT_DEFROUTER),
			    gettext(pt_to_str(PT_ALLOWED_ADDRESS)));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_ALLOWED_DHCP_CIDS),
			    gettext("<client-ID or DUID>,..."));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_LINK_PROTECTION),
			    gettext("<comma-separated list of protections>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_MAC_ADDRESS),
			    gettext("<mac-address>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_MAC_PREFIX),
			    gettext("<mac-prefix>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_MAC_SLOT),
			    gettext("<mac-slot>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_VLAN_ID),
			    gettext("<vlan-id>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_PRIORITY),
			    gettext("<high|medium|low>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_RXRINGS),
			    gettext("<Number of receive rings>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_TXRINGS),
			    gettext("<Number of transmit rings>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_MTU),
			    gettext("<mtu>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_MAXBW),
			    gettext("<full duplex bandwidth of the link>"));
			break;
		case RT_DEVICE:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to configure a device node.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_MATCH), gettext("<device-path>"));
			(void) fprintf(fp, "\t%s %s=boolean\n",
			    cmd_to_str(CMD_SET), pt_to_str(PT_ALLOW_PARTITION));
			(void) fprintf(fp, "\t%s %s=boolean\n",
			    cmd_to_str(CMD_SET), pt_to_str(PT_ALLOW_RAW_IO));
			break;
		case RT_RCTL:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to configure a resource control.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NAME), gettext("<string>"));
			(void) fprintf(fp, "\t%s %s (%s=%s,%s=%s,%s=%s)\n",
			    cmd_to_str(CMD_ADD), pt_to_str(PT_VALUE),
			    pt_to_str(PT_PRIV), gettext("<priv-value>"),
			    pt_to_str(PT_LIMIT), gettext("<number>"),
			    pt_to_str(PT_ACTION), gettext("<action-value>"));
			(void) fprintf(fp, "\t%s %s (%s=%s,%s=%s,%s=%s)\n",
			    cmd_to_str(CMD_REMOVE), pt_to_str(PT_VALUE),
			    pt_to_str(PT_PRIV), gettext("<priv-value>"),
			    pt_to_str(PT_LIMIT), gettext("<number>"),
			    pt_to_str(PT_ACTION), gettext("<action-value>"));
			(void) fprintf(fp, "%s\n\t%s := privileged\n"
			    "\t%s := none | deny\n", gettext("Where"),
			    gettext("<priv-value>"), gettext("<action-value>"));
			break;
		case RT_ATTR:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to configure a generic attribute.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NAME), gettext("<name>"));
			(void) fprintf(fp, "\t%s %s=boolean\n",
			    cmd_to_str(CMD_SET), pt_to_str(PT_TYPE));
			(void) fprintf(fp, "\t%s %s=true | false\n",
			    cmd_to_str(CMD_SET), pt_to_str(PT_VALUE));
			(void) fprintf(fp, gettext("or\n"));
			(void) fprintf(fp, "\t%s %s=int\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_TYPE));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_VALUE), gettext("<integer>"));
			(void) fprintf(fp, gettext("or\n"));
			(void) fprintf(fp, "\t%s %s=string\n",
			    cmd_to_str(CMD_SET), pt_to_str(PT_TYPE));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_VALUE), gettext("<string>"));
			(void) fprintf(fp, gettext("or\n"));
			(void) fprintf(fp, "\t%s %s=uint\n",
			    cmd_to_str(CMD_SET), pt_to_str(PT_TYPE));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_VALUE), gettext("<unsigned integer>"));
			break;
		case RT_DATASET:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to export ZFS datasets.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NAME), gettext("<name>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_ALIAS), gettext("<alias>"));
			break;
		case RT_DCPU:
			(void) fprintf(fp, gettext("The '%s' resource scope "
			    "configures the 'pools' facility to dedicate\na "
			    "subset of the system's processors to this zone "
			    "while it is running.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NCPUS),
			    gettext("<unsigned integer | range>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_IMPORTANCE),
			    gettext("<unsigned integer>"));
			break;
		case RT_PCAP:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to set an upper limit (a cap) on the\n"
			    "percentage of CPU that can be used by this zone.  "
			    "A '%s' value of 1\ncorresponds to one cpu.  The "
			    "value can be set higher than 1, up to the total\n"
			    "number of CPUs on the system.  The value can "
			    "also be less than 1,\nrepresenting a fraction of "
			    "a cpu.\n"),
			    rt_to_str(resource_scope), pt_to_str(PT_NCPUS));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NCPUS), gettext("<unsigned decimal>"));
			break;
		case RT_MCAP:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to set an upper limit (a cap) on the\n"
			    "amount of physical memory, swap space and locked "
			    "memory that can be used by\nthis zone.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_PHYSICAL),
			    gettext("<qualified unsigned decimal>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_SWAP),
			    gettext("<qualified unsigned decimal>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_LOCKED),
			    gettext("<qualified unsigned decimal>"));
			break;
		case RT_ADMIN:
			(void) fprintf(fp, gettext("The '%s' resource scope is "
			    "used to delegate specific zone management\n"
			    "rights to users and roles. These rights are "
			    "only applicable to this zone.\n"),
			    rt_to_str(resource_scope));
			(void) fprintf(fp, gettext("Valid commands:\n"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_USER),
			    gettext("<single user or role name>"));
			(void) fprintf(fp, "\t%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_AUTHS),
			    gettext("<comma separated list>"));
			break;
		}
		(void) fprintf(fp, gettext("And from any resource scope, you "
		    "can:\n"));
		(void) fprintf(fp, "\t%s\t%s\n", cmd_to_str(CMD_END),
		    gettext("(to conclude this operation)"));
		(void) fprintf(fp, "\t%s\t%s\n", cmd_to_str(CMD_CANCEL),
		    gettext("(to cancel this operation)"));
		(void) fprintf(fp, "\t%s\t%s\n", cmd_to_str(CMD_EXIT),
		    gettext("(to exit the zonecfg utility)"));
	}
	if (flags & HELP_USAGE) {
		(void) fprintf(fp, "%s:\t%s %s\n", gettext("usage"),
		    execname, cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\t%s -z <zone>\t\t\t(%s)\n",
		    execname, gettext("interactive"));
		(void) fprintf(fp, "\t%s -z <zone> <command>\n", execname);
		(void) fprintf(fp, "\t%s -z <zone> -f <command-file>\n",
		    execname);
	}
	if (flags & HELP_SUBCMDS) {
		(void) fprintf(fp, "%s:\n\n", gettext("Commands"));
		for (i = 0; i <= CMD_MAX; i++) {
			(void) fprintf(fp, "%s\n", helptab[i].short_usage);
			if (verbose)
				(void) fprintf(fp, "\t%s\n\n", long_help(i));
		}
	}
	if (flags & HELP_SYNTAX) {
		if (!verbose)
			(void) fprintf(fp, "\n");
		(void) fprintf(fp, "<zone> := [A-Za-z0-9][A-Za-z0-9_.-]*\n");
		(void) fprintf(fp, gettext("\t(except the reserved words "
		    "'%s' and anything starting with '%s')\n"), "global",
		    SYS_ZONE_PREFIX);
		(void) fprintf(fp,
		    gettext("\tName must be less than %d characters.\n"),
		    ZONENAME_MAX);
		if (verbose)
			(void) fprintf(fp, "\n");
	}
	if (flags & HELP_NETADDR) {
		(void) fprintf(fp, gettext("\n<net-addr> :="));
		(void) fprintf(fp,
		    gettext("\t<IPv4-address>[/<IPv4-prefix-length>] |\n"));
		(void) fprintf(fp,
		    gettext("\t\t<IPv6-address>/<IPv6-prefix-length> |\n"));
		(void) fprintf(fp,
		    gettext("\t\t<hostname>[/<IPv4-prefix-length>]\n"));
		(void) fprintf(fp, gettext("See inet(3SOCKET) for IPv4 and "
		    "IPv6 address syntax.\n"));
		(void) fprintf(fp, gettext("<IPv4-prefix-length> := [0-32]\n"));
		(void) fprintf(fp,
		    gettext("<IPv6-prefix-length> := [0-128]\n"));
		(void) fprintf(fp,
		    gettext("<hostname> := [A-Za-z0-9][A-Za-z0-9-.]*\n"));
	}
	if (flags & HELP_RESOURCES) {
		(void) fprintf(fp, "<%s> := %s | %s | %s | %s | %s | %s |\n\t"
		    "%s | %s | %s | %s | %s\n\n",
		    gettext("resource type"),
		    rt_to_str(RT_FS), rt_to_str(RT_ANET),
		    rt_to_str(RT_NET), rt_to_str(RT_DEVICE),
		    rt_to_str(RT_RCTL), rt_to_str(RT_ATTR),
		    rt_to_str(RT_DATASET), rt_to_str(RT_DCPU),
		    rt_to_str(RT_PCAP), rt_to_str(RT_MCAP),
		    rt_to_str(RT_ADMIN));
	}
	if (flags & HELP_PROPS) {
		(void) fprintf(fp, gettext("For resource type ... there are "
		    "property types ...:\n"));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_ZONENAME));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_ZONEPATH));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_BRAND));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_AUTOBOOT));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_BOOTARGS));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAC_PROFILE));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_POOL));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_LIMITPRIV));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_SCHED));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_IPTYPE));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_HOSTID));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_FS_ALLOWED));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAXLWPS));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAXPROCS));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAXSHMMEM));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAXSHMIDS));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAXMSGIDS));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_MAXSEMIDS));
		(void) fprintf(fp, "\t%s\t%s\n", gettext("(global)"),
		    pt_to_str(PT_SHARES));
		(void) fprintf(fp, "\t%s\t\t%s, %s, %s, %s, %s\n",
		    rt_to_str(RT_FS), pt_to_str(PT_DIR),
		    pt_to_str(PT_SPECIAL), pt_to_str(PT_RAW),
		    pt_to_str(PT_TYPE), pt_to_str(PT_OPTIONS));
		(void) fprintf(fp, "\t%s\t\t%s, %s, %s, %s, %s\n",
		    rt_to_str(RT_NET),
		    pt_to_str(PT_ADDRESS), pt_to_str(PT_ALLOWED_ADDRESS),
		    pt_to_str(PT_PHYSICAL), pt_to_str(PT_DEFROUTER),
		    pt_to_str(PT_CONFIGURE_ALLOWED_ADDRESS));
		(void) fprintf(fp, "\t%s\t\t%s, %s, %s, %s, %s, %s, %s, %s, "
		    "%s, %s, %s, %s, %s, %s, %s, %s\n", rt_to_str(RT_ANET),
		    pt_to_str(PT_LINKNAME), pt_to_str(PT_LOWER_LINK),
		    pt_to_str(PT_ALLOWED_ADDRESS),
		    pt_to_str(PT_CONFIGURE_ALLOWED_ADDRESS),
		    pt_to_str(PT_DEFROUTER),
		    pt_to_str(PT_ALLOWED_DHCP_CIDS),
		    pt_to_str(PT_LINK_PROTECTION),
		    pt_to_str(PT_MAC_ADDRESS), pt_to_str(PT_MAC_PREFIX),
		    pt_to_str(PT_MAC_SLOT), pt_to_str(PT_VLAN_ID),
		    pt_to_str(PT_PRIORITY), pt_to_str(PT_RXRINGS),
		    pt_to_str(PT_TXRINGS), pt_to_str(PT_MTU),
		    pt_to_str(PT_MAXBW));
		(void) fprintf(fp, "\t%s\t\t%s, %s, %s\n", rt_to_str(RT_DEVICE),
		    pt_to_str(PT_MATCH), pt_to_str(PT_ALLOW_PARTITION),
		    pt_to_str(PT_ALLOW_RAW_IO));
		(void) fprintf(fp, "\t%s\t\t%s, %s\n", rt_to_str(RT_RCTL),
		    pt_to_str(PT_NAME), pt_to_str(PT_VALUE));
		(void) fprintf(fp, "\t%s\t\t%s, %s, %s\n", rt_to_str(RT_ATTR),
		    pt_to_str(PT_NAME), pt_to_str(PT_TYPE),
		    pt_to_str(PT_VALUE));
		(void) fprintf(fp, "\t%s\t\t%s\n", rt_to_str(RT_DATASET),
		    pt_to_str(PT_NAME));
		(void) fprintf(fp, "\t%s\t%s, %s\n", rt_to_str(RT_DCPU),
		    pt_to_str(PT_NCPUS), pt_to_str(PT_IMPORTANCE));
		(void) fprintf(fp, "\t%s\t%s\n", rt_to_str(RT_PCAP),
		    pt_to_str(PT_NCPUS));
		(void) fprintf(fp, "\t%s\t%s, %s, %s\n", rt_to_str(RT_MCAP),
		    pt_to_str(PT_PHYSICAL), pt_to_str(PT_SWAP),
		    pt_to_str(PT_LOCKED));
		(void) fprintf(fp, "\t%s\t\t%s, %s\n", rt_to_str(RT_ADMIN),
		    pt_to_str(PT_USER), pt_to_str(PT_AUTHS));
	}
	if (flags & HELP_AI_REPORT) {
		(void) fprintf(fp, "%s %s\n", execname, SHELP_AI_REPORT);
	}
}

static void
zone_perror(char *prefix, int err, boolean_t set_saw)
{
	zerr("%s: %s", prefix, zonecfg_strerror(err));
	if (set_saw)
		saw_error = B_TRUE;
}

/*
 * zone_perror() expects a single string, but for remove and select
 * we have both the command and the resource type, so this wrapper
 * function serves the same purpose in a slightly different way.
 */

static void
z_cmd_rt_perror(int cmd_num, int res_num, int err, boolean_t set_saw)
{
	zerr("%s %s: %s", cmd_to_str(cmd_num), rt_to_str(res_num),
	    zonecfg_strerror(err));
	if (set_saw)
		saw_error = B_TRUE;
}

/* returns Z_OK if successful, Z_foo from <libzonecfg.h> otherwise */
static int
initialize(boolean_t handle_expected)
{
	int err;
	char brandname[MAXNAMELEN];

	if (zonecfg_check_handle(handle) != Z_OK) {
		if ((err = zonecfg_get_handle(zone, handle)) == Z_OK) {
			got_handle = B_TRUE;
			if (zonecfg_get_brand(handle, brandname,
			    sizeof (brandname)) != Z_OK) {
				zerr("Zone %s is inconsistent: missing "
				    "brand attribute", zone);
				exit(Z_ERR);
			}
			if ((brand = brand_open(brandname)) == NULL) {
				zerr("Zone %s uses non-existent brand \"%s\"."
				    "  Unable to continue", zone, brandname);
				exit(Z_ERR);
			}
			/*
			 * If the user_attr file is newer than
			 * the zone config file, the admins
			 * may need to be updated since the
			 * RBAC files are authoritative for
			 * authorization checks.
			 */
			err = zonecfg_update_userauths(handle, zone);
			if (err == Z_OK) {
				zerr(gettext("The administrative rights "
				    "were updated to match "
				    "the current RBAC configuration.\n"
				    "Use \"info admin\" and \"revert\" to "
				    "compare with the previous settings."));
				need_to_commit = B_TRUE;
			} else if (err != Z_NO_ENTRY) {
				zerr(gettext("failed to update "
				    "admin  rights."));
				exit(Z_ERR);
			} else if (need_to_commit) {
				zerr(gettext("admin rights were updated "
				    "to match RBAC configuration."));
			}

		} else if (global_zone && err == Z_NO_ZONE && !got_handle &&
		    !read_only_mode) {
			/*
			 * We implicitly create the global zone config if it
			 * doesn't exist.
			 */
			zone_dochandle_t tmphandle;

			if ((tmphandle = zonecfg_init_handle()) == NULL) {
				zone_perror(execname, Z_NOMEM, B_TRUE);
				exit(Z_ERR);
			}

			err = zonecfg_get_template_handle(BLANK_TEMPLATE, zone,
			    tmphandle);

			if (err != Z_OK) {
				zonecfg_fini_handle(tmphandle);
				zone_perror(BLANK_TEMPLATE, err, B_TRUE);
				return (err);
			}

			need_to_commit = B_TRUE;
			zonecfg_fini_handle(handle);
			handle = tmphandle;
			got_handle = B_TRUE;

		} else {
			zone_perror(zone, err, handle_expected || got_handle);
			if (err == Z_NO_ZONE && !got_handle &&
			    interactive_mode && !read_only_mode)
				(void) printf(gettext("Use '%s' to begin "
				    "configuring a new zone.\n"),
				    cmd_to_str(CMD_CREATE));
			return (err);
		}
	}
	if (got_handle && brand == NULL) {
		/* Will not succeed until the brand is set. */
		if (zonecfg_get_brand(handle, brandname,
		    sizeof (brandname)) == Z_OK)
			brand = brand_open(brandname);
	}

	return (Z_OK);
}

static boolean_t
state_atleast(zone_state_t state)
{
	zone_state_t state_num;
	int err;

	if ((err = zone_get_state(zone, &state_num)) != Z_OK) {
		/* all states are greater than "non-existent" */
		if (err == Z_NO_ZONE)
			return (B_FALSE);
		zerr(gettext("Unexpectedly failed to determine state "
		    "of zone %s: %s"), zone, zonecfg_strerror(err));
		exit(Z_ERR);
	}
	return (state_num >= state);
}

/*
 * short_usage() is for bad syntax: getopt() issues, too many arguments, etc.
 */

void
short_usage(int command)
{
	/* lex_lineno has already been incremented in the lexer; compensate */
	if (cmd_file_mode) {
		if (strcmp(cmd_file_name, "-") == 0)
			(void) fprintf(stderr,
			    gettext("syntax error on line %d\n"),
			    lex_lineno - 1);
		else
			(void) fprintf(stderr,
			    gettext("syntax error on line %d of %s\n"),
			    lex_lineno - 1, cmd_file_name);
	}
	(void) fprintf(stderr, "%s:\n%s\n", gettext("usage"),
	    helptab[command].short_usage);
	saw_error = B_TRUE;
}

/*
 * long_usage() is for bad semantics: e.g., wrong property type for a given
 * resource type.  It is also used by longer_usage() below.
 */

void
long_usage(uint_t cmd_num, boolean_t set_saw)
{
	(void) fprintf(set_saw ? stderr : stdout, "%s:\n%s\n", gettext("usage"),
	    helptab[cmd_num].short_usage);
	(void) fprintf(set_saw ? stderr : stdout, "\t%s\n", long_help(cmd_num));
	if (set_saw)
		saw_error = B_TRUE;
}

/*
 * longer_usage() is for 'help foo' and 'foo -?': call long_usage() and also
 * any extra usage() flags as appropriate for whatever command.
 */

void
longer_usage(uint_t cmd_num)
{
	long_usage(cmd_num, B_FALSE);
	if (helptab[cmd_num].flags != 0) {
		(void) printf("\n");
		usage(B_TRUE, helptab[cmd_num].flags);
	}
}

/*
 * scope_usage() is simply used when a command is called from the wrong scope.
 */

static void
scope_usage(uint_t cmd_num)
{
	zerr(gettext("The %s command only makes sense in the %s scope."),
	    cmd_to_str(cmd_num),
	    global_scope ?  gettext("resource") : gettext("global"));
	saw_error = B_TRUE;
}

/*
 * On input, B_TRUE => yes, B_FALSE => no.
 * On return, B_TRUE => 1, B_FALSE => no, could not ask => -1.
 */

static int
ask_yesno(boolean_t default_answer, const char *question)
{
	char line[64];	/* should be enough to answer yes or no */

	if (!ok_to_prompt) {
		saw_error = B_TRUE;
		return (-1);
	}
	for (;;) {
		if (printf("%s (%s)? ", question,
		    default_answer ? "[y]/n" : "y/[n]") < 0)
			return (-1);
		if (fgets(line, sizeof (line), stdin) == NULL)
			return (-1);

		if (line[0] == '\n')
			return (default_answer ? 1 : 0);
		if (tolower(line[0]) == 'y')
			return (1);
		if (tolower(line[0]) == 'n')
			return (0);
	}
}

/*
 * Prints warning if zone already exists.
 * In interactive mode, prompts if we should continue anyway and returns Z_OK
 * if so, Z_ERR if not.  In non-interactive mode, exits with Z_ERR.
 *
 * Note that if a zone exists and its state is >= INSTALLED, an error message
 * will be printed and this function will return Z_ERR regardless of mode.
 */

static int
check_if_zone_already_exists(boolean_t force)
{
	char line[ZONENAME_MAX + 128];	/* enough to ask a question */
	zone_dochandle_t tmphandle;
	int res, answer;

	if ((tmphandle = zonecfg_init_handle()) == NULL) {
		zone_perror(execname, Z_NOMEM, B_TRUE);
		exit(Z_ERR);
	}
	res = zonecfg_get_handle(zone, tmphandle);
	zonecfg_fini_handle(tmphandle);
	if (res != Z_OK)
		return (Z_OK);

	if (state_atleast(ZONE_STATE_INSTALLED)) {
		zerr(gettext("Zone %s already installed; %s not allowed."),
		    zone, cmd_to_str(CMD_CREATE));
		return (Z_ERR);
	}

	if (force) {
		(void) printf(gettext("Zone %s already exists; overwriting.\n"),
		    zone);
		return (Z_OK);
	}
	(void) snprintf(line, sizeof (line),
	    gettext("Zone %s already exists; %s anyway"), zone,
	    cmd_to_str(CMD_CREATE));
	if ((answer = ask_yesno(B_FALSE, line)) == -1) {
		zerr(gettext("Zone exists, input not from terminal and -F not "
		    "specified:\n%s command ignored, exiting."),
		    cmd_to_str(CMD_CREATE));
		exit(Z_ERR);
	}
	return (answer == 1 ? Z_OK : Z_ERR);
}

static boolean_t
zone_is_read_only(int cmd_num)
{
	if (strncmp(zone, SYS_ZONE_PREFIX, strlen(SYS_ZONE_PREFIX)) == 0) {
		zerr(gettext("%s: zones beginning with %s are read-only."),
		    zone, SYS_ZONE_PREFIX);
		saw_error = B_TRUE;
		return (B_TRUE);
	}
	if (read_only_mode) {
		zerr(gettext("%s: cannot %s in read-only mode."), zone,
		    cmd_to_str(cmd_num));
		saw_error = B_TRUE;
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Clear the auto-mac-address property in each anet resource in the given
 * zone configuration.
 */
static int
zonecfg_clear_automac(zone_dochandle_t handle)
{
	struct zone_anettab anettab;
	int err = Z_OK;

	if ((err = zonecfg_setanetent(handle)) != Z_OK)
		return (err);
	while (zonecfg_getanetent(handle, &anettab) == Z_OK) {
		if (anettab.zone_anet_auto_mac_addr[0] != '\0') {
			anettab.zone_anet_auto_mac_addr[0] = '\0';
			if ((err = zonecfg_modify_anet(handle, &anettab,
			    &anettab)) != Z_OK)
				break;
		}
	}
	(void) zonecfg_endanetent(handle);
	return (err);
}

/*
 * Create a new configuration.
 */
void
create_func(cmd_t *cmd)
{
	int err, arg;
	char zone_template[ZONENAME_MAX], default_template[ZONENAME_MAX];
	char attach_path[MAXPATHLEN];
	zone_dochandle_t tmphandle;
	boolean_t force = B_FALSE;
	boolean_t attach = B_FALSE;
	boolean_t arg_err = B_FALSE;
	boolean_t use_default_template = B_TRUE;

	assert(cmd != NULL);

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?a:bFt:"))
	    != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?')
				longer_usage(CMD_CREATE);
			else
				short_usage(CMD_CREATE);
			arg_err = B_TRUE;
			break;
		case 'a':
			(void) strlcpy(attach_path, optarg,
			    sizeof (attach_path));
			attach = B_TRUE;
			break;
		case 'b':
			use_default_template = B_FALSE;
			(void) strlcpy(zone_template, BLANK_TEMPLATE,
			    sizeof (zone_template));
			break;
		case 'F':
			force = B_TRUE;
			break;
		case 't':
			/* Support for legacy ipkg brand */
			if (strcmp(optarg, "SUNWipkg") == 0) {
				use_default_template = B_FALSE;
				(void) strlcpy(zone_template, "SUNWsolaris",
				    sizeof (zone_template));
				break;
			}
			use_default_template = B_FALSE;
			(void) strlcpy(zone_template, optarg,
			    sizeof (zone_template));
			break;
		default:
			short_usage(CMD_CREATE);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_CREATE);
		return;
	}

	if (zone_is_read_only(CMD_CREATE))
		return;

	if (check_if_zone_already_exists(force) != Z_OK)
		return;

	if (use_default_template) {
		if (zonecfg_default_template(default_template,
		    sizeof (default_template)) != Z_OK) {
			zerr(gettext("Unable to get zonecfg/default_template "
			    "SMF property.  Verify that the "
			    "svc:/system/zones:default service has a "
			    "zonecfg/default_template property set to the name "
			    "of a valid zone template.  See zonecfg(1M)."));
			exit(Z_ERR);
		}
		if (interactive_mode) {
			(void) printf(gettext("create: Using system default "
			    "template '%s'\n"), default_template);
		}
		(void) strlcpy(zone_template, default_template,
		    sizeof (zone_template));
	}

	/*
	 * Get a temporary handle first.  If that fails, the old handle
	 * will not be lost.  Then finish whichever one we don't need,
	 * to avoid leaks.  Then get the handle for zone_template, and
	 * set the name to zone: this "copy, rename" method is how
	 * create -[b|t] works.
	 */
	if ((tmphandle = zonecfg_init_handle()) == NULL) {
		zone_perror(execname, Z_NOMEM, B_TRUE);
		exit(Z_ERR);
	}

	if (attach) {
		err = zonecfg_get_attach_handle(attach_path, ZONE_DETACHED,
		    zone, B_FALSE, tmphandle);
	} else {
		/*
		 * Once we've loaded the given template, we need to clear any
		 * randomly generated MAC addresses stored in auto-mac-address
		 * properties of anet resources.  If we don't do this, new MAC
		 * addresses will not be generated for the anet resources in
		 * this new zone.
		 */
		if ((err =  zonecfg_get_template_handle(zone_template, zone,
		    tmphandle)) == Z_OK)
			err = zonecfg_clear_automac(tmphandle);
	}

	if (err != Z_OK) {
		zonecfg_fini_handle(tmphandle);
		if (attach && err == Z_NO_ZONE)
			(void) fprintf(stderr, gettext("invalid path to "
			    "detached zone\n"));
		else if (attach && err == Z_INVALID_DOCUMENT)
			(void) fprintf(stderr, gettext("Cannot attach to an "
			    "earlier release of the operating system\n"));
		else
			zone_perror(zone_template, err, B_TRUE);
		return;
	}

	need_to_commit = B_TRUE;
	zonecfg_fini_handle(handle);
	handle = tmphandle;
	got_handle = B_TRUE;
}

/*
 * This malloc()'s memory, which must be freed by the caller.
 */
static char *
quoteit(char *instr)
{
	char *outstr;
	size_t outstrsize = strlen(instr) + 3;	/* 2 quotes + '\0' */

	if ((outstr = malloc(outstrsize)) == NULL) {
		zone_perror(zone, Z_NOMEM, B_FALSE);
		exit(Z_ERR);
	}
	if (strchr(instr, ' ') == NULL) {
		(void) strlcpy(outstr, instr, outstrsize);
		return (outstr);
	}
	(void) snprintf(outstr, outstrsize, "\"%s\"", instr);
	return (outstr);
}

static void
export_prop(FILE *of, int prop_num, char *prop_id)
{
	char *quote_str;

	if (strlen(prop_id) == 0)
		return;
	quote_str = quoteit(prop_id);
	(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
	    pt_to_str(prop_num), quote_str);
	free(quote_str);
}

static void
chomp_newline(char *s1)
{
	size_t len;

	if (((len = strlen(s1)) > 1) && (s1[len-1] == '\n'))
			s1[len-1] = '\0';

}

void
export_func(cmd_t *cmd)
{
	struct zone_nettab nettab;
	struct zone_anettab anettab;
	struct zone_fstab fstab;
	struct zone_devtab devtab;
	struct zone_attrtab attrtab;
	struct zone_rctltab rctltab;
	struct zone_dstab dstab;
	struct zone_psettab psettab;
	struct zone_mcaptab mcaptab;
	struct zone_rctlvaltab *valptr;
	struct zone_admintab admintab;
	int err, arg;
	char zonepath[MAXPATHLEN], outfile[MAXPATHLEN], pool[MAXNAMELEN];
	char bootargs[BOOTARGS_MAX];
	char sched[MAXNAMELEN];
	char brand[MAXNAMELEN];
	char hostidp[HW_HOSTID_LEN];
	char fsallowedp[ZONE_FS_ALLOWED_MAX];
	char *limitpriv;
	FILE *of;
	boolean_t autoboot;
	char macprof[MAXNAMELEN];
	zone_iptype_t iptype;
	boolean_t need_to_close = B_FALSE;
	boolean_t arg_err = B_FALSE;

	assert(cmd != NULL);

	outfile[0] = '\0';
	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?f:")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?')
				longer_usage(CMD_EXPORT);
			else
				short_usage(CMD_EXPORT);
			arg_err = B_TRUE;
			break;
		case 'f':
			(void) strlcpy(outfile, optarg, sizeof (outfile));
			break;
		default:
			short_usage(CMD_EXPORT);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_EXPORT);
		return;
	}
	if (strlen(outfile) == 0) {
		of = stdout;
	} else {
		if ((of = fopen(outfile, "w")) == NULL) {
			zerr(gettext("opening file %s: %s"),
			    outfile, strerror(errno));
			goto done;
		}
		setbuf(of, NULL);
		need_to_close = B_TRUE;
	}

	if ((err = initialize(B_TRUE)) != Z_OK)
		goto done;

	(void) fprintf(of, "%s -b\n", cmd_to_str(CMD_CREATE));

	if (zonecfg_get_zonepath(handle, zonepath, sizeof (zonepath)) == Z_OK &&
	    strlen(zonepath) > 0)
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_ZONEPATH), zonepath);

	if ((zone_get_brand(zone, brand, sizeof (brand)) == Z_OK) &&
	    strlen(brand) > 0)
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_BRAND), brand);

	if (zonecfg_get_autoboot(handle, &autoboot) == Z_OK)
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_AUTOBOOT), autoboot ? "true" : "false");

	if (zonecfg_get_mac_profile(handle, macprof, sizeof (macprof)) ==
	    Z_OK && strlen(macprof) > 0) {
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_MAC_PROFILE), macprof);
	}

	if (zonecfg_get_bootargs(handle, bootargs, sizeof (bootargs)) == Z_OK &&
	    strlen(bootargs) > 0) {
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_BOOTARGS), bootargs);
	}

	if (zonecfg_get_pool(handle, pool, sizeof (pool)) == Z_OK &&
	    strlen(pool) > 0)
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_POOL), pool);

	if (zonecfg_get_limitpriv(handle, &limitpriv) == Z_OK &&
	    strlen(limitpriv) > 0) {
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_LIMITPRIV), limitpriv);
		free(limitpriv);
	}

	if (zonecfg_get_sched_class(handle, sched, sizeof (sched)) == Z_OK &&
	    strlen(sched) > 0)
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_SCHED), sched);

	if (zonecfg_get_iptype(handle, &iptype) == Z_OK) {
		switch (iptype) {
		case ZS_SHARED:
			(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_IPTYPE), "shared");
			break;
		case ZS_EXCLUSIVE:
			(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_IPTYPE), "exclusive");
			break;
		}
	}

	if (zonecfg_get_hostid(handle, hostidp, sizeof (hostidp)) == Z_OK) {
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_HOSTID), hostidp);
	}

	if (zonecfg_get_fs_allowed(handle, fsallowedp,
	    sizeof (fsallowedp)) == Z_OK) {
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_FS_ALLOWED), fsallowedp);
	}

	if ((err = zonecfg_setfsent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getfsent(handle, &fstab) == Z_OK) {
		zone_fsopt_t *optptr;

		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_FS));
		export_prop(of, PT_DIR, fstab.zone_fs_dir);
		export_prop(of, PT_SPECIAL, fstab.zone_fs_special);
		export_prop(of, PT_RAW, fstab.zone_fs_raw);
		export_prop(of, PT_TYPE, fstab.zone_fs_type);
		for (optptr = fstab.zone_fs_options; optptr != NULL;
		    optptr = optptr->zone_fsopt_next) {
			/*
			 * Simple property values with embedded equal signs
			 * need to be quoted to prevent the lexer from
			 * mis-parsing them as complex name=value pairs.
			 */
			if (strchr(optptr->zone_fsopt_opt, '='))
				(void) fprintf(of, "%s %s \"%s\"\n",
				    cmd_to_str(CMD_ADD),
				    pt_to_str(PT_OPTIONS),
				    optptr->zone_fsopt_opt);
			else
				(void) fprintf(of, "%s %s %s\n",
				    cmd_to_str(CMD_ADD),
				    pt_to_str(PT_OPTIONS),
				    optptr->zone_fsopt_opt);
		}
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
		zonecfg_free_fs_option_list(fstab.zone_fs_options);
	}
	(void) zonecfg_endfsent(handle);

	if ((err = zonecfg_setnetent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getnetent(handle, &nettab) == Z_OK) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_NET));
		export_prop(of, PT_ADDRESS, nettab.zone_net_address);
		export_prop(of, PT_ALLOWED_ADDRESS,
		    nettab.zone_net_allowed_addr);
		export_prop(of, PT_CONFIGURE_ALLOWED_ADDRESS,
		    nettab.zone_net_configure_allowed_addr ? "true" : "false");
		export_prop(of, PT_PHYSICAL, nettab.zone_net_physical);
		export_prop(of, PT_DEFROUTER, nettab.zone_net_defrouter);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}
	(void) zonecfg_endnetent(handle);

	if ((err = zonecfg_setanetent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getanetent(handle, &anettab) == Z_OK) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_ANET));
		export_prop(of, PT_LINKNAME, anettab.zone_anet_linkname);
		export_prop(of, PT_LOWER_LINK, anettab.zone_anet_lower_link);
		export_prop(of, PT_ALLOWED_ADDRESS,
		    anettab.zone_anet_allowed_addr);
		export_prop(of, PT_CONFIGURE_ALLOWED_ADDRESS,
		    nettab.zone_net_configure_allowed_addr ? "true" : "false");
		export_prop(of, PT_DEFROUTER, anettab.zone_anet_defrouter);
		export_prop(of, PT_ALLOWED_DHCP_CIDS,
		    anettab.zone_anet_allowed_dhcp_cids);
		export_prop(of, PT_LINK_PROTECTION,
		    anettab.zone_anet_link_protection);
		export_prop(of, PT_MAC_ADDRESS,
		    anettab.zone_anet_mac_addr);
		export_prop(of, PT_AUTO_MAC_ADDRESS,
		    anettab.zone_anet_auto_mac_addr);
		export_prop(of, PT_MAC_PREFIX, anettab.zone_anet_mac_prefix);
		export_prop(of, PT_MAC_SLOT, anettab.zone_anet_mac_slot);
		export_prop(of, PT_VLAN_ID, anettab.zone_anet_vlan_id);
		export_prop(of, PT_PRIORITY, anettab.zone_anet_priority);
		export_prop(of, PT_RXRINGS, anettab.zone_anet_rxrings);
		export_prop(of, PT_TXRINGS, anettab.zone_anet_txrings);
		export_prop(of, PT_MTU, anettab.zone_anet_mtu);
		export_prop(of, PT_MAXBW, anettab.zone_anet_maxbw);
		export_prop(of, PT_RXFANOUT, anettab.zone_anet_rxfanout);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}
	(void) zonecfg_endanetent(handle);

	if ((err = zonecfg_setdevent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getdevent(handle, &devtab) == Z_OK) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_DEVICE));
		export_prop(of, PT_MATCH, devtab.zone_dev_match);
		export_prop(of, PT_ALLOW_PARTITION, devtab.zone_dev_partition);
		export_prop(of, PT_ALLOW_RAW_IO, devtab.zone_dev_raw_io);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}
	(void) zonecfg_enddevent(handle);

	if (zonecfg_getmcapent(handle, &mcaptab) == Z_OK) {
		char buf[128];

		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_MCAP));
		bytes_to_units(mcaptab.zone_physmem_cap, buf, sizeof (buf));
		(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
		    pt_to_str(PT_PHYSICAL), buf);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}

	if ((err = zonecfg_setrctlent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getrctlent(handle, &rctltab) == Z_OK) {
		(void) fprintf(of, "%s rctl\n", cmd_to_str(CMD_ADD));
		export_prop(of, PT_NAME, rctltab.zone_rctl_name);
		for (valptr = rctltab.zone_rctl_valptr; valptr != NULL;
		    valptr = valptr->zone_rctlval_next) {
			fprintf(of, "%s %s (%s=%s,%s=%s,%s=%s)\n",
			    cmd_to_str(CMD_ADD), pt_to_str(PT_VALUE),
			    pt_to_str(PT_PRIV), valptr->zone_rctlval_priv,
			    pt_to_str(PT_LIMIT), valptr->zone_rctlval_limit,
			    pt_to_str(PT_ACTION), valptr->zone_rctlval_action);
		}
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
		zonecfg_free_rctl_value_list(rctltab.zone_rctl_valptr);
	}
	(void) zonecfg_endrctlent(handle);

	if ((err = zonecfg_setattrent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getattrent(handle, &attrtab) == Z_OK) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_ATTR));
		export_prop(of, PT_NAME, attrtab.zone_attr_name);
		export_prop(of, PT_TYPE, attrtab.zone_attr_type);
		export_prop(of, PT_VALUE, attrtab.zone_attr_value);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}
	(void) zonecfg_endattrent(handle);

	if ((err = zonecfg_setdsent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getdsent(handle, &dstab) == Z_OK) {
		char *defalias;		/* default alias */

		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_DATASET));
		export_prop(of, PT_NAME, dstab.zone_dataset_name);

		/*
		 * To allow an exported zonecfg to be usable on systems that
		 * lack dataset aliasing, only show the alias if it is not the
		 * default alias.
		 */
		if ((defalias = strrchr(dstab.zone_dataset_name, '/')) == NULL)
			defalias = dstab.zone_dataset_name;
		else
			defalias++;
		if (strcmp(defalias, dstab.zone_dataset_alias) != 0)
			export_prop(of, PT_ALIAS, dstab.zone_dataset_alias);

		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}
	(void) zonecfg_enddsent(handle);

	if (zonecfg_getpsetent(handle, &psettab) == Z_OK) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_DCPU));
		if (strcmp(psettab.zone_ncpu_min, psettab.zone_ncpu_max) == 0)
			(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NCPUS), psettab.zone_ncpu_max);
		else
			(void) fprintf(of, "%s %s=%s-%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_NCPUS), psettab.zone_ncpu_min,
			    psettab.zone_ncpu_max);
		if (psettab.zone_importance[0] != '\0')
			(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
			    pt_to_str(PT_IMPORTANCE), psettab.zone_importance);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}

	if ((err = zonecfg_setadminent(handle)) != Z_OK) {
		zone_perror(zone, err, B_FALSE);
		goto done;
	}
	while (zonecfg_getadminent(handle, &admintab) == Z_OK) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    rt_to_str(RT_ADMIN));
		export_prop(of, PT_USER, admintab.zone_admin_user);
		export_prop(of, PT_AUTHS, admintab.zone_admin_auths);
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
	}
	(void) zonecfg_endadminent(handle);

	/*
	 * There is nothing to export for pcap since this resource is just
	 * a container for an rctl alias.
	 */

done:
	if (need_to_close)
		(void) fclose(of);
}

void
exit_func(cmd_t *cmd)
{
	int arg, answer;
	boolean_t arg_err = B_FALSE;

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?F")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_EXIT);
			arg_err = B_TRUE;
			break;
		case 'F':
			force_exit = B_TRUE;
			break;
		default:
			short_usage(CMD_EXIT);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind < cmd->cmd_argc) {
		short_usage(CMD_EXIT);
		return;
	}

	if (global_scope || force_exit) {
		time_to_exit = B_TRUE;
		return;
	}

	answer = ask_yesno(B_FALSE, "Resource incomplete; really quit");
	if (answer == -1) {
		zerr(gettext("Resource incomplete, input "
		    "not from terminal and -F not specified:\n%s command "
		    "ignored, but exiting anyway."), cmd_to_str(CMD_EXIT));
		exit(Z_ERR);
	} else if (answer == 1) {
		time_to_exit = B_TRUE;
	}
	/* (answer == 0) => just return */
}

static int
validate_zonepath_syntax(char *path)
{
	if (path[0] != '/') {
		zerr(gettext("%s is not an absolute path."), path);
		return (Z_ERR);
	}
	/* If path is all slashes, then fail */
	if (strspn(path, "/") == strlen(path)) {
		zerr(gettext("/ is not allowed as a %s."),
		    pt_to_str(PT_ZONEPATH));
		return (Z_ERR);
	}
	return (Z_OK);
}

static void
add_resource(cmd_t *cmd)
{
	int type;
	struct zone_psettab tmp_psettab;
	struct zone_mcaptab tmp_mcaptab;
	uint64_t tmp;
	uint64_t tmp_mcap;
	char pool[MAXNAMELEN];

	if ((type = cmd->cmd_res_type) == RT_UNKNOWN) {
		long_usage(CMD_ADD, B_TRUE);
		goto bad;
	}

	switch (type) {
	case RT_FS:
		bzero(&in_progress_fstab, sizeof (in_progress_fstab));
		return;
	case RT_NET:
		bzero(&in_progress_nettab, sizeof (in_progress_nettab));

		/*
		 * Set initial values for optional properties here.
		 * These properties can be later set to have a different value
		 * or cleared to have no value at all.
		 */
		in_progress_nettab.zone_net_configure_allowed_addr = B_TRUE;
		return;
	case RT_ANET:
		bzero(&in_progress_anettab, sizeof (in_progress_anettab));

		/*
		 * Set initial values for optional properties here.
		 * These properties can be later set to have a different value
		 * or cleared to have no value at all.
		 */
		(void) strlcpy(in_progress_anettab.zone_anet_link_protection,
		    "mac-nospoof",
		    sizeof (ipa->zone_anet_link_protection));
		in_progress_anettab.zone_anet_configure_allowed_addr = B_TRUE;
		return;
	case RT_DEVICE:
		bzero(&in_progress_devtab, sizeof (in_progress_devtab));
		return;
	case RT_RCTL:
		if (global_zone)
			zerr(gettext("WARNING: Setting a global zone resource "
			    "control too low could deny\nservice "
			    "to even the root user; "
			    "this could render the system impossible\n"
			    "to administer.  Please use caution."));
		bzero(&in_progress_rctltab, sizeof (in_progress_rctltab));
		return;
	case RT_ATTR:
		bzero(&in_progress_attrtab, sizeof (in_progress_attrtab));
		return;
	case RT_DATASET:
		bzero(&in_progress_dstab, sizeof (in_progress_dstab));
		return;
	case RT_DCPU:
		/* Make sure there isn't already a cpu-set or cpu-cap entry. */
		if (zonecfg_lookup_pset(handle, &tmp_psettab) == Z_OK) {
			zerr(gettext("The %s resource already exists."),
			    rt_to_str(RT_DCPU));
			goto bad;
		}
		if (zonecfg_get_aliased_rctl(handle, ALIAS_CPUCAP, &tmp) !=
		    Z_NO_ENTRY) {
			zerr(gettext("The %s resource already exists."),
			    rt_to_str(RT_PCAP));
			goto bad;
		}

		/* Make sure the pool property isn't set. */
		if (zonecfg_get_pool(handle, pool, sizeof (pool)) == Z_OK &&
		    strlen(pool) > 0) {
			zerr(gettext("The %s property is already set.  "
			    "A persistent pool is incompatible with\nthe %s "
			    "resource."),
			    pt_to_str(PT_POOL), rt_to_str(RT_DCPU));
			goto bad;
		}

		bzero(&in_progress_psettab, sizeof (in_progress_psettab));
		return;
	case RT_PCAP:
		/*
		 * Make sure there isn't already a cpu-set or incompatible
		 * cpu-cap rctls.
		 */
		if (zonecfg_lookup_pset(handle, &tmp_psettab) == Z_OK) {
			zerr(gettext("The %s resource already exists."),
			    rt_to_str(RT_DCPU));
			goto bad;
		}

		switch (zonecfg_get_aliased_rctl(handle, ALIAS_CPUCAP, &tmp)) {
		case Z_ALIAS_DISALLOW:
			zone_perror(rt_to_str(RT_PCAP), Z_ALIAS_DISALLOW,
			    B_FALSE);
			goto bad;

		case Z_OK:
			zerr(gettext("The %s resource already exists."),
			    rt_to_str(RT_PCAP));
			goto bad;

		default:
			break;
		}
		return;
	case RT_MCAP:
		/*
		 * Make sure there isn't already a mem-cap entry or max-swap
		 * or max-locked rctl.
		 */
		if (zonecfg_lookup_mcap(handle, &tmp_mcaptab) == Z_OK ||
		    zonecfg_get_aliased_rctl(handle, ALIAS_MAXSWAP, &tmp_mcap)
		    == Z_OK ||
		    zonecfg_get_aliased_rctl(handle, ALIAS_MAXLOCKEDMEM,
		    &tmp_mcap) == Z_OK) {
			zerr(gettext("The %s resource or a related resource "
			    "control already exists."), rt_to_str(RT_MCAP));
			goto bad;
		}
		if (global_zone)
			zerr(gettext("WARNING: Setting a global zone memory "
			    "cap too low could deny\nservice "
			    "to even the root user; "
			    "this could render the system impossible\n"
			    "to administer.  Please use caution."));
		bzero(&in_progress_mcaptab, sizeof (in_progress_mcaptab));
		return;
	case RT_ADMIN:
		bzero(&in_progress_admintab, sizeof (in_progress_admintab));
		return;
	default:
		zone_perror(rt_to_str(type), Z_NO_RESOURCE_TYPE, B_TRUE);
		long_usage(CMD_ADD, B_TRUE);
		usage(B_FALSE, HELP_RESOURCES);
	}
bad:
	global_scope = B_TRUE;
	end_op = -1;
}

static void
do_complex_rctl_val(complex_property_ptr_t cp)
{
	struct zone_rctlvaltab *rctlvaltab;
	complex_property_ptr_t cx;
	boolean_t seen_priv = B_FALSE, seen_limit = B_FALSE,
	    seen_action = B_FALSE;
	rctlblk_t *rctlblk;
	int err;

	if ((rctlvaltab = alloc_rctlvaltab()) == NULL) {
		zone_perror(zone, Z_NOMEM, B_TRUE);
		exit(Z_ERR);
	}
	for (cx = cp; cx != NULL; cx = cx->cp_next) {
		switch (cx->cp_type) {
		case PT_PRIV:
			if (seen_priv) {
				zerr(gettext("%s already specified"),
				    pt_to_str(PT_PRIV));
				goto bad;
			}
			(void) strlcpy(rctlvaltab->zone_rctlval_priv,
			    cx->cp_value,
			    sizeof (rctlvaltab->zone_rctlval_priv));
			seen_priv = B_TRUE;
			break;
		case PT_LIMIT:
			if (seen_limit) {
				zerr(gettext("%s already specified"),
				    pt_to_str(PT_LIMIT));
				goto bad;
			}
			(void) strlcpy(rctlvaltab->zone_rctlval_limit,
			    cx->cp_value,
			    sizeof (rctlvaltab->zone_rctlval_limit));
			seen_limit = B_TRUE;
			break;
		case PT_ACTION:
			if (seen_action) {
				zerr(gettext("%s already specified"),
				    pt_to_str(PT_ACTION));
				goto bad;
			}
			(void) strlcpy(rctlvaltab->zone_rctlval_action,
			    cx->cp_value,
			    sizeof (rctlvaltab->zone_rctlval_action));
			seen_action = B_TRUE;
			break;
		default:
			zone_perror(pt_to_str(PT_VALUE),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			long_usage(CMD_ADD, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			zonecfg_free_rctl_value_list(rctlvaltab);
			return;
		}
	}
	if (!seen_priv)
		zerr(gettext("%s not specified"), pt_to_str(PT_PRIV));
	if (!seen_limit)
		zerr(gettext("%s not specified"), pt_to_str(PT_LIMIT));
	if (!seen_action)
		zerr(gettext("%s not specified"), pt_to_str(PT_ACTION));
	if (!seen_priv || !seen_limit || !seen_action)
		goto bad;
	rctlvaltab->zone_rctlval_next = NULL;
	rctlblk = alloca(rctlblk_size());
	/*
	 * Make sure the rctl value looks roughly correct; we won't know if
	 * it's truly OK until we verify the configuration on the target
	 * system.
	 */
	if (zonecfg_construct_rctlblk(rctlvaltab, rctlblk) != Z_OK ||
	    !zonecfg_valid_rctlblk(rctlblk)) {
		zerr(gettext("Invalid %s %s specification"), rt_to_str(RT_RCTL),
		    pt_to_str(PT_VALUE));
		goto bad;
	}
	err = zonecfg_add_rctl_value(&in_progress_rctltab, rctlvaltab);
	if (err != Z_OK)
		zone_perror(pt_to_str(PT_VALUE), err, B_TRUE);
	return;

bad:
	zonecfg_free_rctl_value_list(rctlvaltab);
}

static void
add_property(cmd_t *cmd)
{
	char *prop_id;
	int err, res_type, prop_type;
	property_value_ptr_t pp;
	list_property_ptr_t l;

	res_type = resource_scope;
	prop_type = cmd->cmd_prop_name[0];
	if (res_type == RT_UNKNOWN || prop_type == PT_UNKNOWN) {
		long_usage(CMD_ADD, B_TRUE);
		return;
	}

	if (cmd->cmd_prop_nv_pairs != 1) {
		long_usage(CMD_ADD, B_TRUE);
		return;
	}

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (res_type) {
	case RT_FS:
		if (prop_type != PT_OPTIONS) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_ADD, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		pp = cmd->cmd_property_ptr[0];
		if (pp->pv_type != PROP_VAL_SIMPLE &&
		    pp->pv_type != PROP_VAL_LIST) {
			zerr(gettext("A %s or %s value was expected here."),
			    pvt_to_str(PROP_VAL_SIMPLE),
			    pvt_to_str(PROP_VAL_LIST));
			saw_error = B_TRUE;
			return;
		}
		if (pp->pv_type == PROP_VAL_SIMPLE) {
			if (pp->pv_simple == NULL) {
				long_usage(CMD_ADD, B_TRUE);
				return;
			}
			prop_id = pp->pv_simple;
			err = zonecfg_add_fs_option(&in_progress_fstab,
			    prop_id);
			if (err != Z_OK)
				zone_perror(pt_to_str(prop_type), err, B_TRUE);
		} else {
			list_property_ptr_t list;

			for (list = pp->pv_list; list != NULL;
			    list = list->lp_next) {
				prop_id = list->lp_simple;
				if (prop_id == NULL)
					break;
				err = zonecfg_add_fs_option(
				    &in_progress_fstab, prop_id);
				if (err != Z_OK)
					zone_perror(pt_to_str(prop_type), err,
					    B_TRUE);
			}
		}
		return;
	case RT_RCTL:
		if (prop_type != PT_VALUE) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_ADD, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		pp = cmd->cmd_property_ptr[0];
		if (pp->pv_type != PROP_VAL_COMPLEX &&
		    pp->pv_type != PROP_VAL_LIST) {
			zerr(gettext("A %s or %s value was expected here."),
			    pvt_to_str(PROP_VAL_COMPLEX),
			    pvt_to_str(PROP_VAL_LIST));
			saw_error = B_TRUE;
			return;
		}
		if (pp->pv_type == PROP_VAL_COMPLEX) {
			do_complex_rctl_val(pp->pv_complex);
			return;
		}
		for (l = pp->pv_list; l != NULL; l = l->lp_next)
			do_complex_rctl_val(l->lp_complex);
		return;
	default:
		zone_perror(rt_to_str(res_type), Z_NO_RESOURCE_TYPE, B_TRUE);
		long_usage(CMD_ADD, B_TRUE);
		usage(B_FALSE, HELP_RESOURCES);
		return;
	}
}

static boolean_t
gz_invalid_resource(int type)
{
	return (global_zone && (type == RT_FS || type == RT_ANET ||
	    type == RT_NET || type == RT_DEVICE || type == RT_ATTR ||
	    type == RT_DATASET));
}

static boolean_t
gz_invalid_rt_property(int type)
{
	return (global_zone && (type == RT_ZONENAME || type == RT_ZONEPATH ||
	    type == RT_AUTOBOOT || type == RT_LIMITPRIV ||
	    type == RT_BOOTARGS || type == RT_BRAND || type == RT_SCHED ||
	    type == RT_IPTYPE || type == RT_HOSTID || type == RT_FS_ALLOWED ||
	    type == RT_MAC_PROFILE));
}

static boolean_t
gz_invalid_property(int type)
{
	return (global_zone && (type == PT_ZONENAME || type == PT_ZONEPATH ||
	    type == PT_AUTOBOOT || type == PT_LIMITPRIV ||
	    type == PT_BOOTARGS || type == PT_BRAND || type == PT_SCHED ||
	    type == PT_IPTYPE || type == PT_HOSTID || type == PT_FS_ALLOWED ||
	    type == PT_MAC_PROFILE));
}

void
add_func(cmd_t *cmd)
{
	int arg;
	boolean_t arg_err = B_FALSE;

	assert(cmd != NULL);

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_ADD);
			arg_err = B_TRUE;
			break;
		default:
			short_usage(CMD_ADD);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_ADD);
		return;
	}

	if (zone_is_read_only(CMD_ADD))
		return;

	if (initialize(B_TRUE) != Z_OK)
		return;
	if (global_scope) {
		if (gz_invalid_resource(cmd->cmd_res_type)) {
			zerr(gettext("Cannot add a %s resource to the "
			    "global zone."), rt_to_str(cmd->cmd_res_type));
			saw_error = B_TRUE;
			return;
		}

		global_scope = B_FALSE;
		resource_scope = cmd->cmd_res_type;
		end_op = CMD_ADD;
		add_resource(cmd);
	} else
		add_property(cmd);
}

/*
 * This routine has an unusual implementation, because it tries very
 * hard to succeed in the face of a variety of failure modes.
 * The most common and most vexing occurs when the index file and
 * the /etc/zones/<zonename.xml> file are not both present.  In
 * this case, delete must eradicate as much of the zone state as is left
 * so that the user can later create a new zone with the same name.
 */
void
delete_func(cmd_t *cmd)
{
	int err, arg, answer;
	char line[ZONENAME_MAX + 128];	/* enough to ask a question */
	boolean_t force = B_FALSE;
	boolean_t arg_err = B_FALSE;

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?F")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_DELETE);
			arg_err = B_TRUE;
			break;
		case 'F':
			force = B_TRUE;
			break;
		default:
			short_usage(CMD_DELETE);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_DELETE);
		return;
	}

	if (zone_is_read_only(CMD_DELETE))
		return;

	if (!force) {
		/*
		 * Initialize sets up the global called "handle" and warns the
		 * user if the zone is not configured.  In force mode, we don't
		 * trust that evaluation, and hence skip it.  (We don't need the
		 * handle to be loaded anyway, since zonecfg_destroy is done by
		 * zonename). However, we also have to take care to emulate the
		 * messages spit out by initialize; see below.
		 */
		if (initialize(B_TRUE) != Z_OK)
			return;

		(void) snprintf(line, sizeof (line),
		    gettext("Are you sure you want to delete zone %s"), zone);
		if ((answer = ask_yesno(B_FALSE, line)) == -1) {
			zerr(gettext("Input not from terminal and -F not "
			    "specified:\n%s command ignored, exiting."),
			    cmd_to_str(CMD_DELETE));
			exit(Z_ERR);
		}
		if (answer != 1)
			return;
	}

	/*
	 * This function removes the authorizations from user_attr
	 * that correspond to those specified in the configuration
	 */
	if (initialize(B_TRUE) == Z_OK) {
		(void) zonecfg_deauthorize_users(handle, zone);
	}
	if ((err = zonecfg_destroy(zone, force)) != Z_OK) {
		if ((err == Z_BAD_ZONE_STATE) && !force) {
			zerr(gettext("Zone %s not in %s state; %s not "
			    "allowed.  Use -F to force %s."),
			    zone, zone_state_str(ZONE_STATE_CONFIGURED),
			    cmd_to_str(CMD_DELETE), cmd_to_str(CMD_DELETE));
		} else {
			zone_perror(zone, err, B_TRUE);
		}
	}
	need_to_commit = B_FALSE;

	/*
	 * Emulate initialize's messaging; if there wasn't a valid handle to
	 * begin with, then user had typed delete (or delete -F) multiple
	 * times.  So we emit a message.
	 *
	 * We only do this in the 'force' case because normally, initialize()
	 * takes care of this for us.
	 */
	if (force && zonecfg_check_handle(handle) != Z_OK && interactive_mode)
		(void) printf(gettext("Use '%s' to begin "
		    "configuring a new zone.\n"), cmd_to_str(CMD_CREATE));

	/*
	 * Time for a new handle: finish the old one off first
	 * then get a new one properly to avoid leaks.
	 */
	if (got_handle) {
		zonecfg_fini_handle(handle);
		if ((handle = zonecfg_init_handle()) == NULL) {
			zone_perror(execname, Z_NOMEM, B_TRUE);
			exit(Z_ERR);
		}
		if ((err = zonecfg_get_handle(zone, handle)) != Z_OK) {
			/* If there was no zone before, that's OK */
			if (err != Z_NO_ZONE)
				zone_perror(zone, err, B_TRUE);
			got_handle = B_FALSE;
		}
	}
}

static int
fill_in_fstab(cmd_t *cmd, struct zone_fstab *fstab, boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(fstab, sizeof (*fstab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_DIR:
			(void) strlcpy(fstab->zone_fs_dir, pp->pv_simple,
			    sizeof (fstab->zone_fs_dir));
			break;
		case PT_SPECIAL:
			(void) strlcpy(fstab->zone_fs_special, pp->pv_simple,
			    sizeof (fstab->zone_fs_special));
			break;
		case PT_RAW:
			(void) strlcpy(fstab->zone_fs_raw, pp->pv_simple,
			    sizeof (fstab->zone_fs_raw));
			break;
		case PT_TYPE:
			(void) strlcpy(fstab->zone_fs_type, pp->pv_simple,
			    sizeof (fstab->zone_fs_type));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	return (zonecfg_lookup_filesystem(handle, fstab));
}

static int
fill_in_nettab(cmd_t *cmd, struct zone_nettab *nettab,
    boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;
	boolean_t config_addr;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(nettab, sizeof (*nettab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_ADDRESS:
			(void) strlcpy(nettab->zone_net_address,
			    pp->pv_simple, sizeof (nettab->zone_net_address));
			break;
		case PT_ALLOWED_ADDRESS:
			(void) strlcpy(nettab->zone_net_allowed_addr,
			    pp->pv_simple,
			    sizeof (nettab->zone_net_allowed_addr));
			break;
		case PT_CONFIGURE_ALLOWED_ADDRESS:
			if (str_to_bool(PT_CONFIGURE_ALLOWED_ADDRESS,
			    pp->pv_simple, &config_addr) != 0) {
				saw_error = B_TRUE;
				return (Z_INVALID_PROPERTY);
			}
			nettab->zone_net_configure_allowed_addr = config_addr;
			break;
		case PT_PHYSICAL:
			(void) strlcpy(nettab->zone_net_physical,
			    pp->pv_simple,
			    sizeof (nettab->zone_net_physical));
			break;
		case PT_DEFROUTER:
			(void) strlcpy(nettab->zone_net_defrouter,
			    pp->pv_simple,
			    sizeof (nettab->zone_net_defrouter));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	err = zonecfg_lookup_net(handle, nettab);
	return (err);
}

static int
fill_in_anettab(cmd_t *cmd, struct zone_anettab *anettab,
    boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;
	boolean_t config_addr;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(anettab, sizeof (*anettab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_LINKNAME:
			(void) strlcpy(anettab->zone_anet_linkname,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_linkname));
			break;
		case PT_LOWER_LINK:
			(void) strlcpy(anettab->zone_anet_lower_link,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_lower_link));
			break;
		case PT_ALLOWED_ADDRESS:
			(void) strlcpy(
			    anettab->zone_anet_allowed_addr,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_allowed_addr));
			break;
		case PT_CONFIGURE_ALLOWED_ADDRESS:
			if (str_to_bool(PT_CONFIGURE_ALLOWED_ADDRESS,
			    pp->pv_simple, &config_addr) != 0) {
				saw_error = B_TRUE;
				return (Z_INVALID_PROPERTY);
			}
			anettab->zone_anet_configure_allowed_addr = config_addr;
			break;
		case PT_DEFROUTER:
			(void) strlcpy(anettab->zone_anet_defrouter,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_defrouter));
			break;
		case PT_ALLOWED_DHCP_CIDS:
			(void) strlcpy(
			    anettab->zone_anet_allowed_dhcp_cids,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_allowed_dhcp_cids));
			break;
		case PT_LINK_PROTECTION:
			(void) strlcpy(
			    anettab->zone_anet_link_protection,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_link_protection));
			break;
		case PT_MAC_ADDRESS:
			(void) strlcpy(anettab->zone_anet_mac_addr,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_mac_addr));
			break;
		case PT_AUTO_MAC_ADDRESS:
			(void) strlcpy(anettab->zone_anet_auto_mac_addr,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_auto_mac_addr));
			break;
		case PT_MAC_PREFIX:
			(void) strlcpy(anettab->zone_anet_mac_prefix,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_mac_prefix));
			break;
		case PT_MAC_SLOT:
			(void) strlcpy(anettab->zone_anet_mac_slot,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_mac_slot));
			break;
		case PT_VLAN_ID:
			(void) strlcpy(anettab->zone_anet_vlan_id,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_vlan_id));
			break;
		case PT_PRIORITY:
			(void) strlcpy(anettab->zone_anet_priority,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_priority));
			break;
		case PT_RXRINGS:
			(void) strlcpy(anettab->zone_anet_rxrings,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_rxrings));
			break;
		case PT_TXRINGS:
			(void) strlcpy(anettab->zone_anet_txrings,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_txrings));
			break;
		case PT_MTU:
			(void) strlcpy(anettab->zone_anet_mtu,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_mtu));
			break;
		case PT_MAXBW:
			(void) strlcpy(anettab->zone_anet_maxbw,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_maxbw));
			break;
		case PT_RXFANOUT:
			(void) strlcpy(anettab->zone_anet_rxfanout,
			    pp->pv_simple,
			    sizeof (anettab->zone_anet_rxfanout));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	err = zonecfg_lookup_anet(handle, anettab);
	return (err);
}

static int
fill_in_devtab(cmd_t *cmd, struct zone_devtab *devtab, boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(devtab, sizeof (*devtab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_MATCH:
			(void) strlcpy(devtab->zone_dev_match, pp->pv_simple,
			    sizeof (devtab->zone_dev_match));
			break;
		case PT_ALLOW_PARTITION:
			(void) strlcpy(devtab->zone_dev_partition,
			    pp->pv_simple,
			    sizeof (devtab->zone_dev_partition));
			break;
		case PT_ALLOW_RAW_IO:
			(void) strlcpy(devtab->zone_dev_raw_io,
			    pp->pv_simple,
			    sizeof (devtab->zone_dev_raw_io));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	err = zonecfg_lookup_dev(handle, devtab);
	return (err);
}

static int
fill_in_rctltab(cmd_t *cmd, struct zone_rctltab *rctltab,
    boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(rctltab, sizeof (*rctltab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_NAME:
			(void) strlcpy(rctltab->zone_rctl_name, pp->pv_simple,
			    sizeof (rctltab->zone_rctl_name));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	err = zonecfg_lookup_rctl(handle, rctltab);
	return (err);
}

static int
fill_in_attrtab(cmd_t *cmd, struct zone_attrtab *attrtab,
    boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(attrtab, sizeof (*attrtab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_NAME:
			(void) strlcpy(attrtab->zone_attr_name, pp->pv_simple,
			    sizeof (attrtab->zone_attr_name));
			break;
		case PT_TYPE:
			(void) strlcpy(attrtab->zone_attr_type, pp->pv_simple,
			    sizeof (attrtab->zone_attr_type));
			break;
		case PT_VALUE:
			(void) strlcpy(attrtab->zone_attr_value, pp->pv_simple,
			    sizeof (attrtab->zone_attr_value));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	err = zonecfg_lookup_attr(handle, attrtab);
	return (err);
}

static int
fill_in_dstab(cmd_t *cmd, struct zone_dstab *dstab, boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(dstab, sizeof (*dstab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_NAME:
			(void) strlcpy(dstab->zone_dataset_name, pp->pv_simple,
			    sizeof (dstab->zone_dataset_name));
			break;
		case PT_ALIAS:
			(void) strlcpy(dstab->zone_dataset_alias, pp->pv_simple,
			    sizeof (dstab->zone_dataset_alias));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	return (zonecfg_lookup_ds(handle, dstab));
}

static int
fill_in_admintab(cmd_t *cmd, struct zone_admintab *admintab,
    boolean_t fill_in_only)
{
	int err, i;
	property_value_ptr_t pp;

	if ((err = initialize(B_TRUE)) != Z_OK)
		return (err);

	bzero(admintab, sizeof (*admintab));
	for (i = 0; i < cmd->cmd_prop_nv_pairs; i++) {
		pp = cmd->cmd_property_ptr[i];
		if (pp->pv_type != PROP_VAL_SIMPLE || pp->pv_simple == NULL) {
			zerr(gettext("A simple value was expected here."));
			saw_error = B_TRUE;
			return (Z_INSUFFICIENT_SPEC);
		}
		switch (cmd->cmd_prop_name[i]) {
		case PT_USER:
			(void) strlcpy(admintab->zone_admin_user, pp->pv_simple,
			    sizeof (admintab->zone_admin_user));
			break;
		case PT_AUTHS:
			(void) strlcpy(admintab->zone_admin_auths,
			    pp->pv_simple, sizeof (admintab->zone_admin_auths));
			break;
		default:
			zone_perror(pt_to_str(cmd->cmd_prop_name[i]),
			    Z_NO_PROPERTY_TYPE, B_TRUE);
			return (Z_INSUFFICIENT_SPEC);
		}
	}
	if (fill_in_only)
		return (Z_OK);
	err = zonecfg_lookup_admin(handle, admintab);
	return (err);
}

static void
remove_aliased_rctl(int type, char *name)
{
	int err;
	uint64_t tmp;

	if ((err = zonecfg_get_aliased_rctl(handle, name, &tmp)) != Z_OK) {
		zerr("%s %s: %s", cmd_to_str(CMD_CLEAR), pt_to_str(type),
		    zonecfg_strerror(err));
		saw_error = B_TRUE;
		return;
	}
	if ((err = zonecfg_rm_aliased_rctl(handle, name)) != Z_OK) {
		zerr("%s %s: %s", cmd_to_str(CMD_CLEAR), pt_to_str(type),
		    zonecfg_strerror(err));
		saw_error = B_TRUE;
	} else {
		need_to_commit = B_TRUE;
	}
}

static boolean_t
prompt_remove_resource(cmd_t *cmd, char *rsrc)
{
	int num;
	int answer;
	int arg;
	boolean_t force = B_FALSE;
	char prompt[128];
	boolean_t arg_err = B_FALSE;

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "F")) != EOF) {
		switch (arg) {
		case 'F':
			force = B_TRUE;
			break;
		default:
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return (B_FALSE);


	num = zonecfg_num_resources(handle, rsrc);

	if (num == 0) {
		z_cmd_rt_perror(CMD_REMOVE, cmd->cmd_res_type, Z_NO_ENTRY,
		    B_TRUE);
		return (B_FALSE);
	}
	if (num > 1 && !force) {
		if (!interactive_mode) {
			zerr(gettext("There are multiple instances of this "
			    "resource.  Either qualify the resource to\n"
			    "remove a single instance or use the -F option to "
			    "remove all instances."));
			saw_error = B_TRUE;
			return (B_FALSE);
		}
		(void) snprintf(prompt, sizeof (prompt), gettext(
		    "Are you sure you want to remove ALL '%s' resources"),
		    rsrc);
		answer = ask_yesno(B_FALSE, prompt);
		if (answer == -1) {
			zerr(gettext("Resource incomplete."));
			return (B_FALSE);
		}
		if (answer != 1)
			return (B_FALSE);
	}
	return (B_TRUE);
}

static void
remove_fs(cmd_t *cmd)
{
	int err;

	/* traditional, qualified fs removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_fstab fstab;

		if ((err = fill_in_fstab(cmd, &fstab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_FS, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_filesystem(handle, &fstab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_FS, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		zonecfg_free_fs_option_list(fstab.zone_fs_options);
		return;
	}

	/*
	 * unqualified fs removal.  remove all fs's but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "fs"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "fs")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_FS, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_net(cmd_t *cmd)
{
	int err;

	/* traditional, qualified net removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_nettab nettab;

		if ((err = fill_in_nettab(cmd, &nettab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_NET, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_net(handle, &nettab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_NET, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	}

	/*
	 * unqualified net removal.  remove all nets but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "net"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "net")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_NET, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_anet(cmd_t *cmd)
{
	int err;

	/* traditional, qualified anet removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_anettab anettab;

		if ((err = fill_in_anettab(cmd, &anettab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_ANET, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_anet(handle, &anettab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_ANET, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	}

	/*
	 * unqualified anet removal.  remove all anets but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "anet"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "anet")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_ANET, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_device(cmd_t *cmd)
{
	int err;

	/* traditional, qualified device removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_devtab devtab;

		if ((err = fill_in_devtab(cmd, &devtab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_DEVICE, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_dev(handle, &devtab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_DEVICE, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	}

	/*
	 * unqualified device removal.  remove all devices but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "device"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "device")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_DEVICE, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_attr(cmd_t *cmd)
{
	int err;

	/* traditional, qualified attr removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_attrtab attrtab;

		if ((err = fill_in_attrtab(cmd, &attrtab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_ATTR, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_attr(handle, &attrtab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_ATTR, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	}

	/*
	 * unqualified attr removal.  remove all attrs but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "attr"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "attr")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_ATTR, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_dataset(cmd_t *cmd)
{
	int err;

	/* traditional, qualified dataset removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_dstab dstab;

		if ((err = fill_in_dstab(cmd, &dstab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_DATASET, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_ds(handle, &dstab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_DATASET, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	}

	/*
	 * unqualified dataset removal.  remove all datasets but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "dataset"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "dataset")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_DATASET, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_rctl(cmd_t *cmd)
{
	int err;

	/* traditional, qualified rctl removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_rctltab rctltab;

		if ((err = fill_in_rctltab(cmd, &rctltab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_RCTL, err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_rctl(handle, &rctltab)) != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_RCTL, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		zonecfg_free_rctl_value_list(rctltab.zone_rctl_valptr);
		return;
	}

	/*
	 * unqualified rctl removal.  remove all rctls but prompt if more
	 * than one.
	 */
	if (!prompt_remove_resource(cmd, "rctl"))
		return;

	if ((err = zonecfg_del_all_resources(handle, "rctl")) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_RCTL, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_pset()
{
	int err;
	struct zone_psettab psettab;

	if ((err = zonecfg_lookup_pset(handle, &psettab)) != Z_OK) {
		z_cmd_rt_perror(CMD_REMOVE, RT_DCPU, err, B_TRUE);
		return;
	}
	if ((err = zonecfg_delete_pset(handle)) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_DCPU, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_pcap()
{
	int err;
	uint64_t tmp;

	if (zonecfg_get_aliased_rctl(handle, ALIAS_CPUCAP, &tmp) != Z_OK) {
		zerr("%s %s: %s", cmd_to_str(CMD_REMOVE), rt_to_str(RT_PCAP),
		    zonecfg_strerror(Z_NO_RESOURCE_TYPE));
		saw_error = B_TRUE;
		return;
	}

	if ((err = zonecfg_rm_aliased_rctl(handle, ALIAS_CPUCAP)) != Z_OK)
		z_cmd_rt_perror(CMD_REMOVE, RT_PCAP, err, B_TRUE);
	else
		need_to_commit = B_TRUE;
}

static void
remove_mcap()
{
	int err, res1, res2, res3;
	uint64_t tmp;
	struct zone_mcaptab mcaptab;
	boolean_t revert = B_FALSE;

	res1 = zonecfg_lookup_mcap(handle, &mcaptab);
	res2 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXSWAP, &tmp);
	res3 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXLOCKEDMEM, &tmp);

	/* if none of these exist, there is no resource to remove */
	if (res1 != Z_OK && res2 != Z_OK && res3 != Z_OK) {
		zerr("%s %s: %s", cmd_to_str(CMD_REMOVE), rt_to_str(RT_MCAP),
		    zonecfg_strerror(Z_NO_RESOURCE_TYPE));
		saw_error = B_TRUE;
		return;
	}
	if (res1 == Z_OK) {
		if ((err = zonecfg_delete_mcap(handle)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_MCAP, err, B_TRUE);
			revert = B_TRUE;
		} else {
			need_to_commit = B_TRUE;
		}
	}
	if (res2 == Z_OK) {
		if ((err = zonecfg_rm_aliased_rctl(handle, ALIAS_MAXSWAP))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_MCAP, err, B_TRUE);
			revert = B_TRUE;
		} else {
			need_to_commit = B_TRUE;
		}
	}
	if (res3 == Z_OK) {
		if ((err = zonecfg_rm_aliased_rctl(handle, ALIAS_MAXLOCKEDMEM))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_MCAP, err, B_TRUE);
			revert = B_TRUE;
		} else {
			need_to_commit = B_TRUE;
		}
	}

	if (revert)
		need_to_commit = B_FALSE;
}

static void
remove_admin(cmd_t *cmd)
{
	int err;

	/* traditional, qualified attr removal */
	if (cmd->cmd_prop_nv_pairs > 0) {
		struct zone_admintab admintab;

		if ((err = fill_in_admintab(cmd, &admintab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_REMOVE, RT_ADMIN,
			    err, B_TRUE);
			return;
		}
		if ((err = zonecfg_delete_admin(handle, &admintab,
		    zone))
		    != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_ADMIN,
			    err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	} else {
		/*
		 * unqualified admin removal.
		 * remove all admins but prompt if more
		 * than one.
		 */
		if (!prompt_remove_resource(cmd, "admin"))
			return;

		if ((err = zonecfg_delete_admins(handle, zone))
		    != Z_OK)
			z_cmd_rt_perror(CMD_REMOVE, RT_ADMIN,
			    err, B_TRUE);
		else
			need_to_commit = B_TRUE;
	}
}

static void
remove_resource(cmd_t *cmd)
{
	int type;
	int arg;
	boolean_t arg_err = B_FALSE;

	if ((type = cmd->cmd_res_type) == RT_UNKNOWN) {
		long_usage(CMD_REMOVE, B_TRUE);
		return;
	}

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?F")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_REMOVE);
			arg_err = B_TRUE;
			break;
		case 'F':
			break;
		default:
			short_usage(CMD_REMOVE);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (type) {
	case RT_FS:
		remove_fs(cmd);
		return;
	case RT_NET:
		remove_net(cmd);
		return;
	case RT_ANET:
		remove_anet(cmd);
		return;
	case RT_DEVICE:
		remove_device(cmd);
		return;
	case RT_RCTL:
		remove_rctl(cmd);
		return;
	case RT_ATTR:
		remove_attr(cmd);
		return;
	case RT_DATASET:
		remove_dataset(cmd);
		return;
	case RT_DCPU:
		remove_pset();
		return;
	case RT_PCAP:
		remove_pcap();
		return;
	case RT_MCAP:
		remove_mcap();
		return;
	case RT_ADMIN:
		remove_admin(cmd);
		return;
	default:
		zone_perror(rt_to_str(type), Z_NO_RESOURCE_TYPE, B_TRUE);
		long_usage(CMD_REMOVE, B_TRUE);
		usage(B_FALSE, HELP_RESOURCES);
		return;
	}
}

static int
unset_anet_properties(int prop_type)
{
	ipa = &in_progress_anettab;
	if (prop_type == PT_LINKNAME) {
		ipa->zone_anet_linkname[0] = '\0';
	} if (prop_type == PT_LOWER_LINK) {
		ipa->zone_anet_lower_link[0] = '\0';
	} else if (prop_type == PT_ALLOWED_ADDRESS) {
		ipa->zone_anet_allowed_addr[0] = '\0';
	} else if (prop_type == PT_DEFROUTER) {
		ipa->zone_anet_defrouter[0] = '\0';
	} else if (prop_type == PT_ALLOWED_DHCP_CIDS) {
		ipa->zone_anet_allowed_dhcp_cids[0] = '\0';
	} else if (prop_type == PT_LINK_PROTECTION) {
		ipa->zone_anet_link_protection[0] = '\0';
	} else if (prop_type == PT_MAC_ADDRESS) {
		ipa->zone_anet_mac_addr[0] = '\0';
	} else if (prop_type == PT_AUTO_MAC_ADDRESS) {
		ipa->zone_anet_auto_mac_addr[0] = '\0';
	} else if (prop_type == PT_MAC_PREFIX) {
		ipa->zone_anet_mac_prefix[0] = '\0';
	} else if (prop_type == PT_MAC_SLOT) {
		ipa->zone_anet_mac_slot[0] = '\0';
	} else if (prop_type == PT_VLAN_ID) {
		ipa->zone_anet_vlan_id[0] = '\0';
	} else if (prop_type == PT_PRIORITY) {
		ipa->zone_anet_priority[0] = '\0';
	} else if (prop_type == PT_RXRINGS) {
		ipa->zone_anet_rxrings[0] = '\0';
	} else if (prop_type == PT_TXRINGS) {
		ipa->zone_anet_txrings[0] = '\0';
	} else if (prop_type == PT_MTU) {
		ipa->zone_anet_mtu[0] = '\0';
	} else if (prop_type == PT_MAXBW) {
		ipa->zone_anet_maxbw[0] = '\0';
	} else if (prop_type == PT_RXFANOUT) {
		ipa->zone_anet_rxfanout[0] = '\0';
	} else {
		return (-1);
	}
	return (0);
}

static void
remove_property(cmd_t *cmd)
{
	char *prop_id;
	int err, res_type, prop_type;
	property_value_ptr_t pp;
	struct zone_rctlvaltab *rctlvaltab;
	complex_property_ptr_t cx;

	res_type = resource_scope;
	prop_type = cmd->cmd_prop_name[0];
	if (res_type == RT_UNKNOWN || prop_type == PT_UNKNOWN) {
		long_usage(CMD_REMOVE, B_TRUE);
		return;
	}

	if (cmd->cmd_prop_nv_pairs != 1) {
		long_usage(CMD_ADD, B_TRUE);
		return;
	}

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (res_type) {
	case RT_FS:
		if (prop_type != PT_OPTIONS) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_REMOVE, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		pp = cmd->cmd_property_ptr[0];
		if (pp->pv_type == PROP_VAL_COMPLEX) {
			zerr(gettext("A %s or %s value was expected here."),
			    pvt_to_str(PROP_VAL_SIMPLE),
			    pvt_to_str(PROP_VAL_LIST));
			saw_error = B_TRUE;
			return;
		}
		if (pp->pv_type == PROP_VAL_SIMPLE) {
			if (pp->pv_simple == NULL) {
				long_usage(CMD_ADD, B_TRUE);
				return;
			}
			prop_id = pp->pv_simple;
			err = zonecfg_remove_fs_option(&in_progress_fstab,
			    prop_id);
			if (err != Z_OK)
				zone_perror(pt_to_str(prop_type), err, B_TRUE);
		} else {
			list_property_ptr_t list;

			for (list = pp->pv_list; list != NULL;
			    list = list->lp_next) {
				prop_id = list->lp_simple;
				if (prop_id == NULL)
					break;
				err = zonecfg_remove_fs_option(
				    &in_progress_fstab, prop_id);
				if (err != Z_OK)
					zone_perror(pt_to_str(prop_type), err,
					    B_TRUE);
			}
		}
		return;
	case RT_RCTL:
		if (prop_type != PT_VALUE) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_REMOVE, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		pp = cmd->cmd_property_ptr[0];
		if (pp->pv_type != PROP_VAL_COMPLEX) {
			zerr(gettext("A %s value was expected here."),
			    pvt_to_str(PROP_VAL_COMPLEX));
			saw_error = B_TRUE;
			return;
		}
		if ((rctlvaltab = alloc_rctlvaltab()) == NULL) {
			zone_perror(zone, Z_NOMEM, B_TRUE);
			exit(Z_ERR);
		}
		for (cx = pp->pv_complex; cx != NULL; cx = cx->cp_next) {
			switch (cx->cp_type) {
			case PT_PRIV:
				(void) strlcpy(rctlvaltab->zone_rctlval_priv,
				    cx->cp_value,
				    sizeof (rctlvaltab->zone_rctlval_priv));
				break;
			case PT_LIMIT:
				(void) strlcpy(rctlvaltab->zone_rctlval_limit,
				    cx->cp_value,
				    sizeof (rctlvaltab->zone_rctlval_limit));
				break;
			case PT_ACTION:
				(void) strlcpy(rctlvaltab->zone_rctlval_action,
				    cx->cp_value,
				    sizeof (rctlvaltab->zone_rctlval_action));
				break;
			default:
				zone_perror(pt_to_str(prop_type),
				    Z_NO_PROPERTY_TYPE, B_TRUE);
				long_usage(CMD_ADD, B_TRUE);
				usage(B_FALSE, HELP_PROPS);
				zonecfg_free_rctl_value_list(rctlvaltab);
				return;
			}
		}
		rctlvaltab->zone_rctlval_next = NULL;
		err = zonecfg_remove_rctl_value(&in_progress_rctltab,
		    rctlvaltab);
		if (err != Z_OK)
			zone_perror(pt_to_str(prop_type), err, B_TRUE);
		zonecfg_free_rctl_value_list(rctlvaltab);
		return;
	case RT_NET:
		if (prop_type != PT_DEFROUTER) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_REMOVE, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		} else {
			bzero(&in_progress_nettab.zone_net_defrouter,
			    sizeof (in_progress_nettab.zone_net_defrouter));
			return;
		}
	case RT_ANET:
		if (unset_anet_properties(prop_type) < 0) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_REMOVE, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
		}
		return;
	default:
		zone_perror(rt_to_str(res_type), Z_NO_RESOURCE_TYPE, B_TRUE);
		long_usage(CMD_REMOVE, B_TRUE);
		usage(B_FALSE, HELP_RESOURCES);
		return;
	}
}

void
remove_func(cmd_t *cmd)
{
	if (zone_is_read_only(CMD_REMOVE))
		return;

	assert(cmd != NULL);

	if (global_scope) {
		if (gz_invalid_resource(cmd->cmd_res_type)) {
			zerr(gettext("%s is not a valid resource for the "
			    "global zone."), rt_to_str(cmd->cmd_res_type));
			saw_error = B_TRUE;
			return;
		}
		remove_resource(cmd);
	} else {
		remove_property(cmd);
	}
}

static void
clear_property(cmd_t *cmd)
{
	int res_type, prop_type;

	res_type = resource_scope;
	prop_type = cmd->cmd_res_type;
	if (res_type == RT_UNKNOWN || prop_type == PT_UNKNOWN) {
		long_usage(CMD_CLEAR, B_TRUE);
		return;
	}

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (res_type) {
	case RT_FS:
		if (prop_type == PT_RAW) {
			in_progress_fstab.zone_fs_raw[0] = '\0';
			need_to_commit = B_TRUE;
			return;
		}
		break;
	case RT_DCPU:
		if (prop_type == PT_IMPORTANCE) {
			in_progress_psettab.zone_importance[0] = '\0';
			need_to_commit = B_TRUE;
			return;
		}
		break;
	case RT_MCAP:
		switch (prop_type) {
		case PT_PHYSICAL:
			in_progress_mcaptab.zone_physmem_cap[0] = '\0';
			need_to_commit = B_TRUE;
			return;
		case PT_SWAP:
			remove_aliased_rctl(PT_SWAP, ALIAS_MAXSWAP);
			return;
		case PT_LOCKED:
			remove_aliased_rctl(PT_LOCKED, ALIAS_MAXLOCKEDMEM);
			return;
		}
		break;
	case RT_DEVICE:
		switch (prop_type) {
		case PT_ALLOW_RAW_IO:
			in_progress_devtab.zone_dev_raw_io[0] = '\0';
			need_to_commit = B_TRUE;
			return;
		case PT_ALLOW_PARTITION:
			in_progress_devtab.zone_dev_partition[0] = '\0';
			need_to_commit = B_TRUE;
			return;
		}
		break;

	case RT_ANET:
		if (unset_anet_properties(prop_type) == 0) {
			need_to_commit = B_TRUE;
			return;
		}
		break;
	default:
		break;
	}

	zone_perror(pt_to_str(prop_type), Z_CLEAR_DISALLOW, B_TRUE);
}

static void
clear_global(cmd_t *cmd)
{
	int err, type;

	if ((type = cmd->cmd_res_type) == RT_UNKNOWN) {
		long_usage(CMD_CLEAR, B_TRUE);
		return;
	}

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (type) {
	case PT_ZONENAME:
		/* FALLTHRU */
	case PT_ZONEPATH:
		/* FALLTHRU */
	case PT_BRAND:
		zone_perror(pt_to_str(type), Z_CLEAR_DISALLOW, B_TRUE);
		return;
	case PT_AUTOBOOT:
		/* false is default; we'll treat as equivalent to clearing */
		if ((err = zonecfg_set_autoboot(handle, B_FALSE)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_AUTOBOOT, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_POOL:
		if ((err = zonecfg_set_pool(handle, NULL)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_POOL, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_LIMITPRIV:
		if ((err = zonecfg_set_limitpriv(handle, NULL)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_LIMITPRIV, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_BOOTARGS:
		if ((err = zonecfg_set_bootargs(handle, NULL)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_BOOTARGS, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_SCHED:
		if ((err = zonecfg_set_sched(handle, NULL)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_SCHED, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_IPTYPE:
		/*
		 * exclusive is the default; we'll treat that as equivalent
		 * to clearing.
		 */
		if ((err = zonecfg_set_iptype(handle, ZS_EXCLUSIVE)) != Z_OK) {
			z_cmd_rt_perror(CMD_CLEAR, RT_IPTYPE, err, B_TRUE);
		} else {
			ip_type_set = B_FALSE;
			need_to_commit = B_TRUE;
		}
		return;
	case PT_MAXLWPS:
		remove_aliased_rctl(PT_MAXLWPS, ALIAS_MAXLWPS);
		return;
	case PT_MAXPROCS:
		remove_aliased_rctl(PT_MAXPROCS, ALIAS_MAXPROCS);
		return;
	case PT_MAXSHMMEM:
		remove_aliased_rctl(PT_MAXSHMMEM, ALIAS_MAXSHMMEM);
		return;
	case PT_MAXSHMIDS:
		remove_aliased_rctl(PT_MAXSHMIDS, ALIAS_MAXSHMIDS);
		return;
	case PT_MAXMSGIDS:
		remove_aliased_rctl(PT_MAXMSGIDS, ALIAS_MAXMSGIDS);
		return;
	case PT_MAXSEMIDS:
		remove_aliased_rctl(PT_MAXSEMIDS, ALIAS_MAXSEMIDS);
		return;
	case PT_SHARES:
		remove_aliased_rctl(PT_SHARES, ALIAS_SHARES);
		return;
	case PT_MAC_PROFILE:
		/* non-set is default; we'll treat as equivalent to clearing */
		if ((err = zonecfg_set_mac_profile(handle, "")) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_MAC_PROFILE, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_HOSTID:
		if ((err = zonecfg_set_hostid(handle, NULL)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_HOSTID, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case PT_FS_ALLOWED:
		if ((err = zonecfg_set_fs_allowed(handle, NULL)) != Z_OK)
			z_cmd_rt_perror(CMD_CLEAR, RT_FS_ALLOWED, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	default:
		zone_perror(pt_to_str(type), Z_NO_PROPERTY_TYPE, B_TRUE);
		long_usage(CMD_CLEAR, B_TRUE);
		usage(B_FALSE, HELP_PROPS);
		return;
	}
}

void
clear_func(cmd_t *cmd)
{
	if (zone_is_read_only(CMD_CLEAR))
		return;

	assert(cmd != NULL);

	if (global_scope) {
		if (gz_invalid_property(cmd->cmd_res_type)) {
			zerr(gettext("%s is not a valid property for the "
			    "global zone."), pt_to_str(cmd->cmd_res_type));
			saw_error = B_TRUE;
			return;
		}

		clear_global(cmd);
	} else {
		clear_property(cmd);
	}
}

void
select_func(cmd_t *cmd)
{
	int type, err, res;
	uint64_t limit;
	uint64_t tmp;

	if (zone_is_read_only(CMD_SELECT))
		return;

	assert(cmd != NULL);

	if (global_scope) {
		global_scope = B_FALSE;
		resource_scope = cmd->cmd_res_type;
		end_op = CMD_SELECT;
	} else {
		scope_usage(CMD_SELECT);
		return;
	}

	if ((type = cmd->cmd_res_type) == RT_UNKNOWN) {
		long_usage(CMD_SELECT, B_TRUE);
		return;
	}

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (type) {
	case RT_FS:
		if ((err = fill_in_fstab(cmd, &old_fstab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_FS, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_fstab, &in_progress_fstab,
		    sizeof (struct zone_fstab));
		return;
	case RT_NET:
		if ((err = fill_in_nettab(cmd, &old_nettab, B_FALSE))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_NET, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_nettab, &in_progress_nettab,
		    sizeof (struct zone_nettab));
		return;
	case RT_ANET:
		if ((err = fill_in_anettab(cmd, &old_anettab, B_FALSE))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_ANET, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_anettab, &in_progress_anettab,
		    sizeof (struct zone_anettab));
		return;
	case RT_DEVICE:
		if ((err = fill_in_devtab(cmd, &old_devtab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_DEVICE, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_devtab, &in_progress_devtab,
		    sizeof (struct zone_devtab));
		return;
	case RT_RCTL:
		if ((err = fill_in_rctltab(cmd, &old_rctltab, B_FALSE))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_RCTL, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_rctltab, &in_progress_rctltab,
		    sizeof (struct zone_rctltab));
		return;
	case RT_ATTR:
		if ((err = fill_in_attrtab(cmd, &old_attrtab, B_FALSE))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_ATTR, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_attrtab, &in_progress_attrtab,
		    sizeof (struct zone_attrtab));
		return;
	case RT_DATASET:
		if ((err = fill_in_dstab(cmd, &old_dstab, B_FALSE)) != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_DATASET, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_dstab, &in_progress_dstab,
		    sizeof (struct zone_dstab));
		return;
	case RT_DCPU:
		if ((err = zonecfg_lookup_pset(handle, &old_psettab)) != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_DCPU, err, B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_psettab, &in_progress_psettab,
		    sizeof (struct zone_psettab));
		return;
	case RT_PCAP:
		if ((err = zonecfg_get_aliased_rctl(handle, ALIAS_CPUCAP, &tmp))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_PCAP, err, B_TRUE);
			global_scope = B_TRUE;
		}
		return;
	case RT_MCAP:
		/* if none of these exist, there is no resource to select */
		if ((res = zonecfg_lookup_mcap(handle, &old_mcaptab)) != Z_OK &&
		    zonecfg_get_aliased_rctl(handle, ALIAS_MAXSWAP, &limit)
		    != Z_OK &&
		    zonecfg_get_aliased_rctl(handle, ALIAS_MAXLOCKEDMEM, &limit)
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_MCAP, Z_NO_RESOURCE_TYPE,
			    B_TRUE);
			global_scope = B_TRUE;
		}
		if (res == Z_OK)
			bcopy(&old_mcaptab, &in_progress_mcaptab,
			    sizeof (struct zone_mcaptab));
		else
			bzero(&in_progress_mcaptab,
			    sizeof (in_progress_mcaptab));
		return;
	case RT_ADMIN:
		if ((err = fill_in_admintab(cmd, &old_admintab, B_FALSE))
		    != Z_OK) {
			z_cmd_rt_perror(CMD_SELECT, RT_ADMIN, err,
			    B_TRUE);
			global_scope = B_TRUE;
		}
		bcopy(&old_admintab, &in_progress_admintab,
		    sizeof (struct zone_admintab));
		return;
	default:
		zone_perror(rt_to_str(type), Z_NO_RESOURCE_TYPE, B_TRUE);
		long_usage(CMD_SELECT, B_TRUE);
		usage(B_FALSE, HELP_RESOURCES);
		return;
	}
}

/*
 * Network "addresses" can be one of the following forms:
 *	<IPv4 address>
 *	<IPv4 address>/<prefix length>
 *	<IPv6 address>/<prefix length>
 *	<host name>
 *	<host name>/<prefix length>
 * In other words, the "/" followed by a prefix length is allowed but not
 * required for IPv4 addresses and host names, and required for IPv6 addresses.
 * If a prefix length is given, it must be in the allowable range: 0 to 32 for
 * IPv4 addresses and host names, 0 to 128 for IPv6 addresses.
 * Host names must start with an alpha-numeric character, and all subsequent
 * characters must be either alpha-numeric or "-".
 *
 * In some cases, e.g., the nexthop for the defrouter, the context indicates
 * that this is the IPV4_ABITS or IPV6_ABITS netmask, in which case we don't
 * require the /<prefix length> (and should ignore it if provided).
 */

static int
validate_net_address_syntax(char *address, boolean_t ishost)
{
	char *slashp, part1[MAXHOSTNAMELEN];
	struct in6_addr in6;
	struct in_addr in4;
	int prefixlen, i;

	/*
	 * Copy the part before any '/' into part1 or copy the whole
	 * thing if there is no '/'.
	 */
	if ((slashp = strchr(address, '/')) != NULL) {
		*slashp = '\0';
		(void) strlcpy(part1, address, sizeof (part1));
		*slashp = '/';
		prefixlen = atoi(++slashp);
	} else {
		(void) strlcpy(part1, address, sizeof (part1));
	}

	if (ishost && slashp != NULL) {
		zerr(gettext("Warning: prefix length in %s is not required and "
		    "will be ignored. The default host-prefix length "
		    "will be used"), address);
	}


	if (inet_pton(AF_INET6, part1, &in6) == 1) {
		if (ishost) {
			prefixlen = IPV6_ABITS;
		} else if (slashp == NULL) {
			zerr(gettext("%s: IPv6 addresses "
			    "require /prefix-length suffix."), address);
			return (Z_ERR);
		}
		if (prefixlen < 0 || prefixlen > 128) {
			zerr(gettext("%s: IPv6 address "
			    "prefix lengths must be 0 - 128."), address);
			return (Z_ERR);
		}
		return (Z_OK);
	}

	/* At this point, any /prefix must be for IPv4. */
	if (ishost)
		prefixlen = IPV4_ABITS;
	else if (slashp != NULL) {
		if (prefixlen < 0 || prefixlen > 32) {
			zerr(gettext("%s: IPv4 address "
			    "prefix lengths must be 0 - 32."), address);
			return (Z_ERR);
		}
	}

	if (inet_pton(AF_INET, part1, &in4) == 1)
		return (Z_OK);

	/* address may also be a host name */
	if (!isalnum(part1[0])) {
		zerr(gettext("%s: bogus host name or network address syntax"),
		    part1);
		saw_error = B_TRUE;
		usage(B_FALSE, HELP_NETADDR);
		return (Z_ERR);
	}
	for (i = 1; part1[i]; i++)
		if (!isalnum(part1[i]) && part1[i] != '-' && part1[i] != '.') {
			zerr(gettext("%s: bogus host name or "
			    "network address syntax"), part1);
			saw_error = B_TRUE;
			usage(B_FALSE, HELP_NETADDR);
			return (Z_ERR);
		}
	return (Z_OK);
}

static int
validate_net_allowed_address_syntax(char *address)
{
	return (validate_net_address_syntax(address, B_FALSE));
}

static int
validate_net_defrouter_syntax(char *defrouter)
{
	return (validate_net_address_syntax(defrouter, B_TRUE));
}

static int
validate_net_physical_syntax(const char *ifname)
{
	ifspec_t ifnameprop;
	zone_iptype_t iptype;

	if (zonecfg_get_iptype(handle, &iptype) != Z_OK) {
		zerr(gettext("zone configuration has an invalid or nonexistent "
		    "ip-type property"));
		return (Z_ERR);
	}
	switch (iptype) {
	case ZS_SHARED:
		if (ifparse_ifspec(ifname, &ifnameprop) == B_FALSE) {
			zerr(gettext("%s: invalid physical interface name"),
			    ifname);
			return (Z_ERR);
		}
		if (ifnameprop.ifsp_lunvalid) {
			zerr(gettext("%s: LUNs not allowed in physical "
			    "interface names"), ifname);
			return (Z_ERR);
		}
		break;
	case ZS_EXCLUSIVE:
		if (dladm_valid_linkname(ifname) == B_FALSE) {
			if (strchr(ifname, ':') != NULL)
				zerr(gettext("%s: physical interface name "
				    "required; logical interface name not "
				    "allowed"), ifname);
			else
				zerr(gettext("%s: invalid physical interface "
				    "name"), ifname);
			return (Z_ERR);
		}
		break;
	}
	return (Z_OK);
}

static int
validate_link_protection_syntax(char *lp)
{
	uint32_t	ptype;

	if (dladm_str2protect(lp, &ptype) != DLADM_STATUS_OK) {
		zerr(gettext("\"%s\" is not a valid value "
		    "for %s."), lp, pt_to_str(PT_LINK_PROTECTION));
		return (Z_ERR);
	}
	return (Z_OK);
}

/*
 * Check if ip-nospoof is set
 */
static boolean_t
is_ip_nospoof_set(char *lp)
{
	char		*tmplp, *protection, *lasts;
	uint32_t	ptype;

	if (lp == NULL || *lp == '\0')
		return (B_FALSE);

	if ((tmplp = strdup(lp)) == NULL) {
		zerr(gettext("memory allocation failure"));
		return (B_FALSE);
	}

	for (protection = strtok_r(tmplp, ", \t\r\n", &lasts);
	    protection != NULL;
	    protection = strtok_r(NULL, ", \t\r\n", &lasts)) {
		if (protection[0] == '\0')
			continue;
		if (dladm_str2protect(protection, &ptype) != DLADM_STATUS_OK)
			continue;
		if (ptype == MPT_IPNOSPOOF) {
			free(tmplp);
			return (B_TRUE);
		}
	}
	free(tmplp);
	return (B_FALSE);
}

static int
validate_allowed_dhcp_cid_syntax(char *cidstr)
{
	mac_dhcpcid_t	cid;

	if (dladm_str2cid(cidstr, &cid) != DLADM_STATUS_OK) {
		zerr(gettext("\"%s\" is not a valid value "
		    "for %s."), cidstr, pt_to_str(PT_ALLOWED_DHCP_CIDS));
		return (Z_ERR);
	}
	return (Z_OK);
}

static int
validate_mac_address_syntax(char *addr, char *prop_type,
    boolean_t check_type)
{
	vnic_mac_addr_type_t	mac_addr_type;
	uchar_t			*mac_addr = NULL;
	uint_t			mac_len = 0;

	if (check_type && dladm_vnic_str2macaddrtype(addr,
	    &mac_addr_type) == DLADM_STATUS_OK)
		return (Z_OK);

	mac_addr = _link_aton(addr, (int *)&mac_len);
	if (mac_addr == NULL) {
		if (mac_len == (uint_t)-1)
			zerr(gettext("\"%s\" is not a valid %s"),
			    addr, prop_type);
		else
			zerr(gettext("memory allocation failure"));
		return (Z_ERR);
	}
	return (Z_OK);
}

static boolean_t
valid_fs_type(const char *type)
{
	/*
	 * Is this a valid path component?
	 */
	if (strlen(type) + 1 > MAXNAMELEN)
		return (B_FALSE);
	/*
	 * Make sure a bad value for "type" doesn't make
	 * /usr/lib/fs/<type>/mount turn into something else.
	 */
	if (strchr(type, '/') != NULL || type[0] == '\0' ||
	    strcmp(type, ".") == 0 || strcmp(type, "..") == 0)
		return (B_FALSE);
	/*
	 * More detailed verification happens later by zoneadm(1m).
	 */
	return (B_TRUE);
}

/*
 * The file-mac-profile can be set to non-trivial values only if the
 * brand supports it.
 */
static boolean_t
allow_readonly(char *value)
{
	brand_handle_t	bh;
	char		brand[MAXNAMELEN];
	boolean_t	ret;

	if (ZONECFG_READ_WRITE_PROFNAME(value))
		return (B_TRUE);

	if (zonecfg_get_brand(handle, brand, sizeof (brand)) != Z_OK) {
		zerr("%s: %s\n", zone, gettext("could not get zone brand"));
		return (B_FALSE);
	}
	if ((bh = brand_open(brand)) == NULL) {
		zerr("%s: %s\n", zone, gettext("unknown brand."));
		return (B_FALSE);
	}
	ret = brand_allow_readonly(bh);
	brand_close(bh);
	if (!ret)
		zerr(gettext("%s cannot be '%s' when %s is '%s'."),
		    pt_to_str(PT_MAC_PROFILE), value,
		    pt_to_str(PT_BRAND), brand);
	return (ret);
}

static void
set_aliased_rctl(char *alias, int prop_type, char *s)
{
	uint64_t limit;
	int err;
	char tmp[128];

	if (global_zone && strcmp(alias, ALIAS_SHARES) != 0)
		zerr(gettext("WARNING: Setting a global zone resource "
		    "control too low could deny\nservice "
		    "to even the root user; "
		    "this could render the system impossible\n"
		    "to administer.  Please use caution."));

	/* convert memory based properties */
	if (prop_type == PT_MAXSHMMEM) {
		if (!zonecfg_valid_memlimit(s, &limit)) {
			zerr(gettext("A non-negative number with a required "
			    "scale suffix (K, M, G or T) was expected\nhere."));
			saw_error = B_TRUE;
			return;
		}

		(void) snprintf(tmp, sizeof (tmp), "%llu", limit);
		s = tmp;
	}

	if (!zonecfg_aliased_rctl_ok(handle, alias)) {
		zone_perror(pt_to_str(prop_type), Z_ALIAS_DISALLOW, B_FALSE);
		saw_error = B_TRUE;
	} else if (!zonecfg_valid_alias_limit(alias, s, &limit)) {
		zerr(gettext("%s property is out of range."),
		    pt_to_str(prop_type));
		saw_error = B_TRUE;
	} else if ((err = zonecfg_set_aliased_rctl(handle, alias, limit))
	    != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		saw_error = B_TRUE;
	} else {
		need_to_commit = B_TRUE;
	}
}

static int
str_to_bool(int prop, const char *str, boolean_t *out)
{
	if (strcmp(str, "true") == 0) {
		if (out != NULL)
			*out = B_TRUE;
	} else if (strcmp(str, "false") == 0) {
		if (out != NULL)
			*out = B_FALSE;
	} else {
		zerr(gettext("%s value must be '%s' or '%s'."),
		    pt_to_str(prop), "true", "false");
		return (-1);
	}

	return (0);
}

/*
 * Some net/anet properties such as allowed-address, defrouter,
 * link-protection and allowed-dhcp-cids expect either a
 * single value or multiple comma-separated values. The multiple
 * values (and even the single value) can either be specified using
 * the zonecfg's list syntax which requires enclosing square brackets
 * E.g. set allowed-address=[192.168.1.1,192.168.1.2]
 * or without any brackets
 * E.g. set allowed-address=192.168.1.1,192.168.1.2
 * This function handles all the above cases.
 */
static int
set_net_property_simple_or_list(property_value_ptr_t pp, char *buf,
    size_t bufsize, int prop_type, int (*validate)(char *))
{
	list_property_ptr_t	list;
	int			len;
	char			*vals, *val, *lasts;

	/*
	 * zonecfg treats values without any enclosing brackets as
	 * simple types. In our case this could either be a single
	 * value or multiple comma-separated values.
	 */
	if (pp->pv_type == PROP_VAL_SIMPLE) {
		assert(pp->pv_simple != NULL);

		/*
		 * If there is no comma, validate the single
		 * value.
		 */
		if (strchr(pp->pv_simple, ',') == NULL) {
			if (validate(pp->pv_simple) != Z_OK)
				return (Z_ERR);
			(void) strlcpy(buf, pp->pv_simple, bufsize);
			return (Z_OK);
		}


		/*
		 * In case of comma-separated values, we've to first
		 * get the single values and then validate each value.
		 */
		if ((vals = strdup(pp->pv_simple)) == NULL) {
			zerr(gettext("memory allocation failure"));
			return (Z_ERR);
		}
		for (val = strtok_r(vals, ", \t\r\n", &lasts);
		    val != NULL; val = strtok_r(NULL, ", \t\r\n", &lasts)) {
			if (val[0] == '\0')
				continue;
			if (validate(val) != Z_OK) {
				free(vals);
				return (Z_ERR);
			}
		}
		free(vals);
		(void) strlcpy(buf, pp->pv_simple, bufsize);
		return (Z_OK);
	}

	/* We only support simple or list. */
	if (pp->pv_type != PROP_VAL_LIST) {
		zerr(gettext("Invalid value for %s."),
		    pt_to_str(prop_type));
		return (Z_ERR);
	}

	/*
	 * zonecfg treats values with enclosing square brackets as lists and
	 * breaks it down into a linked list of simple values. Here we walk the
	 * linked list and validate each value.
	 */
	bzero(buf, bufsize);
	for (list = pp->pv_list; list != NULL;
	    list = list->lp_next) {
		if (list->lp_simple == NULL || list->lp_simple[0] == '\0')
			continue;
		if (validate(list->lp_simple) != Z_OK) {
			bzero(buf, bufsize);
			return (Z_ERR);
		}
		(void) strlcat(buf, list->lp_simple, bufsize);
		(void) strlcat(buf, ",", bufsize);
	}

	/* Remove the extra comma at the end of the buffer */
	if ((len = strlen(buf)) > 0)
		buf[len - 1] = '\0';
	return (Z_OK);
}

void
set_func(cmd_t *cmd)
{
	char *prop_id, *end;
	int arg, err, res_type, prop_type;
	property_value_ptr_t pp;
	boolean_t autoboot;
	zone_iptype_t iptype;
	boolean_t force_set = B_FALSE;
	size_t physmem_size = sizeof (in_progress_mcaptab.zone_physmem_cap);
	uint64_t mem_cap, mem_limit, maxbw;
	float cap;
	char *unitp;
	struct zone_psettab tmp_psettab;
	boolean_t arg_err = B_FALSE;
	long num;
	mac_priority_level_t priority;
	boolean_t config_addr;

	if (zone_is_read_only(CMD_SET))
		return;

	assert(cmd != NULL);

	optind = opterr = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "F")) != EOF) {
		switch (arg) {
		case 'F':
			force_set = B_TRUE;
			break;
		default:
			if (optopt == '?')
				longer_usage(CMD_SET);
			else
				short_usage(CMD_SET);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	prop_type = cmd->cmd_prop_name[0];
	if (global_scope) {
		if (gz_invalid_property(prop_type)) {
			zerr(gettext("%s is not a valid property for the "
			    "global zone."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}

		if (prop_type == PT_ZONENAME) {
			res_type = RT_ZONENAME;
		} else if (prop_type == PT_ZONEPATH) {
			res_type = RT_ZONEPATH;
		} else if (prop_type == PT_AUTOBOOT) {
			res_type = RT_AUTOBOOT;
		} else if (prop_type == PT_BRAND) {
			res_type = RT_BRAND;
		} else if (prop_type == PT_POOL) {
			res_type = RT_POOL;
		} else if (prop_type == PT_LIMITPRIV) {
			res_type = RT_LIMITPRIV;
		} else if (prop_type == PT_BOOTARGS) {
			res_type = RT_BOOTARGS;
		} else if (prop_type == PT_SCHED) {
			res_type = RT_SCHED;
		} else if (prop_type == PT_IPTYPE) {
			res_type = RT_IPTYPE;
		} else if (prop_type == PT_MAXLWPS) {
			res_type = RT_MAXLWPS;
		} else if (prop_type == PT_MAXPROCS) {
			res_type = RT_MAXPROCS;
		} else if (prop_type == PT_MAXSHMMEM) {
			res_type = RT_MAXSHMMEM;
		} else if (prop_type == PT_MAXSHMIDS) {
			res_type = RT_MAXSHMIDS;
		} else if (prop_type == PT_MAXMSGIDS) {
			res_type = RT_MAXMSGIDS;
		} else if (prop_type == PT_MAXSEMIDS) {
			res_type = RT_MAXSEMIDS;
		} else if (prop_type == PT_SHARES) {
			res_type = RT_SHARES;
		} else if (prop_type == PT_HOSTID) {
			res_type = RT_HOSTID;
		} else if (prop_type == PT_FS_ALLOWED) {
			res_type = RT_FS_ALLOWED;
		} else if (prop_type == PT_MAC_PROFILE) {
			res_type = RT_MAC_PROFILE;
		} else {
			zerr(gettext("Cannot set a resource-specific property "
			    "from the global scope."));
			saw_error = B_TRUE;
			return;
		}
	} else {
		res_type = resource_scope;
	}

	if (force_set) {
		if (res_type != RT_ZONEPATH) {
			zerr(gettext("Only zonepath setting can be forced."));
			saw_error = B_TRUE;
			return;
		}
		if (!zonecfg_in_alt_root()) {
			zerr(gettext("Zonepath is changeable only in an "
			    "alternate root."));
			saw_error = B_TRUE;
			return;
		}
	}

	pp = cmd->cmd_property_ptr[0];
	/*
	 * A nasty expression but not that complicated:
	 * 1. fs options are simple or list (tested below)
	 * 2. rctl value's are complex or list (tested below)
	 * 3. Some net and anet properties are simple or list (tested below)
	 * Anything else should be simple.
	 */
	if (!(res_type == RT_FS && prop_type == PT_OPTIONS) &&
	    !(res_type == RT_RCTL && prop_type == PT_VALUE) &&
	    !((res_type == RT_NET || res_type == RT_ANET) &&
	    (prop_type == PT_ALLOWED_ADDRESS ||
	    prop_type == PT_DEFROUTER ||
	    prop_type == PT_ALLOWED_DHCP_CIDS ||
	    prop_type == PT_LINK_PROTECTION)) &&
	    (pp->pv_type != PROP_VAL_SIMPLE ||
	    (prop_id = pp->pv_simple) == NULL)) {
		zerr(gettext("A %s value was expected here."),
		    pvt_to_str(PROP_VAL_SIMPLE));
		saw_error = B_TRUE;
		return;
	}
	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_SET, B_TRUE);
		return;
	}

	/*
	 * Special case: the user can change the zone name prior to 'create';
	 * if the zone already exists, we fall through letting initialize()
	 * and the rest of the logic run.
	 */
	if (res_type == RT_ZONENAME && got_handle == B_FALSE &&
	    !state_atleast(ZONE_STATE_CONFIGURED)) {
		if ((err = zonecfg_validate_zonename(prop_id)) != Z_OK) {
			zone_perror(prop_id, err, B_TRUE);
			usage(B_FALSE, HELP_SYNTAX);
			return;
		}
		(void) strlcpy(zone, prop_id, sizeof (zone));
		return;
	}

	if (initialize(B_TRUE) != Z_OK)
		return;

	switch (res_type) {
	case RT_ZONENAME:
		if ((err = zonecfg_set_name(handle, prop_id)) != Z_OK) {
			/*
			 * Use prop_id instead of 'zone' here, since we're
			 * reporting a problem about the *new* zonename.
			 */
			zone_perror(prop_id, err, B_TRUE);
			usage(B_FALSE, HELP_SYNTAX);
		} else {
			need_to_commit = B_TRUE;
			(void) strlcpy(zone, prop_id, sizeof (zone));
		}
		return;
	case RT_ZONEPATH:
		if (!force_set && state_atleast(ZONE_STATE_INSTALLED)) {
			zerr(gettext("Zone %s already installed; %s %s not "
			    "allowed."), zone, cmd_to_str(CMD_SET),
			    rt_to_str(RT_ZONEPATH));
			return;
		}
		if (validate_zonepath_syntax(prop_id) != Z_OK) {
			saw_error = B_TRUE;
			return;
		}
		if ((err = zonecfg_set_zonepath(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_BRAND:
		if (state_atleast(ZONE_STATE_INSTALLED)) {
			zerr(gettext("Zone %s already installed; %s %s not "
			    "allowed."), zone, cmd_to_str(CMD_SET),
			    rt_to_str(RT_BRAND));
			return;
		}
		if ((err = zonecfg_set_brand(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_AUTOBOOT:
		if (str_to_bool(PT_AUTOBOOT, prop_id, &autoboot)) {
			saw_error = B_TRUE;
			return;
		}
		if ((err = zonecfg_set_autoboot(handle, autoboot)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_MAC_PROFILE:
		if (!allow_readonly(prop_id)) {
			saw_error = B_TRUE;
			return;
		}
		if ((err = zonecfg_set_mac_profile(handle, prop_id)) !=
		    Z_OK) {
			zone_perror(zone, err, B_TRUE);
		} else {
			need_to_commit = B_TRUE;
		}
		return;
	case RT_POOL:
		/* don't allow use of the reserved temporary pool names */
		if (strncmp("SUNW", prop_id, 4) == 0) {
			zerr(gettext("pool names starting with SUNW are "
			    "reserved."));
			saw_error = B_TRUE;
			return;
		}

		/* can't set pool if dedicated-cpu exists */
		if (zonecfg_lookup_pset(handle, &tmp_psettab) == Z_OK) {
			zerr(gettext("The %s resource already exists.  "
			    "A persistent pool is incompatible\nwith the %s "
			    "resource."), rt_to_str(RT_DCPU),
			    rt_to_str(RT_DCPU));
			saw_error = B_TRUE;
			return;
		}

		if ((err = zonecfg_set_pool(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_LIMITPRIV:
		if ((err = zonecfg_set_limitpriv(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_BOOTARGS:
		if ((err = zonecfg_set_bootargs(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_SCHED:
		if ((err = zonecfg_set_sched(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_IPTYPE:
		if (strcmp(prop_id, "shared") == 0) {
			iptype = ZS_SHARED;
		} else if (strcmp(prop_id, "exclusive") == 0) {
			iptype = ZS_EXCLUSIVE;
		} else {
			zerr(gettext("%s value must be '%s' or '%s'."),
			    pt_to_str(PT_IPTYPE), "shared", "exclusive");
			saw_error = B_TRUE;
			return;
		}
		if ((err = zonecfg_set_iptype(handle, iptype)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		ip_type_set = B_TRUE;
		return;
	case RT_MAXLWPS:
		set_aliased_rctl(ALIAS_MAXLWPS, prop_type, prop_id);
		return;
	case RT_MAXPROCS:
		set_aliased_rctl(ALIAS_MAXPROCS, prop_type, prop_id);
		return;
	case RT_MAXSHMMEM:
		set_aliased_rctl(ALIAS_MAXSHMMEM, prop_type, prop_id);
		return;
	case RT_MAXSHMIDS:
		set_aliased_rctl(ALIAS_MAXSHMIDS, prop_type, prop_id);
		return;
	case RT_MAXMSGIDS:
		set_aliased_rctl(ALIAS_MAXMSGIDS, prop_type, prop_id);
		return;
	case RT_MAXSEMIDS:
		set_aliased_rctl(ALIAS_MAXSEMIDS, prop_type, prop_id);
		return;
	case RT_SHARES:
		set_aliased_rctl(ALIAS_SHARES, prop_type, prop_id);
		return;
	case RT_HOSTID:
		if ((err = zonecfg_set_hostid(handle, prop_id)) != Z_OK) {
			if (err == Z_TOO_BIG) {
				zerr(gettext("hostid string is too large: %s"),
				    prop_id);
				saw_error = B_TRUE;
			} else {
				zone_perror(pt_to_str(prop_type), err, B_TRUE);
			}
			return;
		}
		need_to_commit = B_TRUE;
		return;
	case RT_FS_ALLOWED:
		if ((err = zonecfg_set_fs_allowed(handle, prop_id)) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_FS:
		switch (prop_type) {
		case PT_DIR:
			(void) strlcpy(in_progress_fstab.zone_fs_dir, prop_id,
			    sizeof (in_progress_fstab.zone_fs_dir));
			return;
		case PT_SPECIAL:
			(void) strlcpy(in_progress_fstab.zone_fs_special,
			    prop_id,
			    sizeof (in_progress_fstab.zone_fs_special));
			return;
		case PT_RAW:
			(void) strlcpy(in_progress_fstab.zone_fs_raw,
			    prop_id, sizeof (in_progress_fstab.zone_fs_raw));
			return;
		case PT_TYPE:
			if (!valid_fs_type(prop_id)) {
				zerr(gettext("\"%s\" is not a valid %s."),
				    prop_id, pt_to_str(PT_TYPE));
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(in_progress_fstab.zone_fs_type, prop_id,
			    sizeof (in_progress_fstab.zone_fs_type));
			return;
		case PT_OPTIONS:
			if (pp->pv_type != PROP_VAL_SIMPLE &&
			    pp->pv_type != PROP_VAL_LIST) {
				zerr(gettext("A %s or %s value was expected "
				    "here."), pvt_to_str(PROP_VAL_SIMPLE),
				    pvt_to_str(PROP_VAL_LIST));
				saw_error = B_TRUE;
				return;
			}
			zonecfg_free_fs_option_list(
			    in_progress_fstab.zone_fs_options);
			in_progress_fstab.zone_fs_options = NULL;
			if (!(pp->pv_type == PROP_VAL_LIST &&
			    pp->pv_list == NULL))
				add_property(cmd);
			return;
		default:
			break;
		}
		zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE, B_TRUE);
		long_usage(CMD_SET, B_TRUE);
		usage(B_FALSE, HELP_PROPS);
		return;
	case RT_NET:
		ipn = &in_progress_nettab;
		switch (prop_type) {
		case PT_ADDRESS:
			if (validate_net_address_syntax(prop_id, B_FALSE)
			    != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipn->zone_net_address,
			    prop_id, sizeof (ipn->zone_net_address));
			break;
		case PT_ALLOWED_ADDRESS:
			if (set_net_property_simple_or_list(pp,
			    ipn->zone_net_allowed_addr,
			    sizeof (ipn->zone_net_allowed_addr),
			    PT_ALLOWED_ADDRESS,
			    validate_net_allowed_address_syntax) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			break;
		case PT_CONFIGURE_ALLOWED_ADDRESS:
			if (str_to_bool(PT_CONFIGURE_ALLOWED_ADDRESS, prop_id,
			    &config_addr) != 0) {
				saw_error = B_TRUE;
				return;
			}
			ipn->zone_net_configure_allowed_addr = config_addr;
			break;
		case PT_PHYSICAL:
			if (validate_net_physical_syntax(prop_id) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipn->zone_net_physical,
			    prop_id, sizeof (ipn->zone_net_physical));
			break;
		case PT_DEFROUTER:
			if (set_net_property_simple_or_list(pp,
			    ipn->zone_net_defrouter,
			    sizeof (ipn->zone_net_defrouter),
			    PT_DEFROUTER,
			    validate_net_defrouter_syntax) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			break;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		return;
	case RT_ANET:
		ipa = &in_progress_anettab;
		switch (prop_type) {
		case PT_LINKNAME:
			/*
			 * Validation done in end_func() to prevent
			 * end_func() from choosing a default value.
			 */
			(void) strlcpy(ipa->zone_anet_linkname,
			    prop_id,
			    sizeof (ipa->zone_anet_linkname));
			break;
		case PT_LOWER_LINK:
			/*
			 * Validation done in end_func() to prevent
			 * end_func() from choosing a default value.
			 */
			(void) strlcpy(
			    ipa->zone_anet_lower_link,
			    prop_id,
			    sizeof (ipa->zone_anet_lower_link));
			break;
		case PT_ALLOWED_ADDRESS:
			if (set_net_property_simple_or_list(pp,
			    ipa->zone_anet_allowed_addr,
			    sizeof (ipa->zone_anet_allowed_addr),
			    PT_ALLOWED_ADDRESS,
			    validate_net_allowed_address_syntax) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			break;
		case PT_CONFIGURE_ALLOWED_ADDRESS:
			if (str_to_bool(PT_CONFIGURE_ALLOWED_ADDRESS, prop_id,
			    &config_addr) != 0) {
				saw_error = B_TRUE;
				return;
			}
			ipa->zone_anet_configure_allowed_addr = config_addr;
			break;
		case PT_DEFROUTER:
			if (set_net_property_simple_or_list(pp,
			    ipa->zone_anet_defrouter,
			    sizeof (ipa->zone_anet_defrouter),
			    PT_DEFROUTER,
			    validate_net_defrouter_syntax) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			break;
		case PT_ALLOWED_DHCP_CIDS:
			if (set_net_property_simple_or_list(pp,
			    ipa->zone_anet_allowed_dhcp_cids,
			    sizeof (ipa->zone_anet_allowed_dhcp_cids),
			    PT_ALLOWED_DHCP_CIDS,
			    validate_allowed_dhcp_cid_syntax) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			break;
		case PT_LINK_PROTECTION:
			if (set_net_property_simple_or_list(pp,
			    ipa->zone_anet_link_protection,
			    sizeof (ipa->zone_anet_link_protection),
			    PT_LINK_PROTECTION,
			    validate_link_protection_syntax) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			break;
		case PT_MAC_ADDRESS:
			if (validate_mac_address_syntax(prop_id,
			    pt_to_str(PT_MAC_ADDRESS), B_TRUE) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(
			    ipa->zone_anet_mac_addr,
			    prop_id,
			    sizeof (ipa->zone_anet_mac_addr));
			break;
		case PT_AUTO_MAC_ADDRESS:
			if (validate_mac_address_syntax(prop_id,
			    pt_to_str(PT_MAC_ADDRESS), B_TRUE) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(
			    ipa->zone_anet_auto_mac_addr,
			    prop_id,
			    sizeof (ipa->zone_anet_auto_mac_addr));
			break;
		case PT_MAC_PREFIX:
			if (validate_mac_address_syntax(prop_id,
			    pt_to_str(PT_MAC_PREFIX), B_FALSE) != Z_OK) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(
			    ipa->zone_anet_mac_prefix,
			    prop_id,
			    sizeof (ipa->zone_anet_mac_prefix));
			break;
		case PT_MAC_SLOT:
			errno = 0;
			num = strtol(prop_id, &end, 0);
			if (errno != 0 || *end != '\0' || num < 0) {
				zerr(
				    gettext("\"%s\" is not a valid %s."),
				    prop_id, pt_to_str(PT_MAC_SLOT));
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipa->zone_anet_mac_slot,
			    prop_id,
			    sizeof (ipa->zone_anet_mac_slot));
			break;
		case PT_VLAN_ID:
			errno = 0;
			num = strtol(prop_id, &end, 0);
			if (errno != 0 || *end != '\0' || num < 1) {
				zerr(
				    gettext("\"%s\" is not a valid %s."),
				    prop_id, pt_to_str(PT_VLAN_ID));
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipa->zone_anet_vlan_id,
			    prop_id,
			    sizeof (ipa->zone_anet_vlan_id));
			break;
		case PT_PRIORITY:
			if (dladm_str2pri(prop_id, &priority)
			    != DLADM_STATUS_OK) {
				zerr(
				    gettext("\"%s\" is not a valid %s."),
				    prop_id, pt_to_str(PT_PRIORITY));
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipa->zone_anet_priority,
			    prop_id,
			    sizeof (ipa->zone_anet_priority));
			break;
		case PT_RXRINGS:
			(void) strlcpy(ipa->zone_anet_rxrings,
			    prop_id,
			    sizeof (ipa->zone_anet_rxrings));
			break;
		case PT_TXRINGS:
			(void) strlcpy(ipa->zone_anet_txrings,
			    prop_id,
			    sizeof (ipa->zone_anet_txrings));
			break;
		case PT_MTU:
			errno = 0;
			num = strtol(prop_id, &end, 0);
			if (errno != 0 || *end != '\0' || num < 1) {
				zerr(
				    gettext("\"%s\" is not a valid %s."),
				    prop_id, pt_to_str(PT_MTU));
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipa->zone_anet_mtu,
			    prop_id,
			    sizeof (ipa->zone_anet_mtu));
			break;
		case PT_MAXBW:
			errno = 0;
			if (dladm_str2bw(prop_id, &maxbw) != DLADM_STATUS_OK) {
				zerr(
				    gettext("\"%s\" is not a valid %s."),
				    prop_id, pt_to_str(PT_MAXBW));
				saw_error = B_TRUE;
				return;
			}
			if (maxbw < MRP_MAXBW_MINVAL && maxbw != 0) {
				zerr(
				    gettext("%s must be >= %u bits per second"),
				    pt_to_str(PT_MAXBW), MRP_MAXBW_MINVAL);
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(ipa->zone_anet_maxbw,
			    prop_id,
			    sizeof (ipa->zone_anet_maxbw));
			break;
		case PT_RXFANOUT:
			(void) strlcpy(ipa->zone_anet_rxfanout,
			    prop_id,
			    sizeof (ipa->zone_anet_rxfanout));
			break;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		return;
	case RT_DEVICE:
		switch (prop_type) {
		case PT_MATCH:
			(void) strlcpy(in_progress_devtab.zone_dev_match,
			    prop_id,
			    sizeof (in_progress_devtab.zone_dev_match));
			break;
		case PT_ALLOW_PARTITION:
			if (str_to_bool(PT_ALLOW_PARTITION, prop_id, NULL)) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(in_progress_devtab.zone_dev_partition,
			    prop_id,
			    sizeof (in_progress_devtab.zone_dev_partition));
			break;
		case PT_ALLOW_RAW_IO:
			if (str_to_bool(PT_ALLOW_RAW_IO, prop_id, NULL)) {
				saw_error = B_TRUE;
				return;
			}
			(void) strlcpy(in_progress_devtab.zone_dev_raw_io,
			    prop_id,
			    sizeof (in_progress_devtab.zone_dev_raw_io));
			break;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		return;
	case RT_RCTL:
		switch (prop_type) {
		case PT_NAME:
			if (!zonecfg_valid_rctlname(prop_id)) {
				zerr(gettext("'%s' is not a valid zone %s "
				    "name."), prop_id, rt_to_str(RT_RCTL));
				return;
			}
			(void) strlcpy(in_progress_rctltab.zone_rctl_name,
			    prop_id,
			    sizeof (in_progress_rctltab.zone_rctl_name));
			break;
		case PT_VALUE:
			if (pp->pv_type != PROP_VAL_COMPLEX &&
			    pp->pv_type != PROP_VAL_LIST) {
				zerr(gettext("A %s or %s value was expected "
				    "here."), pvt_to_str(PROP_VAL_COMPLEX),
				    pvt_to_str(PROP_VAL_LIST));
				saw_error = B_TRUE;
				return;
			}
			zonecfg_free_rctl_value_list(
			    in_progress_rctltab.zone_rctl_valptr);
			in_progress_rctltab.zone_rctl_valptr = NULL;
			if (!(pp->pv_type == PROP_VAL_LIST &&
			    pp->pv_list == NULL))
				add_property(cmd);
			break;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		return;
	case RT_ATTR:
		switch (prop_type) {
		case PT_NAME:
			(void) strlcpy(in_progress_attrtab.zone_attr_name,
			    prop_id,
			    sizeof (in_progress_attrtab.zone_attr_name));
			break;
		case PT_TYPE:
			(void) strlcpy(in_progress_attrtab.zone_attr_type,
			    prop_id,
			    sizeof (in_progress_attrtab.zone_attr_type));
			break;
		case PT_VALUE:
			(void) strlcpy(in_progress_attrtab.zone_attr_value,
			    prop_id,
			    sizeof (in_progress_attrtab.zone_attr_value));
			break;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		return;
	case RT_DATASET:
		switch (prop_type) {
		case PT_NAME:
			(void) strlcpy(in_progress_dstab.zone_dataset_name,
			    prop_id,
			    sizeof (in_progress_dstab.zone_dataset_name));
			return;
		case PT_ALIAS:
			(void) strlcpy(in_progress_dstab.zone_dataset_alias,
			    prop_id,
			    sizeof (in_progress_dstab.zone_dataset_alias));
			return;
		default:
			break;
		}
		zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE, B_TRUE);
		long_usage(CMD_SET, B_TRUE);
		usage(B_FALSE, HELP_PROPS);
		return;
	case RT_DCPU:
		switch (prop_type) {
		char *lowp, *highp;

		case PT_NCPUS:
			lowp = prop_id;
			if ((highp = strchr(prop_id, '-')) != NULL)
				*highp++ = '\0';
			else
				highp = lowp;

			/* Make sure the input makes sense. */
			if (!zonecfg_valid_ncpus(lowp, highp)) {
				zerr(gettext("%s property is out of range."),
				    pt_to_str(PT_NCPUS));
				saw_error = B_TRUE;
				return;
			}

			(void) strlcpy(
			    in_progress_psettab.zone_ncpu_min, lowp,
			    sizeof (in_progress_psettab.zone_ncpu_min));
			(void) strlcpy(
			    in_progress_psettab.zone_ncpu_max, highp,
			    sizeof (in_progress_psettab.zone_ncpu_max));
			return;
		case PT_IMPORTANCE:
			/* Make sure the value makes sense. */
			if (!zonecfg_valid_importance(prop_id)) {
				zerr(gettext("%s property is out of range."),
				    pt_to_str(PT_IMPORTANCE));
				saw_error = B_TRUE;
				return;
			}

			(void) strlcpy(in_progress_psettab.zone_importance,
			    prop_id,
			    sizeof (in_progress_psettab.zone_importance));
			return;
		default:
			break;
		}
		zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE, B_TRUE);
		long_usage(CMD_SET, B_TRUE);
		usage(B_FALSE, HELP_PROPS);
		return;
	case RT_PCAP:
		if (prop_type != PT_NCPUS) {
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}

		/*
		 * We already checked that an rctl alias is allowed in
		 * the add_resource() function.
		 */

		if ((cap = strtof(prop_id, &unitp)) <= 0 || *unitp != '\0' ||
		    (int)(cap * 100) < 1) {
			zerr(gettext("%s property is out of range."),
			    pt_to_str(PT_NCPUS));
			saw_error = B_TRUE;
			return;
		}

		if ((err = zonecfg_set_aliased_rctl(handle, ALIAS_CPUCAP,
		    (int)(cap * 100))) != Z_OK)
			zone_perror(zone, err, B_TRUE);
		else
			need_to_commit = B_TRUE;
		return;
	case RT_MCAP:
		switch (prop_type) {
		case PT_PHYSICAL:
			if (!zonecfg_valid_memlimit(prop_id, &mem_cap)) {
				zerr(gettext("A positive number with a "
				    "required scale suffix (K, M, G or T) was "
				    "expected here."));
				saw_error = B_TRUE;
			} else if (mem_cap < ONE_MB) {
				zerr(gettext("%s value is too small.  It must "
				    "be at least 1M."), pt_to_str(PT_PHYSICAL));
				saw_error = B_TRUE;
			} else {
				snprintf(in_progress_mcaptab.zone_physmem_cap,
				    physmem_size, "%llu", mem_cap);
			}
			break;
		case PT_SWAP:
			/*
			 * We have to check if an rctl is allowed here since
			 * there might already be a rctl defined that blocks
			 * the alias.
			 */
			if (!zonecfg_aliased_rctl_ok(handle, ALIAS_MAXSWAP)) {
				zone_perror(pt_to_str(PT_MAXSWAP),
				    Z_ALIAS_DISALLOW, B_FALSE);
				saw_error = B_TRUE;
				return;
			}

			if (global_zone)
				mem_limit = ONE_MB * 100;
			else
				mem_limit = ONE_MB * 50;

			if (!zonecfg_valid_memlimit(prop_id, &mem_cap)) {
				zerr(gettext("A positive number with a "
				    "required scale suffix (K, M, G or T) was "
				    "expected here."));
				saw_error = B_TRUE;
			} else if (mem_cap < mem_limit) {
				char buf[128];

				(void) snprintf(buf, sizeof (buf), "%llu",
				    mem_limit);
				bytes_to_units(buf, buf, sizeof (buf));
				zerr(gettext("%s value is too small.  It must "
				    "be at least %s."), pt_to_str(PT_SWAP),
				    buf);
				saw_error = B_TRUE;
			} else {
				if ((err = zonecfg_set_aliased_rctl(handle,
				    ALIAS_MAXSWAP, mem_cap)) != Z_OK)
					zone_perror(zone, err, B_TRUE);
				else
					need_to_commit = B_TRUE;
			}
			break;
		case PT_LOCKED:
			/*
			 * We have to check if an rctl is allowed here since
			 * there might already be a rctl defined that blocks
			 * the alias.
			 */
			if (!zonecfg_aliased_rctl_ok(handle,
			    ALIAS_MAXLOCKEDMEM)) {
				zone_perror(pt_to_str(PT_LOCKED),
				    Z_ALIAS_DISALLOW, B_FALSE);
				saw_error = B_TRUE;
				return;
			}

			if (!zonecfg_valid_memlimit(prop_id, &mem_cap)) {
				zerr(gettext("A non-negative number with a "
				    "required scale suffix (K, M, G or T) was "
				    "expected\nhere."));
				saw_error = B_TRUE;
			} else {
				if ((err = zonecfg_set_aliased_rctl(handle,
				    ALIAS_MAXLOCKEDMEM, mem_cap)) != Z_OK)
					zone_perror(zone, err, B_TRUE);
				else
					need_to_commit = B_TRUE;
			}
			break;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
		return;
	case RT_ADMIN:
		switch (prop_type) {
		case PT_USER:
			(void) strlcpy(in_progress_admintab.zone_admin_user,
			    prop_id,
			    sizeof (in_progress_admintab.zone_admin_user));
			return;
		case PT_AUTHS:
			(void) strlcpy(in_progress_admintab.zone_admin_auths,
			    prop_id,
			    sizeof (in_progress_admintab.zone_admin_auths));
			return;
		default:
			zone_perror(pt_to_str(prop_type), Z_NO_PROPERTY_TYPE,
			    B_TRUE);
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, HELP_PROPS);
			return;
		}
	default:
		zone_perror(rt_to_str(res_type), Z_NO_RESOURCE_TYPE, B_TRUE);
		long_usage(CMD_SET, B_TRUE);
		usage(B_FALSE, HELP_RESOURCES);
		return;
	}
}

static void
output_prop(FILE *fp, int pnum, char *pval, boolean_t print_notspec)
{
	char *qstr;

	if (*pval != '\0') {
		qstr = quoteit(pval);
		if (pnum == PT_SWAP || pnum == PT_LOCKED)
			(void) fprintf(fp, "\t[%s: %s]\n", pt_to_str(pnum),
			    qstr);
		else
			(void) fprintf(fp, "\t%s: %s\n", pt_to_str(pnum), qstr);
		free(qstr);
	} else if (print_notspec)
		(void) fprintf(fp, gettext("\t%s not specified\n"),
		    pt_to_str(pnum));
}

static void
info_zonename(zone_dochandle_t handle, FILE *fp)
{
	char zonename[ZONENAME_MAX];

	if (zonecfg_get_name(handle, zonename, sizeof (zonename)) == Z_OK)
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_ZONENAME),
		    zonename);
	else
		(void) fprintf(fp, gettext("%s not specified\n"),
		    pt_to_str(PT_ZONENAME));
}

static void
info_zonepath(zone_dochandle_t handle, FILE *fp)
{
	char zonepath[MAXPATHLEN];

	if (zonecfg_get_zonepath(handle, zonepath, sizeof (zonepath)) == Z_OK)
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_ZONEPATH),
		    zonepath);
	else {
		(void) fprintf(fp, gettext("%s not specified\n"),
		    pt_to_str(PT_ZONEPATH));
	}
}

static void
info_brand(zone_dochandle_t handle, FILE *fp)
{
	char brand[MAXNAMELEN];

	if (zonecfg_get_brand(handle, brand, sizeof (brand)) == Z_OK)
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_BRAND),
		    brand);
	else
		(void) fprintf(fp, "%s %s\n", pt_to_str(PT_BRAND),
		    gettext("not specified"));
}

static void
info_autoboot(zone_dochandle_t handle, FILE *fp)
{
	boolean_t autoboot;
	int err;

	if ((err = zonecfg_get_autoboot(handle, &autoboot)) == Z_OK)
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_AUTOBOOT),
		    autoboot ? "true" : "false");
	else
		zone_perror(zone, err, B_TRUE);
}

static void
info_mac_profile(zone_dochandle_t handle, FILE *fp)
{
	char macprof[MAXNAMELEN];
	int err;

	if ((err = zonecfg_get_mac_profile(handle, macprof,
	    sizeof (macprof))) == Z_OK) {
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_MAC_PROFILE),
		    macprof);
	} else
		zone_perror(zone, err, B_TRUE);
}

static void
info_pool(zone_dochandle_t handle, FILE *fp)
{
	char pool[MAXNAMELEN];
	int err;

	if ((err = zonecfg_get_pool(handle, pool, sizeof (pool))) == Z_OK)
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_POOL), pool);
	else
		zone_perror(zone, err, B_TRUE);
}

static void
info_limitpriv(zone_dochandle_t handle, FILE *fp)
{
	char *limitpriv;
	int err;

	if ((err = zonecfg_get_limitpriv(handle, &limitpriv)) == Z_OK) {
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_LIMITPRIV),
		    limitpriv);
		free(limitpriv);
	} else {
		zone_perror(zone, err, B_TRUE);
	}
}

static void
info_bootargs(zone_dochandle_t handle, FILE *fp)
{
	char bootargs[BOOTARGS_MAX];
	int err;

	if ((err = zonecfg_get_bootargs(handle, bootargs,
	    sizeof (bootargs))) == Z_OK) {
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_BOOTARGS),
		    bootargs);
	} else {
		zone_perror(zone, err, B_TRUE);
	}
}

static void
info_sched(zone_dochandle_t handle, FILE *fp)
{
	char sched[MAXNAMELEN];
	int err;

	if ((err = zonecfg_get_sched_class(handle, sched, sizeof (sched)))
	    == Z_OK) {
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_SCHED), sched);
	} else {
		zone_perror(zone, err, B_TRUE);
	}
}

static void
info_iptype(zone_dochandle_t handle, FILE *fp)
{
	zone_iptype_t iptype;
	int err;

	if ((err = zonecfg_get_iptype(handle, &iptype)) == Z_OK) {
		switch (iptype) {
		case ZS_SHARED:
			(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_IPTYPE),
			    "shared");
			break;
		case ZS_EXCLUSIVE:
			(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_IPTYPE),
			    "exclusive");
			break;
		}
	} else {
		zone_perror(zone, err, B_TRUE);
	}
}

static void
info_hostid(zone_dochandle_t handle, FILE *fp)
{
	char hostidp[HW_HOSTID_LEN];
	int err;

	if ((err = zonecfg_get_hostid(handle, hostidp,
	    sizeof (hostidp))) == Z_OK) {
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_HOSTID), hostidp);
	} else if (err == Z_BAD_PROPERTY) {
		(void) fprintf(fp, "%s: \n", pt_to_str(PT_HOSTID));
	} else {
		zone_perror(zone, err, B_TRUE);
	}
}

static void
info_fs_allowed(zone_dochandle_t handle, FILE *fp)
{
	char fsallowedp[ZONE_FS_ALLOWED_MAX];
	int err;

	if ((err = zonecfg_get_fs_allowed(handle, fsallowedp,
	    sizeof (fsallowedp))) == Z_OK) {
		(void) fprintf(fp, "%s: %s\n", pt_to_str(PT_FS_ALLOWED),
		    fsallowedp);
	} else if (err == Z_BAD_PROPERTY) {
		(void) fprintf(fp, "%s: \n", pt_to_str(PT_FS_ALLOWED));
	} else {
		zone_perror(zone, err, B_TRUE);
	}
}

static void
output_fs(FILE *fp, struct zone_fstab *fstab)
{
	zone_fsopt_t *this;

	(void) fprintf(fp, "%s:\n", rt_to_str(RT_FS));
	output_prop(fp, PT_DIR, fstab->zone_fs_dir, B_TRUE);
	output_prop(fp, PT_SPECIAL, fstab->zone_fs_special, B_TRUE);
	output_prop(fp, PT_RAW, fstab->zone_fs_raw, B_TRUE);
	output_prop(fp, PT_TYPE, fstab->zone_fs_type, B_TRUE);
	(void) fprintf(fp, "\t%s: [", pt_to_str(PT_OPTIONS));
	for (this = fstab->zone_fs_options; this != NULL;
	    this = this->zone_fsopt_next) {
		if (strchr(this->zone_fsopt_opt, '='))
			(void) fprintf(fp, "\"%s\"", this->zone_fsopt_opt);
		else
			(void) fprintf(fp, "%s", this->zone_fsopt_opt);
		if (this->zone_fsopt_next != NULL)
			(void) fprintf(fp, ",");
	}
	(void) fprintf(fp, "]\n");
}

static void
info_fs(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_fstab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setfsent(handle) != Z_OK)
		return;
	while (zonecfg_getfsent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_fs(fp, &lookup);
			goto loopend;
		}
		if (fill_in_fstab(cmd, &user, B_TRUE) != Z_OK)
			goto loopend;
		if (strlen(user.zone_fs_dir) > 0 &&
		    strcmp(user.zone_fs_dir, lookup.zone_fs_dir) != 0)
			goto loopend;	/* no match */
		if (strlen(user.zone_fs_special) > 0 &&
		    strcmp(user.zone_fs_special, lookup.zone_fs_special) != 0)
			goto loopend;	/* no match */
		if (strlen(user.zone_fs_type) > 0 &&
		    strcmp(user.zone_fs_type, lookup.zone_fs_type) != 0)
			goto loopend;	/* no match */
		output_fs(fp, &lookup);
		output = B_TRUE;
loopend:
		zonecfg_free_fs_option_list(lookup.zone_fs_options);
	}
	(void) zonecfg_endfsent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_FS));
}

static void
output_net(FILE *fp, struct zone_nettab *nettab)
{
	(void) fprintf(fp, "%s:\n", rt_to_str(RT_NET));
	output_prop(fp, PT_ADDRESS, nettab->zone_net_address, B_TRUE);
	output_prop(fp, PT_ALLOWED_ADDRESS,
	    nettab->zone_net_allowed_addr, B_TRUE);
	output_prop(fp, PT_CONFIGURE_ALLOWED_ADDRESS,
	    nettab->zone_net_configure_allowed_addr ? "true" : "false", B_TRUE);
	output_prop(fp, PT_PHYSICAL, nettab->zone_net_physical, B_TRUE);
	output_prop(fp, PT_DEFROUTER, nettab->zone_net_defrouter, B_TRUE);
}

static void
info_net(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_nettab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setnetent(handle) != Z_OK)
		return;
	while (zonecfg_getnetent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_net(fp, &lookup);
			continue;
		}
		if (fill_in_nettab(cmd, &user, B_TRUE) != Z_OK)
			continue;
		if (strlen(user.zone_net_physical) > 0 &&
		    strcmp(user.zone_net_physical,
		    lookup.zone_net_physical) != 0)
			continue;	/* no match */
		/* If present make sure it matches */
		if (strlen(user.zone_net_address) > 0 &&
		    !zonecfg_same_net_address(user.zone_net_address,
		    lookup.zone_net_address))
			continue;	/* no match */
		output_net(fp, &lookup);
		output = B_TRUE;
	}
	(void) zonecfg_endnetent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_NET));
}

/*
 * Returns integer portion of the next available linkname.
 * Example: If net0 is available, return 0
 */
static int
get_next_linkname(zone_dochandle_t handle, char *linkname,
    size_t bufsize, boolean_t increment)
{
	static uint_t	nextppa = 0;
	uint_t		startppa;

	/*
	 * The increment flag is only consulted when we are about to
	 * return the available integer for the linkname. If
	 * increment == true then we increment our internal nextppa value
	 * before we return.
	 *
	 * zonecfg's info subcommand calls this function with
	 * increment = false to get the 'same' available value every time
	 * this function is called.
	 * Example: net1, net1, net1
	 *
	 * And zonecfg's set command calls this function with increment = true
	 * to get different value (i.e. 'next' available value) every
	 * time this function is called.
	 * Example: net1, net2, net3
	 */

	startppa = nextppa;
	do {
		(void) snprintf(linkname, bufsize, "net%d", nextppa);
		if (zonecfg_lookup_linkname(handle, linkname) == B_FALSE) {
			if (increment && ++nextppa == (uint_t)-1)
				nextppa = 0;
			return (0);
		}
		if (++nextppa == (uint_t)-1)
			nextppa = 0;
	} while (nextppa != startppa);
	return (-1);
}

static void
output_anet(zone_dochandle_t handle, FILE *fp,
    struct zone_anettab *anettab)
{
	char	linkname[MAXLINKNAMELEN];

	(void) fprintf(fp, "%s:\n", rt_to_str(RT_ANET));

	if (anettab->zone_anet_linkname[0] != '\0' ||
	    get_next_linkname(handle, linkname,
	    sizeof (linkname), B_FALSE) < 0) {
		output_prop(fp, PT_LINKNAME, anettab->zone_anet_linkname,
		    B_TRUE);
	} else {
		output_prop(fp, PT_LINKNAME, linkname, B_TRUE);
	}

	if (anettab->zone_anet_lower_link[0] != '\0') {
		output_prop(fp, PT_LOWER_LINK, anettab->zone_anet_lower_link,
		    B_FALSE);
	} else {
		output_prop(fp, PT_LOWER_LINK, "auto", B_FALSE);
	}

	output_prop(fp, PT_ALLOWED_ADDRESS,
	    anettab->zone_anet_allowed_addr, B_TRUE);
	output_prop(fp, PT_CONFIGURE_ALLOWED_ADDRESS,
	    anettab->zone_anet_configure_allowed_addr ? "true" : "false",
	    B_TRUE);
	output_prop(fp, PT_DEFROUTER, anettab->zone_anet_defrouter, B_TRUE);
	output_prop(fp, PT_ALLOWED_DHCP_CIDS,
	    anettab->zone_anet_allowed_dhcp_cids, B_TRUE);

	if (anettab->zone_anet_allowed_addr[0] == '\0' ||
	    is_ip_nospoof_set(anettab->zone_anet_link_protection)) {
		output_prop(fp, PT_LINK_PROTECTION,
		    anettab->zone_anet_link_protection, B_TRUE);
	} else {
		/*
		 * If allowed-address is set, show ip-nospooof too.
		 * when displaying the protections.
		 */
		if (anettab->zone_anet_link_protection[0] == '\0') {
			output_prop(fp, PT_LINK_PROTECTION,
			    "ip-nospoof", B_TRUE);
		} else {
			char	link_protections[MAX_LINK_PROTECTIONS];
			(void) snprintf(link_protections,
			    sizeof (link_protections), "%s, %s",
			    anettab->zone_anet_link_protection,
			    "ip-nospoof");
			output_prop(fp, PT_LINK_PROTECTION,
			    link_protections, B_TRUE);
		}
	}

	if (anettab->zone_anet_mac_addr[0] != '\0') {
		output_prop(fp, PT_MAC_ADDRESS,
		    anettab->zone_anet_mac_addr, B_TRUE);
	} else {
		output_prop(fp, PT_MAC_ADDRESS, "random", B_FALSE);
	}

	output_prop(fp, PT_AUTO_MAC_ADDRESS,
	    anettab->zone_anet_auto_mac_addr, B_FALSE);
	output_prop(fp, PT_MAC_PREFIX, anettab->zone_anet_mac_prefix, B_TRUE);
	output_prop(fp, PT_MAC_SLOT, anettab->zone_anet_mac_slot, B_TRUE);
	output_prop(fp, PT_VLAN_ID, anettab->zone_anet_vlan_id, B_TRUE);
	output_prop(fp, PT_PRIORITY, anettab->zone_anet_priority, B_TRUE);
	output_prop(fp, PT_RXRINGS, anettab->zone_anet_rxrings, B_TRUE);
	output_prop(fp, PT_TXRINGS, anettab->zone_anet_txrings, B_TRUE);
	output_prop(fp, PT_MTU, anettab->zone_anet_mtu, B_TRUE);
	output_prop(fp, PT_MAXBW, anettab->zone_anet_maxbw, B_TRUE);
	output_prop(fp, PT_RXFANOUT, anettab->zone_anet_rxfanout, B_TRUE);
}

#define	_CMP_PROPERTY(pname, cmpfunc) \
	if (user.pname[0] != '\0' && \
	    cmpfunc(user.pname, lookup.pname) != 0) \
		continue;

static void
info_anet(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_anettab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setanetent(handle) != Z_OK)
		return;
	while (zonecfg_getanetent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_anet(handle, fp, &lookup);
			continue;
		}
		if (fill_in_anettab(cmd, &user, B_TRUE) != Z_OK)
			continue;

		_CMP_PROPERTY(zone_anet_linkname, strcmp);
		_CMP_PROPERTY(zone_anet_lower_link, strcmp);
		_CMP_PROPERTY(zone_anet_priority, strcasecmp);
		_CMP_PROPERTY(zone_anet_rxrings, strcasecmp);
		_CMP_PROPERTY(zone_anet_txrings, strcasecmp);
		_CMP_PROPERTY(zone_anet_mtu, strcasecmp);
		_CMP_PROPERTY(zone_anet_maxbw, strcasecmp);
		_CMP_PROPERTY(zone_anet_mac_slot, strcmp);
		_CMP_PROPERTY(zone_anet_vlan_id, strcmp);
		_CMP_PROPERTY(zone_anet_mac_addr, strcasecmp);
		_CMP_PROPERTY(zone_anet_mac_prefix, strcasecmp);
		_CMP_PROPERTY(zone_anet_allowed_addr, zonecfg_csscmp);
		_CMP_PROPERTY(zone_anet_defrouter, zonecfg_csscmp);
		_CMP_PROPERTY(zone_anet_allowed_dhcp_cids, zonecfg_csscmp);
		_CMP_PROPERTY(zone_anet_link_protection, zonecfg_csscmp);
		_CMP_PROPERTY(zone_anet_rxfanout, strcasecmp);

		output_anet(handle, fp, &lookup);
		output = B_TRUE;
	}
	(void) zonecfg_endanetent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) printf(gettext("No such %s resource.\n"),
		    rt_to_str(RT_ANET));
}

static void
output_dev(FILE *fp, struct zone_devtab *devtab)
{
	(void) fprintf(fp, "%s:\n", rt_to_str(RT_DEVICE));
	output_prop(fp, PT_MATCH, devtab->zone_dev_match, B_TRUE);
	output_prop(fp, PT_ALLOW_PARTITION, devtab->zone_dev_partition, B_TRUE);
	output_prop(fp, PT_ALLOW_RAW_IO, devtab->zone_dev_raw_io, B_TRUE);
}

static void
info_dev(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_devtab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setdevent(handle) != Z_OK)
		return;
	while (zonecfg_getdevent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_dev(fp, &lookup);
			continue;
		}
		if (fill_in_devtab(cmd, &user, B_TRUE) != Z_OK)
			continue;
		if (strlen(user.zone_dev_match) > 0 &&
		    strcmp(user.zone_dev_match, lookup.zone_dev_match) != 0)
			continue;	/* no match */
		output_dev(fp, &lookup);
		output = B_TRUE;
	}
	(void) zonecfg_enddevent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_DEVICE));
}

static void
output_rctl(FILE *fp, struct zone_rctltab *rctltab)
{
	struct zone_rctlvaltab *valptr;

	(void) fprintf(fp, "%s:\n", rt_to_str(RT_RCTL));
	output_prop(fp, PT_NAME, rctltab->zone_rctl_name, B_TRUE);
	for (valptr = rctltab->zone_rctl_valptr; valptr != NULL;
	    valptr = valptr->zone_rctlval_next) {
		fprintf(fp, "\t%s: (%s=%s,%s=%s,%s=%s)\n",
		    pt_to_str(PT_VALUE),
		    pt_to_str(PT_PRIV), valptr->zone_rctlval_priv,
		    pt_to_str(PT_LIMIT), valptr->zone_rctlval_limit,
		    pt_to_str(PT_ACTION), valptr->zone_rctlval_action);
	}
}

static void
info_rctl(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_rctltab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setrctlent(handle) != Z_OK)
		return;
	while (zonecfg_getrctlent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_rctl(fp, &lookup);
		} else if (fill_in_rctltab(cmd, &user, B_TRUE) == Z_OK &&
		    (strlen(user.zone_rctl_name) == 0 ||
		    strcmp(user.zone_rctl_name, lookup.zone_rctl_name) == 0)) {
			output_rctl(fp, &lookup);
			output = B_TRUE;
		}
		zonecfg_free_rctl_value_list(lookup.zone_rctl_valptr);
	}
	(void) zonecfg_endrctlent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_RCTL));
}

static void
output_attr(FILE *fp, struct zone_attrtab *attrtab)
{
	(void) fprintf(fp, "%s:\n", rt_to_str(RT_ATTR));
	output_prop(fp, PT_NAME, attrtab->zone_attr_name, B_TRUE);
	output_prop(fp, PT_TYPE, attrtab->zone_attr_type, B_TRUE);
	output_prop(fp, PT_VALUE, attrtab->zone_attr_value, B_TRUE);
}

static void
info_attr(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_attrtab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setattrent(handle) != Z_OK)
		return;
	while (zonecfg_getattrent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_attr(fp, &lookup);
			continue;
		}
		if (fill_in_attrtab(cmd, &user, B_TRUE) != Z_OK)
			continue;
		if (strlen(user.zone_attr_name) > 0 &&
		    strcmp(user.zone_attr_name, lookup.zone_attr_name) != 0)
			continue;	/* no match */
		if (strlen(user.zone_attr_type) > 0 &&
		    strcmp(user.zone_attr_type, lookup.zone_attr_type) != 0)
			continue;	/* no match */
		if (strlen(user.zone_attr_value) > 0 &&
		    strcmp(user.zone_attr_value, lookup.zone_attr_value) != 0)
			continue;	/* no match */
		output_attr(fp, &lookup);
		output = B_TRUE;
	}
	(void) zonecfg_endattrent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_ATTR));
}

static void
output_ds(FILE *fp, struct zone_dstab *dstab)
{
	(void) fprintf(fp, "%s:\n", rt_to_str(RT_DATASET));
	output_prop(fp, PT_NAME, dstab->zone_dataset_name, B_TRUE);
	output_prop(fp, PT_ALIAS, dstab->zone_dataset_alias, B_TRUE);
}

static void
info_ds(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_dstab lookup, user;
	boolean_t output = B_FALSE;

	if (zonecfg_setdsent(handle) != Z_OK)
		return;
	while (zonecfg_getdsent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_ds(fp, &lookup);
			continue;
		}
		if (fill_in_dstab(cmd, &user, B_TRUE) != Z_OK)
			continue;
		if (strlen(user.zone_dataset_name) > 0 &&
		    strcmp(user.zone_dataset_name,
		    lookup.zone_dataset_name) != 0)
			continue;	/* no match */
		if (strlen(user.zone_dataset_alias) > 0 &&
		    strcmp(user.zone_dataset_alias,
		    lookup.zone_dataset_alias) != 0)
			continue;	/* no match */
		output_ds(fp, &lookup);
		output = B_TRUE;
	}
	(void) zonecfg_enddsent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_DATASET));
}

static void
output_pset(FILE *fp, struct zone_psettab *psettab)
{
	(void) fprintf(fp, "%s:\n", rt_to_str(RT_DCPU));
	if (strcmp(psettab->zone_ncpu_min, psettab->zone_ncpu_max) == 0)
		(void) fprintf(fp, "\t%s: %s\n", pt_to_str(PT_NCPUS),
		    psettab->zone_ncpu_max);
	else
		(void) fprintf(fp, "\t%s: %s-%s\n", pt_to_str(PT_NCPUS),
		    psettab->zone_ncpu_min, psettab->zone_ncpu_max);
	if (psettab->zone_importance[0] != '\0')
		(void) fprintf(fp, "\t%s: %s\n", pt_to_str(PT_IMPORTANCE),
		    psettab->zone_importance);
}

static void
info_pset(zone_dochandle_t handle, FILE *fp)
{
	struct zone_psettab lookup;

	if (zonecfg_getpsetent(handle, &lookup) == Z_OK)
		output_pset(fp, &lookup);
}

static void
output_pcap(FILE *fp)
{
	uint64_t cap;

	if (zonecfg_get_aliased_rctl(handle, ALIAS_CPUCAP, &cap) == Z_OK) {
		float scaled = (float)cap / 100;
		(void) fprintf(fp, "%s:\n", rt_to_str(RT_PCAP));
		(void) fprintf(fp, "\t[%s: %.2f]\n", pt_to_str(PT_NCPUS),
		    scaled);
	}
}

static void
info_pcap(FILE *fp)
{
	output_pcap(fp);
}


static void
info_aliased_rctl(zone_dochandle_t handle, FILE *fp, char *alias)
{
	uint64_t limit;

	if (zonecfg_get_aliased_rctl(handle, alias, &limit) == Z_OK) {
		/* convert memory based properties */
		if (strcmp(alias, ALIAS_MAXSHMMEM) == 0) {
			char buf[128];

			(void) snprintf(buf, sizeof (buf), "%llu", limit);
			bytes_to_units(buf, buf, sizeof (buf));
			(void) fprintf(fp, "[%s: %s]\n", alias, buf);
			return;
		}

		(void) fprintf(fp, "[%s: %llu]\n", alias, limit);
	}
}

static void
bytes_to_units(char *str, char *buf, int bufsize)
{
	unsigned long long num;
	unsigned long long save = 0;
	char *units = "BKMGT";
	char *up = units;

	num = strtoll(str, NULL, 10);

	if (num < 1024) {
		(void) snprintf(buf, bufsize, "%llu", num);
		return;
	}

	while ((num >= 1024) && (*up != 'T')) {
		up++; /* next unit of measurement */
		save = num;
		num = (num + 512) >> 10;
	}

	/* check if we should output a fraction.  snprintf will round for us */
	if (save % 1024 != 0 && ((save >> 10) < 10))
		(void) snprintf(buf, bufsize, "%2.1f%c", ((float)save / 1024),
		    *up);
	else
		(void) snprintf(buf, bufsize, "%llu%c", num, *up);
}

static void
output_mcap(FILE *fp, struct zone_mcaptab *mcaptab, int showswap,
    uint64_t maxswap, int showlocked, uint64_t maxlocked)
{
	char buf[128];

	(void) fprintf(fp, "%s:\n", rt_to_str(RT_MCAP));
	if (mcaptab->zone_physmem_cap[0] != '\0') {
		bytes_to_units(mcaptab->zone_physmem_cap, buf, sizeof (buf));
		output_prop(fp, PT_PHYSICAL, buf, B_TRUE);
	}

	if (showswap == Z_OK) {
		(void) snprintf(buf, sizeof (buf), "%llu", maxswap);
		bytes_to_units(buf, buf, sizeof (buf));
		output_prop(fp, PT_SWAP, buf, B_TRUE);
	}

	if (showlocked == Z_OK) {
		(void) snprintf(buf, sizeof (buf), "%llu", maxlocked);
		bytes_to_units(buf, buf, sizeof (buf));
		output_prop(fp, PT_LOCKED, buf, B_TRUE);
	}
}

static void
info_mcap(zone_dochandle_t handle, FILE *fp)
{
	int res1, res2, res3;
	uint64_t swap_limit;
	uint64_t locked_limit;
	struct zone_mcaptab lookup;

	bzero(&lookup, sizeof (lookup));
	res1 = zonecfg_getmcapent(handle, &lookup);
	res2 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXSWAP, &swap_limit);
	res3 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXLOCKEDMEM,
	    &locked_limit);

	if (res1 == Z_OK || res2 == Z_OK || res3 == Z_OK)
		output_mcap(fp, &lookup, res2, swap_limit, res3, locked_limit);
}

static void
output_auth(FILE *fp, struct zone_admintab *admintab)
{
	(void) fprintf(fp, "%s:\n", rt_to_str(RT_ADMIN));
	output_prop(fp, PT_USER, admintab->zone_admin_user, B_TRUE);
	output_prop(fp, PT_AUTHS, admintab->zone_admin_auths, B_TRUE);
}

static void
info_auth(zone_dochandle_t handle, FILE *fp, cmd_t *cmd)
{
	struct zone_admintab lookup, user;
	boolean_t output = B_FALSE;
	int err;

	if ((err = zonecfg_setadminent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getadminent(handle, &lookup) == Z_OK) {
		if (cmd->cmd_prop_nv_pairs == 0) {
			output_auth(fp, &lookup);
			continue;
		}
		if (fill_in_admintab(cmd, &user, B_TRUE) != Z_OK)
			continue;
		if (strlen(user.zone_admin_user) > 0 &&
		    strcmp(user.zone_admin_user, lookup.zone_admin_user) != 0)
			continue;	/* no match */
		output_auth(fp, &lookup);
		output = B_TRUE;
	}
	(void) zonecfg_endadminent(handle);
	/*
	 * If a property n/v pair was specified, warn the user if there was
	 * nothing to output.
	 */
	if (!output && cmd->cmd_prop_nv_pairs > 0)
		(void) fprintf(stderr, gettext("No such %s resource.\n"),
		    rt_to_str(RT_ADMIN));
}

void
info_func(cmd_t *cmd)
{
	FILE *fp = stdout;
	int res1, res2, type;
	uint64_t swap_limit, locked_limit;

	assert(cmd != NULL);

	if (initialize(B_TRUE) != Z_OK)
		return;

	if (interactive_mode)
		setbuf(fp, NULL);

	if (!global_scope) {
		switch (resource_scope) {
		case RT_FS:
			output_fs(fp, &in_progress_fstab);
			break;
		case RT_NET:
			output_net(fp, &in_progress_nettab);
			break;
		case RT_ANET:
			output_anet(handle, fp, &in_progress_anettab);
			break;
		case RT_DEVICE:
			output_dev(fp, &in_progress_devtab);
			break;
		case RT_RCTL:
			output_rctl(fp, &in_progress_rctltab);
			break;
		case RT_ATTR:
			output_attr(fp, &in_progress_attrtab);
			break;
		case RT_DATASET:
			output_ds(fp, &in_progress_dstab);
			break;
		case RT_DCPU:
			output_pset(fp, &in_progress_psettab);
			break;
		case RT_PCAP:
			output_pcap(fp);
			break;
		case RT_MCAP:
			res1 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXSWAP,
			    &swap_limit);
			res2 = zonecfg_get_aliased_rctl(handle,
			    ALIAS_MAXLOCKEDMEM, &locked_limit);
			output_mcap(fp, &in_progress_mcaptab, res1, swap_limit,
			    res2, locked_limit);
			break;
		case RT_ADMIN:
			output_auth(fp, &in_progress_admintab);
			break;
		}
		return;
	}

	type = cmd->cmd_res_type;

	if (gz_invalid_rt_property(type)) {
		zerr(gettext("%s is not a valid property for the global zone."),
		    rt_to_str(type));
		return;
	}

	if (gz_invalid_resource(type)) {
		zerr(gettext("%s is not a valid resource for the global zone."),
		    rt_to_str(type));
		return;
	}

	switch (cmd->cmd_res_type) {
	case RT_UNKNOWN:
		info_zonename(handle, fp);
		if (!global_zone) {
			info_zonepath(handle, fp);
			info_brand(handle, fp);
			info_autoboot(handle, fp);
			info_bootargs(handle, fp);
			info_mac_profile(handle, fp);
		}
		info_pool(handle, fp);
		if (!global_zone) {
			info_limitpriv(handle, fp);
			info_sched(handle, fp);
			info_iptype(handle, fp);
			info_hostid(handle, fp);
			info_fs_allowed(handle, fp);
		}
		info_aliased_rctl(handle, fp, ALIAS_MAXLWPS);
		info_aliased_rctl(handle, fp, ALIAS_MAXPROCS);
		info_aliased_rctl(handle, fp, ALIAS_MAXSHMMEM);
		info_aliased_rctl(handle, fp, ALIAS_MAXSHMIDS);
		info_aliased_rctl(handle, fp, ALIAS_MAXMSGIDS);
		info_aliased_rctl(handle, fp, ALIAS_MAXSEMIDS);
		info_aliased_rctl(handle, fp, ALIAS_SHARES);
		if (!global_zone) {
			info_fs(handle, fp, cmd);
			info_net(handle, fp, cmd);
			info_anet(handle, fp, cmd);
			info_dev(handle, fp, cmd);
		}
		info_pset(handle, fp);
		info_pcap(fp);
		info_mcap(handle, fp);
		if (!global_zone) {
			info_attr(handle, fp, cmd);
			info_ds(handle, fp, cmd);
			info_auth(handle, fp, cmd);
		}
		info_rctl(handle, fp, cmd);
		break;
	case RT_ZONENAME:
		info_zonename(handle, fp);
		break;
	case RT_ZONEPATH:
		info_zonepath(handle, fp);
		break;
	case RT_BRAND:
		info_brand(handle, fp);
		break;
	case RT_AUTOBOOT:
		info_autoboot(handle, fp);
		break;
	case RT_POOL:
		info_pool(handle, fp);
		break;
	case RT_LIMITPRIV:
		info_limitpriv(handle, fp);
		break;
	case RT_BOOTARGS:
		info_bootargs(handle, fp);
		break;
	case RT_SCHED:
		info_sched(handle, fp);
		break;
	case RT_IPTYPE:
		info_iptype(handle, fp);
		break;
	case RT_MAXLWPS:
		info_aliased_rctl(handle, fp, ALIAS_MAXLWPS);
		break;
	case RT_MAXPROCS:
		info_aliased_rctl(handle, fp, ALIAS_MAXPROCS);
		break;
	case RT_MAXSHMMEM:
		info_aliased_rctl(handle, fp, ALIAS_MAXSHMMEM);
		break;
	case RT_MAXSHMIDS:
		info_aliased_rctl(handle, fp, ALIAS_MAXSHMIDS);
		break;
	case RT_MAXMSGIDS:
		info_aliased_rctl(handle, fp, ALIAS_MAXMSGIDS);
		break;
	case RT_MAXSEMIDS:
		info_aliased_rctl(handle, fp, ALIAS_MAXSEMIDS);
		break;
	case RT_SHARES:
		info_aliased_rctl(handle, fp, ALIAS_SHARES);
		break;
	case RT_FS:
		info_fs(handle, fp, cmd);
		break;
	case RT_NET:
		info_net(handle, fp, cmd);
		break;
	case RT_ANET:
		info_anet(handle, fp, cmd);
		break;
	case RT_DEVICE:
		info_dev(handle, fp, cmd);
		break;
	case RT_RCTL:
		info_rctl(handle, fp, cmd);
		break;
	case RT_ATTR:
		info_attr(handle, fp, cmd);
		break;
	case RT_DATASET:
		info_ds(handle, fp, cmd);
		break;
	case RT_DCPU:
		info_pset(handle, fp);
		break;
	case RT_PCAP:
		info_pcap(fp);
		break;
	case RT_MCAP:
		info_mcap(handle, fp);
		break;
	case RT_HOSTID:
		info_hostid(handle, fp);
		break;
	case RT_ADMIN:
		info_auth(handle, fp, cmd);
		break;
	case RT_FS_ALLOWED:
		info_fs_allowed(handle, fp);
		break;
	case RT_MAC_PROFILE:
		info_mac_profile(handle, fp);
		break;
	default:
		zone_perror(rt_to_str(cmd->cmd_res_type), Z_NO_RESOURCE_TYPE,
		    B_TRUE);
	}
}

/*
 * Helper function for verify-- checks that a required string property
 * exists.
 */
static void
check_reqd_prop(char *attr, int rt, int pt, int *ret_val)
{
	if (strlen(attr) == 0) {
		zerr(gettext("%s: %s not specified"), rt_to_str(rt),
		    pt_to_str(pt));
		saw_error = B_TRUE;
		if (*ret_val == Z_OK)
			*ret_val = Z_REQD_PROPERTY_MISSING;
	}
}

static int
do_subproc(char *cmdbuf)
{
	char inbuf[MAX_CMD_LEN];
	FILE *file;
	int status;

	file = popen(cmdbuf, "r");
	if (file == NULL) {
		zerr(gettext("Could not launch: %s"), cmdbuf);
		return (-1);
	}

	while (fgets(inbuf, sizeof (inbuf), file) != NULL)
		fprintf(stderr, "%s", inbuf);
	status = pclose(file);

	if (WIFSIGNALED(status)) {
		zerr(gettext("%s unexpectedly terminated due to signal %d"),
		    cmdbuf, WTERMSIG(status));
		return (-1);
	}
	assert(WIFEXITED(status));
	return (WEXITSTATUS(status));
}

static int
brand_verify(zone_dochandle_t handle)
{
	char xml_file[32];
	char cmdbuf[MAX_CMD_LEN];
	brand_handle_t bh;
	char brand[MAXNAMELEN];
	int err;

	if (zonecfg_get_brand(handle, brand, sizeof (brand)) != Z_OK) {
		zerr("%s: %s\n", zone, gettext("could not get zone brand"));
		return (Z_INVALID_DOCUMENT);
	}
	if ((bh = brand_open(brand)) == NULL) {
		zerr("%s: %s\n", zone, gettext("unknown brand."));
		return (Z_INVALID_DOCUMENT);
	}

	/*
	 * Fetch the verify command, if any, from the brand configuration
	 * and build the command line to execute it.
	 */
	strcpy(cmdbuf, EXEC_PREFIX);
	err = brand_get_verify_cfg(bh, cmdbuf + EXEC_LEN,
	    sizeof (cmdbuf) - (EXEC_LEN + (strlen(xml_file) + 1)));
	brand_close(bh);
	if (err != Z_OK) {
		zerr("%s: %s\n", zone,
		    gettext("could not get brand verification command"));
		return (Z_INVALID_DOCUMENT);
	}

	/*
	 * If the brand doesn't provide a verification routine, we just
	 * return success.
	 */
	if (strlen(cmdbuf) == EXEC_LEN)
		return (Z_OK);

	/*
	 * Dump the current config information for this zone to a file.
	 */
	strcpy(xml_file, "/tmp/zonecfg_verify.XXXXXX");
	if (mkstemp(xml_file) == NULL)
		return (Z_TEMP_FILE);
	if ((err = zonecfg_verify_save(handle, xml_file)) != Z_OK) {
		(void) unlink(xml_file);
		return (err);
	}

	/*
	 * Execute the verification command.
	 */
	if ((strlcat(cmdbuf, " ", MAX_CMD_LEN) >= MAX_CMD_LEN) ||
	    (strlcat(cmdbuf, xml_file, MAX_CMD_LEN) >= MAX_CMD_LEN)) {
		err = Z_BRAND_ERROR;
	} else {
		err = do_subproc(cmdbuf);
	}

	(void) unlink(xml_file);
	return ((err == Z_OK) ? Z_OK : Z_BRAND_ERROR);
}

/*
 * See the DTD for which attributes are required for which resources.
 *
 * This function can be called by commit_func(), which needs to save things,
 * in addition to the general call from parse_and_run(), which doesn't need
 * things saved.  Since the parameters are standardized, we distinguish by
 * having commit_func() call here with cmd->cmd_arg set to "save" to indicate
 * that a save is needed.
 */
void
verify_func(cmd_t *cmd)
{
	struct zone_nettab nettab;
	struct zone_anettab anettab;
	struct zone_fstab fstab;
	struct zone_attrtab attrtab;
	struct zone_rctltab rctltab;
	struct zone_dstab dstab;
	struct zone_psettab psettab;
	struct zone_admintab admintab;
	char zonepath[MAXPATHLEN];
	char sched[MAXNAMELEN];
	char brand[MAXNAMELEN];
	char hostidp[HW_HOSTID_LEN];
	char fsallowedp[ZONE_FS_ALLOWED_MAX];
	int err, ret_val = Z_OK, arg;
	int pset_res;
	boolean_t save = B_FALSE;
	boolean_t arg_err = B_FALSE;
	zone_iptype_t iptype;
	boolean_t has_cpu_shares = B_FALSE;
	boolean_t has_cpu_cap = B_FALSE;
	boolean_t verify_verbose = B_FALSE;

	assert(cmd != NULL);

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "v?")) != EOF) {
		switch (arg) {
		case 'v':
			verify_verbose = B_TRUE;
			break;
		case '?':
			longer_usage(CMD_VERIFY);
			arg_err = B_TRUE;
			break;
		default:
			short_usage(CMD_VERIFY);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind > cmd->cmd_argc) {
		short_usage(CMD_VERIFY);
		return;
	}
	if (!global_scope) {
		scope_usage(CMD_VERIFY);
		return;
	}

	if (zone_is_read_only(CMD_VERIFY))
		return;

	if (cmd->cmd_argc > 0 && (strcmp(cmd->cmd_argv[0], "save") == 0))
		save = B_TRUE;
	if (initialize(B_TRUE) != Z_OK)
		return;

	if (zonecfg_get_zonepath(handle, zonepath, sizeof (zonepath)) != Z_OK &&
	    !global_zone) {
		zerr(gettext("%s not specified"), pt_to_str(PT_ZONEPATH));
		ret_val = Z_REQD_RESOURCE_MISSING;
		saw_error = B_TRUE;
	}
	if (strlen(zonepath) == 0 && !global_zone) {
		zerr(gettext("%s cannot be empty."), pt_to_str(PT_ZONEPATH));
		ret_val = Z_REQD_RESOURCE_MISSING;
		saw_error = B_TRUE;
	}

	if ((err = zonecfg_get_brand(handle, brand, sizeof (brand))) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	if ((err = brand_verify(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}

	if (zonecfg_get_iptype(handle, &iptype) != Z_OK) {
		zerr("%s %s", gettext("cannot get"), pt_to_str(PT_IPTYPE));
		ret_val = Z_REQD_RESOURCE_MISSING;
		saw_error = B_TRUE;
	}

	if (zonecfg_get_hostid(handle, hostidp,
	    sizeof (hostidp)) == Z_INVALID_PROPERTY) {
		zerr(gettext("%s: invalid hostid: %s"),
		    zone, hostidp);
		return;
	}

	if (zonecfg_get_fs_allowed(handle, fsallowedp,
	    sizeof (fsallowedp)) == Z_INVALID_PROPERTY) {
		zerr(gettext("%s: invalid fs-allowed: %s"),
		    zone, fsallowedp);
		return;
	}

	if ((err = zonecfg_setfsent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getfsent(handle, &fstab) == Z_OK) {
		check_reqd_prop(fstab.zone_fs_dir, RT_FS, PT_DIR, &ret_val);
		check_reqd_prop(fstab.zone_fs_special, RT_FS, PT_SPECIAL,
		    &ret_val);
		check_reqd_prop(fstab.zone_fs_type, RT_FS, PT_TYPE, &ret_val);

		zonecfg_free_fs_option_list(fstab.zone_fs_options);
	}
	(void) zonecfg_endfsent(handle);

	if ((err = zonecfg_setnetent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getnetent(handle, &nettab) == Z_OK) {

		/*
		 * physical is required in all cases.
		 * A shared IP requires an address,
		 * and may include a default router, while
		 * an exclusive IP must have neither an address
		 * nor a default router.
		 * The physical interface name must be valid in all cases.
		 */
		check_reqd_prop(nettab.zone_net_physical, RT_NET,
		    PT_PHYSICAL, &ret_val);
		if (validate_net_physical_syntax(nettab.zone_net_physical) !=
		    Z_OK) {
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_INVAL;
		}

		switch (iptype) {
		case ZS_SHARED:
			check_reqd_prop(nettab.zone_net_address, RT_NET,
			    PT_ADDRESS, &ret_val);
			if (strlen(nettab.zone_net_allowed_addr)
			    > 0) {
				zerr(gettext("%s: %s cannot be specified "
				    "if %s = shared"),
				    rt_to_str(RT_NET),
				    pt_to_str(PT_ALLOWED_ADDRESS),
				    pt_to_str(PT_IPTYPE));
				saw_error = B_TRUE;
				if (ret_val == Z_OK)
					ret_val = Z_INVAL;
			}
			break;
		case ZS_EXCLUSIVE:
			if (strlen(nettab.zone_net_address) > 0) {
				zerr(gettext("%s: %s cannot be specified "
				    "if %s = exclusive"),
				    rt_to_str(RT_NET),
				    pt_to_str(PT_ADDRESS),
				    pt_to_str(PT_IPTYPE));
				if (!ip_type_set) {
					(void) fprintf(stderr, gettext("%s is "
					    "set to 'exclusive' by default.\n"),
					    pt_to_str(PT_IPTYPE));
				}
				saw_error = B_TRUE;
				if (ret_val == Z_OK)
					ret_val = Z_INVAL;
			}

			/*
			 * Make sure that if defrouter is specified then so is
			 * allowed-address.
			 */
			if (strlen(nettab.zone_net_allowed_addr) == 0 &&
			    strlen(nettab.zone_net_defrouter) > 0) {
				zerr(gettext("%s: %s cannot be specified "
				    "without %s"),
				    rt_to_str(RT_NET), pt_to_str(PT_DEFROUTER),
				    pt_to_str(PT_ALLOWED_ADDRESS));
				saw_error = B_TRUE;
				if (ret_val == Z_OK)
					ret_val = Z_INVAL;
			}
			if (strlen(nettab.zone_net_defrouter) > 0 &&
			    !nettab.zone_net_configure_allowed_addr)
				zerr(gettext("%s: %s is set, but will not be "
				    "configured"), rt_to_str(RT_NET),
				    pt_to_str(PT_DEFROUTER));
			break;
		}
	}
	(void) zonecfg_endnetent(handle);

	if (iptype == ZS_SHARED) {
		if ((err = zonecfg_setanetent(handle)) != Z_OK) {
			zone_perror(zone, err, B_TRUE);
			return;
		}
		if (zonecfg_getanetent(handle, &anettab) == Z_OK) {
			zerr(gettext("%s resources can only be specified if %s "
			    "= exclusive"),
			    rt_to_str(RT_ANET), pt_to_str(PT_IPTYPE));
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_INCOMPATIBLE;
		}
		(void) zonecfg_endanetent(handle);
	}

	if ((err = zonecfg_setrctlent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getrctlent(handle, &rctltab) == Z_OK) {
		check_reqd_prop(rctltab.zone_rctl_name, RT_RCTL, PT_NAME,
		    &ret_val);

		if (strcmp(rctltab.zone_rctl_name, "zone.cpu-shares") == 0)
			has_cpu_shares = B_TRUE;

		if (strcmp(rctltab.zone_rctl_name, "zone.cpu-cap") == 0)
			has_cpu_cap = B_TRUE;

		if (rctltab.zone_rctl_valptr == NULL) {
			zerr(gettext("%s: no %s specified"),
			    rt_to_str(RT_RCTL), pt_to_str(PT_VALUE));
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_REQD_PROPERTY_MISSING;
		} else {
			zonecfg_free_rctl_value_list(rctltab.zone_rctl_valptr);
		}
	}
	(void) zonecfg_endrctlent(handle);

	if ((pset_res = zonecfg_lookup_pset(handle, &psettab)) == Z_OK &&
	    has_cpu_shares) {
		zerr(gettext("%s zone.cpu-shares and %s are incompatible."),
		    rt_to_str(RT_RCTL), rt_to_str(RT_DCPU));
		saw_error = B_TRUE;
		if (ret_val == Z_OK)
			ret_val = Z_INCOMPATIBLE;
	}

	if (has_cpu_shares && zonecfg_get_sched_class(handle, sched,
	    sizeof (sched)) == Z_OK && strlen(sched) > 0 &&
	    strcmp(sched, "FSS") != 0) {
		zerr(gettext("WARNING: %s zone.cpu-shares and %s=%s are "
		    "incompatible"),
		    rt_to_str(RT_RCTL), rt_to_str(RT_SCHED), sched);
		saw_error = B_TRUE;
		if (ret_val == Z_OK)
			ret_val = Z_INCOMPATIBLE;
	}

	if (pset_res == Z_OK && has_cpu_cap) {
		zerr(gettext("%s zone.cpu-cap and the %s are incompatible."),
		    rt_to_str(RT_RCTL), rt_to_str(RT_DCPU));
		saw_error = B_TRUE;
		if (ret_val == Z_OK)
			ret_val = Z_INCOMPATIBLE;
	}

	if ((err = zonecfg_setattrent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getattrent(handle, &attrtab) == Z_OK) {
		check_reqd_prop(attrtab.zone_attr_name, RT_ATTR, PT_NAME,
		    &ret_val);
		check_reqd_prop(attrtab.zone_attr_type, RT_ATTR, PT_TYPE,
		    &ret_val);
		check_reqd_prop(attrtab.zone_attr_value, RT_ATTR, PT_VALUE,
		    &ret_val);
	}
	(void) zonecfg_endattrent(handle);

	if ((err = zonecfg_setdsent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getdsent(handle, &dstab) == Z_OK) {
		/* Verify dataset name */
		if (strlen(dstab.zone_dataset_name) == 0) {
			zerr("%s: %s %s", rt_to_str(RT_DATASET),
			    pt_to_str(PT_NAME), gettext("not specified"));
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_REQD_PROPERTY_MISSING;
		} else if (!zfs_name_valid(dstab.zone_dataset_name,
		    ZFS_TYPE_FILESYSTEM)) {
			zerr("%s: %s %s", rt_to_str(RT_DATASET),
			    pt_to_str(PT_NAME), gettext("invalid"));
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_BAD_PROPERTY;
		}
		/* Verify dataset alias */
		if (strlen(dstab.zone_dataset_alias) == 0) {
			zerr("%s: %s %s", rt_to_str(RT_DATASET),
			    pt_to_str(PT_ALIAS), gettext("not specified"));
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_REQD_PROPERTY_MISSING;
		} else if (!zfs_name_valid(dstab.zone_dataset_alias,
		    ZFS_TYPE_POOL)) {
			zerr("%s: %s %s", rt_to_str(RT_DATASET),
			    pt_to_str(PT_ALIAS), gettext("invalid"));
			saw_error = B_TRUE;
			if (ret_val == Z_OK)
				ret_val = Z_INVALID_PROPERTY;
		}
	}
	(void) zonecfg_enddsent(handle);

	if (verify_verbose) {
		int cnt = 0;
		int i = 0;
		struct zone_devtab *tabs;

		/*
		 * Call warn_device_conflict_with_devalias() on each device.
		 * We need to build up an array of devtabs because
		 * warn_device_conflict_with_devalias() will call
		 * zonecfg_setdsent(), which is equivalent to
		 * zonecfg_setdevent().
		 */
		cnt = zonecfg_num_resources(handle, "device");
		if (cnt == 0)
			goto verify_alias_conflict_done;

		if ((tabs = calloc(sizeof (*tabs), cnt)) == NULL) {
			zone_perror(zone, Z_NOMEM, B_TRUE);
			return;
		}

		if ((err = zonecfg_setdevent(handle)) != Z_OK) {
			zone_perror(zone, err, B_TRUE);
			return;
		}
		while (zonecfg_getdevent(handle, &(tabs[i])) == Z_OK) {
			assert(i < cnt);
			i++;
		}
		(void) zonecfg_enddsent(handle);

		/* Finally, we are ready to check for conflicts */
		for (i = 0; i < cnt; i++) {
			if (warn_device_conflict_with_dsalias(handle,
			    &(tabs[i]))) {
				saw_error = B_TRUE;
				if (ret_val == Z_OK)
					ret_val = Z_POSSIBLE_CONFLICT;
			}
		}
		free(tabs);
	}
verify_alias_conflict_done:

	if ((err = zonecfg_setadminent(handle)) != Z_OK) {
		zone_perror(zone, err, B_TRUE);
		return;
	}
	while (zonecfg_getadminent(handle, &admintab) == Z_OK) {
		check_reqd_prop(admintab.zone_admin_user, RT_ADMIN,
		    PT_USER, &ret_val);
		check_reqd_prop(admintab.zone_admin_auths, RT_ADMIN,
		    PT_AUTHS, &ret_val);
		if ((ret_val == Z_OK) && (getpwnam(admintab.zone_admin_user)
		    == NULL)) {
			zerr(gettext("%s %s is not a valid username"),
			    pt_to_str(PT_USER),
			    admintab.zone_admin_user);
			ret_val = Z_BAD_PROPERTY;
		}
		if ((ret_val == Z_OK) && (!zonecfg_valid_auths(
		    admintab.zone_admin_auths, zone))) {
			ret_val = Z_BAD_PROPERTY;
		}
	}
	(void) zonecfg_endadminent(handle);

	if (!global_scope) {
		zerr(gettext("resource specification incomplete"));
		saw_error = B_TRUE;
		if (ret_val == Z_OK)
			ret_val = Z_INSUFFICIENT_SPEC;
	}

	if (save) {
		if (ret_val == Z_OK) {
			if ((ret_val = zonecfg_save(handle)) == Z_OK) {
				need_to_commit = B_FALSE;
				(void) strlcpy(revert_zone, zone,
				    sizeof (revert_zone));
			}
		} else {
			zerr(gettext("Zone %s failed to verify"), zone);
		}
	}
	if (ret_val != Z_OK)
		zone_perror(zone, ret_val, B_TRUE);
}

void
cancel_func(cmd_t *cmd)
{
	int arg;
	boolean_t arg_err = B_FALSE;

	assert(cmd != NULL);

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_CANCEL);
			arg_err = B_TRUE;
			break;
		default:
			short_usage(CMD_CANCEL);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_CANCEL);
		return;
	}

	if (global_scope)
		scope_usage(CMD_CANCEL);
	global_scope = B_TRUE;
	zonecfg_free_fs_option_list(in_progress_fstab.zone_fs_options);
	bzero(&in_progress_fstab, sizeof (in_progress_fstab));
	bzero(&in_progress_nettab, sizeof (in_progress_nettab));
	bzero(&in_progress_anettab, sizeof (in_progress_anettab));
	bzero(&in_progress_devtab, sizeof (in_progress_devtab));
	zonecfg_free_rctl_value_list(in_progress_rctltab.zone_rctl_valptr);
	bzero(&in_progress_rctltab, sizeof (in_progress_rctltab));
	bzero(&in_progress_attrtab, sizeof (in_progress_attrtab));
	bzero(&in_progress_dstab, sizeof (in_progress_dstab));
}

static int
validate_attr_name(char *name)
{
	int i;

	if (!isalnum(name[0])) {
		zerr(gettext("Invalid %s %s %s: must start with an alpha-"
		    "numeric character."), rt_to_str(RT_ATTR),
		    pt_to_str(PT_NAME), name);
		return (Z_INVAL);
	}
	for (i = 1; name[i]; i++)
		if (!isalnum(name[i]) && name[i] != '-' && name[i] != '.') {
			zerr(gettext("Invalid %s %s %s: can only contain "
			    "alpha-numeric characters, plus '-' and '.'."),
			    rt_to_str(RT_ATTR), pt_to_str(PT_NAME), name);
			return (Z_INVAL);
		}
	return (Z_OK);
}

static int
validate_attr_type_val(struct zone_attrtab *attrtab)
{
	boolean_t boolval;
	int64_t intval;
	char strval[MAXNAMELEN];
	uint64_t uintval;

	if (strcmp(attrtab->zone_attr_type, "boolean") == 0) {
		if (zonecfg_get_attr_boolean(attrtab, &boolval) == Z_OK)
			return (Z_OK);
		zerr(gettext("invalid %s value for %s=%s"),
		    rt_to_str(RT_ATTR), pt_to_str(PT_TYPE), "boolean");
		return (Z_ERR);
	}

	if (strcmp(attrtab->zone_attr_type, "int") == 0) {
		if (zonecfg_get_attr_int(attrtab, &intval) == Z_OK)
			return (Z_OK);
		zerr(gettext("invalid %s value for %s=%s"),
		    rt_to_str(RT_ATTR), pt_to_str(PT_TYPE), "int");
		return (Z_ERR);
	}

	if (strcmp(attrtab->zone_attr_type, "string") == 0) {
		if (zonecfg_get_attr_string(attrtab, strval,
		    sizeof (strval)) == Z_OK)
			return (Z_OK);
		zerr(gettext("invalid %s value for %s=%s"),
		    rt_to_str(RT_ATTR), pt_to_str(PT_TYPE), "string");
		return (Z_ERR);
	}

	if (strcmp(attrtab->zone_attr_type, "uint") == 0) {
		if (zonecfg_get_attr_uint(attrtab, &uintval) == Z_OK)
			return (Z_OK);
		zerr(gettext("invalid %s value for %s=%s"),
		    rt_to_str(RT_ATTR), pt_to_str(PT_TYPE), "uint");
		return (Z_ERR);
	}

	zerr(gettext("invalid %s %s '%s'"), rt_to_str(RT_ATTR),
	    pt_to_str(PT_TYPE), attrtab->zone_attr_type);
	return (Z_ERR);
}

/*
 * Helper function for end_func-- checks the existence of a given property
 * and emits a message if not specified.
 */
static int
end_check_reqd(char *attr, int pt, boolean_t *validation_failed)
{
	if (strlen(attr) == 0) {
		*validation_failed = B_TRUE;
		zerr(gettext("%s not specified"), pt_to_str(pt));
		return (Z_ERR);
	}
	return (Z_OK);
}

static void
net_exists_error(struct zone_nettab net)
{
	if (strlen(net.zone_net_address) > 0) {
		zerr(gettext("A %s resource with the %s '%s', "
		    "and %s '%s' already exists."),
		    rt_to_str(RT_NET),
		    pt_to_str(PT_PHYSICAL),
		    net.zone_net_physical,
		    pt_to_str(PT_ADDRESS),
		    in_progress_nettab.zone_net_address);
	} else {
		zerr(gettext("A %s resource with the %s '%s', "
		    "and %s '%s' already exists."),
		    rt_to_str(RT_NET),
		    pt_to_str(PT_PHYSICAL),
		    net.zone_net_physical,
		    pt_to_str(PT_ALLOWED_ADDRESS),
		    net.zone_net_allowed_addr);
	}
}

/*
 * Check the zone configuration to be sure that the dataset referred to by
 * name is not a hierarchical conflict.  That is, a/b and a/b/c cannot
 * both be delegated, but a/b and a/bc can be.
 *
 * If a conflict is found the return value is B_TRUE.  If conflict is not
 * NULL, the name of the conflicting dataset is stored in the buffer referenced
 * by conflict, subject to the size limitation conflict_sz.  If no conflicts
 * are seen, the return value is B_FALSE and conflict is not referenced.
 */
static boolean_t
has_ds_name_conflict(zone_dochandle_t handle, const char *name,
    char *conflict, size_t conflict_sz)
{
	struct zone_dstab tab;
	size_t curlen, namelen, cmplen;

	if (zonecfg_setdsent(handle) != Z_OK)
		return (B_FALSE);

	namelen = strlen(name);
	while (zonecfg_getdsent(handle, &tab) == Z_OK) {
		curlen = strlen(tab.zone_dataset_name);
		cmplen = (curlen < namelen) ? curlen : namelen;
		if (strncmp(name, tab.zone_dataset_name, cmplen) != 0)
			continue;

		if ((name[cmplen] == '\0' || name[cmplen] == '/') &&
		    (tab.zone_dataset_name[cmplen] == '\0' ||
		    tab.zone_dataset_name[cmplen] == '/')) {
			if (conflict != NULL)
				strlcpy(conflict, tab.zone_dataset_name,
				    conflict_sz);
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Trim any trailing portion of a /dev/... path such that it contains no
 * more than "/dev/zvol/r?dsk/<pool>", storing the result in dst.  Both
 * src and dst are at least sz bytes long.  No checking is performed to
 * be sure that src starts with /dev/zvol.  It is up to a subsequent
 * fnmatch() to do that.
 */
static void
make_devzvolpool_match(const char *src, char *dst, size_t sz)
{
	const char	*s;		/* Current char in src */
	char		*d;		/* Current char in dst */
	int		slashes;	/* How many slashes found so far */

	/*
	 * Copy the first part of the match from the devtab such that
	 * match then contains "/dev/zvol/r?dsk/<pool>".  This is
	 * tolerant of patterns in the match.
	 */
	for (s = src, d = dst, slashes = 0; ((s - src) < sz) && (*s != '\0');
	    s++, d++) {
		/* Skip all but the first consecutive slash */
		while (*s == '/' && *(s + 1) == '/')
			s++;
		*d = *s;
		if (*s == '/') {
			/* prevent d from advancing */
			if (++slashes == 5)
				break;
		}
	}
	*d = '\0';
}

/*
 * Check to see if the the specified match pattern generates potential
 * conflicts in /dev/zvol with a virtual pool by the name stored in alias.
 * Returns B_TRUE if a potential conflict exists.
 */
static boolean_t
devzvol_dsalias_conflict(const char *match, const char *alias) {
	char	devpath[MAXPATHLEN];	/* /dev/zvol/dsk/<alias> */

	(void) snprintf(devpath, sizeof (devpath), ZVOL_FULL_DEV_DIR "%s",
	    alias);
	if (fnmatch(match, devpath, FNM_PATHNAME) == 0)
		return (B_TRUE);

	(void) snprintf(devpath, sizeof (devpath), ZVOL_FULL_RDEV_DIR "%s",
	    alias);
	if (fnmatch(match, devpath, FNM_PATHNAME) == 0)
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * Walk through the zone's device resources looking for any that have a value
 * for 'match' that can cause zvols created in aliased datasets to be hidden.
 * Prints a message for each potential conflict. Returns B_TRUE if conflicts
 * exist, otherwise B_FALSE.
 */
static boolean_t
warn_dsalias_conflict_with_devs(zone_dochandle_t handle,
    struct zone_dstab *dstab)
{
	struct zone_devtab devtab;
	boolean_t	ret = B_FALSE;
	char		match[MAXPATHLEN];	/* pattern from dev rsrc */

	if (zonecfg_setdevent(handle) != Z_OK)
		return (B_FALSE);

	while (zonecfg_getdevent(handle, &devtab) == Z_OK) {
		make_devzvolpool_match(devtab.zone_dev_match, match,
		    sizeof (match));

		/* Check for matches of device pool name and dataset alias. */
		if (devzvol_dsalias_conflict(match,
		    dstab->zone_dataset_alias) == B_FALSE)
			continue;

		/* Potential conflict found */
		zerr(gettext("A %s resource with the %s '%s'\nexists and can "
		    "hide ZFS volumes created in\n%s '%s' with %s '%s'."),
		    rt_to_str(RT_DEVICE), pt_to_str(PT_MATCH),
		    devtab.zone_dev_match, rt_to_str(RT_DATASET),
		    dstab->zone_dataset_name, pt_to_str(PT_ALIAS),
		    dstab->zone_dataset_alias);
		ret = B_TRUE;
	}

	(void) zonecfg_enddevent(handle);
	return (ret);
}

/*
 * Callback which is called on each platform dataset to determine if the
 * device match in the zone_devtab referenced by 'data' has potential conflicts
 * with /dev/zvol/r?dsk/<dsalias>.  Prints a message for each potential
 * conflict. Returns 1 if conflicts exist, otherwise 0.
 */
/*ARGSUSED1*/
int
platform_dev_dsalias_conflict_cb(void *data, const char *dsname,
    const char *dsalias)
{
	struct zone_devtab *devtab = data;
	char	match[MAXPATHLEN];	/* Shortened pattern from devtab */

	make_devzvolpool_match(devtab->zone_dev_match, match, sizeof (match));

	if (devzvol_dsalias_conflict(match, dsalias) == B_FALSE)
		return (0);

	zerr(gettext("A platform %s resource with the %s '%s' exists.  Devices "
	    "with\n%s '%s' can hide volumes created within the\nvirtual pool "
	    "'%s'."), rt_to_str(RT_DATASET), pt_to_str(PT_ALIAS), dsalias,
	    pt_to_str(PT_MATCH), devtab->zone_dev_match, dsalias);
	return (1);
}

/*
 * Walk through the zone's dataset resources and platform datasets looking for
 * any that have an alias that can conflict with the device resource's match.
 * Prints a message for each potential conflict. Returns B_TRUE if conflicts
 * exist, otherwise B_FALSE.
 */
static boolean_t
warn_device_conflict_with_dsalias(zone_dochandle_t handle,
    struct zone_devtab *devtab)
{
	struct zone_dstab dstab;
	boolean_t	ret = B_FALSE;		/* Any problems so far? */
	char		match[MAXPATHLEN];	/* pattern from dev rsrc */

	if (zonecfg_setdsent(handle) != Z_OK)
		return (B_FALSE);

	make_devzvolpool_match(devtab->zone_dev_match, match, sizeof (match));

	/* Look at platform datasets */
	if (brand != NULL) {
		char zp[ZONENAME_MAX];

		if (zonecfg_get_zonepath(handle, zp, sizeof (zp)) != Z_OK)
			zp[0] = '\0';
		if (brand_platform_iter_datasets(brand, zp,
		    platform_dev_dsalias_conflict_cb, (void*) devtab) != 0) {
			/* callback printed message */
			ret = B_TRUE;
		}
	}

	/* Look at dataset resources */
	while (zonecfg_getdsent(handle, &dstab) == Z_OK) {
		/* Check for matches of device pool name and dataset alias. */
		if (devzvol_dsalias_conflict(match,
		    dstab.zone_dataset_alias) == B_FALSE)
			continue;

		/* Potential conflict found */
		zerr(gettext("A %s resource with the %s '%s' exists.  Devices "
		    "with %s\n'%s' can hide volumes created within the\n"
		    "virtual pool '%s'."), rt_to_str(RT_DATASET),
		    pt_to_str(PT_ALIAS), dstab.zone_dataset_alias,
		    pt_to_str(PT_MATCH), devtab->zone_dev_match,
		    dstab.zone_dataset_alias);
		ret = B_TRUE;
	}

	(void) zonecfg_enddsent(handle);
	return (ret);
}

static int
platform_dataset_conflict_cb(void *data, const char *platname,
    const char *platalias)
{
	struct zone_dstab *dstab = data;
	char *newname = dstab->zone_dataset_name;
	char *newalias = dstab->zone_dataset_alias;
	int retval = 0;


	/* Use default alias if no alias defined in platform.xml */
	if (platalias == NULL || *platalias == '\0') {
		platalias = strrchr(platname, '/');
		if (platalias == NULL)
			platalias = platname;
		else
			platalias++;
	}

	/* Be sure the platform dataset is not overridden */
	if (strcmp(newname, platname) == 0) {
		zerr(gettext("The %s '%s' is implicitly delegated as a "
		    "platform %s"),
		    rt_to_str(RT_DATASET), newname, rt_to_str(RT_DATASET));
		retval = -1;
	} else {
		/* Be sure that there is no partial match as well */
		int newlen = strlen(newname);
		int platlen = strlen(platname);
		int cmplen = (newlen < platlen) ? newlen : platlen;

		if (strncmp(newname, platname, cmplen) == 0 &&
		    ((newname[cmplen] == '\0' || newname[cmplen] == '/') ||
		    (platname[cmplen] == '\0' || platname[cmplen] == '/'))) {
			zerr(gettext("The %s '%s' conflicts with platform "
			    "%s '%s'"), rt_to_str(RT_DATASET), newname,
			    rt_to_str(RT_DATASET), platname);
			retval = -1;
		}
	}

	/* Be sure the platform alias is not reused. */
	if (strcmp(newalias, platalias) == 0) {
		zerr(gettext("The %s %s '%s' is an %s for the platform %s "
		    "'%s'"), rt_to_str(RT_DATASET), pt_to_str(PT_ALIAS),
		    platalias, pt_to_str(PT_ALIAS), rt_to_str(RT_DATASET),
		    platname);
		retval = -1;
	}

	return (retval);
}

void
end_func(cmd_t *cmd)
{
	boolean_t validation_failed = B_FALSE;
	boolean_t arg_err = B_FALSE;
	struct zone_fstab tmp_fstab;
	struct zone_nettab tmp_nettab;
	struct zone_devtab tmp_devtab;
	struct zone_rctltab tmp_rctltab;
	struct zone_attrtab tmp_attrtab;
	struct zone_dstab tmp_dstab;
	struct zone_admintab tmp_admintab;
	int err, arg, res1, res2, res3;
	uint64_t swap_limit;
	uint64_t locked_limit;
	uint64_t proc_cap;
	char conflict[MAXNAMELEN];

	assert(cmd != NULL);

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_END);
			arg_err = B_TRUE;
			break;
		default:
			short_usage(CMD_END);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_END);
		return;
	}

	if (global_scope) {
		scope_usage(CMD_END);
		return;
	}

	assert(end_op == CMD_ADD || end_op == CMD_SELECT);

	switch (resource_scope) {
	case RT_FS:
		/* First make sure everything was filled in. */
		if (end_check_reqd(in_progress_fstab.zone_fs_dir,
		    PT_DIR, &validation_failed) == Z_OK) {
			if (in_progress_fstab.zone_fs_dir[0] != '/') {
				zerr(gettext("%s %s is not an absolute path."),
				    pt_to_str(PT_DIR),
				    in_progress_fstab.zone_fs_dir);
				validation_failed = B_TRUE;
			}
		}

		(void) end_check_reqd(in_progress_fstab.zone_fs_special,
		    PT_SPECIAL, &validation_failed);

		if (in_progress_fstab.zone_fs_raw[0] != '\0' &&
		    in_progress_fstab.zone_fs_raw[0] != '/') {
			zerr(gettext("%s %s is not an absolute path."),
			    pt_to_str(PT_RAW),
			    in_progress_fstab.zone_fs_raw);
			validation_failed = B_TRUE;
		}

		(void) end_check_reqd(in_progress_fstab.zone_fs_type, PT_TYPE,
		    &validation_failed);

		if (validation_failed) {
			saw_error = B_TRUE;
			return;
		}

		if (end_op == CMD_ADD) {
			/* Make sure there isn't already one like this. */
			bzero(&tmp_fstab, sizeof (tmp_fstab));
			(void) strlcpy(tmp_fstab.zone_fs_dir,
			    in_progress_fstab.zone_fs_dir,
			    sizeof (tmp_fstab.zone_fs_dir));
			err = zonecfg_lookup_filesystem(handle, &tmp_fstab);
			zonecfg_free_fs_option_list(tmp_fstab.zone_fs_options);
			if (err == Z_OK) {
				zerr(gettext("A %s resource "
				    "with the %s '%s' already exists."),
				    rt_to_str(RT_FS), pt_to_str(PT_DIR),
				    in_progress_fstab.zone_fs_dir);
				saw_error = B_TRUE;
				return;
			}
			err = zonecfg_add_filesystem(handle,
			    &in_progress_fstab);
		} else {
			err = zonecfg_modify_filesystem(handle, &old_fstab,
			    &in_progress_fstab);
		}
		zonecfg_free_fs_option_list(in_progress_fstab.zone_fs_options);
		in_progress_fstab.zone_fs_options = NULL;
		break;

	case RT_NET:
		/*
		 * First make sure everything was filled in.
		 * Since we don't know whether IP will be shared
		 * or exclusive here, some checks are deferred until
		 * the verify command.
		 */
		(void) end_check_reqd(in_progress_nettab.zone_net_physical,
		    PT_PHYSICAL, &validation_failed);

		if (validation_failed) {
			saw_error = B_TRUE;
			return;
		}

		if (end_op == CMD_ADD) {
			/* Make sure there isn't already one like this. */
			bzero(&tmp_nettab, sizeof (tmp_nettab));
			(void) strlcpy(tmp_nettab.zone_net_physical,
			    in_progress_nettab.zone_net_physical,
			    sizeof (tmp_nettab.zone_net_physical));
			(void) strlcpy(tmp_nettab.zone_net_address,
			    in_progress_nettab.zone_net_address,
			    sizeof (tmp_nettab.zone_net_address));
			(void) strlcpy(
			    tmp_nettab.zone_net_allowed_addr,
			    in_progress_nettab.zone_net_allowed_addr,
			    sizeof (tmp_nettab.zone_net_allowed_addr));
			(void) strlcpy(tmp_nettab.zone_net_defrouter,
			    in_progress_nettab.zone_net_defrouter,
			    sizeof (tmp_nettab.zone_net_defrouter));
			if (zonecfg_lookup_net(handle, &tmp_nettab) == Z_OK) {
				net_exists_error(in_progress_nettab);
				saw_error = B_TRUE;
				return;
			}
			err = zonecfg_add_net(handle, &in_progress_nettab);
		} else {
			err = zonecfg_modify_net(handle, &old_nettab,
			    &in_progress_nettab);
		}
		break;

	case RT_ANET:
		/*
		 * Validations that can be done solely based on
		 * the anet resource that is being added/modified
		 * are done here.
		 * Validations that require other resources or
		 * properties (such as iptype) in addition to the
		 * anet resource are done during the 'verify' step.
		 */
		ipa = &in_progress_anettab;

		if (ipa->zone_anet_lower_link[0] == '\0') {
			/* If lower link is not set then set it to auto */
			(void) strlcpy(ipa->zone_anet_lower_link,
			    "auto", sizeof (ipa->zone_anet_lower_link));
		} else {
			if (strcasecmp(ipa->zone_anet_lower_link,
			    "auto") != 0 &&
			    validate_net_physical_syntax(
			    ipa->zone_anet_lower_link) != Z_OK) {
				zerr(gettext("\"%s\" is not a valid %s."),
				    ipa->zone_anet_lower_link,
				    pt_to_str(PT_LOWER_LINK));
				saw_error = B_TRUE;
				return;
			}
		}

		/* If mac_addr link is not set then set it to random */
		if (ipa->zone_anet_mac_addr[0] == '\0') {
			(void) strlcpy(ipa->zone_anet_mac_addr,
			    "random", sizeof (ipa->zone_anet_mac_addr));
		}

		if (strlen(ipa->zone_anet_allowed_addr) == 0) {
			/*
			 * Print error message if defrouter is specified
			 * but allowed-address is not.
			 */
			if (strlen(ipa->zone_anet_defrouter) > 0) {
				zerr(gettext("%s: %s cannot be specified "
				    "without %s"),
				    rt_to_str(RT_ANET),
				    pt_to_str(PT_DEFROUTER),
				    pt_to_str(PT_ALLOWED_ADDRESS));
				saw_error = B_TRUE;
				return;
			}

			/*
			 * Print warning if ip-nospoof is enabled
			 * but allowed-address is missing.
			 */
			if (is_ip_nospoof_set(
			    ipa->zone_anet_link_protection)) {
				zerr(gettext("WARNING: ip-nospoof link "
				    "protection will be ignored because %s "
				    "has not been set."),
				    pt_to_str(PT_ALLOWED_ADDRESS));
			}
		}
		if (strlen(ipa->zone_anet_defrouter) > 0 &&
		    !ipa->zone_anet_configure_allowed_addr)
			zerr(gettext("%s: %s is set, but will not be "
			    "configured"), rt_to_str(RT_ANET),
			    pt_to_str(PT_DEFROUTER));

		if (ipa->zone_anet_linkname[0] == '\0') {
			/*
			 * Choose an unused linkname for this
			 * anet resource.
			 */
			if (get_next_linkname(handle, ipa->zone_anet_linkname,
			    sizeof (ipa->zone_anet_linkname), B_TRUE) < 0) {
				zerr(gettext("Unable to automatically "
				    "set a linkname for the %s resource. "
				    "Please set the %s property and retry."),
				    rt_to_str(RT_ANET),
				    pt_to_str(PT_LINKNAME));
				saw_error = B_TRUE;
				return;
			}
		} else {
			/* Validate the syntax of the given linkname */
			if (validate_net_physical_syntax(
			    ipa->zone_anet_linkname) != Z_OK) {
				zerr(gettext("\"%s\" is not a valid %s."),
				    ipa->zone_anet_linkname,
				    pt_to_str(PT_LINKNAME));
				saw_error = B_TRUE;
				return;
			}

			/*
			 * If we are adding anet resource with a
			 * explicit linkname or modifying the linkname
			 * property of an existing anet resource then
			 * make sure that another anet/net resource
			 * with the same linkname/physical does
			 * not exists.
			 */
			if (end_op == CMD_ADD ||
			    (strcmp(ipa->zone_anet_linkname,
			    old_anettab.zone_anet_linkname) != 0)) {
				if (zonecfg_lookup_linkname(handle,
				    ipa->zone_anet_linkname) == B_TRUE) {
					zerr(
					    gettext("The linkname %s is already"
					    " in use by another resource."),
					    ipa->zone_anet_linkname);
					saw_error = B_TRUE;
					return;
				}
			}
		}

		if (end_op == CMD_ADD) {
			err = zonecfg_add_anet(handle, ipa);
		} else {
			err = zonecfg_modify_anet(handle, &old_anettab, ipa);
		}
		break;

	case RT_DEVICE:
		/* First make sure everything was filled in. */
		(void) end_check_reqd(in_progress_devtab.zone_dev_match,
		    PT_MATCH, &validation_failed);

		if (validation_failed) {
			saw_error = B_TRUE;
			return;
		}

		if (end_op == CMD_ADD) {
			/*
			 * Make sure there isn't already one like this.
			 * PT_MATCH is the primary key, so we don't fill
			 * in the others.
			 */
			bzero(&tmp_devtab, sizeof (tmp_devtab));
			(void) strlcpy(tmp_devtab.zone_dev_match,
			    in_progress_devtab.zone_dev_match,
			    sizeof (tmp_devtab.zone_dev_match));

			if (zonecfg_lookup_dev(handle, &tmp_devtab) == Z_OK) {
				zerr(gettext("A %s resource with the %s '%s' "
				    "already exists."), rt_to_str(RT_DEVICE),
				    pt_to_str(PT_MATCH),
				    in_progress_devtab.zone_dev_match);
				saw_error = B_TRUE;
				return;
			}
			err = zonecfg_add_dev(handle, &in_progress_devtab);
		} else {
			err = zonecfg_modify_dev(handle, &old_devtab,
			    &in_progress_devtab);
		}
		(void) warn_device_conflict_with_dsalias(handle,
		    &in_progress_devtab);
		break;

	case RT_RCTL:
		/* First make sure everything was filled in. */
		(void) end_check_reqd(in_progress_rctltab.zone_rctl_name,
		    PT_NAME, &validation_failed);

		if (in_progress_rctltab.zone_rctl_valptr == NULL) {
			zerr(gettext("no %s specified"), pt_to_str(PT_VALUE));
			validation_failed = B_TRUE;
		}

		if (validation_failed) {
			saw_error = B_TRUE;
			return;
		}

		if (end_op == CMD_ADD) {
			/* Make sure there isn't already one like this. */
			(void) strlcpy(tmp_rctltab.zone_rctl_name,
			    in_progress_rctltab.zone_rctl_name,
			    sizeof (tmp_rctltab.zone_rctl_name));
			tmp_rctltab.zone_rctl_valptr = NULL;
			err = zonecfg_lookup_rctl(handle, &tmp_rctltab);
			zonecfg_free_rctl_value_list(
			    tmp_rctltab.zone_rctl_valptr);
			if (err == Z_OK) {
				zerr(gettext("A %s resource "
				    "with the %s '%s' already exists."),
				    rt_to_str(RT_RCTL), pt_to_str(PT_NAME),
				    in_progress_rctltab.zone_rctl_name);
				saw_error = B_TRUE;
				return;
			}
			err = zonecfg_add_rctl(handle, &in_progress_rctltab);
		} else {
			err = zonecfg_modify_rctl(handle, &old_rctltab,
			    &in_progress_rctltab);
		}
		if (err == Z_OK) {
			zonecfg_free_rctl_value_list(
			    in_progress_rctltab.zone_rctl_valptr);
			in_progress_rctltab.zone_rctl_valptr = NULL;
		}
		break;

	case RT_ATTR:
		/* First make sure everything was filled in. */
		(void) end_check_reqd(in_progress_attrtab.zone_attr_name,
		    PT_NAME, &validation_failed);
		(void) end_check_reqd(in_progress_attrtab.zone_attr_type,
		    PT_TYPE, &validation_failed);
		(void) end_check_reqd(in_progress_attrtab.zone_attr_value,
		    PT_VALUE, &validation_failed);

		if (validate_attr_name(in_progress_attrtab.zone_attr_name) !=
		    Z_OK)
			validation_failed = B_TRUE;

		if (validate_attr_type_val(&in_progress_attrtab) != Z_OK)
			validation_failed = B_TRUE;

		if (validation_failed) {
			saw_error = B_TRUE;
			return;
		}
		if (end_op == CMD_ADD) {
			/* Make sure there isn't already one like this. */
			bzero(&tmp_attrtab, sizeof (tmp_attrtab));
			(void) strlcpy(tmp_attrtab.zone_attr_name,
			    in_progress_attrtab.zone_attr_name,
			    sizeof (tmp_attrtab.zone_attr_name));
			if (zonecfg_lookup_attr(handle, &tmp_attrtab) == Z_OK) {
				zerr(gettext("An %s resource "
				    "with the %s '%s' already exists."),
				    rt_to_str(RT_ATTR), pt_to_str(PT_NAME),
				    in_progress_attrtab.zone_attr_name);
				saw_error = B_TRUE;
				return;
			}
			err = zonecfg_add_attr(handle, &in_progress_attrtab);
		} else {
			err = zonecfg_modify_attr(handle, &old_attrtab,
			    &in_progress_attrtab);
		}
		break;
	case RT_DATASET:
		/* First make sure everything was filled in. */
		if (strlen(in_progress_dstab.zone_dataset_name) == 0) {
			zerr("%s %s", pt_to_str(PT_NAME),
			    gettext("not specified"));
			saw_error = B_TRUE;
			return;
		}

		/* Make sure this dataset isn't already in the config. */
		if (end_op == CMD_ADD) {
			bzero(&tmp_dstab, sizeof (tmp_dstab));
			(void) strlcpy(tmp_dstab.zone_dataset_name,
			    in_progress_dstab.zone_dataset_name,
			    sizeof (tmp_dstab.zone_dataset_name));

			err = zonecfg_lookup_ds(handle, &tmp_dstab);
			if (err == Z_OK) {
				zerr(gettext("A %s resource "
				    "with the %s '%s' already exists."),
				    rt_to_str(RT_DATASET), pt_to_str(PT_NAME),
				    in_progress_dstab.zone_dataset_name);
				saw_error = B_TRUE;
				return;
			}
		}

		/*
		 * Make sure that aliasing won't get confused by delegating
		 * a dataset and its hierarchical descendant.
		 */
		if (end_op == CMD_ADD &&
		    has_ds_name_conflict(handle,
		    in_progress_dstab.zone_dataset_name, conflict,
		    sizeof (conflict))) {
			zerr(gettext("A %s resource with the %s '%s' "
			    "already exists and conflicts with '%s'"),
			    rt_to_str(RT_DATASET), pt_to_str(PT_NAME),
			    conflict, in_progress_dstab.zone_dataset_name);
			saw_error = B_TRUE;
			return;
		}

		/* Create automatic alias if not already specified. */
		if (strlen(in_progress_dstab.zone_dataset_alias) == 0) {
			char *alias =
			    strrchr(in_progress_dstab.zone_dataset_name, '/');
			if (alias == NULL)
				alias = in_progress_dstab.zone_dataset_name;
			else
				alias++;

			strlcpy(in_progress_dstab.zone_dataset_alias, alias,
			    sizeof (in_progress_dstab.zone_dataset_alias));
		}

		/* Be sure the alias is OK as a virtual pool name. */
		if (!zfs_name_valid(in_progress_dstab.zone_dataset_alias,
		    ZFS_TYPE_POOL)) {
			zerr(gettext("Invalid %s '%s'"), pt_to_str(PT_ALIAS),
			    in_progress_dstab.zone_dataset_alias);
			saw_error = B_TRUE;
			return;
		}

		/* Make sure that the alias is not already used.  */
		if (end_op == CMD_ADD) {
			bzero(&tmp_dstab, sizeof (tmp_dstab));
			(void) strlcpy(tmp_dstab.zone_dataset_alias,
			    in_progress_dstab.zone_dataset_alias,
			    sizeof (tmp_dstab.zone_dataset_alias));

			if ((err = zonecfg_lookup_ds(handle, &tmp_dstab)) ==
			    Z_OK) {
				zerr(gettext("A %s resource with the %s '%s' "
				    "already exists."), rt_to_str(RT_DATASET),
				    pt_to_str(PT_ALIAS),
				    in_progress_dstab.zone_dataset_alias);
				saw_error = B_TRUE;
				return;
			}
		}

		/*
		 * Check for conflicts with platform datasets.  We can only
		 * do this if the zone is configured far enough to have a
		 * valid brand handle.
		 */
		if (brand != NULL) {
			char zonepath[ZONENAME_MAX];

			if (zonecfg_get_zonepath(handle, zonepath,
			    sizeof (zonepath)) != Z_OK)
				zonepath[0] = '\0';
			if (brand_platform_iter_datasets(brand, zonepath,
			    platform_dataset_conflict_cb,
			    (void*) &in_progress_dstab) != 0) {
				saw_error = B_TRUE;
				return;
			}
		}

		/*
		 * Check for potential conflicts with device resources.
		 * This may generate warnings messages of potential problems
		 * that are not actual configuration errors.
		 */
		(void) warn_dsalias_conflict_with_devs(handle,
		    &in_progress_dstab);

		/*
		 * Perform the add or modify.  Note that if modifying an
		 * existing dataset, not all checks above are performed due
		 * to a limitation in the implementation.  In order to perform
		 * all checks, we would need to have a means to ignore
		 * conflicts with the node that is being worked on.
		 */
		if (end_op == CMD_ADD)
			err = zonecfg_add_ds(handle, &in_progress_dstab);
		else
			err = zonecfg_modify_ds(handle, &old_dstab,
			    &in_progress_dstab);
		break;
	case RT_DCPU:
		/* Make sure everything was filled in. */
		if (end_check_reqd(in_progress_psettab.zone_ncpu_min,
		    PT_NCPUS, &validation_failed) != Z_OK) {
			saw_error = B_TRUE;
			return;
		}

		if (end_op == CMD_ADD) {
			err = zonecfg_add_pset(handle, &in_progress_psettab);
		} else {
			err = zonecfg_modify_pset(handle, &in_progress_psettab);
		}
		break;
	case RT_PCAP:
		/* Make sure everything was filled in. */
		if (zonecfg_get_aliased_rctl(handle, ALIAS_CPUCAP, &proc_cap)
		    != Z_OK) {
			zerr(gettext("%s not specified"), pt_to_str(PT_NCPUS));
			saw_error = B_TRUE;
			validation_failed = B_TRUE;
			return;
		}
		err = Z_OK;
		break;
	case RT_MCAP:
		/* Make sure everything was filled in. */
		res1 = strlen(in_progress_mcaptab.zone_physmem_cap) == 0 ?
		    Z_ERR : Z_OK;
		res2 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXSWAP,
		    &swap_limit);
		res3 = zonecfg_get_aliased_rctl(handle, ALIAS_MAXLOCKEDMEM,
		    &locked_limit);

		if (res1 != Z_OK && res2 != Z_OK && res3 != Z_OK) {
			zerr(gettext("No property was specified.  One of %s, "
			    "%s or %s is required."), pt_to_str(PT_PHYSICAL),
			    pt_to_str(PT_SWAP), pt_to_str(PT_LOCKED));
			saw_error = B_TRUE;
			return;
		}

		/* if phys & locked are both set, verify locked <= phys */
		if (res1 == Z_OK && res3 == Z_OK) {
			uint64_t phys_limit;
			char *endp;

			phys_limit = strtoull(
			    in_progress_mcaptab.zone_physmem_cap, &endp, 10);
			if (phys_limit < locked_limit) {
				zerr(gettext("The %s cap must be less than or "
				    "equal to the %s cap."),
				    pt_to_str(PT_LOCKED),
				    pt_to_str(PT_PHYSICAL));
				saw_error = B_TRUE;
				return;
			}
		}

		err = Z_OK;
		if (res1 == Z_OK) {
			/*
			 * We could be ending from either an add operation
			 * or a select operation.  Since all of the properties
			 * within this resource are optional, we always use
			 * modify on the mcap entry.  zonecfg_modify_mcap()
			 * will handle both adding and modifying a memory cap.
			 */
			err = zonecfg_modify_mcap(handle, &in_progress_mcaptab);
		} else if (end_op == CMD_SELECT) {
			/*
			 * If we're ending from a select and the physical
			 * memory cap is empty then the user could have cleared
			 * the physical cap value, so try to delete the entry.
			 */
			(void) zonecfg_delete_mcap(handle);
		}
		break;
	case RT_ADMIN:
		/* First make sure everything was filled in. */
		if (end_check_reqd(in_progress_admintab.zone_admin_user,
		    PT_USER, &validation_failed) == Z_OK) {
			if (getpwnam(in_progress_admintab.zone_admin_user)
			    == NULL) {
				zerr(gettext("%s %s is not a valid username"),
				    pt_to_str(PT_USER),
				    in_progress_admintab.zone_admin_user);
				validation_failed = B_TRUE;
			}
		}

		if (end_check_reqd(in_progress_admintab.zone_admin_auths,
		    PT_AUTHS, &validation_failed) == Z_OK) {
			if (!zonecfg_valid_auths(
			    in_progress_admintab.zone_admin_auths,
			    zone)) {
				validation_failed = B_TRUE;
			}
		}

		if (validation_failed) {
			saw_error = B_TRUE;
			return;
		}

		if (end_op == CMD_ADD) {
			/* Make sure there isn't already one like this. */
			bzero(&tmp_admintab, sizeof (tmp_admintab));
			(void) strlcpy(tmp_admintab.zone_admin_user,
			    in_progress_admintab.zone_admin_user,
			    sizeof (tmp_admintab.zone_admin_user));
			err = zonecfg_lookup_admin(
			    handle, &tmp_admintab);
			if (err == Z_OK) {
				zerr(gettext("A %s resource "
				    "with the %s '%s' already exists."),
				    rt_to_str(RT_ADMIN),
				    pt_to_str(PT_USER),
				    in_progress_admintab.zone_admin_user);
				saw_error = B_TRUE;
				return;
			}
			err = zonecfg_add_admin(handle,
			    &in_progress_admintab, zone);
		} else {
			err = zonecfg_modify_admin(handle,
			    &old_admintab, &in_progress_admintab,
			    zone);
		}
		break;
	default:
		zone_perror(rt_to_str(resource_scope), Z_NO_RESOURCE_TYPE,
		    B_TRUE);
		saw_error = B_TRUE;
		return;
	}

	if (err != Z_OK) {
		zone_perror(zone, err, B_TRUE);
	} else {
		need_to_commit = B_TRUE;
		global_scope = B_TRUE;
		end_op = -1;
	}
}

void
commit_func(cmd_t *cmd)
{
	int arg;
	boolean_t arg_err = B_FALSE;

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_COMMIT);
			arg_err = B_TRUE;
			break;
		default:
			short_usage(CMD_COMMIT);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_COMMIT);
		return;
	}

	if (zone_is_read_only(CMD_COMMIT))
		return;

	assert(cmd != NULL);

	cmd->cmd_argc = 1;
	/*
	 * cmd_arg normally comes from a strdup() in the lexer, and the
	 * whole cmd structure and its (char *) attributes are freed at
	 * the completion of each command, so the strdup() below is needed
	 * to match this and prevent a core dump from trying to free()
	 * something that can't be.
	 */
	if ((cmd->cmd_argv[0] = strdup("save")) == NULL) {
		zone_perror(zone, Z_NOMEM, B_TRUE);
		exit(Z_ERR);
	}
	cmd->cmd_argv[1] = NULL;
	verify_func(cmd);
}

void
revert_func(cmd_t *cmd)
{
	char line[128];	/* enough to ask a question */
	boolean_t force = B_FALSE;
	boolean_t arg_err = B_FALSE;
	int err, arg, answer;

	optind = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "?F")) != EOF) {
		switch (arg) {
		case '?':
			longer_usage(CMD_REVERT);
			arg_err = B_TRUE;
			break;
		case 'F':
			force = B_TRUE;
			break;
		default:
			short_usage(CMD_REVERT);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (optind != cmd->cmd_argc) {
		short_usage(CMD_REVERT);
		return;
	}

	if (zone_is_read_only(CMD_REVERT))
		return;

	if (!global_scope) {
		zerr(gettext("You can only use %s in the global scope.\nUse"
		    " '%s' to cancel changes to a resource specification."),
		    cmd_to_str(CMD_REVERT), cmd_to_str(CMD_CANCEL));
		saw_error = B_TRUE;
		return;
	}

	if (zonecfg_check_handle(handle) != Z_OK) {
		zerr(gettext("No changes to revert."));
		saw_error = B_TRUE;
		return;
	}

	if (!force) {
		(void) snprintf(line, sizeof (line),
		    gettext("Are you sure you want to revert"));
		if ((answer = ask_yesno(B_FALSE, line)) == -1) {
			zerr(gettext("Input not from terminal and -F not "
			    "specified:\n%s command ignored, exiting."),
			    cmd_to_str(CMD_REVERT));
			exit(Z_ERR);
		}
		if (answer != 1)
			return;
	}

	/*
	 * Reset any pending admins that were
	 * removed from the previous zone
	 */
	zonecfg_remove_userauths(handle, "", zone, B_FALSE);

	/*
	 * Time for a new handle: finish the old one off first
	 * then get a new one properly to avoid leaks.
	 */
	zonecfg_fini_handle(handle);
	if ((handle = zonecfg_init_handle()) == NULL) {
		zone_perror(execname, Z_NOMEM, B_TRUE);
		exit(Z_ERR);
	}

	if ((err = zonecfg_get_handle(revert_zone, handle)) != Z_OK) {
		saw_error = B_TRUE;
		got_handle = B_FALSE;
		if (err == Z_NO_ZONE)
			zerr(gettext("%s: no such saved zone to revert to."),
			    revert_zone);
		else
			zone_perror(zone, err, B_TRUE);
	}
	(void) strlcpy(zone, revert_zone, sizeof (zone));
}

void
help_func(cmd_t *cmd)
{
	int i;

	assert(cmd != NULL);

	if (cmd->cmd_argc == 0) {
		usage(B_TRUE, global_scope ? HELP_SUBCMDS : HELP_RES_SCOPE);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "usage") == 0) {
		usage(B_TRUE, HELP_USAGE);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "commands") == 0) {
		usage(B_TRUE, HELP_SUBCMDS);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "syntax") == 0) {
		usage(B_TRUE, HELP_SYNTAX | HELP_RES_PROPS);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "-?") == 0) {
		longer_usage(CMD_HELP);
		return;
	}

	for (i = 0; i <= CMD_MAX; i++) {
		if (strcmp(cmd->cmd_argv[0], cmd_to_str(i)) == 0) {
			longer_usage(i);
			return;
		}
	}
	/* We do not use zerr() here because we do not want its extra \n. */
	(void) fprintf(stderr, gettext("Unknown help subject %s.  "),
	    cmd->cmd_argv[0]);
	usage(B_FALSE, HELP_META);
}

static int
string_to_yyin(char *string)
{
	if ((yyin = tmpfile()) == NULL) {
		zone_perror(execname, Z_TEMP_FILE, B_TRUE);
		return (Z_ERR);
	}
	if (fwrite(string, strlen(string), 1, yyin) != 1) {
		zone_perror(execname, Z_TEMP_FILE, B_TRUE);
		return (Z_ERR);
	}
	if (fseek(yyin, 0, SEEK_SET) != 0) {
		zone_perror(execname, Z_TEMP_FILE, B_TRUE);
		return (Z_ERR);
	}
	return (Z_OK);
}

/* This is the back-end helper function for read_input() below. */

static int
cleanup()
{
	int answer;
	cmd_t *cmd;

	if (!interactive_mode && !cmd_file_mode) {
		/*
		 * If we're not in interactive mode, and we're not in command
		 * file mode, then we must be in commands-from-the-command-line
		 * mode.  As such, we can't loop back and ask for more input.
		 * It was OK to prompt for such things as whether or not to
		 * really delete a zone in the command handler called from
		 * yyparse() above, but "really quit?" makes no sense in this
		 * context.  So disable prompting.
		 */
		ok_to_prompt = B_FALSE;
	}
	if (!global_scope) {
		if (!time_to_exit) {
			/*
			 * Just print a simple error message in the -1 case,
			 * since exit_func() already handles that case, and
			 * EOF means we are finished anyway.
			 */
			answer = ask_yesno(B_FALSE,
			    gettext("Resource incomplete; really quit"));
			if (answer == -1) {
				zerr(gettext("Resource incomplete."));
				return (Z_ERR);
			}
			if (answer != 1) {
				yyin = stdin;
				return (Z_REPEAT);
			}
		} else {
			saw_error = B_TRUE;
		}
	}
	/*
	 * Make sure we tried something and that the handle checks
	 * out, or we would get a false error trying to commit.
	 */
	if (need_to_commit && zonecfg_check_handle(handle) == Z_OK) {
		if ((cmd = alloc_cmd()) == NULL) {
			zone_perror(zone, Z_NOMEM, B_TRUE);
			return (Z_ERR);
		}
		cmd->cmd_argc = 0;
		cmd->cmd_argv[0] = NULL;
		commit_func(cmd);
		free_cmd(cmd);
		/*
		 * need_to_commit will get set back to FALSE if the
		 * configuration is saved successfully.
		 */
		if (need_to_commit) {
			if (force_exit) {
				zerr(gettext("Configuration not saved."));
				return (Z_ERR);
			}
			answer = ask_yesno(B_FALSE,
			    gettext("Configuration not saved; really quit"));
			if (answer == -1) {
				zerr(gettext("Configuration not saved."));
				return (Z_ERR);
			}
			if (answer != 1) {
				time_to_exit = B_FALSE;
				yyin = stdin;
				return (Z_REPEAT);
			}
		}
	}
	return ((need_to_commit || saw_error) ? Z_ERR : Z_OK);
}

/*
 * read_input() is the driver of this program.  It is a wrapper around
 * yyparse(), printing appropriate prompts when needed, checking for
 * exit conditions and reacting appropriately [the latter in its cleanup()
 * helper function].
 *
 * Like most zonecfg functions, it returns Z_OK or Z_ERR, *or* Z_REPEAT
 * so do_interactive() knows that we are not really done (i.e, we asked
 * the user if we should really quit and the user said no).
 */
static int
read_input()
{
	boolean_t yyin_is_a_tty = isatty(fileno(yyin));
	/*
	 * The prompt is "e:z> " or "e:z:r> " where e is execname, z is zone
	 * and r is resource_scope: 5 is for the two ":"s + "> " + terminator.
	 */
	char prompt[MAXPATHLEN + ZONENAME_MAX + MAX_RT_STRLEN + 5], *line;

	/* yyin should have been set to the appropriate (FILE *) if not stdin */
	newline_terminated = B_TRUE;
	for (;;) {
		if (yyin_is_a_tty) {
			if (newline_terminated) {
				if (global_scope)
					(void) snprintf(prompt, sizeof (prompt),
					    "%s:%s> ", execname, zone);
				else
					(void) snprintf(prompt, sizeof (prompt),
					    "%s:%s:%s> ", execname, zone,
					    rt_to_str(resource_scope));
			}
			/*
			 * If the user hits ^C then we want to catch it and
			 * start over.  If the user hits EOF then we want to
			 * bail out.
			 */
			line = gl_get_line(gl, prompt, NULL, -1);
			if (gl_return_status(gl) == GLR_SIGNAL) {
				gl_abandon_line(gl);
				continue;
			}
			if (line == NULL)
				break;
			(void) string_to_yyin(line);
			while (!feof(yyin))
				yyparse();
		} else {
			yyparse();
		}
		/* Bail out on an error in command file mode. */
		if (saw_error && cmd_file_mode && !interactive_mode)
			time_to_exit = B_TRUE;
		if (time_to_exit || (!yyin_is_a_tty && feof(yyin)))
			break;
	}
	return (cleanup());
}

/*
 * This function is used in the zonecfg-interactive-mode scenario: it just
 * calls read_input() until we are done.
 */

static int
do_interactive(void)
{
	int err;

	interactive_mode = B_TRUE;
	if (!read_only_mode) {
		/*
		 * Try to set things up proactively in interactive mode, so
		 * that if the zone in question does not exist yet, we can
		 * provide the user with a clue.
		 */
		(void) initialize(B_FALSE);
	}
	do {
		err = read_input();
	} while (err == Z_REPEAT);
	return (err);
}

/*
 * cmd_file is slightly more complicated, as it has to open the command file
 * and set yyin appropriately.  Once that is done, though, it just calls
 * read_input(), and only once, since prompting is not possible.
 */

static int
cmd_file(char *file)
{
	FILE *infile;
	int err;
	struct stat statbuf;
	boolean_t using_real_file = (strcmp(file, "-") != 0);

	if (using_real_file) {
		/*
		 * zerr() prints a line number in cmd_file_mode, which we do
		 * not want here, so temporarily unset it.
		 */
		cmd_file_mode = B_FALSE;
		if ((infile = fopen(file, "r")) == NULL) {
			zerr(gettext("could not open file %s: %s"),
			    file, strerror(errno));
			return (Z_ERR);
		}
		if ((err = fstat(fileno(infile), &statbuf)) != 0) {
			zerr(gettext("could not stat file %s: %s"),
			    file, strerror(errno));
			err = Z_ERR;
			goto done;
		}
		if (!S_ISREG(statbuf.st_mode)) {
			zerr(gettext("%s is not a regular file."), file);
			err = Z_ERR;
			goto done;
		}
		yyin = infile;
		cmd_file_mode = B_TRUE;
		ok_to_prompt = B_FALSE;
	} else {
		/*
		 * "-f -" is essentially the same as interactive mode,
		 * so treat it that way.
		 */
		interactive_mode = B_TRUE;
	}
	/* Z_REPEAT is for interactive mode; treat it like Z_ERR here. */
	if ((err = read_input()) == Z_REPEAT)
		err = Z_ERR;
done:
	if (using_real_file)
		(void) fclose(infile);
	return (err);
}

/*
 * Check if given zonepath is already on list.
 * If already on list, print zonepath collision failure.
 * If not, add zonepath and the zonename to the list.
 * Returns Z_OK if ok; Z_ERR if failure
 */
static int
ai_report_check_and_add_list(const char *zonename, const char *zonepath,
    nvlist_t *list_headp)
{
	char *propval;
	nvpair_t *nvp;

	for (nvp = nvlist_next_nvpair(list_headp, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(list_headp, nvp)) {
		if (nvpair_value_string(nvp, &propval) != 0)
			return (Z_ERR);
		/* If value already on list, fail */
		if (strcmp(propval, zonepath) == 0) {
			zerr(gettext("zone %s and zone %s are configured "
			    "with the same zonepath %s"),
			    nvpair_name(nvp), zonename, zonepath);
			return (Z_ERR);
		}
	}
	if (nvlist_add_string(list_headp, zonename, zonepath) != 0)
		return (Z_ERR);
	return (Z_OK);
}

/*
 * Check if given value is already on list.
 * If on list, return with Z_OK.
 * If not on list then add value to list.
 * Returns Z_OK if ok; Z_ERR if failure
 */
static int
ai_report_add_list(const char *name, const char *value, nvlist_t *list_headp)
{
	char *propval;
	nvpair_t *nvp;

	for (nvp = nvlist_next_nvpair(list_headp, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(list_headp, nvp)) {
		if (nvpair_value_string(nvp, &propval) != 0)
			return (Z_ERR);
		/* If value already on list, just return */
		if (strcmp(propval, value) == 0)
			return (Z_OK);
	}
	if (nvlist_add_string(list_headp, name, value) != 0)
		return (Z_ERR);
	return (Z_OK);
}

static int
ai_report_print_list(FILE *of, const char *print_string, nvlist_t *list_headp)
{
	char *propval;
	nvpair_t *nvp;

	for (nvp = nvlist_next_nvpair(list_headp, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(list_headp, nvp)) {
		if (nvpair_value_string(nvp, &propval) != 0)
			return (Z_ERR);
		fprintf(of, "%s=%s\n", print_string, propval);
	}
	return (Z_OK);
}

/*
 * Given a zone config file (in zonecfg export format), find the
 * zonepath, zonepath_parent and zfs datasets and add to their corresponding
 * linked lists.  Fail if zonepath collision is detected.
 * Also determine the zone name and add to the zonepath linked list.
 */
static int
ai_report_get_info(char *cname, nvlist_t *zonepath_headp,
nvlist_t *parent_headp, nvlist_t *dataset_headp)
{
	char zonepath[MAXPATHLEN];
	char zonename[MAXPATHLEN];
	char *p1, *p2;
	struct stat statbuf;
	int err = Z_OK;
	struct zone_dstab dstab;
	struct zone_fstab lookup;

	/* Name of zone is the directory containing the config file */
	if (cname[0] != '/') {
		zerr(gettext("%s: path to config file must be full path."),
		    cname);
		return (Z_ERR);
	}
	p1 = strrchr(cname, '/');
	*p1 = '\0';
	p2 = strrchr(cname, '/');
	if (p2 == NULL) {
		*p1 = '/';
		zerr(gettext("%s: config file must have parent directory "
		    "that is zonename."), cname);
		return (Z_ERR);
	}
	p2++;
	(void) strlcpy(zonename, p2, sizeof (zonename));
	*p1 = '/';

	/* Setup global variable zone to use internal functions */
	(void) strlcpy(zone, zonename, sizeof (zone));
	if (check_if_zone_already_exists(B_FALSE) != Z_OK) {
		zerr(gettext("zone %s already exists."), zone);
		return (Z_ERR);
	}

	if (stat(cname, &statbuf) == -1) {
		zerr(gettext("zone config file %s doesn't exist."), cname);
		return (Z_ERR);
	}

	/* creates zone given zone config file */
	cmd_file_name = (char *)cname;
	err = cmd_file((char *)cname);
	if (err != Z_OK) {
		return (err);
	}
	err = zonecfg_get_zonepath(handle, zonepath, sizeof (zonepath));
	if (err != Z_OK) {
		goto done;
	}

	/* Add zonename and zonepath to list checking for collision. */
	if ((err = ai_report_check_and_add_list(zonename, zonepath,
	    zonepath_headp)) != Z_OK)
		goto done;

	/* Get parent directory and add to parent list */
	p1 = strrchr(zonepath, '/');
	if ((p1 == zonepath) || (p1 == NULL)) {
		zerr(gettext("Zone %s: The parent directory of "
		    "zonepath '%s' must be a ZFS dataset."),
		    zonename, zonepath);
		err = Z_ERR;
		goto done;
	}
	*p1 = '\0';
	err = ai_report_add_list(zonename, zonepath, parent_headp);
	*p1 = '/';
	if (err != Z_OK)
		goto done;

	/* Get datasets given to the zone and add to list. */
	if ((err = zonecfg_setdsent(handle)) != Z_OK)
		goto done;
	while (zonecfg_getdsent(handle, &dstab) == Z_OK) {
		if ((err = ai_report_add_list(zonename,
		    dstab.zone_dataset_name, dataset_headp)) != Z_OK)
			goto done;
	}
	(void) zonecfg_enddsent(handle);

	/* Get zfs filesystems given to the zone and add to list. */
	if ((err = zonecfg_setfsent(handle)) != Z_OK)
		goto done;
	while (zonecfg_getfsent(handle, &lookup) == Z_OK) {
		if (strcmp(lookup.zone_fs_type, "zfs") == 0) {
			if ((err = ai_report_add_list(zonename,
			    lookup.zone_fs_special, dataset_headp)) != Z_OK)
				goto done;
		}
	}
	(void) zonecfg_endfsent(handle);

	/* unconfigure zone */
	err = zonecfg_destroy(zone, B_FALSE);
	return (err);

done:
	(void) zonecfg_destroy(zone, B_FALSE);
	return (err);
}

/*
 * Called during AI to report the zonepath parent directories and the
 * required zfs datasets for a group of zones.  Also detects collisions
 * among zonepaths and delegated zfs datasets for the given list of zones.
 *
 * Takes as input a file containing a list of zone configuration files
 * each of which is in 'zonecfg export' format.
 * May also take an output file.  If no output file is given then
 * output is directed to stdout.
 */
int
ai_report_func(int argc, char *argv[])
{
	char cname[MAXPATHLEN];		/* config entry in input file */
	int err = Z_OK;
	char *outfile = NULL;
	FILE *infile, *of;
	nvlist_t *zonepath_head = NULL;
	nvlist_t *parent_head = NULL;
	nvlist_t *dataset_head = NULL;
	int arg;

	optind = 0;
	while ((arg = getopt(argc, argv, "f:o:")) != EOF) {
		switch (arg) {
		case 'f':
			input_file_name = optarg;
			break;
		case 'o':
			outfile = optarg;
			break;
		default:
			usage(B_FALSE, HELP_AI_REPORT);
			exit(Z_USAGE);
		}
	}
	if ((argc > optind) || (input_file_name == NULL)) {
		usage(B_FALSE, HELP_AI_REPORT);
		return (Z_USAGE);
	}

	if ((nvlist_alloc(&zonepath_head, 0, 0) != 0) ||
	    (nvlist_alloc(&parent_head, 0, 0) != 0) ||
	    (nvlist_alloc(&dataset_head, 0, 0) != 0)) {
		err = Z_ERR;
		goto done;
	}

	/*
	 * For each entry in file call ai_report_get_info to
	 * get zonepath, zonepath_parent and zfs_datasets and add to
	 * their respective lists.  Will also check that zonepath doesn't
	 * collide with any previously recorded zonepaths.
	 */
	if ((infile = fopen(input_file_name, "r")) == NULL) {
		zerr(gettext("Unable to open file %s: %s ."),
		    input_file_name, strerror(errno));
		err = Z_ERR;
		goto done;
	}
	while (fgets(cname, sizeof (cname), infile) != NULL) {
		chomp_newline(cname);
		err = ai_report_get_info(cname, zonepath_head,
		    parent_head, dataset_head);
		if (err != Z_OK) {
			(void) fclose(infile);
			goto done;
		}
	}
	(void) fclose(infile);

	/* Determine output file */
	if (outfile == NULL) {
		of = stdout;
	} else {
		if ((of = fopen(outfile, "w")) == NULL) {
			zerr(gettext("opening file %s: %s"),
			    outfile, strerror(errno));
			err = Z_ERR;
			goto done;
		}
	}

	/* Print zonepath_parent and zfs_datasets lists. */
	if ((err = ai_report_print_list(of, "zonepath_parent",
	    parent_head)) != Z_OK)
		goto done;

	if ((err = ai_report_print_list(of, "zfs_dataset",
	    dataset_head)) != Z_OK)
		goto done;

	if ((of != NULL) && (of != stdout))
		(void) fclose(of);

done:
	nvlist_free(zonepath_head);
	nvlist_free(parent_head);
	nvlist_free(dataset_head);
	return (err);
}

/*
 * Since yacc is based on reading from a (FILE *) whereas what we get from
 * the command line is in argv format, we need to convert when the user
 * gives us commands directly from the command line.  That is done here by
 * concatenating the argv list into a space-separated string, writing it
 * to a temp file, and rewinding the file so yyin can be set to it.  Then
 * we call read_input(), and only once, since prompting about whether to
 * continue or quit would make no sense in this context.
 */

static int
one_command_at_a_time(int argc, char *argv[])
{
	char *command;
	size_t len = 2; /* terminal \n\0 */
	int i, err;

	for (i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	if ((command = malloc(len)) == NULL) {
		zone_perror(execname, Z_NOMEM, B_TRUE);
		return (Z_ERR);
	}
	(void) strlcpy(command, argv[0], len);
	for (i = 1; i < argc; i++) {
		(void) strlcat(command, " ", len);
		(void) strlcat(command, argv[i], len);
	}
	(void) strlcat(command, "\n", len);
	err = string_to_yyin(command);
	free(command);
	if (err != Z_OK)
		return (err);
	while (!feof(yyin))
		yyparse();
	return (cleanup());
}

static char *
get_execbasename(char *execfullname)
{
	char *last_slash, *execbasename;

	/* guard against '/' at end of command invocation */
	for (;;) {
		last_slash = strrchr(execfullname, '/');
		if (last_slash == NULL) {
			execbasename = execfullname;
			break;
		} else {
			execbasename = last_slash + 1;
			if (*execbasename == '\0') {
				*last_slash = '\0';
				continue;
			}
			break;
		}
	}
	return (execbasename);
}

int
main(int argc, char *argv[])
{
	int err, arg;
	struct stat st;

	/* This must be before anything goes to stdout. */
	setbuf(stdout, NULL);

	saw_error = B_FALSE;
	cmd_file_mode = B_FALSE;
	execname = get_execbasename(argv[0]);

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if (getzoneid() != GLOBAL_ZONEID) {
		zerr(gettext("%s can only be run from the global zone."),
		    execname);
		exit(Z_ERR);
	}

	if (argc < 2) {
		usage(B_FALSE, HELP_USAGE | HELP_SUBCMDS);
		exit(Z_USAGE);
	}
	if (strcmp(argv[1], cmd_to_str(CMD_HELP)) == 0) {
		(void) one_command_at_a_time(argc - 1, &(argv[1]));
		exit(Z_OK);
	}

	while ((arg = getopt(argc, argv, "?f:R:z:")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?')
				usage(B_TRUE, HELP_USAGE | HELP_SUBCMDS);
			else
				usage(B_FALSE, HELP_USAGE);
			exit(Z_USAGE);
			/* NOTREACHED */
		case 'f':
			cmd_file_name = optarg;
			cmd_file_mode = B_TRUE;
			break;
		case 'R':
			if (*optarg != '/') {
				zerr(gettext("root path must be absolute: %s"),
				    optarg);
				exit(Z_USAGE);
			}
			if (stat(optarg, &st) == -1 || !S_ISDIR(st.st_mode)) {
				zerr(gettext(
				    "root path must be a directory: %s"),
				    optarg);
				exit(Z_USAGE);
			}
			zonecfg_set_root(optarg);
			break;
		case 'z':
			if (strcmp(optarg, GLOBAL_ZONENAME) == 0) {
				global_zone = B_TRUE;
			} else if (zonecfg_validate_zonename(optarg) != Z_OK) {
				zone_perror(optarg, Z_BOGUS_ZONE_NAME, B_TRUE);
				usage(B_FALSE, HELP_SYNTAX);
				exit(Z_USAGE);
			}
			(void) strlcpy(zone, optarg, sizeof (zone));
			(void) strlcpy(revert_zone, optarg, sizeof (zone));
			break;
		default:
			usage(B_FALSE, HELP_USAGE);
			exit(Z_USAGE);
		}
	}

	if (strcmp(zone, "") == 0) {
		/* If no zone given, check for auto-install-report as 1st arg */
		if ((optind == 1) && (strcmp(argv[optind++], AI_REPORT) == 0))
			return (ai_report_func((argc - optind),
			    &(argv[optind])));
		usage(B_FALSE, HELP_USAGE);
		exit(Z_USAGE);
	}

	if (optind > argc) {
		usage(B_FALSE, HELP_USAGE);
		exit(Z_USAGE);
	}

	if ((err = zonecfg_access(zone, W_OK)) == Z_OK) {
		read_only_mode = B_FALSE;
	} else if (err == Z_ACCES) {
		read_only_mode = B_TRUE;
		/* skip this message in one-off from command line mode */
		if (optind == argc)
			(void) fprintf(stderr, gettext("WARNING: you do not "
			    "have write access to this zone's configuration "
			    "file;\ngoing into read-only mode.\n"));
	} else {
		fprintf(stderr, "%s: Could not access zone configuration "
		    "store: %s\n", execname, zonecfg_strerror(err));
		exit(Z_ERR);
	}

	if ((handle = zonecfg_init_handle()) == NULL) {
		zone_perror(execname, Z_NOMEM, B_TRUE);
		exit(Z_ERR);
	}

	if (dladm_open(&dld_handle) != DLADM_STATUS_OK)
		zerr(gettext("%s: failed to open dladm handle"), execname);

	/*
	 * This may get set back to FALSE again in cmd_file() if cmd_file_name
	 * is a "real" file as opposed to "-" (i.e. meaning use stdin).
	 */
	if (isatty(STDIN_FILENO))
		ok_to_prompt = B_TRUE;
	if ((gl = new_GetLine(MAX_LINE_LEN, MAX_CMD_HIST)) == NULL)
		exit(Z_ERR);
	if (gl_customize_completion(gl, NULL, cmd_cpl_fn) != 0)
		exit(Z_ERR);
	(void) sigset(SIGINT, SIG_IGN);
	if (optind == argc) {
		if (!cmd_file_mode)
			err = do_interactive();
		else
			err = cmd_file(cmd_file_name);
	} else {
		err = one_command_at_a_time(argc - optind, &(argv[optind]));
	}
	zonecfg_fini_handle(handle);
	if (dld_handle != NULL)
		dladm_close(dld_handle);
	if (brand != NULL)
		brand_close(brand);
	(void) del_GetLine(gl);
	return (err);
}
