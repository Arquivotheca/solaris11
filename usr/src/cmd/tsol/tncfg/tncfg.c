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
 * tncfg is a lex/yacc based command interpreter used to manage template
 * configurations.  The lexer (see tncfg_lex.l) builds up tokens, which
 * the grammar (see tncfg_grammar.y) builds up into commands, some of
 * which takes resources and/or properties as arguments.  See the block
 * comments near the end of tncfg_grammar.y for how the data structures
 * which keep track of these resources and properties are built up.
 *
 * The resource/property data structures are inserted into a command
 * structure (see tncfg.h), which also keeps track of command names,
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
#include <sys/param.h>
#include <locale.h>
#include <libintl.h>
#include <alloca.h>
#include <signal.h>
#include <wait.h>
#include <libtecla.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>
#include <deflt.h>
#include <syslog.h>
#include <libcmdutils.h>
#include <libtsnet.h>
#include <note.h>
#include <pwd.h>
#include <netdb.h>
#include <auth_attr.h>
#include <arpa/inet.h>
#include <libzonecfg.h>
#include <tsol/label.h>
#include <sys/tsol/label_macro.h>
#include <sys/tsol/tndb.h>
#include <nss_dbdefs.h>
#include "tncfg.h"
#include <nssec.h>
#include <nsswitch.h>

#define	pt_to_mask(value)	((unsigned int)(1<<(value)))

#define	TNRHTP_TMP		"/etc/security/tsol/tnrhtptmp"
#define	TNRHDB_TMP		"/etc/security/tsol/tnrhdbtmp"
#define	TNZONECFG_TMP		"/etc/security/tsol/tnzonecfgtmp"

#define	EXIT_OK		0
#define	EXIT_FATAL	1
#define	EXIT_NON_FATAL	2

#define	PRINT_DEFAULT	0x0000
#define	PRINT_NAME	0x0010
#define	PRINT_LONG	0x0020
#define	PRINT_ALL	0x0040

#ifndef TEXT_DOMAIN			/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"
#endif

#if !defined(TEXT_DOMAIN)		/* should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it wasn't */
#endif


typedef struct rhostlist {
	tsol_rhent_t *rhentp;
	boolean_t need_to_commit;
	struct rhostlist *next;
} rhostlist_t;

typedef struct mlplist {
	tsol_mlp_t *mlp;
	struct mlplist *next;
} mlplist_t;

struct help {
	uint_t	cmd_num;
	char	*cmd_name;
	uint_t	flags;
	char	*short_usage;
};

struct prop_help {
	uint_t	prop_num;
	uint_t	flags;
	int	type;
	char	*short_usage;
};

static boolean_t add_rhent(rhostlist_t **, tsol_rhent_t *, boolean_t);

extern int yyparse(void);
extern int lex_lineno;
extern char *_strtok_escape(char *, char *, char **);
extern char *_strpbrk_escape(char *, char *);
extern char *_unescape(char *, char *);
extern char *_escape(char *, char *);


#define	MAX_LINE_LEN	1024
#define	MAX_CMD_HIST	1024
#define	MAX_CMD_LEN	1024

/*
 * Each SHELP_ should be a simple string.
 */

#define	SHELP_ADD	"add <property-name>=<property-value>"
#define	SHELP_CLEAR	"clear <property-name>"
#define	SHELP_COMMIT	"commit"
#define	SHELP_DELETE	"delete [-F]"
#define	SHELP_EXIT	"exit [-F]"
#define	SHELP_EXPORT	"export [-f output-file]"
#define	SHELP_HELP	"help [usage] [subcommands] [properties] "\
	"[<subcommand>] [<property>]"
#define	SHELP_INFO	"info [<property-value>]"
#define	SHELP_REMOVE	"remove <property-name>=<property-value>"
#define	SHELP_REVERT	"revert [-F]"
#define	SHELP_SET	"set <property-name>=<property-value>"
#define	SHELP_VERIFY	"verify"
#define	SHELP_LIST	"list"
#define	SHELP_GET	"get host=<IP address[/prefix]>"

static struct help helptab[] = {
	{ CMD_ADD,	"add",		HELP_ADDREM,	SHELP_ADD, },
	{ CMD_CLEAR,	"clear",	HELP_PROPS,	SHELP_CLEAR, },
	{ CMD_COMMIT,	"commit",	0,		SHELP_COMMIT, },
	{ CMD_DELETE,	"delete",	0,		SHELP_DELETE, },
	{ CMD_EXIT,	"exit",		0,		SHELP_EXIT, },
	{ CMD_EXPORT,	"export",	0,		SHELP_EXPORT, },
	{ CMD_HELP,	"help",		0,		SHELP_HELP },
	{ CMD_INFO,	"info",		HELP_PROPS,	SHELP_INFO, },
	{ CMD_REMOVE,	"remove",	HELP_ADDREM,	SHELP_REMOVE, },
	{ CMD_REVERT,	"revert",	0,		SHELP_REVERT, },
	{ CMD_SET,	"set",		HELP_SIMPLE,	SHELP_SET, },
	{ CMD_VERIFY,	"verify",	0,		SHELP_VERIFY, },
	{ CMD_LIST,	"list",		0,		SHELP_LIST, },
	{ CMD_GET,	"get",		0,		SHELP_GET, },
	{ 0 },
};

#define	SHELP_TPNAME	"<template name>"
#define	SHELP_HTYPE	"cipso|unlabeled"
#define	SHELP_DOI	"<positive integer>"
#define	SHELP_LABEL	"<sensitivity label>"
#define	SHELP_HOST	"<IP address[/prefix]>"

#define	SHELP_ZNNAME	"<zone name>"
#define	SHELP_YESNO	"yes|no"
#define	SHELP_MLP	"<port>[-<port2>]/tcp|udp"

#define	TEXT_TYPE	1
#define	INT_TYPE	2
#define	LABEL_TYPE	3
#define	IPADDR_TYPE	4
#define	MLP_TYPE	5

/*
 * TODO: These defines are temporary until they are
 * avaliable in another include file.
 */
#define	EX_SUCCESS 0
#define	EX_NAME_NOT_EXIST 9
#define	EX_UPDATE 10
/*
 * Help syntax for template properties
 */
static struct prop_help *prop_helptab;
static struct prop_help tmpl_helptab[] = {
	{ PT_TPNAME,		HELP_PRESERVE,	TEXT_TYPE,	SHELP_TPNAME, },
	{ PT_HOSTTYPE,		HELP_PRESERVE,	TEXT_TYPE,	SHELP_HTYPE, },
	{ PT_DOI,		HELP_PRESERVE,	INT_TYPE,	SHELP_DOI, },
	{ PT_DEFLABEL,		HELP_PRESERVE,	LABEL_TYPE,	SHELP_LABEL, },
	{ PT_MINLABEL,		HELP_PRESERVE,	LABEL_TYPE,	SHELP_LABEL, },
	{ PT_MAXLABEL,		HELP_PRESERVE,	LABEL_TYPE,	SHELP_LABEL, },
	{ PT_LABELSET,		HELP_ADDREM,	LABEL_TYPE,	SHELP_LABEL, },
	{ PT_HOSTADDR,		HELP_ADDREM,	IPADDR_TYPE,	SHELP_HOST, },
	{ 0 },
};

/*
 * Help syntax for zone properties
 */
static struct prop_help zone_helptab[] = {
	{ PT_TPNAME,		HELP_PRESERVE,	TEXT_TYPE,	SHELP_ZNNAME, },
	{ PT_ZONELABEL,		HELP_PRESERVE,	LABEL_TYPE,	SHELP_LABEL, },
	{ PT_VISIBLE,		HELP_PRESERVE,	INT_TYPE,	SHELP_YESNO, },
	{ PT_MLP_PRIVATE,	HELP_ADDREM,	MLP_TYPE,	SHELP_MLP, },
	{ PT_MLP_SHARED,	HELP_ADDREM,	MLP_TYPE,	SHELP_MLP, },
	{ 0 },
};

/* These *must* match the order of the PT_ define's from tncfg.h */
char *prop_types[] = {
	"error",
	"name",
	TP_HOSTTYPE,
	TP_DOI,
	TP_DEFLABEL,
	"min_label",
	"max_label",
	"aux_label",
	"host",
	"label",
	"visible",
	"mlp_private",
	"mlp_shared",
	NULL
};


#define	FILES_SCOPE 1
#define	LDAP_SCOPE 2
#define	TEMP_SCOPE 3
static int repo_scope = -1;
static const char **attr_plist = NULL;
static sec_repository_t *mod_rep = NULL;

/*
 * The various _cmds[] lists below are for command tab-completion.
 */

static const char **prop_scope_cmds;
static const char *tmpl_scope_cmds[] = {
	"list",
	"commit",
	"delete",
	"exit",
	"export",
	"get host=",
	"help properties",
	"help subcommands",
	"help usage",
	"info",
	"info name",
	"info doi",
	"info host_type",
	"info def_label",
	"info min_label",
	"info max_label",
	"info aux_label",
	"info host",
	"revert",
	"verify",
	"add host=",
	"add aux_label=\"",
	"clear aux_label",
	"clear host",
	"remove host=",
	"remove aux_label=\"",
	"set name=",
	"set doi=",
	"set host_type=cipso",
	"set host_type=unlabeled",
	"set def_label=\"",
	"set min_label=\"",
	"set max_label=\"",
	"add aux_label=\"",
	"add host=",
	NULL
};
static const char *zone_scope_cmds[] = {
	"list",
	"commit",
	"delete",
	"exit",
	"get host=",
	"export",
	"help properties",
	"help subcommands",
	"help usage",
	"info",
	"info name",
	"info visible",
	"info label",
	"info mlp_private",
	"info mlp_shared",
	"revert",
	"verify",
	"add mlp_private=",
	"add mlp_shared=",
	"clear mlp_private",
	"clear mlp_shared",
	"remove mlp_private=",
	"remove mlp_shared=",
	"set name=",
	"set visible=yes",
	"set visible=no",
	"set label=\"",
	NULL
};

/* Global variables */

/* set early in main(), never modified thereafter, used all over the place */
static char *execname;
static char *username;
static char *tpname = NULL;
static char *orig_tpname = NULL;
static char *zonename = NULL;
static char *orig_zonename = NULL;

/* set in main(), used all over the place */

/* used all over the place */

/* set in modifying functions, checked in read_input() */
static int need_to_commit = 0;
boolean_t saw_error;

/* set in yacc parser, checked in read_input() */
boolean_t newline_terminated;

/* set in main(), checked in lex error handler */
boolean_t cmd_file_mode;

/* set in exit_func(), checked in read_input() */
static boolean_t time_to_exit = B_FALSE, force_exit = B_FALSE;

/* used in short_usage() and zerr() */
static char *cmd_file_name = NULL;

/* checked in read_input() and other places */
static boolean_t ok_to_prompt = B_FALSE;

/* set and checked in initialize_tmpl() */
tsol_tpent_t	*cur_template = NULL;
rhostlist_t	*cur_hosts, *cur_host, *del_hosts = NULL;
static boolean_t resource_exists = B_FALSE;

/* set and checked in initialize_zone() */
tsol_zcent_t	*cur_zone = NULL;
mlplist_t	*cur_private_mlps, *cur_shared_mlps = NULL;

/* initialized in do_interactive() */
static boolean_t interactive_mode;

/* set in main(), checked in multiple places */
static boolean_t read_only_mode = B_FALSE;
static boolean_t resource_verified = B_FALSE;

/* scope is outer/template or inner/resource */
static int resource_scope = -1;	/* in the RT_ list from tncfg.h */

int num_prop_vals;		/* for grammar */

static GetLine *gl;	/* The gl_get_line() resource object */

/* Functions begin here */

static char *
pt_to_str(int prop_type)
{
	assert(prop_type >= PT_MIN && prop_type <= PT_MAX);
	return (prop_types[prop_type]);
}

static char *
cmd_to_str(int cmd_num)
{
	assert(cmd_num >= CMD_MIN && cmd_num <= CMD_MAX);
	return (helptab[cmd_num].cmd_name);
}

static void zerr(const char *, ...);

static void
free_rhent(rhostlist_t	*host)
{
	tsol_freerhent(host->rhentp);
	free(host);
}
static void
free_mlpent(mlplist_t	*mlpent)
{
	free(mlpent->mlp);
	free(mlpent);
}

static void
free_plist(const char **plist)
{
	int i = 0;
	char *name;

	if (plist) {
		while (plist[i]) {
			name = (char *)plist[i++];
			free(name);
		}
		free(plist);
		plist = NULL;
	}
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
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	(void) snprintf(outstr, outstrsize, "\"%s\"", instr);
	return (outstr);
}

static void
l_to_str(const m_label_t *l, char **str, int ltype)
{
	if (label_to_str(l, str, ltype, DEF_NAMES) != 0)
		*str = strdup(gettext("translation failed"));
}

/*
 * Produce ascii format of address and prefix length
 */
static void
translate_inet_addr(tsol_rhent_t *rhentp, int *alen, char abuf[], int abuflen)
{
	void *aptr;
	char tmpbuf[20];	/* long enoigh for prefix */

	(void) snprintf(tmpbuf, sizeof (tmpbuf), "/%d", rhentp->rh_prefix);

	if (rhentp->rh_address.ta_family == AF_INET6) {
		aptr = &(rhentp->rh_address.ta_addr_v6);
		(void) inet_ntop(rhentp->rh_address.ta_family, aptr, abuf,
		    abuflen);
		if (strlcat(abuf, tmpbuf, abuflen) >= abuflen)
			(void) fprintf(stderr, gettext(
			    "tnctl: buffer overflow detected: %s\n"),
			    abuf);
		*alen = strlen(abuf);
	} else {
		aptr = &(rhentp->rh_address.ta_addr_v4);
		(void) inet_ntop(rhentp->rh_address.ta_family, aptr, abuf,
		    abuflen);
		if (strlcat(abuf, tmpbuf, abuflen) >= abuflen)
			(void) fprintf(stderr, gettext(
			    "tnctl: buffer overflow detected: %s\n"),
			    abuf);
		*alen = strlen(abuf);
	}
}

#define	MAX_MLP_STRLEN 30
#define	MAX_MLP_TOKLEN 10

static const char **
get_plist(int prop_type)
{
	int i = 0;
	int maxattrs = 100;
	const char **plist;

	if ((plist =
	    malloc((maxattrs + 1) * sizeof (char *))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	free_plist(attr_plist);
	attr_plist = plist;

	i = 0;
	if (resource_scope == RT_ZONE) {
		mlplist_t *next = NULL;
		tsol_mlp_t *mlp;
		struct protoent *pe;

		if (prop_type == PT_MLP_SHARED)
			next = cur_shared_mlps;
		else if (prop_type == PT_MLP_PRIVATE)
			next = cur_private_mlps;
		while (next) {
			char *mlpstr;
			char token[MAX_MLP_TOKLEN];

			if ((mlpstr =
			    malloc((MAX_MLP_STRLEN))) == NULL) {
				zerr(gettext("Out of memory"));
				exit(Z_ERR);
			}

			mlp = next->mlp;
			(void) snprintf(mlpstr, MAX_MLP_STRLEN, "%u",
			    mlp->mlp_port);
			if ((mlp->mlp_port_upper != 0) &&
			    (mlp->mlp_port != mlp->mlp_port_upper)) {
				(void) snprintf(token, MAX_MLP_TOKLEN,
				"-%u", mlp->mlp_port_upper);
				strcat(mlpstr, token);
			}
			if ((pe = getprotobynumber(mlp->mlp_ipp)) == NULL)
				(void) snprintf(token, MAX_MLP_TOKLEN,
				"/%u", mlp->mlp_ipp);
			else
				(void) snprintf(token, MAX_MLP_TOKLEN,
				"/%s", pe->p_name);
			strcat(mlpstr, token);
			plist[i++] = mlpstr;
			next = next->next;
			if (i == maxattrs)
				break;
		}
	} else {
		if (prop_type == PT_LABELSET) {
			const m_label_t *l1, *l2;
			char *str;

			l1 = (const m_label_t *)&cur_template->tp_gw_sl_set[0];
			l2 = (const m_label_t *)&cur_template->
			    tp_gw_sl_set[NSLS_MAX];
			for (; l1 < l2; l1++) {
				if (!BLTYPE(l1, SUN_SL_ID))
					continue;
				l_to_str(l1, &str, M_LABEL);
				plist[i++] = quoteit(str);
				free(str);
			}
		} else if (prop_type == PT_HOSTADDR) {
			rhostlist_t *next = cur_hosts;

			while (next) {
				/*  5 bytes for prefix */
				char abuf[INET6_ADDRSTRLEN + 5];
				int alen;

				translate_inet_addr(next->rhentp,
				    &alen, abuf, sizeof (abuf));
				plist[i++] = strdup(abuf);
				next = next->next;
				if (i == maxattrs)
					break;
			}
		}
	}
	plist[i] = NULL;
	return (plist);
}

static boolean_t
initial_match(const char *line1, const char *line2, int word_end)
{
	if (word_end <= 0)
		return (B_TRUE);
	return (strncmp(line1, line2, word_end) == 0);
}

static int
add_stuff(WordCompletion *cpl, const char *line1, const char **list,
    int word_start, int word_end)
{
	int i, err;
	int len = word_end - word_start;

	for (i = 0; list != NULL && list[i] != NULL; i++) {
		if (initial_match(line1 + word_start, list[i], len)) {
			err = cpl_add_completion(cpl, line1, word_start,
			    word_end, list[i] + len, "", "");
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

static int
value_completion(struct prop_help *ph, const char **scope_cmds,
    WordCompletion *cpl, const char *line, int word_end)
{
	int i;

	for (i = CMD_MIN; i <= CMD_MAX; i++) {
		if (helptab[i].flags & (HELP_VALUE)) {
			int j;
			int cmd_num = helptab[i].cmd_num;

			for (j = 0; ph[j].prop_num != 0; j++) {
				int prop_num = ph[j].prop_num;
				char *prop_name = pt_to_str(prop_num);
				char subcommand[100];
				int len;

				len = snprintf(subcommand, 100,
				    "%s %s=", helptab[i].cmd_name,
				    prop_name);
				if (strncmp(line, subcommand, len) != 0)
					continue;
				switch (ph[j].type) {
				case MLP_TYPE:
				case IPADDR_TYPE:
				case LABEL_TYPE:
					if (cmd_num == CMD_REMOVE)
						return (add_stuff(cpl, line,
						    get_plist(prop_num),
						    len, word_end));
					else
						break;
				}
			}
		}
	}
	return (add_stuff(cpl, line, scope_cmds, 0, word_end));
}

static
/* ARGSUSED */
CPL_MATCH_FN(cmd_cpl_fn)
{
	return (value_completion(prop_helptab, prop_scope_cmds,
	    cpl, line, word_end));
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
			case PROP_VAL_LIST:
				free_list(pp->pv_list);
				break;
			}
		}
	for (i = 0; i < cmd->cmd_argc; i++)
		free(cmd->cmd_argv[i]);
	free(cmd);
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
		case CMD_EXIT:
			return (gettext("Exits the program.  The -F flag can "
			    "be used to force the action."));
		case CMD_EXPORT:
			return (gettext("Prints resource to standard "
			    "output, or to output-file if\n\tspecified, in "
			    "a form suitable for use in a command-file."));
		case CMD_ADD:
			return (gettext("Adds specified property to "
			    "the resource."));
		case CMD_DELETE:
			return (gettext("Deletes the specified resource.\n\t"
			    "The -F flag can be used to force the action."));
		case CMD_REMOVE:
			return (gettext("Removes specified property from "
			    "the resource."));
		case CMD_SET:
			return (gettext("Sets property values."));
		case CMD_CLEAR:
			return (gettext("Clears property values."));
		case CMD_GET:
			return (gettext("Displays the matching "
			    "template for a host address."));
		case CMD_LIST:
			return (gettext("Displays the names of the "
			    "active templates or zones."));
		case CMD_INFO:
			return (gettext("Displays information about the "
			    "current resource."));
		case CMD_VERIFY:
			return (gettext("Verifies current resource "
			    "for correctness (some properties types\n\thave "
			    "required values)."));
		case CMD_COMMIT:
			(void) snprintf(line, sizeof (line),
			    gettext("Commits current resource.  "
			    "This operation is\n\tattempted "
			    "automatically upon completion of a %s "
			    "session."), execname);
			return (line);
		case CMD_REVERT:
			return (gettext("Reverts the resource back to the "
			    "last committed state.  The -F flag\n\tcan be "
			    "used to force the action."));
	}
	/* NOTREACHED */
	return (NULL);
}

static char *
long_prop_help(int prop_num)
{
	switch (prop_num) {
	case PT_TPNAME:
		switch (resource_scope) {
		case RT_TEMPLATE:
		default:
		return (gettext("The name of the template. "\
		    "The initial value for the name is\n\tspecified  "\
		    "using -t option via the command line. Setting\n\t"\
		    "this property results in a newly named property "\
		    "which inherits\n\tthe settings from the previous "\
		    "template."));
		case RT_ZONE:
		return (gettext("The name of the zone. "\
		    "The initial value for the name is\n\tspecified  "\
		    "using -z option via the command line. Setting\n\t"\
		    "this property results in a newly named property "\
		    "which inherits\n\tthe settings from the previous "\
		    "zone."));
		}
	case PT_DOI:
		return (gettext("A positive integer specifying "\
		    "the Domain of Interpretation.\n\tThe default is 1."));
	case PT_DEFLABEL:
		return (gettext("The default label assigned to IP packets "\
		    "that aren't explicitly\n\tlabeled "\
		    "via cipso or IPsec."));
	case PT_HOSTTYPE:
		return (gettext("The 'cipso' host type is used for "\
		    "hosts that explicitly label\n\ttheir IP packets. " \
		    "When the 'unlabeled' host type is used,\n\tthe value " \
		    "specified in the 'def_label' property is implicitly\n\t" \
		    "applied to the received IP packets."));
	case PT_MINLABEL:
		return (gettext("The minimum label in the range for "\
		    "IP packets that are accepted\n\tby multilevel services."));
	case PT_MAXLABEL:
		return (gettext("The maximim label in the range for "\
		    "IP packets that are accepted\n\tby multilevel services."));
	case PT_LABELSET:
		return (gettext("Additional labels, outside of the specified " \
		    "range, for IP packets\n\tthat are accepted by " \
		    "multilevel services. Up to four may be specified."));
	case PT_HOSTADDR:
		return (gettext("A host name or an IP address to which the " \
		    "template properties\n\tapply. For IP addresses, " \
		    "both IPv4 and IPv6 formats may be\n\tused, " \
		    "followed by an optional slash and " \
		    "prefix specifying the\n\tnumber of bits to match "\
		    "against IP addresses. When a prefix\n\tis not specified, "\
		    "octets containing zero are treated as wildcards.\n\tThe " \
		    "IPv4 address 0.0.0.0 has a prefix length of of zero " \
		    "and\n\tmatches any IPv4 address. The matching entry with "\
		    "the longest\n\tprefix determines which template to "
		    "assign to an IP packet"));
	case PT_VISIBLE:
		return (gettext("Specifies whether the zone responds to " \
		    "ping requests fron hosts with\n\tunequal labels. " \
		    "Default is 'no'."));
	case PT_ZONELABEL:
		return (gettext("The sensitivity label of the zone. It "\
		    "must be unique."));
	case PT_MLP_PRIVATE:
		return (gettext("A single port number, or a range of ports "\
		    "that privileged services\n\tmay bind to and then accept " \
		    "requests from clients whose labels are\n\twithin the " \
		    "range or set specifed in their matching templates.\n\t" \
		    "The propery applies to all " \
		    "interfaces that are private to the zone."));
	case PT_MLP_SHARED:
		return (gettext("A single port number, or a range of ports "\
		    "that privileged services\n\tmay bind to and then accept " \
		    "requests from clients whose labels are\n\twithin the " \
		    "range or set specifed in their matching templates.\n\t" \
		    "The propery applies to any 'all-zones' interfaces, " \
		    "and must not\n\toverlap with the 'mlp_shared' "\
		    "ports specified for other zones."));
	/* NOTREACHED */
	}
	return (NULL);
}

/*
 * scope_usage() is simply used when a command is called from the wrong scope.
 */

static void
scope_usage(uint_t prop_num)
{
	zerr(gettext("The %s property is only valid in the %s context."),
	    pt_to_str(prop_num),
	    (resource_scope == RT_TEMPLATE) ?
	    gettext("zone") : gettext("template"));
	saw_error = B_TRUE;
}

/*
 * longer_prop_usage() is for 'help foo' and 'foo -?': call long_prop_usage()
 * and also any extra usage() flags as appropriate for whatever command.
 */

void
longer_prop_usage(uint_t prop_num)
{
	int i;
	char *prop_name = pt_to_str(prop_num);

	for (i = 0; prop_helptab[i].prop_num != 0; i++) {
		if (prop_num == prop_helptab[i].prop_num) {
			(void) fprintf(stdout, "\t%s\n",
			    long_prop_help(prop_num));
			(void) fprintf(stdout, "%s:\n", gettext("usage"));
			(void) fprintf(stdout, "\t%s=%s\n", prop_name,
			    gettext(prop_helptab[i].short_usage));
			return;
		}
	}
	scope_usage(prop_num);
}

static void
property_syntax(FILE *fp, struct prop_help *ph, int cmd_num, int flags)
{
	int i;

	for (i = 0; ph[i].prop_num != 0; i++) {
		if (ph[i].flags & flags) {
			(void) fprintf(fp, "\t%s %s=%s\n",
			    cmd_to_str(cmd_num),
			    pt_to_str(ph[i].prop_num),
			    gettext(ph[i].short_usage));
		} else if ((flags & HELP_PROPS) &&
		    ((cmd_num != CMD_CLEAR) ||
		    (~ph[i].flags & HELP_REQUIRED))) {
			(void) fprintf(fp, "\t%s %s\n",
			    cmd_to_str(cmd_num),
			    pt_to_str(ph[i].prop_num));
		}
	}
}

/*
 * Called with verbose TRUE when help is explicitly requested, FALSE for
 * unexpected errors.
 */

void
usage(boolean_t verbose, uint_t cmd_num, uint_t flags)
{
	FILE *fp = verbose ? stdout : stderr;
	int i;

	if (flags & HELP_META) {
		(void) fprintf(fp, gettext("More help is available for the "
		    "following:\n"));
		(void) fprintf(fp, "\tusage ('%s usage')\n\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\n\tsubcommands ('%s subcommands')\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\tproperties ('%s properties')\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\tusage ('%s usage')\n\n",
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, gettext("You may obtain help on any "
		    "command by typing '%s <command>.'\n"),
		    cmd_to_str(CMD_HELP));
		(void) fprintf(fp, gettext("You may also obtain help on any "
		    "property by typing '%s <property>.'\n"),
		    cmd_to_str(CMD_HELP));
	}
	if (flags & HELP_USAGE) {
		(void) fprintf(fp, "%s:\t%s %s\n", gettext("usage"),
		    execname, cmd_to_str(CMD_HELP));
		(void) fprintf(fp, "\t%s -z <zonename>  [-e] "\
		    "[<subcommand>]\n", execname);
		(void) fprintf(fp, "\t%s -z <zonename> "
		    " [-e] -f <command-file>\n", execname);
		(void) fprintf(fp, "\t%s [-t <template>] "\
		    "[-e|-S [files|ldap]] [<subcommand>]\n", execname);
		(void) fprintf(fp, "\t%s [-t <template>] "
		    "[-e|-S [files|ldap]] -f <command-file>\n", execname);
	}
	if (flags & HELP_SUBCMDS) {
		(void) fprintf(fp, "%s:\n", gettext("Subcommands"));
		for (i = 0; i <= CMD_MAX; i++) {
			(void) fprintf(fp, "\t%s\n", helptab[i].short_usage);
			if (verbose)
				(void) fprintf(fp, "\t%s\n\n", long_help(i));
		}
	}
	if (flags & HELP_PROPERTIES) {
		(void) fprintf(fp, "%s:\n", gettext("Properties"));
			for (i = 0; prop_helptab[i].prop_num != 0; i++) {
			(void) fprintf(fp, "\t%s=%s\n",
			    pt_to_str(prop_helptab[i].prop_num),
			    gettext(prop_helptab[i].short_usage));
		}
	}
	if (flags & (HELP_PROPS|HELP_VALUE)) {
		property_syntax(fp, prop_helptab, cmd_num,
		    helptab[cmd_num].flags);
	}
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
	(void) fprintf(stderr, "%s:\n\t%s\n", gettext("usage"),
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
	(void) fprintf(set_saw ? stderr : stdout, "\t%s\n", long_help(cmd_num));
	(void) fprintf(set_saw ? stderr : stdout, "%s:\n\t%s\n",
	    gettext("usage"), helptab[cmd_num].short_usage);
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
		usage(B_FALSE, cmd_num, helptab[cmd_num].flags);
	}
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

static boolean_t
resource_is_read_only(int cmd_num)
{
	if (read_only_mode) {
		zerr(gettext("The %s is read-only. Can't %s properties."),
		    (resource_scope == RT_TEMPLATE) ?
		    gettext("template") : gettext("zone"),
		    cmd_to_str(cmd_num));
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}

static boolean_t
cipso_representable(const bslabel_t *lab, int prop_type)
{
	const _blevel_impl_t *blab = (const _blevel_impl_t *)lab;
	int lclass;
	uint32_t c8;
	boolean_t valid = B_TRUE;
	char *str;
	m_label_t admin_high;

	if ((resource_scope == RT_TEMPLATE) &&
	    cur_template->host_type != SUN_CIPSO)
		return (valid);

	l_to_str(lab, &str, M_LABEL);

	bslhigh(&admin_high);
	if (prop_type == PT_MAXLABEL) {
		if (blequal(lab, &admin_high))
			return (valid);
	}

	lclass = LCLASS(blab);
	if (lclass & 0xff00) {
		(void) fprintf(stderr, gettext("The %s '%s'\n"
		    " has a classification greater than 255.\n"),
		    pt_to_str(prop_type), str);
		valid = B_FALSE;
	}
	c8 = blab->compartments.c8;
#ifdef  _BIG_ENDIAN
	if (c8 & 0x0000ffff) {
#else
	if (c8 & 0xffff0000) {
#endif
		(void) fprintf(stderr, gettext("The %s '%s'\n"
		    " has compartment bits greater than 240.\n"),
		    pt_to_str(prop_type), str);
		valid = B_FALSE;
	}
	return (valid);
}

static tsol_rhent_t *
host2rhent(char *host)
{
	tsol_rhstr_t rhstr;
	tsol_rhent_t *rhentp;
	struct hostent *hp;
	in6_addr_t in6;
	int err;
	char *errstr;
	char abuf[INET6_ADDRSTRLEN];

	if (isalpha(host[0])) {
		rhentp = malloc(sizeof (tsol_rhent_t));
		if ((hp = getipnodebyname(host, AF_INET6,
		    AI_ALL | AI_ADDRCONFIG | AI_V4MAPPED, &err))
		    == NULL) {
			zerr(gettext("unknown host: %s"), host);
			return (NULL);
		}
		(void) memset(rhentp, 0, sizeof (tsol_rhent_t));
		(void) memcpy(&in6, hp->h_addr, hp->h_length);

		if (IN6_IS_ADDR_V4MAPPED(&in6)) {
			rhentp->rh_address.ta_family = AF_INET;
			IN6_V4MAPPED_TO_INADDR(&in6,
			    &rhentp->rh_address.ta_addr_v4);
			(void) inet_ntop(AF_INET,
			    &rhentp->rh_address.ta_addr_v4, abuf,
			    sizeof (abuf));
			rhentp->rh_prefix = 32;
		} else {
			rhentp->rh_address.ta_family = AF_INET6;
			rhentp->rh_address.ta_addr_v6 = in6;
			(void) inet_ntop(AF_INET6, &in6,
			    abuf, sizeof (abuf));
			rhentp->rh_prefix = 128;
		}
		strlcpy(rhentp->rh_template, tpname, TNTNAMSIZ);
	} else {
		rhstr.template = tpname;
		rhstr.address = host;
		if (strchr(rhstr.address, ':') == NULL)
			rhstr.family = AF_INET;
		else
			rhstr.family = AF_INET6;

		rhentp = rhstr_to_ent(&rhstr, &err, &errstr);
		if (rhentp == NULL) {
			zerr(gettext("Invalid host: %s"), errstr);
			return (NULL);
		}
	}
	return (rhentp);
}

static rhostlist_t *
find_rhent(rhostlist_t **current, char *address)
{
	rhostlist_t **prev, *next;
	tsol_rhent_t *rhentp;

	if (*current == NULL) {
		return (NULL);
	}
	if ((rhentp = host2rhent(address)) == NULL) {
		saw_error = B_TRUE;
		return (NULL);
	}

	prev = current;
	next = *prev;
	while (next) {
		if (TNADDR_EQ(&rhentp->rh_address, &next->rhentp->rh_address)) {
			return (next);
		}
		prev = &next->next;
		next = *prev;
	}
	/*
	 * If the entry isn't in the current host list
	 * check if it is already loaded in the kernel.
	 * If so, add it to the deleted list so it will
	 * be removed when the template is committed.
	 */
	fprintf(stderr, "repo_scope = %d\n", repo_scope);
	if (repo_scope == TEMP_SCOPE) {
		if (tnrh(TNDB_GET, rhentp) == 0) {
			if (strncmp(rhentp->rh_template,
			    tpname, TNTNAMSIZ) == 0) {
				printf("%s already matches "
				    "this template\n", address);
				(void) add_rhent(&del_hosts, rhentp, B_TRUE);
				need_to_commit |= pt_to_mask(PT_HOSTADDR);
				free(rhentp);
				return (NULL);
			}
		}
	}
	zerr(gettext("The host address does not "
	    "match an existing entry."));
	saw_error = B_TRUE;
	free(rhentp);
	return (NULL);
}

static boolean_t
remove_rhent(rhostlist_t **current, rhostlist_t *host)
{
	rhostlist_t **prev, *next, *new;
	boolean_t move2del = B_FALSE;

	prev = current;
	next = *prev;

	/*
	 * If an existing template is being modified
	 * then we need to retain this host entry
	 * so that it can be explicitly deleted later.
	 */
	if (current == &cur_hosts)
		move2del = B_TRUE;

	while (next) {
		if (TNADDR_EQ(&host->rhentp->rh_address,
		    &next->rhentp->rh_address)) {
			new = next;
			*prev = next->next;
			next = *prev;
			new->next = NULL;
			if (move2del) {
				if (!new->need_to_commit)
					(void) add_rhent(&del_hosts,
					    new->rhentp,
					    new->need_to_commit);
				free(new);
			} else {
				free_rhent(new);
			}
			return (B_TRUE);
		}
		prev = &next->next;
		next = *prev;
	}
	return (B_FALSE);
}

static boolean_t
add_rhent(rhostlist_t **current, tsol_rhent_t *rhentp, boolean_t commit)
{
	rhostlist_t **prev, *next;

	if ((cur_host = (rhostlist_t *)malloc(sizeof (rhostlist_t)))
	    == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	cur_host->rhentp = rhentp;
	cur_host->need_to_commit = commit;
	cur_host->next = NULL;

	/*
	 * If we're adding an entry to cur_hosts
	 * make sure it isn't also in the list of
	 * hosts to delete.
	 */
	if (current == &cur_hosts) {
		if (remove_rhent(&del_hosts, cur_host))
			cur_host->need_to_commit = B_FALSE;
	}

	if (*current == NULL) {
		*current = cur_host;
		cur_host->next = NULL;
		return (B_TRUE);
	}

	prev = current;
	next = *prev;
	while (next) {
		if (TNADDR_EQ(&rhentp->rh_address,
		    &next->rhentp->rh_address)) {
			/*
			 * Command is already in the template
			 */
			return (B_FALSE);
		}
		prev = &next->next;
		next = *prev;
	}
	cur_host->next = NULL;
	*prev = cur_host;
	return (B_TRUE);
}

static int
str_to_mlp(char *mlp_str, tsol_mlp_t *zone_mlp)
{
	char *cp;
	ulong_t ulv;
	struct protoent proto;
	char gbuf[64];	/* long enough for "x-y/proto" */

	(void) memset(zone_mlp, 0, sizeof (tsol_mlp_t));

	ulv = strtoul(mlp_str, &cp, 0);
	zone_mlp->mlp_port = (uint16_t)ulv;
	zone_mlp->mlp_port_upper = 0;
	if (ulv == 0 || ulv > 65535)
		return (-1);
	if (*cp == '-') {
		ulv = strtol(cp + 1, &cp, 0);
		zone_mlp->mlp_port_upper = (uint16_t)ulv;
		if (ulv <= zone_mlp->mlp_port || ulv > 65535)
			return (-1);
	}
	if (*cp != '/')
		return (-1);
	mlp_str = cp + 1;
	ulv = strtol(mlp_str, &cp, 0);
	if (errno == 0 && ulv <= 255 && *cp == '\0')
		zone_mlp->mlp_ipp = (uint8_t)ulv;
	else if (getprotobyname_r(mlp_str, &proto, gbuf,
	    sizeof (gbuf)) != NULL) {
		zone_mlp->mlp_ipp = proto.p_proto;
	} else {
		return (-1);
	}
	return (0);
}

static mlplist_t *
find_mlp(mlplist_t **current, char *mlpstr)
{
	mlplist_t **prev, *next;
	tsol_mlp_t mlp;

	if (*current == NULL) {
		return (NULL);
	}
	if ((str_to_mlp(mlpstr, &mlp)) == -1) {
		saw_error = B_TRUE;
		return (NULL);
	}

	prev = current;
	next = *prev;
	while (next) {
		if (mlp.mlp_port == next->mlp->mlp_port &&
		    mlp.mlp_ipp == next->mlp->mlp_ipp) {
			return (next);
		}
		prev = &next->next;
		next = *prev;
	}
	return (NULL);
}

static boolean_t
remove_mlp(mlplist_t **current, mlplist_t *mlpent)
{
	mlplist_t **prev, *next, *new;

	prev = current;
	next = *prev;

	while (next) {
		if (mlpent->mlp->mlp_port == next->mlp->mlp_port &&
		    mlpent->mlp->mlp_ipp == next->mlp->mlp_ipp) {
			new = next;
			*prev = next->next;
			next = *prev;
			new->next = NULL;
			free_mlpent(new);
			return (B_TRUE);
		}
		prev = &next->next;
		next = *prev;
	}
	return (B_FALSE);
}

static boolean_t
add_mlp(mlplist_t **current, tsol_mlp_t *new_mlp)
{
	mlplist_t **prev, *next;
	mlplist_t *cur_mlp;
	tsol_mlp_t *mlp;

	if ((cur_mlp = (mlplist_t *)malloc(sizeof (mlplist_t)))
	    == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	if ((mlp = (tsol_mlp_t *)malloc(sizeof (tsol_mlp_t)))
	    == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	*mlp = *new_mlp;
	if (mlp->mlp_port_upper == 0)
		mlp->mlp_port_upper = mlp->mlp_port;
	cur_mlp->mlp = mlp;
	cur_mlp->next = NULL;

	if (*current == NULL) {
		*current = cur_mlp;
		cur_mlp->next = NULL;
		return (B_TRUE);
	}

	prev = current;
	next = *prev;
	while (next) {
		if (next->mlp->mlp_port_upper == 0)
			next->mlp->mlp_port_upper = next->mlp->mlp_port;
		if (mlp->mlp_ipp == next->mlp->mlp_ipp &&
		    !(mlp->mlp_port_upper < next->mlp->mlp_port ||
		    mlp->mlp_port > next->mlp->mlp_port_upper)) {
			/*
			 * MLP is already in the zone's list
			 */
			return (B_FALSE);
		}
		prev = &next->next;
		next = *prev;
	}
	cur_mlp->next = NULL;
	*prev = cur_mlp;
	return (B_TRUE);
}

/*
 * This code should be replaced by functions that
 * do specific files and ldap lookups.
 */
static void
gettphosts(char *template)
{
	tsol_rhent_t *rhentp;
	nss_XbyY_buf_t *buf = NULL;

	init_nss_buffer(SEC_REP_DB_TNRHDB, &buf);
	mod_rep->rops->set_rhent(1);
	while ((rhentp = mod_rep->rops->get_rhent(buf)) != NULL) {
		if (strncmp(rhentp->rh_template, template, TNTNAMSIZ) == 0) {

			add_rhent(&cur_hosts, rhentp, B_FALSE);
		}
	}
	mod_rep->rops->end_rhent();
	free_nss_buffer(&buf);
}
/*
 * There is no interface to get the kernel cache of hosts
 * by template name. So this code starts with the hosts
 * that are specified in the tnrhdb name service and then
 * queries the kernel cache to find matching entries.
 * It may miss some previously applied temporary assignments.
 */

static void
getkerneltphosts(char *template)
{
	FILE *rh;
	tsol_rhent_t *rhentp;
	boolean_t err;

	rh = fopen(TNRHDB_PATH, "r");
	if (rh == NULL) {
		zerr(gettext("cannot open %s"), TNRHDB_PATH);
		exit(Z_ERR);
	}
	while ((rhentp = tsol_fgetrhent(rh, &err)) != NULL) {
		if (tnrh(TNDB_GET, rhentp) == 0) {
			if (strncmp(rhentp->rh_template, template,
			    TNTNAMSIZ) == 0) {
				tsol_rhent_t *newrhp;

				if ((newrhp =
				    (tsol_rhent_t *)malloc(
				    sizeof (tsol_rhent_t))) == NULL) {
					zerr(gettext("Out of memory"));
					exit(Z_ERR);
				}
				*newrhp = *rhentp;
				add_rhent(&cur_hosts, newrhp, B_FALSE);
			}
		}
		free(rhentp);
	}
}

static boolean_t
initialize_tmpl()
{
	m_label_t *l1;
	int i, status;
	nss_XbyY_buf_t *buf = NULL;

	prop_helptab = tmpl_helptab;
	prop_scope_cmds = tmpl_scope_cmds;

	if (!chkauthattr("solaris.label.network.manage", username)) {
		read_only_mode = B_TRUE;
	}

	switch (repo_scope) {
	case FILES_SCOPE:
	case LDAP_SCOPE:
		init_nss_buffer(SEC_REP_DB_TNRHTP, &buf);
		status = mod_rep->rops->get_tnrhtp(tpname, &cur_template, buf);
		if (status == 0 && cur_template != NULL) {
			resource_exists = B_TRUE;
			gettphosts(tpname);
			return (B_TRUE);
		}
		break;
	case TEMP_SCOPE:
		if ((cur_template =
		    (tsol_tpent_t *)malloc(sizeof (tsol_tpent_t))) == NULL) {
			zerr(gettext("Out of memory"));
			exit(Z_ERR);
		}
		(void) strlcpy(cur_template->name, tpname,
		    sizeof (cur_template->name));

		if (tnrhtp(TNDB_GET, cur_template) == 0) {
			resource_exists = B_TRUE;
			getkerneltphosts(tpname);
			return (B_TRUE);
		} else {
			free(cur_template);
			break;
		}
	}

	if ((cur_template =
	    (tsol_tpent_t *)malloc(sizeof (tsol_tpent_t))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	if (read_only_mode) {
		zerr(gettext("The template %s does not exist."),
		    tpname);
		exit(Z_ERR);
	}
	printf(gettext("A new template will be created\n"));
	strncpy(cur_template->name, tpname, TNTNAMSIZ);
	cur_template->host_type = UNLABELED;
	cur_template->tp_doi = 1;
	cur_template->un.unl.mask = NULL;
	cur_template->un.unl.mask |= TSOL_MSK_SL_RANGE_TSOL;
	cur_template->un.unl.mask |= TSOL_MSK_DEF_LABEL;
	bsllow(&cur_template->un.unl.def_label);
	bsllow(&cur_template->un.unl.gw_sl_range.lower_bound);
	bslhigh(&cur_template->un.unl.gw_sl_range.upper_bound);
	l1 = &cur_template->tp_gw_sl_set[0];
	for (i = 0; i < NSLS_MAX; i++, l1++) {
		l1->id = 0;
	}
	return (B_TRUE);
}

static tsol_zcent_t *
tsol_getzcbyname(char *zone_name)
{
	FILE *fp;
	tsol_zcent_t *zcent = NULL;
	char line[NSS_LINELEN_TSOL_ZC];

	if ((fp = fopen(TNZONECFG_PATH, "r")) == NULL) {
		zerr(gettext("cannot open %s"), TNZONECFG_PATH);
		return (NULL);
	}

	while (fgets(line, sizeof (line), fp) != NULL) {
		/*
		 * Check for malformed database
		 */
		if (strlen(line) == NSS_LINELEN_TSOL_ZC - 1)
			break;
		if ((zcent = tsol_sgetzcent(line, NULL, NULL)) == NULL)
			continue;
		if (strcmp(zcent->zc_name, zone_name) == 0)
			break;
		tsol_freezcent(zcent);
		zcent = NULL;
	}
	(void) fclose(fp);
	return (zcent);
}

static void
iterate_mlps(zoneid_t zoneid, int flags)
{
	tsol_mlpent_t mlpent;

	mlpent.tsme_zoneid = zoneid;
	mlpent.tsme_flags = flags;
	mlpent.tsme_mlp.mlp_ipp = 0;
	mlpent.tsme_mlp.mlp_port = 0;
	mlpent.tsme_mlp.mlp_port_upper = 0;
	while (tnmlp(TNDB_GET, &mlpent) != -1) {
		add_mlp(&cur_private_mlps, &mlpent.tsme_mlp);
		if (mlpent.tsme_mlp.mlp_ipp == 255) {
			mlpent.tsme_mlp.mlp_port++;
			mlpent.tsme_mlp.mlp_ipp = 0;
		} else {
			mlpent.tsme_mlp.mlp_ipp++;
		}
	}
}

static boolean_t
initialize_zone()
{
	tsol_mlp_t *mlp;
	m_label_t *l1;
	zoneid_t zoneid;

	prop_helptab = zone_helptab;
	prop_scope_cmds = zone_scope_cmds;

	if (!chkauthattr("solaris.label.zone.manage", username)) {
		(void) fprintf(stdout, gettext("%s cannot manage "
		    "the zone '%s'\n"\
		    "so it is set to read-only.\n"),
		    username, zonename);
		read_only_mode = B_TRUE;
	}

	switch (repo_scope) {
	case FILES_SCOPE:
		cur_zone = tsol_getzcbyname(zonename);
		if (cur_zone != NULL) {
			resource_exists = B_TRUE;
			mlp = cur_zone->zc_private_mlp;
			while (!TSOL_MLP_END(mlp)) {
				add_mlp(&cur_private_mlps, mlp);
				mlp++;
			}
			mlp = cur_zone->zc_shared_mlp;
			while (!TSOL_MLP_END(mlp)) {
				add_mlp(&cur_shared_mlps, mlp);
				mlp++;
			}
			return (B_TRUE);
		} else {
			uuid_t uuid;

			if (zonecfg_get_uuid((const char *) zonename,
			    uuid) == Z_NO_ZONE) {
				zerr(gettext("Zone %s has not been "
				    "configured."), zonename);
				exit(Z_ERR);
			}
			if ((cur_zone = (tsol_zcent_t *)
			    malloc(sizeof (tsol_zcent_t))) == NULL) {
				zerr(gettext("Out of memory"));
				exit(Z_ERR);
			}
			cur_zone->zc_match = 0;
			cur_zone->zc_doi = 1;
			bsllow(&cur_zone->zc_label);

			if ((cur_zone->zc_private_mlp = (tsol_mlp_t *)
			    malloc(sizeof (tsol_mlp_t))) == NULL) {
				zerr(gettext("Out of memory"));
				exit(Z_ERR);
			}
			cur_zone->zc_private_mlp->mlp_ipp = 0;
			cur_zone->zc_private_mlp->mlp_port = 0;

			if ((cur_zone->zc_shared_mlp = (tsol_mlp_t *)
			    malloc(sizeof (tsol_mlp_t))) == NULL) {
				zerr(gettext("Out of memory"));
				exit(Z_ERR);
			}
			cur_zone->zc_shared_mlp->mlp_ipp = 0;
			cur_zone->zc_shared_mlp->mlp_port = 0;
			strncpy(cur_zone->zc_name, zonename, ZONENAME_MAX);
			return (B_TRUE);
		}
	case TEMP_SCOPE:
		if ((zoneid = getzoneidbyname(zonename)) < GLOBAL_ZONEID) {
			zerr("zone is not ready or running");
			saw_error = B_TRUE;
			exit(Z_ERR);
		}
		if ((cur_zone = (tsol_zcent_t *)malloc(sizeof (tsol_zcent_t)))
		    == NULL) {
			zerr(gettext("Out of memory"));
			exit(Z_ERR);
		}
		strncpy(cur_zone->zc_name, zonename, ZONENAME_MAX);
		/*
		 * There is no interface to get the kernel settings for
		 * the zone's zc_match or zc_doi properties.
		 */
		cur_zone->zc_match = (zoneid == 0)?1:0;
		cur_zone->zc_doi = 1;

		/*
		 * The zone label cannot be changed dynamically.
		 */
		l1 = &cur_zone->zc_label;
		*l1 = *getzonelabelbyid(zoneid);

		iterate_mlps(zoneid, 0);
		iterate_mlps(zoneid, TSOL_MEF_SHARED);
		return (B_TRUE);
	}
	return (B_FALSE);
}

static void
export_template(FILE *of)
{
	rhostlist_t *next;
	char *str;
	const m_label_t *l1, *l2;

	(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
	    pt_to_str(PT_TPNAME), cur_template->name);
	switch (cur_template->host_type) {
	case UNLABELED:
		(void) fprintf(of, "%s %s=%s\n",
		    cmd_to_str(CMD_SET), pt_to_str(PT_HOSTTYPE),
		    TP_UNLABELED);
		(void) fprintf(of, "%s %s=%d\n",
		    cmd_to_str(CMD_SET), pt_to_str(PT_DOI),
		    cur_template->tp_doi);

		if (cur_template->tp_mask_unl & TSOL_MSK_DEF_LABEL) {
			l_to_str(&cur_template->tp_def_label, &str, M_LABEL);
			(void) fprintf(of, "%s %s=\"%s\"\n",
			    cmd_to_str(CMD_SET),
			    pt_to_str(PT_DEFLABEL), str);
			free(str);
		}
		break;
	case SUN_CIPSO:
		(void) fprintf(of, "%s %s=%s\n",
		    cmd_to_str(CMD_SET), pt_to_str(PT_HOSTTYPE),
		    TP_CIPSO);
		(void) fprintf(of, "%s %s=%d\n",
		    cmd_to_str(CMD_SET), pt_to_str(PT_DOI),
		    cur_template->tp_doi);
		break;
	}
	if (cur_template->tp_mask_unl & TSOL_MSK_SL_RANGE_TSOL) {
		l_to_str(&cur_template->tp_gw_sl_range.lower_bound,
		    &str, M_LABEL);
		(void) fprintf(of, "%s %s=\"%s\"\n",
		    cmd_to_str(CMD_SET),
		    pt_to_str(PT_MINLABEL), str);
		free(str);

		l_to_str(&cur_template->tp_gw_sl_range.upper_bound,
		    &str, M_LABEL);
		(void) fprintf(of, "%s %s=\"%s\"\n",
		    cmd_to_str(CMD_SET),
		    pt_to_str(PT_MAXLABEL), str);
		free(str);

		l1 = (const m_label_t *)&cur_template->tp_gw_sl_set[0];
		l2 = (const m_label_t *)&cur_template->tp_gw_sl_set[NSLS_MAX];
		for (; l1 < l2; l1++) {
			if (!BLTYPE(l1, SUN_SL_ID))
				continue;
			l_to_str(l1, &str, M_LABEL);
			(void) fprintf(of, "%s %s=\"%s\"\n",
			    cmd_to_str(CMD_ADD),
			    pt_to_str(PT_LABELSET), str);
			free(str);
		}
	}
	next = cur_hosts;
	while (next) {
		int alen;
		char abuf[INET6_ADDRSTRLEN + 5]; /*  5 bytes for prefix */

		translate_inet_addr(next->rhentp,
		    &alen, abuf, sizeof (abuf));
		(void) fprintf(of, "%s %s=%s\n",
		    cmd_to_str(CMD_ADD), pt_to_str(PT_HOSTADDR), abuf);
		next = next->next;
	}
}

static int
export_mlps(FILE *of, mlplist_t *mlplist, int type)
{
	struct protoent *pe;
	mlplist_t *next;
	tsol_mlp_t *mlp;

	next = mlplist;
	while (next) {
		mlp = next->mlp;
		(void) fprintf(of, "%s %s=%u",
		    cmd_to_str(CMD_ADD),
		    pt_to_str(type),
		    mlp->mlp_port);
		if ((mlp->mlp_port_upper != 0) &&
		    (mlp->mlp_port != mlp->mlp_port_upper))
			(void) fprintf(of, "-%u", mlp->mlp_port_upper);
		if ((pe = getprotobynumber(mlp->mlp_ipp)) == NULL)
			(void) fprintf(of, "/%u\n", mlp->mlp_ipp);
		else
			(void) fprintf(of, "/%s\n", pe->p_name);
		next = next->next;
	}
	return (0);
}

static void
export_zone(FILE *of)
{
	char *str;

	(void) fprintf(of, "%s %s=%s\n",
	    cmd_to_str(CMD_SET),
	    pt_to_str(PT_TPNAME), cur_zone->zc_name);
	(void) fprintf(of, "%s %s=%s\n",
	    cmd_to_str(CMD_SET),
	    pt_to_str(PT_VISIBLE), cur_zone->zc_match? "yes":"no");
	l_to_str(&cur_zone->zc_label, &str, M_LABEL);
	(void) fprintf(of, "%s %s=\"%s\"\n",
	    cmd_to_str(CMD_SET),
	    pt_to_str(PT_ZONELABEL), str);
	free(str);
	export_mlps(of, cur_private_mlps, PT_MLP_PRIVATE);
	export_mlps(of, cur_shared_mlps, PT_MLP_SHARED);
}

void
export_func(cmd_t *cmd)
{
	int arg;
	char outfile[MAXPATHLEN];
	FILE *of;
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
	switch (resource_scope) {
	case RT_TEMPLATE:
		export_template(of);
		break;
	case RT_ZONE:
		export_zone(of);
		break;
	}
	done:
		if (need_to_close)
			(void) fclose(of);
}

void
exit_func(cmd_t *cmd)
{
	int arg, answer;
	boolean_t force_exit = B_FALSE;
	boolean_t arg_err = B_FALSE;

	assert(cmd != NULL);

	optind = opterr = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "F")) != EOF) {
		switch (arg) {
		case 'F':
			force_exit = B_TRUE;
			break;
		default:
			if (optopt == '?')
				longer_usage(CMD_EXIT);
			else
				short_usage(CMD_EXIT);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (need_to_commit) {
		commit_func(cmd);
		if (!resource_verified && !force_exit) {
			answer = ask_yesno(B_FALSE, gettext(
			    "Verification failed; really quit"));
			if (answer == -1) {
				zerr(gettext("Verification failed, "
				    "input not from terminal and -F not "
				    "specified:\n%s command "
				    "ignored, but exiting anyway."),
				    cmd_to_str(CMD_EXIT));
				exit(Z_ERR);
			} else if (answer == 0) {
				return;
			}
		exit(Z_ERR);
		}
	}
	exit(Z_OK);
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
put_tpent(tsol_tpent_t	*tp, int flags)
{
	rhostlist_t *next;
	int cmd;

	if (flags & DEL_MASK)
		cmd = TNDB_DELETE;
	else
		cmd = TNDB_LOAD;

	need_to_commit &= ~pt_to_mask(PT_HOSTADDR);
	if (need_to_commit) {
		if (repo_scope == FILES_SCOPE || repo_scope == LDAP_SCOPE) {
			if (mod_rep->rops->put_tnrhtp(tp, flags) != Z_OK) {
				fprintf(stderr, gettext("commit failed.\n"));
				saw_error = B_TRUE;
			}
		}
		if (tnrhtp(cmd, tp) != 0 && errno == EFAULT) {
			fprintf(stderr, gettext("Unable to update "
			    "template entry\n"));
		}
	}
	next = cur_hosts;
	while (next) {
		if (next->need_to_commit) {
			if (repo_scope == FILES_SCOPE ||
			    repo_scope == LDAP_SCOPE) {
				if (mod_rep->rops->put_tnrhdb(next->rhentp,
				    ADD_MASK) != Z_OK) {
					fprintf(stderr,
					    gettext("commit failed.\n"));
					saw_error = B_TRUE;
				}
			}

			/* update the tnrhdb entry in the kernel */
			if (tnrh(TNDB_LOAD, next->rhentp) != 0) {
				if (errno == EFAULT) {
					fprintf(stderr, "Unable to update "
					    "host entry");
				}
			}
		}
		next->need_to_commit = B_FALSE;
		next = next->next;
	}
	next = del_hosts;
	while (next) {
		cur_host = next;
		if (repo_scope == FILES_SCOPE ||
		    repo_scope == LDAP_SCOPE) {
			if (mod_rep->rops->put_tnrhdb(cur_host->rhentp,
			    DEL_MASK) != Z_OK) {
				fprintf(stderr,
				    gettext("commit failed.\n"));
				saw_error = B_TRUE;
			}
		}
		/* update the tnrhdb entry in the kernel */
		if (tnrh(TNDB_DELETE, cur_host->rhentp) != 0) {
			if (errno == EFAULT) {
				fprintf(stderr, "Unable to update "
				    "host entry");
			}
		}
		next = cur_host->next;
		remove_rhent(&del_hosts, cur_host);
	}
	return (Z_OK);
}

static int
fprint_mlps(FILE *out, mlplist_t *mlplist)
{
	struct protoent *pe;
	mlplist_t *next;
	tsol_mlp_t *mlp;
	boolean_t first = B_TRUE;

	next = mlplist;
	while (next) {
		mlp = next->mlp;
		if (first)
			first = B_FALSE;
		else
			(void) fprintf(out, ";");

		(void) fprintf(out, "%u", mlp->mlp_port);
		if ((mlp->mlp_port_upper != 0) &&
		    (mlp->mlp_port != mlp->mlp_port_upper))
			(void) fprintf(out, "-%u", mlp->mlp_port_upper);
		if ((pe = getprotobynumber(mlp->mlp_ipp)) == NULL)
			(void) fprintf(out, "/%u", mlp->mlp_ipp);
		else
			(void) fprintf(out, "/%s", pe->p_name);
		next = next->next;
	}
	return (0);
}

static void
fprint_tnzonecfg(FILE *out, tsol_zcent_t *zcent)
{
	char *str;

	l_to_str(&cur_zone->zc_label, &str, M_INTERNAL);
	(void) fprintf(out, "%s:%s:%u:", zcent->zc_name,
	    str, zcent->zc_match);
	free(str);
	fprint_mlps(out, cur_private_mlps);
	(void) fprintf(out, ":");

	fprint_mlps(out, cur_shared_mlps);
	(void) fprintf(out, "\n");
}

static int
files_put_tnzonecfg(tsol_zcent_t *zcent, int flags)
{
	FILE *tsolzonecfg; /* /etc/security/tsol file */
	FILE *tmpzonecfg; /* temp file */
	int o_mask; /* old umask value */
	int added = 0, modified = 0, deleted = 0;
	struct stat sb; /* stat buf to copy modes */
	int haserr = 0, linenum = 0;
	char line[NSS_LINELEN_TSOL_ZC];
	tsol_zcent_t *file_zcentp;

	if (!zcent || !zcent->zc_name || !strlen(zcent->zc_name)) {
		return (SEC_REP_INVALID_ARG);
	}

	if ((tsolzonecfg = fopen(TNZONECFG_PATH, "r")) == NULL) {
		return (EX_UPDATE);
	}

	if (fstat(fileno(tsolzonecfg), &sb) < 0) {
		/* If we can't get mode, take a default */
		sb.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}

	o_mask = umask(077);
	tmpzonecfg = fopen(TNZONECFG_TMP, "w+");
	(void) umask(o_mask);

	if (tmpzonecfg == NULL) {
		fclose(tsolzonecfg);
		return (EX_UPDATE);
	}

	if (fchmod(fileno(tmpzonecfg), sb.st_mode) != 0 ||
	    fchown(fileno(tmpzonecfg), sb.st_uid, sb.st_gid) != 0 ||
	    lockf(fileno(tmpzonecfg), F_LOCK, 0) != 0) {
		fclose(tsolzonecfg);
		fclose(tmpzonecfg);
		unlink(TNZONECFG_TMP);
		return (EX_UPDATE);
	}

	while (fgets(line, sizeof (line), tsolzonecfg) != NULL &&
	    !haserr) {
		linenum++;

		if (line[0] == '#' ||line[0] == '\n') {
			fputs(line, tmpzonecfg);
			continue;
		}
		if ((file_zcentp = tsol_sgetzcent(line, NULL, NULL)) == NULL)
			continue;


		if (file_zcentp->zc_name && strlen(file_zcentp->zc_name) &&
		    (strcmp(file_zcentp->zc_name, zcent->zc_name) == 0)) {
			if (flags & ADD_MASK) {
				const char *err_msg = gettext("ERROR: "
				    "Duplicate entry in tnzonecfg file"
				    " at line %d.\n");
				(void) fprintf(stderr, err_msg, linenum);
				break;
			}
			if (flags & DEL_MASK) {
				deleted = 1;
				tsol_freezcent(file_zcentp);
				continue;
			}
			if (flags & MOD_MASK) {
				fprint_tnzonecfg(tmpzonecfg, zcent);
				modified = 1;
				tsol_freezcent(file_zcentp);
				continue;
			}
		}
		fputs(line, tmpzonecfg);
		tsol_freezcent(file_zcentp);
	}

	haserr = !feof(tsolzonecfg);
	if (haserr) {
		const char *err_msg = gettext("ERROR: Syntax error in "
		    "tnzonecfg file at line %d.\n");
		(void) fprintf(stderr, err_msg, linenum);
	}

	if (fclose(tsolzonecfg) != 0 || haserr) {
		(void) unlink(TNZONECFG_TMP);
		return (EX_UPDATE);
	}

	if (flags & ADD_MASK) {
		fprint_tnzonecfg(tmpzonecfg, zcent);
		added = 1;
	}
	if (added || modified || deleted) {
		if (rename(TNZONECFG_TMP, TNZONECFG_PATH) < 0) {
			fclose(tmpzonecfg);
			unlink(TNZONECFG_TMP);
			return (EX_UPDATE);
		}
	} else {
		(void) unlink(TNZONECFG_TMP);
		return (EX_NAME_NOT_EXIST);
	}

	(void) fclose(tmpzonecfg);

	return (EX_SUCCESS);
}

static void
handle_mlps(zoneid_t zoneid, mlplist_t *mlplist, int flags, int cmd)
{
	tsol_mlpent_t tsme;
	mlplist_t *next;

	tsme.tsme_zoneid = zoneid;
	tsme.tsme_flags = flags;
	next = mlplist;
	while (next) {
		tsme.tsme_mlp = *next->mlp;
		if (tnmlp(cmd, &tsme) != 0) {
			if (errno == EFAULT) {
				fprintf(stderr, "Unable to update "
				    "multilevel ports");
			}
		next = next->next;
		}
	}
}

int
put_zcent(tsol_zcent_t	*zcent, int flags)
{
	tsol_mlpent_t tsme;
	zoneid_t zoneid;

	if (need_to_commit) {
		if (repo_scope == FILES_SCOPE)
			files_put_tnzonecfg(zcent, flags);

		if ((zoneid = getzoneidbyname(zcent->zc_name)) >=
		    GLOBAL_ZONEID) {
			tsme.tsme_zoneid = zoneid;
			tsme.tsme_flags = 0;
			(void) tnmlp(TNDB_FLUSH, &tsme);

			handle_mlps(zoneid, cur_private_mlps, 0, TNDB_LOAD);
			handle_mlps(zoneid, cur_shared_mlps, TSOL_MEF_SHARED,
			    TNDB_LOAD);
		}
	}
	return (Z_OK);
}

static char *
check_label_conflicts(tsol_zcent_t *zcent)
{
	FILE *tsolzonecfg; /* /etc/security/tsol file */
	char line[NSS_LINELEN_TSOL_ZC];
	tsol_zcent_t *file_zcentp;
	m_label_t *l1;
	char *other_zone;

	l1 = &zcent->zc_label;

	if ((tsolzonecfg = fopen(TNZONECFG_PATH, "r")) == NULL) {
		return (NULL);
	}

	while (fgets(line, sizeof (line), tsolzonecfg) != NULL) {
		if ((file_zcentp = tsol_sgetzcent(line, NULL, NULL)) == NULL)
			continue;

		if (file_zcentp->zc_name && strlen(file_zcentp->zc_name) &&
		    (strcmp(file_zcentp->zc_name, zcent->zc_name) == 0))
			continue;
		if (blequal(l1, &file_zcentp->zc_label)) {
			other_zone = strdup(file_zcentp->zc_name);
			tsol_freezcent(file_zcentp);
			(void) fclose(tsolzonecfg);
			return (other_zone);
		}
		tsol_freezcent(file_zcentp);
	}
	(void) fclose(tsolzonecfg);
	return (NULL);
}

void
list_zones()
{
	FILE *tsolzonecfg; /* /etc/security/tsol file */
	char line[NSS_LINELEN_TSOL_ZC];
	tsol_zcent_t *file_zcentp;

	if ((tsolzonecfg = fopen(TNZONECFG_PATH, "r")) == NULL) {
		return;
	}

	while (fgets(line, sizeof (line), tsolzonecfg) != NULL) {
		if ((file_zcentp = tsol_sgetzcent(line, NULL, NULL)) == NULL)
			continue;

		printf("\t%s\n", file_zcentp->zc_name);
		tsol_freezcent(file_zcentp);
	}
	(void) fclose(tsolzonecfg);
}

static char *
check_host_conflicts(rhostlist_t *hosts, tsol_rhent_t **conflict)
{
	tsol_rhent_t *db_rhentp;
	char *other_template;
	nss_XbyY_buf_t *buf = NULL;
	rhostlist_t *next;

	if (hosts == NULL)
		return (NULL);

	mod_rep->rops->set_rhent(1);
	init_nss_buffer(SEC_REP_DB_TNRHTP, &buf);
	while ((db_rhentp = mod_rep->rops->get_rhent(buf)) != NULL) {
		next = hosts;
		while (next) {
			if ((strcmp(cur_template->name,
			    db_rhentp->rh_template) != 0) &&
			    (TNADDR_EQ(&db_rhentp->rh_address,
			    &next->rhentp->rh_address))) {
				other_template =
				    strdup(db_rhentp->rh_template);
				tsol_freerhent(db_rhentp);
				*conflict = next->rhentp;
				mod_rep->rops->end_rhent();
				free_nss_buffer(&buf);
				return (other_template);
			}
			next = next->next;
		}
		tsol_freerhent(db_rhentp);
	}
	mod_rep->rops->end_rhent();
	free_nss_buffer(&buf);
	return (NULL);
}

void
list_templates()
{
	tsol_tpent_t tp;
	tsol_tpent_t *tpp;

	tpp = &tp;
	tsol_settpent(1);

	while ((tpp = tsol_gettpent()) != NULL) {
		printf("\t%s\n", tpp->name);
	}
	tsol_endtpent();
}

static void
add_property(cmd_t *cmd)
{
	int prop_type;
	property_value_ptr_t pp;
	char *prop_id;
	tsol_rhent_t *rhentp;
	int err;
	m_label_t *l1;
	m_label_t new_label, *new_labelp = &new_label;
	m_label_t *free_label;
	tsol_mlp_t mlp;
	int i;

	prop_type = cmd->cmd_prop_name[0];

	if (cmd->cmd_prop_nv_pairs != 1) {
		long_usage(CMD_ADD, B_TRUE);
		return;
	}
	pp = cmd->cmd_property_ptr[0];

	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_ADD, B_TRUE);
		return;
	}
	if (resource_is_read_only(CMD_ADD)) {
		saw_error = B_TRUE;
		return;
	}
	if (((prop_id = pp->pv_simple) == NULL)) {
		zerr(gettext("The property is not valid."));
		saw_error = B_TRUE;
		return;
	}

	switch (resource_scope) {
	case RT_TEMPLATE:
		switch (prop_type) {
		case PT_HOSTADDR:
			if ((rhentp = host2rhent(prop_id)) == NULL) {
				saw_error = B_TRUE;
				return;
			} else {
				tsol_rhent_t test_rhent, *test_rhentp;

				test_rhentp = &test_rhent;
				*test_rhentp = *rhentp;
				if (add_rhent(&cur_hosts, rhentp, B_TRUE)
				    == B_FALSE) {
					zerr(gettext("%s is already included "
					    "in the template"),
					    prop_id);
					saw_error = B_TRUE;
					return;
				}
				if (tnrh(TNDB_GET, test_rhentp) == 0) {
					if (strncmp(test_rhentp->rh_template,
					    tpname, TNTNAMSIZ) == 0) {
						printf("%s already "
						    "matches this template\n",
						    prop_id);
					} else {
						printf("%s previously "
						    "matched the %s template\n",
						    prop_id,
						    test_rhentp->rh_template);
					}
				}
				need_to_commit |= pt_to_mask(PT_HOSTADDR);
				return;
			}
		case PT_LABELSET:
			bsllow(new_labelp);
			if (str_to_label(prop_id, &new_labelp,
			    MAC_LABEL, L_DEFAULT, &err) == -1) {
				zerr(gettext("invalid label"));
				saw_error = B_TRUE;
				return;
			}
			free_label = NULL;
			l1 = &cur_template->tp_gw_sl_set[0];
			for (i = 0; i < NSLS_MAX; i++, l1++) {
				if (BLTYPE(l1, SUN_SL_ID)) {
					if (blequal(l1, new_labelp)) {
						zerr(gettext("The label '%s' "
						    "is already specified."),
						    prop_id);
						saw_error = B_TRUE;
						return;
					}
				} else {
					free_label = l1;
				}
			}
			if (free_label) {
				*free_label =  *new_labelp;
				need_to_commit |=
				    pt_to_mask(prop_type);
				return;
			} else {
				zerr(gettext("Only four additional labels are "
				    "allowed"));
				saw_error = B_TRUE;
				return;
			}
		case PT_TPNAME:
		case PT_HOSTTYPE:
		case PT_DOI:
		case PT_DEFLABEL:
		case PT_MINLABEL:
		case PT_MAXLABEL:
			zerr(gettext("Cannot '%s' to '%s' property. "
			    "Use '%s' instead."),
			    cmd_to_str(CMD_ADD),
			    pt_to_str(prop_type),
			    cmd_to_str(CMD_SET));
			saw_error = B_TRUE;
			return;
		default:
			zerr(gettext("The '%s' property only applies "
			    "to zones."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	case RT_ZONE:
		switch (prop_type) {
		case PT_MLP_SHARED:
		case PT_MLP_PRIVATE:
			if (str_to_mlp(prop_id, &mlp) == -1) {
				zerr(gettext("invalid mlp specification"));
				saw_error = B_TRUE;
				return;
			}
			if (prop_type == PT_MLP_PRIVATE)
				err = add_mlp(&cur_private_mlps, &mlp);
			else
				err = add_mlp(&cur_shared_mlps, &mlp);
			if (err == B_FALSE) {
				zerr(gettext("%s is already included in "
				    "the %s configuration"),
				    prop_id, pt_to_str(prop_type));
				saw_error = B_TRUE;
			}
			need_to_commit |= pt_to_mask(prop_type);
			return;
		case PT_TPNAME:
		case PT_VISIBLE:
		case PT_ZONELABEL:
			zerr(gettext("Cannot '%s' to '%s' property. "
			    "Use '%s' instead."),
			    cmd_to_str(CMD_ADD),
			    pt_to_str(prop_type),
			    cmd_to_str(CMD_SET));
			saw_error = B_TRUE;
			return;
		default:
			zerr(gettext("The '%s' property only applies "
			    "to templates."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	}
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
	add_property(cmd);
}

void
delete_func(cmd_t *cmd)
{
	rhostlist_t *next;
	int arg, answer;
	boolean_t force_delete = B_FALSE;
	boolean_t arg_err = B_FALSE;

	assert(cmd != NULL);

	optind = opterr = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "F")) != EOF) {
		switch (arg) {
		case 'F':
			force_delete = B_TRUE;
			break;
		default:
			if (optopt == '?')
				longer_usage(CMD_DELETE);
			else
				short_usage(CMD_DELETE);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	if (resource_is_read_only(CMD_DELETE)) {
		saw_error = B_TRUE;
		return;
	}

	switch (resource_scope) {
	case RT_TEMPLATE:
	default:
		if (!force_delete) {
			if ((answer = ask_yesno(B_FALSE, gettext("Are you "
			    "sure you want to delete the template"))) == -1) {
				zerr(gettext("Input not from terminal and "
				    "-F not specified:\n%s command ignored, "
				    " exiting."), cmd_to_str(CMD_DELETE));
				exit(Z_ERR);
			}
			if (answer != 1)
				return;
		}
		if (!resource_exists)
			break;
		/*
		 * Revert to last saved copy so that we can delete
		 * the correct host entries.
		 */

		tsol_freetpent(cur_template);
		cur_template = NULL;
		if (!initialize_tmpl())
			exit(Z_ERR);
		/*
		 * First move all the hosts to the deleted list.
		 * Then the template entry;
		 */
		next = cur_hosts;
		while (next != NULL) {
			rhostlist_t *host;

			host = next;
			next = next->next;
			(void) remove_rhent(&cur_hosts,
			    host);
			need_to_commit |= pt_to_mask(PT_HOSTADDR);
		}
		need_to_commit |= pt_to_mask(PT_TPNAME);
		put_tpent(cur_template, DEL_MASK);
		break;
	case RT_ZONE:
		if (strcmp(cur_zone->zc_name, GLOBAL_ZONENAME) == 0) {
			zerr(gettext("The global zone cannot be deleted."));
			saw_error = B_TRUE;
			return;
		}
		if (!force_delete) {
			if ((answer = ask_yesno(B_FALSE, gettext("Are you "
			    "sure you want to delete the zone properties")))
			    == -1) {
				zerr(gettext("Input not from terminal and "
				    "-F not specified:\n%s command ignored, "
				    " exiting."), cmd_to_str(CMD_DELETE));
				exit(Z_ERR);
			}
			if (answer != 1)
				return;
		}
		if (resource_exists) {
			need_to_commit |= pt_to_mask(PT_TPNAME);
			put_zcent(cur_zone, DEL_MASK);
		}
		break;
	}
	exit(Z_OK);
}

void
remove_func(cmd_t *cmd)
{
	int prop_type;
	property_value_ptr_t pp;
	char *prop_id;
	rhostlist_t *host;
	m_label_t *l1, new_label, *l2 = &new_label;
	mlplist_t *mlp;
	int i;
	int err;

	assert(cmd != NULL);

	prop_type = cmd->cmd_prop_name[0];

	if (cmd->cmd_prop_nv_pairs != 1) {
		long_usage(CMD_REMOVE, B_TRUE);
		return;
	}

	pp = cmd->cmd_property_ptr[0];
	if (((prop_id = pp->pv_simple) == NULL)) {
		zerr(gettext("The host address is not valid."));
		saw_error = B_TRUE;
		return;
	}

	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_REMOVE, B_TRUE);
		return;
	}
	if (resource_is_read_only(CMD_REMOVE)) {
		saw_error = B_TRUE;
		return;
	}
	switch (resource_scope) {
	case RT_TEMPLATE:
		switch (prop_type) {
		case PT_HOSTADDR:
			host = find_rhent(&cur_hosts, prop_id);
			if (host == NULL) {
				return;
			}
			if (!remove_rhent(&cur_hosts, host)) {
				saw_error = B_TRUE;
				return;
			}
			need_to_commit |= pt_to_mask(prop_type);
			return;
		case PT_LABELSET:
			bsllow(l2);
			if (str_to_label(prop_id, &l2, MAC_LABEL,
			    L_DEFAULT, &err) == -1) {
				zerr(gettext("invalid label"));
				saw_error = B_TRUE;
				return;
			}
			l1 = &cur_template->tp_gw_sl_set[0];
			for (i = 0; i < NSLS_MAX; i++, l1++) {
				if ((BLTYPE(l1, SUN_SL_ID)) &&
				    blequal(l1, l2)) {
					((_mac_label_impl_t *)l1)->id = 0;
					need_to_commit |= pt_to_mask(prop_type);
					return;
				}
			}
			zerr(gettext("'%s' is not in the extra label set"),
			    prop_id);
			saw_error = B_TRUE;
			return;
		case PT_TPNAME:
		case PT_HOSTTYPE:
		case PT_DOI:
		case PT_DEFLABEL:
		case PT_MINLABEL:
		case PT_MAXLABEL:
			zerr(gettext("Cannot '%s' from '%s' property. "),
			    cmd_to_str(CMD_REMOVE),
			    pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		default:
			zerr(gettext("The '%s' property only applies "
			    "to zones."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	case RT_ZONE:
		switch (prop_type) {
		case PT_MLP_PRIVATE:
			mlp = find_mlp(&cur_private_mlps, prop_id);
			if (mlp == NULL) {
				zerr(gettext("The mlp does not match an "
				    "existing entry."));
				saw_error = B_TRUE;
				return;
			}
			if (!remove_mlp(&cur_private_mlps, mlp)) {
				saw_error = B_TRUE;
				return;
			}
			need_to_commit |= pt_to_mask(prop_type);
			return;
		case PT_MLP_SHARED:
			mlp = find_mlp(&cur_shared_mlps, prop_id);
			if (mlp == NULL) {
				zerr(gettext("The mlp does not match an "
				    "existing entry."));
				saw_error = B_TRUE;
				return;
			}
			if (!remove_mlp(&cur_shared_mlps, mlp)) {
				saw_error = B_TRUE;
				return;
			}
			need_to_commit |= pt_to_mask(prop_type);
			return;
		case PT_TPNAME:
		case PT_VISIBLE:
		case PT_ZONELABEL:
			zerr(gettext("Cannot '%s' from '%s' property. "),
			    cmd_to_str(CMD_REMOVE),
			    pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		default:
			zerr(gettext("The '%s' property only applies "
			    "to templates."), pt_to_str(prop_type));
			saw_error = B_TRUE;
		}
	}
}

void
clear_func(cmd_t *cmd)
{
	int prop_type;
	m_label_t *l1;
	int i;
	rhostlist_t *next;
	mlplist_t *nextmlp;

	assert(cmd != NULL);

	prop_type = cmd->cmd_res_type;
	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_CLEAR, B_TRUE);
		return;
	}

	if (resource_is_read_only(CMD_CLEAR)) {
		saw_error = B_TRUE;
		return;
	}
	switch (resource_scope) {
	case RT_TEMPLATE:
		switch (prop_type) {
		case PT_HOSTADDR:
			next = cur_hosts;
			while (next != NULL) {
				next = next->next;
				(void) remove_rhent(&cur_hosts,
				    cur_hosts);
			}
			need_to_commit |= pt_to_mask(PT_HOSTADDR);
			return;
		case PT_LABELSET:
			l1 = &cur_template->tp_gw_sl_set[0];
			for (i = 0; i < NSLS_MAX; i++, l1++) {
				((_mac_label_impl_t *)l1)->id = 0;
			}
			need_to_commit |= pt_to_mask(prop_type);
			return;
		default:
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, CMD_CLEAR, HELP_PROPS);
			return;
		}
	case RT_ZONE:
		switch (prop_type) {
		case PT_MLP_PRIVATE:
			nextmlp = cur_private_mlps;
			while (nextmlp != NULL) {
				nextmlp = nextmlp->next;
				(void) remove_mlp(&cur_private_mlps,
				    cur_private_mlps);
			}
			need_to_commit |= pt_to_mask(PT_MLP_PRIVATE);
			return;
		case PT_MLP_SHARED:
			nextmlp = cur_shared_mlps;
			while (nextmlp != NULL) {
				nextmlp = nextmlp->next;
				(void) remove_mlp(&cur_shared_mlps,
				    cur_shared_mlps);
			}
			need_to_commit |= pt_to_mask(PT_MLP_SHARED);
			return;
		}
	}
}

void
set_func(cmd_t *cmd)
{
	char *prop_id;
	int prop_type;
	property_value_ptr_t pp;
	rhostlist_t *next;
	int doi;
	m_label_t *blabel = NULL;
	tsol_tpent_t	*tp = cur_template;
	int err;

	assert(cmd != NULL);

	prop_type = cmd->cmd_prop_name[0];

	pp = cmd->cmd_property_ptr[0];
	prop_id = pp->pv_simple;

	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_SET, B_TRUE);
		return;
	}
	switch (resource_scope) {
	case RT_TEMPLATE:
		if (prop_type == PT_TPNAME) {
			nss_XbyY_buf_t *buf = NULL;
			tsol_tpent_t *tmpl;
			/*
			 * If new template name is different from current name
			 * and if it already exists, don't allow it.
			 */
			if (strlen(prop_id) >= TNTNAMSIZ) {
				zerr(gettext("The template name is too long"));
				saw_error = B_TRUE;
				return;
			}
			if (tp->name != NULL &&
			    (strcmp(tp->name, prop_id) == 0)) {
				return;
			}
			switch (repo_scope) {
			case FILES_SCOPE:
			case LDAP_SCOPE:
				init_nss_buffer(SEC_REP_DB_TNRHTP, &buf);
				if (mod_rep->rops->get_tnrhtp(prop_id,
				    &tmpl, buf) == 0) {
					free(tmpl);
					zerr(gettext("Cannot switch to an "
					    "existing template."));
					saw_error = B_TRUE;
					return;
				}
				break;
			case TEMP_SCOPE:
				if ((tmpl =
				    (tsol_tpent_t *)malloc(
				    sizeof (tsol_tpent_t))) == NULL) {
					zerr(gettext("Out of memory"));
					exit(Z_ERR);
				}
				(void) strlcpy(tmpl->name, prop_id,
				    sizeof (tmpl->name));
				if (tnrhtp(TNDB_GET, tmpl) == 0) {
					free(tmpl);
					zerr(gettext("Cannot switch to an "
					    "existing template."));
					saw_error = B_TRUE;
					return;
				}
				break;
			}
			free(tpname);
			resource_exists = B_FALSE;
			tpname = strdup(prop_id);
			strncpy(tp->name, tpname, TNTNAMSIZ);
			/*
			 * Remove read-only status if present
			 */
			read_only_mode = B_FALSE;
			/*
			 * Remove all the host address entries
			 */
			next = cur_hosts;
			while (next) {
				next = next->next;
				(void) remove_rhent(&cur_hosts,
				    cur_hosts);
			}
			next = del_hosts;
			while (next) {
				cur_host = next;
				next = cur_host->next;
				remove_rhent(&del_hosts, cur_host);
			}
			need_to_commit |= pt_to_mask(PT_TPNAME);
			return;
		} else if (resource_is_read_only(CMD_SET)) {
			saw_error = B_TRUE;
			return;
		} else switch (prop_type) {
			case PT_HOSTTYPE:
				if (strcmp(prop_id, TP_CIPSO) == 0) {
					tp->host_type = SUN_CIPSO;
					tp->un.unl.mask &=
					    ~TSOL_MSK_DEF_LABEL;
					need_to_commit |=
					    pt_to_mask(PT_HOSTTYPE);
					return;
				} else if (strcmp(prop_id, TP_UNLABELED) == 0) {
					tp->host_type = UNLABELED;
					tp->un.unl.mask |=
					    TSOL_MSK_DEF_LABEL;
					need_to_commit |=
					    pt_to_mask(PT_HOSTTYPE);
					need_to_commit |=
					    pt_to_mask(PT_DEFLABEL);
				} else  {
					zerr(gettext("Host type must be cipso "
					    "or unlabeled"));
					saw_error = B_TRUE;
				}
				return;
			case PT_DOI:
				if ((doi = atoi(prop_id)) > 0) {
					tp->tp_doi = doi;
					need_to_commit |= pt_to_mask(PT_DOI);
					return;
				} else  {
					zerr(gettext("DOI must be a positive "
					    "integer"));
					saw_error = B_TRUE;
				}
				return;
			case PT_DEFLABEL:
				blabel = &tp->un.unl.def_label;
				if (str_to_label(prop_id, &blabel, MAC_LABEL,
				    L_DEFAULT, &err) == -1) {
					zerr(gettext("invalid label"));
					saw_error = B_TRUE;
					return;
				}
				need_to_commit |= pt_to_mask(PT_DEFLABEL);
				return;
			case PT_MINLABEL:
				blabel = &tp->un.unl.gw_sl_range.lower_bound;
				if (str_to_label(prop_id, &blabel, MAC_LABEL,
				    L_DEFAULT, &err) == -1) {
					zerr(gettext("invalid label"));
					saw_error = B_TRUE;
					return;
				}
				need_to_commit |= pt_to_mask(PT_MINLABEL);
				return;
			case PT_MAXLABEL:
				blabel = &tp->un.unl.gw_sl_range.upper_bound;
				if (str_to_label(prop_id, &blabel, MAC_LABEL,
				    L_DEFAULT, &err) == -1) {
					zerr(gettext("invalid label"));
					saw_error = B_TRUE;
					return;
				}
				need_to_commit |= pt_to_mask(PT_MAXLABEL);
				return;
			case PT_HOSTADDR:
			case PT_LABELSET:
				zerr(gettext("Cannot '%s' the '%s' property. "
				    "Use '%s' instead."),
				    cmd_to_str(CMD_SET),
				    pt_to_str(prop_type),
				    cmd_to_str(CMD_ADD));
				saw_error = B_TRUE;
				return;
			default:
				zerr(gettext("The '%s' property only applies "
				    "to zones."), pt_to_str(prop_type));
				saw_error = B_TRUE;
				return;
		}
	case RT_ZONE:
		switch (prop_type) {
		case PT_TPNAME:
			if (repo_scope == TEMP_SCOPE) {
				zerr(gettext("Can't change %s property of "
				    "active zone."), pt_to_str(prop_type));
				saw_error = B_TRUE;
				return;
			}
			if (resource_is_read_only(CMD_SET)) {
				saw_error = B_TRUE;
				return;
			}
			/*
			 * If new zone name is different from current name
			 * and if it already exists, don't allow it.
			 */
			if ((strcmp(cur_zone->zc_name, prop_id) != 0) &&
			    (tsol_getzcbyname(prop_id) != NULL)) {
				/* FREE THE EXISTING ZONECFG MEMORY */
				zerr(gettext("Cannot switch to an existing "
				    "zone."));
				saw_error = B_TRUE;
				return;
			}
			resource_exists = B_FALSE;
			zonename = strdup(prop_id);
			strncpy(cur_zone->zc_name, zonename, ZONENAME_MAX);
			/*
			 * Remove read-only status if present
			 */
			read_only_mode = B_FALSE;
			need_to_commit |= pt_to_mask(PT_TPNAME);
			need_to_commit |= pt_to_mask(PT_ZONELABEL);
			return;
		case PT_VISIBLE:
			if (repo_scope == TEMP_SCOPE) {
				zerr(gettext("Can't change %s property of "
				    "active zone."), pt_to_str(prop_type));
				saw_error = B_TRUE;
				return;
			}
			if (tolower(prop_id[0]) == 'y') {
				cur_zone->zc_match = 1;
			} else if (tolower(prop_id[0]) == 'n') {
				cur_zone->zc_match = 0;
			} else {
				zerr(gettext("The %s property values are "
				    "'yes' and no'."), pt_to_str(PT_VISIBLE));
				saw_error = B_TRUE;
				return;
			}
			need_to_commit |= pt_to_mask(PT_VISIBLE);
			return;
		case PT_ZONELABEL:
			if (strcmp(cur_zone->zc_name, GLOBAL_ZONENAME) == 0) {
				zerr(gettext("The global zone's label cannot"
				    " be changed."));
				saw_error = B_TRUE;
				return;
			}
			if (repo_scope == TEMP_SCOPE) {
				zerr(gettext("Can't change %s property of "
				    "active zone."), pt_to_str(prop_type));
				saw_error = B_TRUE;
				return;
			}
			blabel = &cur_zone->zc_label;
			if (str_to_label(prop_id, &blabel, MAC_LABEL,
			    L_DEFAULT, &err) == -1) {
				zerr(gettext("invalid label"));
				saw_error = B_TRUE;
				return;
			}
			need_to_commit |= pt_to_mask(PT_ZONELABEL);
			return;
		case PT_MLP_PRIVATE:
		case PT_MLP_SHARED:
			zerr(gettext("Cannot '%s' the '%s' property. "
			    "Use '%s' instead."),
			    cmd_to_str(CMD_SET),
			    pt_to_str(prop_type),
			    cmd_to_str(CMD_ADD));
			saw_error = B_TRUE;
			return;
		default:
			zerr(gettext("The '%s' property only applies "
			    "to templates."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	}
}

static int
print_labelset(FILE *fp, tsol_tpent_t *tp)
{
	char *str;
	const m_label_t *l1, *l2;

	switch (tp->host_type) {
	case UNLABELED:
		l1 = (const m_label_t *)&tp->tp_gw_sl_set[0];
		l2 = (const m_label_t *)&tp->tp_gw_sl_set[NSLS_MAX];
		break;
	case SUN_CIPSO:
		l1 = (const m_label_t *)&tp->tp_sl_set_cipso[0];
		l2 = (const m_label_t *)&tp->tp_sl_set_cipso[NSLS_MAX];
		break;
	default:
		(void) fprintf(fp, gettext("unsupported host type: %ld\n"),
		    tp->host_type);
		return (1);
	}

	for (; l1 < l2; l1++) {
		if (!BLTYPE(l1, SUN_SL_ID))
			continue;
		l_to_str(l1, &str, M_LABEL);
		(void) fprintf(fp, "\t%s=%s\n",
		    pt_to_str(PT_LABELSET), str);
		free(str);
	}
	return (0);
}

/*
 * Print all of the MLPs for the given zone.
 */

static int
info_mlps(FILE *fp, mlplist_t *mlplist, int type)
{
	struct protoent *pe;
	mlplist_t *next;
	tsol_mlp_t *mlp;

	next = mlplist;
	while (next) {
		mlp = next->mlp;
		(void) fprintf(fp, "\t%s=%u", pt_to_str(type),
		    mlp->mlp_port);
		if ((mlp->mlp_port_upper != 0) &&
		    (mlp->mlp_port != mlp->mlp_port_upper))
			(void) printf("-%u", mlp->mlp_port_upper);
		if ((pe = getprotobynumber(mlp->mlp_ipp)) == NULL)
			(void) fprintf(fp, "/%u\n", mlp->mlp_ipp);
		else
			(void) fprintf(fp, "/%s\n", pe->p_name);
		next = next->next;
	}
	return (0);
}

void
info_func(cmd_t *cmd)
{
	FILE *fp = stdout;
	rhostlist_t *next;
	int i;
	int prop_type;
	boolean_t once = B_FALSE;
	char *str;

	assert(cmd != NULL);

	if (cmd->cmd_argc == 0) {
		prop_type = cmd->cmd_prop_name[0];
	}
	/* don't page error output */
	if (interactive_mode) {
		setbuf(fp, NULL);
	} else if (!resource_exists && !cmd_file_mode) {
		if (prop_type)
			zerr(gettext("Property %s is not specified."),
			    pt_to_str(prop_type));
		else
			zerr(gettext("Resource does not exist."));
		exit(Z_ERR);
	}

	for (i = 0; prop_helptab[i].prop_num != NULL; i++) {
		if ((i == 0) && prop_type)
			once = B_TRUE;
		else
			prop_type = prop_helptab[i].prop_num;

		switch (resource_scope) {
		case RT_TEMPLATE:
			switch (prop_type) {
			case PT_TPNAME:
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prop_type),
				    cur_template->name);
				break;
			case PT_HOSTTYPE:
				switch (cur_template->host_type) {
				case UNLABELED:
					fprintf(fp, "\t%s=%s\n",
					    pt_to_str(prop_type), TP_UNLABELED);
					break;
				case SUN_CIPSO:
					fprintf(fp, "\t%s=%s\n",
					    pt_to_str(prop_type), TP_CIPSO);
					break;
				}
				break;
			case PT_DOI:
				fprintf(fp, "\t%s=%d\n",
				    pt_to_str(prop_type),
				    cur_template->tp_doi);
				break;
			case PT_DEFLABEL:
				if (cur_template->host_type == UNLABELED) {
					l_to_str(&cur_template->tp_def_label,
					    &str, M_LABEL);
					fprintf(fp, "\t%s=%s\n",
					    pt_to_str(prop_type), str);
					free(str);
				}
				break;
			case PT_MINLABEL:
				l_to_str(&cur_template->
				    tp_gw_sl_range.lower_bound,
				    &str, M_LABEL);
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prop_type), str);
				free(str);
				break;
			case PT_MAXLABEL:
				l_to_str(&cur_template->
				    tp_gw_sl_range.upper_bound,
				    &str, M_LABEL);
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prop_type), str);
				free(str);
				break;
			case PT_LABELSET:
				print_labelset(fp, cur_template);
				break;
			case PT_HOSTADDR:
				next = cur_hosts;
				while (next) {
					int alen;
					char abuf[INET6_ADDRSTRLEN + 5];

					translate_inet_addr(next->rhentp,
					    &alen, abuf, sizeof (abuf));
					(void) fprintf(fp, "\t%s=%s\n",
					    pt_to_str(prop_type), abuf);
					next = next->next;
				}
				break;
			default:
				scope_usage(prop_type);
				break;
			}
			break;
		case RT_ZONE:
			switch (prop_type) {
			case PT_TPNAME:
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prop_type),
				    cur_zone->zc_name);
				break;
			case PT_VISIBLE:
				if (repo_scope == TEMP_SCOPE)
					break;
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prop_type),
				    cur_zone->zc_match? "yes":"no");
				break;
			case PT_ZONELABEL:
				l_to_str(&cur_zone->zc_label,
				    &str, M_LABEL);
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prop_type), str);
				free(str);
				break;
			case PT_MLP_PRIVATE:
				info_mlps(fp, cur_private_mlps,
				    prop_type);
				break;
			case PT_MLP_SHARED:
				info_mlps(fp, cur_shared_mlps,
				    prop_type);
				break;
			default:
				scope_usage(prop_type);
				break;
			}
			break;
		}
		if (once)
			break;
	}
}

void
list_func(cmd_t *cmd)
{
	NOTE(ARGUNUSED(cmd))
	switch (resource_scope) {
	case RT_TEMPLATE:
	default:
		list_templates();
		break;
	case RT_ZONE:
		list_zones();
		break;
	}
}

void
get_func(cmd_t *cmd)
{
	int prop_type;
	property_value_ptr_t pp;
	char *prop_id;
	tsol_rhent_t *rhentp;

	prop_type = cmd->cmd_prop_name[0];

	if (cmd->cmd_prop_nv_pairs != 1) {
		long_usage(CMD_GET, B_TRUE);
		return;
	}
	pp = cmd->cmd_property_ptr[0];

	if (prop_type != PT_HOSTADDR) {
		long_usage(CMD_GET, B_TRUE);
		return;
	}
	if (((prop_id = pp->pv_simple) == NULL)) {
		zerr(gettext("The property is not valid."));
		saw_error = B_TRUE;
		return;
	}

	if ((rhentp = host2rhent(prop_id)) == NULL) {
		saw_error = B_TRUE;
		return;
	} else {
		if (tnrh(TNDB_GET, rhentp) == 0) {
			printf("\t%s\n", rhentp->rh_template);
		} else {
			printf(gettext("\t%No matching template\n"));
		}
	}
}

/*
 * This function can be called by commit_func(), which needs to save things,
 * in addition to the general call from parse_and_run(), which doesn't need
 * things saved.  Since the parameters are standardized, we distinguish by
 * having commit_func() call here with cmd->cmd_arg set to "save" to indicate
 * that a save is needed.
 */

void
verify_func(cmd_t *cmd)
{
	NOTE(ARGUNUSED(cmd))
	int i, j;
	boolean_t verified = B_TRUE;
	int prop_type;
	m_label_t *l1;
	m_range_t range;
	tsol_rhent_t *dup_host;
	char *str;
	char *name;

	for (i = 0; prop_helptab[i].prop_num != 0; i++) {

		prop_type = prop_helptab[i].prop_num;
		if (!(need_to_commit & (pt_to_mask(prop_type))))
			continue;

		switch (prop_type) {
		case PT_TPNAME:
			if (resource_scope == RT_ZONE) {
				char brand[32];

				name = cur_zone->zc_name;
				if ((zone_get_brand(name,
				    brand, 32)) != 0) {
					zerr(gettext("zone '%s' must be "
					    "configured before assigning "
					    "properties."), name);
					saw_error = B_TRUE;
					verified = B_FALSE;
					break;
				}
				if ((strcmp(brand, "labeled")) != 0) {
					zerr(gettext("zone '%s' must have its "
					    "brand property set to 'labeled'"),
					    name);
					saw_error = B_TRUE;
					verified = B_FALSE;
					break;
				}
				if ((strchr(name, ':')) != NULL) {
					zerr(gettext("The zone name '%s' "
					    "cannot include a colon."), name);
					saw_error = B_TRUE;
					verified = B_FALSE;
					break;
				}
			} else {
				name = cur_template->name;
				if ((strchr(name, ':')) != NULL) {
					zerr(gettext("The template name '%s' "
					    "cannot include a colon."), name);
					saw_error = B_TRUE;
					verified = B_FALSE;
					break;
				}
			}

			break;
		case PT_HOSTTYPE:
			/* already validated as SUNW_CIPSO or LABELED */
			break;
		case PT_DOI:
			/* already validated in set_func() */
			break;
		case PT_DEFLABEL:
			if ((cur_template->host_type == UNLABELED) &&
			    (!bslvalid(&cur_template->tp_def_label))) {
				zerr(gettext("The def_label property is "
				    "not valid"));
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			break;
		case PT_MINLABEL:
			l1 = &cur_template->tp_gw_sl_range.lower_bound;
			if (!bldominates(
			    &cur_template->tp_gw_sl_range.upper_bound, l1)) {
				zerr(gettext("The min_label is not dominated "
				    "by the max_label property"));
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			if (!cipso_representable(l1, prop_type)) {
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			break;
		case PT_MAXLABEL:
			l1 = &cur_template->tp_gw_sl_range.upper_bound;
			if (!bldominates(l1,
			    &cur_template->tp_gw_sl_range.lower_bound)) {
				zerr(gettext("The max_label does not dominate "
				    "by the min_label property"));
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			if (!cipso_representable(l1, prop_type)) {
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			break;
		case PT_LABELSET:
			/* for each */
			/* must be outside of min max range */
			l1 = &cur_template->tp_gw_sl_set[0];
			range.lower_bound  =
			    &cur_template->tp_gw_sl_range.lower_bound;
			range.upper_bound  =
			    &cur_template->tp_gw_sl_range.upper_bound;
			for (j = 0; j < NSLS_MAX; j++, l1++) {
				if ((BLTYPE(l1, SUN_SL_ID)) &&
				    blinrange(l1, &range)) {
					l_to_str(l1, &str, M_LABEL);
					zerr(gettext("The %s '%s'\n is already "
					    "implied by the min/max_label "
					    "range."),
					    pt_to_str(PT_LABELSET), str);
					verified = B_FALSE;
					saw_error = B_TRUE;
				}
				if ((l1->id != 0) &&
				    (!cipso_representable(l1, prop_type))) {
					verified = B_FALSE;
					saw_error = B_TRUE;
				}
			}
			break;
		case PT_HOSTADDR:
			switch (repo_scope) {
			case FILES_SCOPE:
			case LDAP_SCOPE:
				if ((name = check_host_conflicts(cur_hosts,
				    &dup_host)) != NULL) {
					int alen;
					char abuf[INET6_ADDRSTRLEN + 5];

					translate_inet_addr(dup_host,
					    &alen, abuf, sizeof (abuf));
					zerr(gettext("The IP address %s"
					    " is already assigned to '%s'."),
					    abuf, name);
					free(name);
					verified = B_FALSE;
					saw_error = B_TRUE;
				}
				break;
			case TEMP_SCOPE:
				break;
			}
			break;
		case PT_ZONELABEL:
			l1 = &cur_zone->zc_label;
			if (!cipso_representable(l1, prop_type)) {
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			if ((name = check_label_conflicts(cur_zone)) != NULL) {
				zerr(gettext("The zone's label is already "
				    "assigned to '%s'."), name);
				free(name);
				verified = B_FALSE;
				saw_error = B_TRUE;
			}
			break;
		}
	}
	resource_verified = verified;
}

void
commit_func(cmd_t *cmd)
{
	int flags;

	verify_func(cmd);

	if (!resource_verified)
		return;

	if (resource_exists)
		flags = MOD_MASK;
	else
		flags = ADD_MASK;

	switch (resource_scope) {
	case RT_TEMPLATE:
		if (put_tpent(cur_template, flags) == Z_OK)
			need_to_commit = 0;
		break;
	case RT_ZONE:
		if (put_zcent(cur_zone, flags) == Z_OK)
			need_to_commit = 0;
		break;
	}
}

void
revert_func(cmd_t *cmd)
{
	int arg, answer;
	boolean_t force_revert = B_FALSE;
	boolean_t arg_err = B_FALSE;
	rhostlist_t *cur_host, *next_host;
	mlplist_t *cur_mlp, *next_mlp;

	assert(cmd != NULL);

	optind = opterr = 0;
	while ((arg = getopt(cmd->cmd_argc, cmd->cmd_argv, "F")) != EOF) {
		switch (arg) {
		case 'F':
			force_revert = B_TRUE;
			break;
		default:
			if (optopt == '?')
				longer_usage(CMD_REVERT);
			else
				short_usage(CMD_REVERT);
			arg_err = B_TRUE;
			break;
		}
	}
	if (arg_err)
		return;

	switch (resource_scope) {
	case RT_TEMPLATE:
		if (!force_revert) {
			if ((answer = ask_yesno(B_FALSE, gettext("Are you "
			    "sure you want to revert the template"))) == -1) {
				zerr(gettext("Input not from terminal and "
				    "-F not specified:\n%s command ignored, "
				    " exiting."), cmd_to_str(CMD_REVERT));
				exit(Z_ERR);
			}
			if (answer != 1)
				return;
		}

		next_host = cur_hosts;
		while (next_host) {
			cur_host = next_host;
			next_host = cur_host->next;
			remove_rhent(&cur_hosts, cur_host);
		}
		next_host = del_hosts;
		while (next_host) {
			cur_host = next_host;
			next_host = cur_host->next;
			remove_rhent(&del_hosts, cur_host);
			/* removed the free_rhent. */
		}

		need_to_commit = 0;
		saw_error = B_FALSE;

		tsol_freetpent(cur_template);
		tpname = strdup(orig_tpname);
		if (!initialize_tmpl())
			exit(Z_ERR);
		break;
	case RT_ZONE:
		if (!force_revert) {
			if ((answer = ask_yesno(B_FALSE, gettext("Are you "
			    "sure you want to revert the zone properties")))
			    == -1) {
				zerr(gettext("Input not from terminal and "
				    "-F not specified:\n%s command ignored, "
				    " exiting."), cmd_to_str(CMD_REVERT));
				exit(Z_ERR);
			}
			if (answer != 1)
				return;
		}
		next_mlp = cur_private_mlps;
		while (next_mlp) {
			cur_mlp = next_mlp;
			next_mlp = cur_mlp->next;
			remove_mlp(&cur_private_mlps, cur_mlp);
		}
		next_mlp = cur_shared_mlps;
		while (next_mlp) {
			cur_mlp = next_mlp;
			next_mlp = cur_mlp->next;
			remove_mlp(&cur_private_mlps, cur_mlp);
		}
		need_to_commit = 0;
		saw_error = B_FALSE;

		tsol_freezcent(cur_zone);
		zonename = strdup(orig_zonename);
		if (!initialize_zone())
			exit(Z_ERR);
		}
}

void
help_func(cmd_t *cmd)
{
	int i;
	int prop_type;


	assert(cmd != NULL);

	if (cmd->cmd_argc == 0) {
		prop_type = cmd->cmd_prop_name[0];
		if (prop_type) {
			longer_prop_usage(prop_type);
			return;
		} else {
			(void) printf("%s\n", helptab[CMD_HELP].short_usage);
			return;
		}
	}
	if (strcmp(cmd->cmd_argv[0], "usage") == 0) {
		usage(B_FALSE, CMD_HELP, HELP_USAGE);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "subcommands") == 0) {
		usage(B_FALSE, CMD_HELP, HELP_SUBCMDS);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "properties") == 0) {
		usage(B_FALSE, CMD_HELP, HELP_PROPERTIES);
		return;
	}
	if (strcmp(cmd->cmd_argv[0], "-?") == 0) {
		(void) fprintf(stdout, "%s\n", helptab[CMD_HELP].short_usage);
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
	usage(B_FALSE, CMD_HELP, HELP_META);
}

/* This is the back-end helper function for read_input() below. */

static int
string_to_yyin(char *string)
{
	if ((yyin = tmpfile()) == NULL) {
		zerr(gettext("can't write temp file"));
		return (Z_ERR);
	}
	if (fwrite(string, strlen(string), 1, yyin) != 1) {
		zerr(gettext("can't write temp file"));
		return (Z_ERR);
	}
	if (fseek(yyin, 0, SEEK_SET) != 0) {
		zerr(gettext("can't write temp file"));
		return (Z_ERR);
	}
	return (Z_OK);
}
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
		 * really delete a template in the command handler called from
		 * yyparse() above, but "really quit?" makes no sense in this
		 * context.  So disable prompting.
		 */
		ok_to_prompt = B_FALSE;
	}
	/*
	 * Make sure we tried something and that the handle checks
	 * out, or we would get a false error trying to commit.
	 */
	if (need_to_commit) {
		if ((cmd = alloc_cmd()) == NULL) {
			zerr(gettext("Out of memory"));
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
				zerr(gettext("Resource not saved."));
				return (Z_ERR);
			}
			answer = ask_yesno(B_FALSE,
			    gettext("Resource not saved; really quit"));
			if (answer == -1) {
				zerr(gettext("Resource not saved."));
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
 * Like most tncfg functions, it returns Z_OK or Z_ERR, *or* Z_REPEAT
 * so do_interactive() knows that we are not really done (i.e, we asked
 * the user if we should really quit and the user said no).
 */
static int
read_input()
{
	boolean_t yyin_is_a_tty = isatty(fileno(yyin));
	/*
	 * The prompt is "e:p> " or "e:p:c> " where e is execname, p is template
	 * and c is command name: 5 is for the two ":"s + "> " + terminator.
	 */
	char prompt[MAXPATHLEN + 5], *line;

	/* yyin should have been set to the appropriate (FILE *) if not stdin */
	newline_terminated = B_TRUE;
	for (;;) {
		if (yyin_is_a_tty) {
			if (newline_terminated) {
				switch (resource_scope) {
				case RT_TEMPLATE:
				default:
					(void) snprintf(prompt, sizeof (prompt),
					    "%s:%s> ", execname, tpname);
					break;
				case RT_ZONE:
					(void) snprintf(prompt, sizeof (prompt),
					    "%s:%s> ", execname, zonename);
					break;
				}
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
 * This function is used in the tncfg-interactive-mode scenario: it just
 * calls read_input() until we are done.
 */

static int
do_interactive(void)
{
	int err;

	interactive_mode = B_TRUE;
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
		zerr(gettext("Out of memory"));
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
getusername()
{
	struct passwd	*pw;

	pw = getpwuid(getuid());
	if (pw == NULL) {
		(void) printf(gettext("Unable to get user name\n"));
		exit(Z_ERR);
	}
	return (strdup(pw->pw_name));
}

int
main(int argc, char *argv[])
{
	int err, arg;
	int	rep_count = 0;
	char *repository = NULL;

	/* This must be before anything goes to stdout. */
	setbuf(stdout, NULL);

	saw_error = B_FALSE;
	cmd_file_mode = B_FALSE;
	execname = get_execbasename(argv[0]);
	username = getusername();

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if (argc > 1 && (strcmp(argv[1], cmd_to_str(CMD_HELP)) == 0)) {
		usage(B_FALSE, CMD_HELP, HELP_USAGE | HELP_SUBCMDS);
		exit(Z_USAGE);
	}

	if (!is_system_labeled()) {
		zerr(gettext("%s: Trusted Extensions must be enabled."),
		    execname);
		exit(Z_ERR);
	}
	if ((getzoneid()) != GLOBAL_ZONEID) {
		zerr(gettext("%s: must be run from the global zone."),
		    execname);
		exit(Z_ERR);
	}

	while ((arg = getopt(argc, argv, "?et:z:S:f:")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?')
				usage(B_FALSE, CMD_HELP,
				    HELP_USAGE | HELP_SUBCMDS);
			else
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
			/* NOTREACHED */
		case 'f':
			cmd_file_name = strdup(optarg);
			cmd_file_mode = B_TRUE;
			break;
		case 'e':
			if (repo_scope != -1) {
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			}
			rep_count++;
			repo_scope = TEMP_SCOPE;
			break;
		case 't':
			if (resource_scope == RT_TEMPLATE) {
				fprintf(stderr, "%s", gettext("ERROR:"
				    " -t option specified multiple"
				    " times.\n"));
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			} else if (resource_scope == RT_ZONE) {
				fprintf(stderr, "%s", gettext("ERROR:"
				    " -t and -z options are mutually"
				    " exclusive.\n"));
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			} else {
				tpname = strdup(optarg);
				resource_scope = RT_TEMPLATE;
				break;
			}
			break;
		case 'z':
			if (resource_scope == RT_ZONE) {
				fprintf(stderr, "%s", gettext("ERROR:"
				    " -z option specified multiple"
				    " times.\n"));
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			} else if (resource_scope == RT_TEMPLATE) {
				fprintf(stderr, "%s", gettext("ERROR:"
				    " -t and -z options are mutually"
				    " exclusive.\n"));
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			} else {
				zonename = strdup(optarg);
				resource_scope = RT_ZONE;
				break;
			}
			break;
		case 'S':
			repository = optarg;
			rep_count++;
			if (rep_count > 1) {
				fprintf(stderr, "%s", gettext("ERROR:"
				    " -S option is not valid.\n"));
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			}
			if (strncmp(repository, "files", 5) == 0)
				repo_scope = FILES_SCOPE;
			else if (strncmp(repository, "ldap", 4) == 0)
				repo_scope = LDAP_SCOPE;
			else {
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			}
			break;

		default:
			usage(B_FALSE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
		}
	}
	if (resource_scope == RT_ZONE &&
	    (repo_scope == FILES_SCOPE || repo_scope == LDAP_SCOPE)) {
		fprintf(stderr, "%s", gettext("ERROR:"
		    " -z and -S options are mutually"
		    " exclusive.\n"));
		usage(B_FALSE, CMD_HELP, HELP_USAGE);
		exit(Z_USAGE);
	}
	if (rep_count == 0 && tpname != NULL) {
		int status;
		status = find_in_nss(SEC_REP_DB_TNRHTP, tpname, &mod_rep);
		if (status == SEC_REP_NOT_FOUND) {
			if (get_repository_handle(NSS_REP_FILES,
			    &mod_rep) != 0)
				exit(Z_ERR);
		} else if (status == SEC_REP_SYSTEM_ERROR)
				exit(Z_ERR);
		repo_scope = mod_rep->type;
	} else if (rep_count > 0) {
		if (get_repository_handle(repository, &mod_rep) != 0)
			exit(Z_ERR);
	}

	if (repo_scope == -1 && mod_rep == NULL) {
		repo_scope = FILES_SCOPE;
		if (get_repository_handle(NSS_REP_FILES, &mod_rep) != 0)
			exit(Z_ERR);

	}

	switch (resource_scope) {
	case RT_TEMPLATE:
		if (tpname == NULL) {
			usage(B_TRUE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
		}
		if (optind > argc || strcmp(tpname, "") == 0) {
			usage(B_TRUE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
		}
		if (strlen(tpname) >= TNTNAMSIZ) {
			zerr(gettext("The template name is too long"));
			saw_error = B_TRUE;
			exit(Z_ERR);
		}
		if (!initialize_tmpl())
			exit(Z_ERR);
		orig_tpname = strdup(tpname);
		break;
	case RT_ZONE:
		if (zonename == NULL) {
			usage(B_TRUE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
		}
		if (optind > argc || strcmp(zonename, "") == 0) {
			usage(B_TRUE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
		}
		if (!initialize_zone())
			exit(Z_ERR);
		orig_zonename = strdup(zonename);
		tpname = "cipso";
		break;
	default:
		tpname = strdup("cipso");
		resource_scope = RT_TEMPLATE;
		if (!initialize_tmpl())
			exit(Z_ERR);
		orig_tpname = strdup(tpname);
		break;
	}

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
		if (!cmd_file_mode) {
			err = do_interactive();
		} else {
			err = cmd_file(cmd_file_name);
		}
	} else {
		err = one_command_at_a_time(argc - optind, &(argv[optind]));
	}
	(void) del_GetLine(gl);
	return (err);
}
