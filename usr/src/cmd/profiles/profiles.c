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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * profiles is a lex/yacc based command interpreter used to manage profile
 * configurations.  The lexer (see profiles_lex.l) builds up tokens, which
 * the grammar (see profiles_grammar.y) builds up into commands, some of
 * which takes resources and/or properties as arguments.  See the block
 * comments near the end of profiles_grammar.y for how the data structures
 * which keep track of these resources and properties are built up.
 *
 * The resource/property data structures are inserted into a command
 * structure (see profiles.h), which also keeps track of command names,
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
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>
#include <deflt.h>
#include <secdb.h>
#include <user_attr.h>
#include <prof_attr.h>
#include <exec_attr.h>
#include <auth_attr.h>
#include <priv.h>
#include <syslog.h>
#include <bsm/libbsm.h>
#include <libcmdutils.h>
#include <note.h>
#include <auth_list.h>
#include "profiles.h"
#include <nsswitch.h>
#include <nss.h>
#include "nssec.h"

#define	EXIT_OK		0
#define	EXIT_FATAL	1
#define	EXIT_NON_FATAL	2

#define	TMP_BUF_LEN	2048		/* size of temp string buffer */

#define	PRINT_DEFAULT	0x0000
#define	PRINT_NAME	0x0010
#define	PRINT_LONG	0x0020
#define	PRINT_ALL	0x0040

#ifndef TEXT_DOMAIN			/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"
#endif

static void print_profs_long(FILE *, execattr_t *);
static void print_profile_help_kv(FILE *, kva_t *, char *);
static boolean_t verify_pathname(char *, int, char *);
static boolean_t verify(int, int, char *);
static boolean_t remove_execattr(execattr_t **, char *);

#if !defined(TEXT_DOMAIN)		/* should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it wasn't */
#endif

#define	EXEC_PREFIX	"exec "
#define	EXEC_LEN	(strlen(EXEC_PREFIX))
#define	pt_to_mask(value)	((unsigned int)(1<<(value)))

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

typedef struct attrs {
	char *attrvals[MAXPROFS];
	int attr_cnt;
} attrs_t;

typedef struct privattrs
{
	char *privs;
} priv_attr_t;

typedef struct print_opts {
	FILE *fp;
	int print_flag;
} print_opts_t;

extern int yyparse(void);
extern int lex_lineno;
extern char *_strtok_escape(char *, char *, char **);
extern char *_strpbrk_escape(char *, char *);
extern char *_unescape(char *, char *);


#define	MAX_LINE_LEN	1024
#define	MAX_CMD_HIST	1024
#define	MAX_CMD_LEN	1024

#define	AUDIT_FLAGS	"audit_flags"
#define	ALWAYSAUDIT	"always_audit"
#define	NEVERAUDIT	"never_audit"

/*
 * Each SHELP_ should be a simple string.
 */

#define	SHELP_ADD	"add <property-name>=<property-value>"
#define	SHELP_CANCEL	"cancel"
#define	SHELP_CLEAR	"clear <property-name>"
#define	SHELP_COMMIT	"commit"
#define	SHELP_DELETE	"delete [-F]"
#define	SHELP_END	"end"
#define	SHELP_EXIT	"exit [-F]"
#define	SHELP_EXPORT	"export [-f output-file]"
#define	SHELP_HELP	"help [usage] [subcommands] [properties] "\
	"[<subcommand>] [<property>]"
#define	SHELP_INFO	"info [<property-value>]"
#define	SHELP_REMOVE	"remove [-F] <property-name>[=<property-value>]"
#define	SHELP_REVERT	"revert [-F]"
#define	SHELP_SELECT	"select cmd=<path>"
#define	SHELP_SET	"set <property-name>=<property-value>"
#define	SHELP_VERIFY	"verify"

static struct help helptab[] = {
	{ CMD_ADD,	"add",		HELP_ADDREM,	SHELP_ADD, },
	{ CMD_CANCEL,	"cancel",	HELP_RES_SCOPE,	SHELP_CANCEL, },
	{ CMD_CLEAR,	"clear",	HELP_PROPS,	SHELP_CLEAR, },
	{ CMD_COMMIT,	"commit",	0,		SHELP_COMMIT, },
	{ CMD_DELETE,	"delete",	0,		SHELP_DELETE, },
	{ CMD_END,	"end",		HELP_RES_SCOPE,	SHELP_END, },
	{ CMD_EXIT,	"exit",		0,		SHELP_EXIT, },
	{ CMD_EXPORT,	"export",	0,		SHELP_EXPORT, },
	{ CMD_HELP,	"help",		0,		SHELP_HELP },
	{ CMD_INFO,	"info",		HELP_PROPS,	SHELP_INFO, },
	{ CMD_REMOVE,	"remove",	HELP_ADDREM,	SHELP_REMOVE, },
	{ CMD_REVERT,	"revert",	0,		SHELP_REVERT, },
	{ CMD_SELECT,	"select",	HELP_CMDMODE,	SHELP_SELECT, },
	{ CMD_SET,	"set",		HELP_VALUE,	SHELP_SET, },
	{ CMD_VERIFY,	"verify",	0,		SHELP_VERIFY, },
	{ 0 },
};

#define	SHELP_PROF	"<profile name>"
#define	SHELP_PROFS	"<profile name>[,<profile name>]..."
#define	SHELP_AUTHS	"<authorization>[,<authorization>]..."
#define	SHELP_PRIVS	"<privilege>[,<privilege>]..."
#define	SHELP_AFLAGS	"<audit_flag>[,<audit_flag>]..."
#define	SHELP_TEXT	"<descriptive text>"
#define	SHELP_FILE	"<html filename>"
#define	SHELP_PATH	"<full path name>"
#define	SHELP_USER	"<user name or id>"
#define	SHELP_GROUP	"<group name or id>"

#define	TEXT_TYPE	1
#define	AUTH_TYPE	2
#define	PROF_TYPE	3
#define	PRIV_TYPE	4
#define	AUDIT_TYPE	5
#define	FILE_TYPE	6
#define	PATH_TYPE	7
#define	UID_TYPE	8
#define	GID_TYPE	9

/*
 * Help syntax for profile properties
 */
static struct prop_help prof_helptab[] = {
	{ PT_PROFNAME,		HELP_PRESERVE,	TEXT_TYPE,	SHELP_PROF, },
	{ PT_DESCRIPTION,	HELP_SIMPLE,	TEXT_TYPE,	SHELP_TEXT, },
	{ PT_AUTHORIZATION,	HELP_LIST,	AUTH_TYPE,	SHELP_AUTHS, },
	{ PT_SUBPROFILE,	HELP_LIST,	PROF_TYPE,	SHELP_PROFS, },
	{ PT_PRIVS,		HELP_LIST,	PRIV_TYPE,	SHELP_PRIVS, },
	{ PT_LIMPRIV,		HELP_LIST,	PRIV_TYPE,	SHELP_PRIVS, },
	{ PT_DFLTPRIV,		HELP_LIST,	PRIV_TYPE,	SHELP_PRIVS, },
	{ PT_ALWAYSAUDIT,	HELP_LIST,	AUDIT_TYPE,	SHELP_AFLAGS, },
	{ PT_NEVERAUDIT,	HELP_LIST,	AUDIT_TYPE,	SHELP_AFLAGS, },
	{ PT_HELPFILE,		HELP_SIMPLE,	PATH_TYPE,	SHELP_FILE },
	{ PT_COMMAND,		HELP_COMMAND,	PATH_TYPE,	SHELP_PATH },
	{ 0 },
};

/*
 * Help syntax for command properties
 */
static struct prop_help cmd_helptab[] = {
	{ PT_PATHNAME,		HELP_PRESERVE,	PATH_TYPE,	SHELP_PATH, },
	{ PT_PRIVS,		HELP_LIST,	PRIV_TYPE,	SHELP_PRIVS, },
	{ PT_LIMITPRIVS,	HELP_LIST,	PRIV_TYPE,	SHELP_PRIVS, },
	{ PT_EUID,		HELP_SIMPLE,	UID_TYPE,	SHELP_USER, },
	{ PT_UID,		HELP_SIMPLE,	UID_TYPE,	SHELP_USER, },
	{ PT_EGID,		HELP_SIMPLE,	GID_TYPE,	SHELP_GROUP, },
	{ PT_GID,		HELP_SIMPLE,	GID_TYPE,	SHELP_GROUP, },
	{ 0 },
};

#define	MAX_RT_STRLEN	16

/* These *must* match the order of the RT_ define's from profiles.h */
char *res_types[] = {
	PROFATTR_DB_TBLT,
	KV_COMMAND,
	NULL
};

/* These *must* match the order of the PT_ define's from profiles.h */
char *prop_types[] = {
	PROFATTR_DB_TBLT,
	PROFATTR_COL0_KW,
	PROFATTR_AUTHS_KW,
	PROFATTR_PROFS_KW,
	PROFATTR_PRIVS_KW,
	USERATTR_LIMPRIV_KW,
	USERATTR_DFLTPRIV_KW,
	PROFATTR_COL3_KW,
	AUTHATTR_HELP_KW,
	EXECATTR_COL5_KW,
	EXECATTR_EUID_KW,
	EXECATTR_UID_KW,
	EXECATTR_EGID_KW,
	EXECATTR_GID_KW,
	EXECATTR_LPRIV_KW,
	ALWAYSAUDIT,
	NEVERAUDIT,
	KV_COMMAND,
	NULL
};

/* These *must* match the order of the PROP_VAL_ define's from profiles.h */
static char *prop_val_types[] = {
	"simple",
	"list",
};

static const char **priv_list = NULL;
static const char **auth_list = NULL;
static const char **prof_list = NULL;
static const char **audit_list = NULL;
static sec_repository_t *lk_rep = NULL;
static sec_repository_t *mod_rep = NULL;

/*
 * The various _cmds[] lists below are for command tab-completion.
 */

static const char *profile_scope_cmds[] = {
	"commit",
	"delete",
	"exit",
	"export",
	"help properties",
	"help subcommands",
	"help usage",
	"info",
	"revert",
	"verify",
	"add cmd=/",
	"add auths=",
	"add profiles=\"",
	"add privs=",
	"add limitpriv=",
	"add defaultpriv=",
	"add always_audit=",
	"add never_audit=",
	"clear auths",
	"clear help",
	"clear profiles",
	"clear privs",
	"clear limitpriv",
	"clear defaultpriv",
	"clear always_audit",
	"clear never_audit",
	"remove cmd=/",
	"remove auths=",
	"remove profiles=\"",
	"remove privs=",
	"remove limitpriv=",
	"remove defaultpriv=",
	"remove always_audit=",
	"remove never_audit=",
	"select cmd=",
	"set name=\"",
	"set auths=",
	"set profiles=\"",
	"set privs=",
	"set limitpriv=",
	"set defaultpriv=",
	"set always_audit=",
	"set never_audit=",
	"set desc=\"",
	"set help=",
	"info name",
	"info auths",
	"info profiles",
	"info privs",
	"info limitpriv",
	"info defaultpriv",
	"info always_audit",
	"info never_audit",
	"info desc",
	"info help",
	"info cmd",
	NULL
};

static const char *cmd_res_scope_cmds[] = {
	"add limitprivs=",
	"add privs=",
	"remove limitprivs=",
	"remove privs=",
	"set limitprivs=",
	"set privs=",
	"set id=",
	"set gid=",
	"set uid=",
	"set egid=",
	"set euid=",
	"info",
	"info limitprivs",
	"info privs",
	"info gid",
	"info uid",
	"info egid",
	"info euid",
	"clear limitprivs",
	"clear privs",
	"clear gid",
	"clear uid",
	"clear egid",
	"clear euid",
	"cancel",
	"end",
	"help properties",
	"help subcommands",
	NULL
};

/* Global variables */

/* set early in main(), never modified thereafter, used all over the place */
static char *execname;
static char *cur_cmdname;
static char *username;
static char *profname = NULL;
static char *orig_profname = NULL;

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

/* set and checked in initialize() */

/* initialized in do_interactive(), checked in initialize() */
static boolean_t interactive_mode;

/* set in main(), checked in multiple places */
static boolean_t read_only_mode = B_FALSE;
static boolean_t profile_verified = B_FALSE;

/* scope is outer/profile or inner/resource */
static boolean_t profile_scope = B_TRUE;
static int resource_scope;	/* should be in the RT_ list from profiles.h */
static int end_op = -1;		/* operation on end is either add or modify */

int num_prop_vals;		/* for grammar */

/*
 * These are for keeping track of resources as they are specified as part of
 * the multi-step process.  They should be initialized by add_resource() or
 * select_func() and filled in by add_property() or set_func().
 */

profattr_t	*cur_profile = NULL;
execattr_t	*cur_commands, *cur_command, *mod_command, *del_commands = NULL;

static GetLine *gl;	/* The gl_get_line() resource object */

/* Functions begin here */

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

static void zerr(const char *, ...);
static boolean_t check_has_profile(char *);

static char *strdup_check(char *ptr)
{
	char *temp = NULL;
	if ((temp = strdup(ptr)) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	return (temp);
}

static const char **
get_privlist()
{
	int i;
	int maxprivs = 0;
	const char *pname;
	const char **plist;

	while (priv_getbynum(maxprivs))
		maxprivs++;
	if ((plist = malloc((maxprivs + 1) * sizeof (char *))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	for (i = 0; ((pname = priv_getbynum(i)) != NULL); ) {
		plist[i++] = pname;
	}
	plist[i] = NULL;
	return (plist);
}

static const char **
get_proflist()
{
	int i;
	int maxprofs = 0;
	profattr_t *nextprof;
	const char **plist;
	char *name;
	char *quoted_name;
	size_t list_len;
	size_t path_len;

	setprofattr();
	while (getprofattr())
		maxprofs++;
	setprofattr();
	list_len = (maxprofs + 1) * sizeof (char *);
	if ((plist = malloc(list_len)) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	for (i = 0; ((nextprof = getprofattr()) != NULL); i++) {
		name = nextprof->name;
		path_len = strlen(name) + 3; /* add quotes */
		if ((quoted_name = malloc(path_len)) == NULL) {
			zerr(gettext("Out of memory"));
			exit(Z_ERR);
		}
		snprintf(quoted_name, path_len, "\"%s\"", name);
		plist[i] = quoted_name;
	}
	plist[i] = NULL;
	return (plist);
}

static const char **
get_authlist()
{
	int i;
	int maxauths = 0;
	authattr_t *nextauth;
	const char **plist;
	char *name;

	setauthattr();
	while (getauthattr())
		maxauths++;
	setauthattr();
	if ((plist = malloc((maxauths + 1) * sizeof (char *))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	for (i = 0; ((nextauth = getauthattr()) != NULL); ) {
		name = nextauth->name;
		/*
		 * Skip header entries ending in 'dot'
		 */
		if (name[strlen(name) - 1] != '.') {
			plist[i++] = strdup_check(name);
		}
	}
	plist[i] = NULL;
	return (plist);
}

static const char **
get_auditlist()
{
	int i;
	int maxclasses = 0;
	au_class_ent_t *nextclass;
	const char **plist;

	setauclass();
	while (getauclassent())
		maxclasses++;
	setauclass();
	if ((plist = malloc((maxclasses + 1) * sizeof (char *))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	for (i = 0; ((nextclass = getauclassent()) != NULL); i++) {
		plist[i] = strdup_check(nextclass->ac_name);
	}
	plist[i] = NULL;
	return (plist);
}

static void
free_plist(const char **plist)
{
	int i = 0;
	char *name;

	if (plist) {
		while (plist[i]) {
			name = (char *)plist[i++];
			if (name)
				free(name);
		}
		if (plist != NULL)
			free(plist);
		plist = NULL;
	}
}

static const char **
get_execlist()
{
	int i;
	int maxexecs = 0;
	execattr_t *next;
	const char **plist;
	static const char **exec_list = NULL;

	next = cur_commands;
	while (next) {
		if (next->attr == NULL) {
			next->attr = _new_kva(1);
			next->attr->length = 1;
			next->attr->data[0].key =
			    strdup_check(EXECATTR_EUID_KW);
		}
		maxexecs++;
		next = next->next;
	}
	if ((plist = malloc((maxexecs + 1) * sizeof (char *))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	free_plist(exec_list);
	exec_list = plist;
	next = cur_commands;
	i = 0;
	while (next) {
		plist[i++] = strdup_check(next->id);
		next = next->next;
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
	if (strchr(instr, ' ') == NULL) {
		(void) strlcpy(outstr, instr, outstrsize);
		return (outstr);
	}
	(void) snprintf(outstr, outstrsize, "\"%s\"", instr);
	return (outstr);
}

static const char **
get_plist(int prop_type)
{
	int i = 0;
	int maxattrs = 100;
	const char **plist;
	char *val;
	kva_t *attr;
	static const char **attr_plist = NULL;

	if ((plist =
	    malloc((maxattrs + 1) * sizeof (char *))) == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	free_plist(attr_plist);
	attr_plist = plist;

	i = 0;
	if (resource_scope == RT_COMMAND)
		attr = cur_command->attr;
	else
		attr = cur_profile->attr;

	val = kva_match(attr, pt_to_str(prop_type));
	if (val != NULL) {
		char *vals;
		char *token, *lasts;

		vals = strdup_check(val);
		for (token = strtok_r(vals, ",", &lasts);
		    token != NULL;
		    token = strtok_r(NULL, ",", &lasts)) {
			if (prop_type == PT_SUBPROFILE) {
				plist[i++] = quoteit(token);
			} else {
				plist[i++] = strdup_check(token);
			}
			if (i == maxattrs)
				break;
		}
	}
	plist[i] = NULL;
	return (plist);
}

static int
add_stuff(WordCompletion *cpl, const char *line1, const char **list,
    int word_start, int word_end)
{
	int i, err;
	int len = word_end - word_start;

	for (i = 0; list[i] != NULL; i++) {
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
		if (helptab[i].flags & (HELP_CMDMODE|HELP_VALUE)) {
			int j;
			int cmd_num = helptab[i].cmd_num;

			for (j = 0; ph[j].prop_num != 0; j++) {
				int prop_num = ph[j].prop_num;
				char *prop_name = pt_to_str(prop_num);
				char subcommand[100];
				const char **list;
				int len;

				len = snprintf(subcommand, 100,
				    "%s %s=", helptab[i].cmd_name,
				    prop_name);
				if (strncmp(line, subcommand, len) != 0)
					continue;
				switch (ph[j].type) {
				case PRIV_TYPE:
					if (cmd_num == CMD_REMOVE)
						list = get_plist(prop_num);
					else
						list = priv_list;
					return (add_stuff(cpl,
					    line, list, len,
					    word_end));
				case AUDIT_TYPE:
					if (cmd_num == CMD_REMOVE)
						list = get_plist(prop_num);
					else
						list = audit_list;
					return (add_stuff(cpl,
					    line, list,
					    len, word_end));
				case PROF_TYPE:
					if (cmd_num == CMD_REMOVE)
						list = get_plist(prop_num);
					else
						list = prof_list;
					return (add_stuff(cpl,
					    line, list, len,
					    word_end));
				case AUTH_TYPE:
					if (cmd_num == CMD_REMOVE)
						list = get_plist(prop_num);
					else
						list = auth_list;
					return (add_stuff(cpl,
					    line, list, len,
					    word_end));
				case PATH_TYPE:
					if ((cmd_num == CMD_SELECT) ||
					    (cmd_num == CMD_REMOVE)) {
						return (add_stuff(cpl,
						    line, get_execlist(),
						    len, word_end));
					} else {
						(void) cpl_complete_word(cpl,
						    line + len,
						    word_end - len, "/usr",
						    cpl_file_completions);
						return (0);
					}
				}
			}
		}
	}
	return (add_stuff(cpl, line, scope_cmds, 0, word_end));
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
				if (pp->pv_simple != NULL)
					free(pp->pv_simple);
				break;
			case PROP_VAL_LIST:
				if (pp->pv_list != NULL)
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
	if (list->lp_simple != NULL && strlen(list->lp_simple))
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
			return (gettext("Prints profile to standard "
			    "output, or to output-file if\n\tspecified, in "
			    "a form suitable for use in a command-file."));
		case CMD_ADD:
			return (gettext("Add specified command to "
			    "the profile,\n\tor specified values to "
			    "the property."));
		case CMD_DELETE:
			return (gettext("Deletes the specified profile.\n\t"
			    "The -F flag can be used to force the action."));
		case CMD_REMOVE:
			return (gettext("Remove specified command from "
			    "the profile,\n\tor specified values from "
			    "the property.\n\tThe -F flag can be used to "
			    "force the action."));
		case CMD_SELECT:
			(void) snprintf(line, sizeof (line),
			    gettext("Selects a command to modify.  "
			    "Command modification is completed\n\twith the "
			    "command \"%s\".  The pathname must be specified."),
			    cmd_to_str(CMD_END));
			return (line);
		case CMD_SET:
			return (gettext("Sets property values."));
		case CMD_CLEAR:
			return (gettext("Clears property values."));
		case CMD_INFO:
			return (gettext("Displays information about the "
			    "current profile."));
		case CMD_VERIFY:
			return (gettext("Verifies current profile "
			    "for correctness (some properties types\n\thave "
			    "required values)."));
		case CMD_COMMIT:
			(void) snprintf(line, sizeof (line),
			    gettext("Commits current profile.  "
			    "This operation is\n\tattempted "
			    "automatically upon completion of a %s "
			    "session."), execname);
			return (line);
		case CMD_REVERT:
			return (gettext("Reverts the profile back to the "
			    "last committed state.  The -F flag\n\tcan be "
			    "used to force the action."));
		case CMD_CANCEL:
			return (gettext("Cancels command/property "
			    "specification."));
		case CMD_END:
			return (gettext("Ends command/property "
			    "specification."));
	}
	/* NOTREACHED */
	return (NULL);
}

static char *
long_prop_help(int prop_num)
{
	switch (prop_num) {
	case PT_PROFNAME:
		return (gettext("The name of the profile. "\
		    "The initial value for the name is\n\tspecified  "\
		    "using -p option via the command line.Setting\n\t"\
		    "this property results in a newly named property "\
		    "which\n\tinherits the settings from the previous "\
		    "profile."));
	case PT_DESCRIPTION:
		return (gettext("A short description of "\
		    "the purpose of the profile."));
	case PT_AUTHORIZATION:
		return (gettext("One or more comma separated "\
		    "authorizations to be added to the\n\t"\
		    "profile. See auth_attrs(4)."));
	case PT_SUBPROFILE:
		return (gettext("One or more comma separated supplementary "\
		    "profiles to be\n\tincluded by the current profile."));
	case PT_PRIVS:
		if (resource_scope)
			return (gettext("The effective set of privileges "\
			    "of the process that \n\texecutes the command."));
		else
			return (gettext("The set of privileges that may be "\
			    "specified using the\n\t-P option of pfexec(1M)."));
	case PT_DFLTPRIV:
		return (gettext("The default set of privileges assigned to "\
		    "a user's initial\n\tprocess at login."));
	case PT_LIMPRIV:
		return (gettext("The maximum set of privileges a user, or any "\
		    "process started\n\tby the user can obtain."));
	case PT_HELPFILE:
		return (gettext("The name of the file containing the help "\
		    "text for the\n\tprofile in html format."));
	case PT_PATHNAME:
		return (gettext("The full pathname of an executable file "\
		    "or the asterisk (*)\n\tsymbol, meaning all commands. "\
		    "An asterisk that replaces the\n\tfilename component in a "\
		    "pathname indicates all files in the \n\tdirectory. "\
		    "This property is initially set to the value "\
		    "that was\n\tspecified by the previous cmd property."));
	case PT_UID:
		return (gettext("The real user ID of the process "\
		    "that executes the command."));
	case PT_EUID:
		return (gettext("The effective user ID of the process "\
		    "that executes the command."));
	case PT_GID:
		return (gettext("The real group ID of the process "\
		    "that executes the command."));
	case PT_EGID:
		return (gettext("The effective group ID of the process "\
		    "that executes the command."));
	case PT_LIMITPRIVS:
		return (gettext("The maximum set of privileges of the "\
		    "process that \n\texecutes the command."));
	case PT_ALWAYSAUDIT:
		return (gettext("The audit flags specifying event classes "\
		    "to always audit.\n\tThe first occurance of this keyword, "\
		    "either in the user's\n\tuser_attr(4) entry, or in the "\
		    "ordered list of assigned profiles\n\tis applied at "\
		    "login."));
	case PT_NEVERAUDIT:
		return (gettext("The audit flags specifying event classes "\
		    "to never audit.\n\tThe first occurance of this keyword, "\
		    "either in the user's\n\tuser_attr(4) entry, or in the "\
		    "ordered list of assigned profiles\n\tis applied at "\
		    "login."));
	case PT_COMMAND:
		return (gettext("The full pathname of an executable file "\
		    "or the asterisk (*)\n\tsymbol, meaning all commands. "\
		    "An asterisk that replaces the\n\tfilename component in a "\
		    "pathname indicates all files in the \n\tdirectory. "\
		    "This property is used with the 'add' command to "\
		    "\n\tenter the context for setting properties on "\
		    "executable files."));
		}
	/* NOTREACHED */
	return (NULL);
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

	(void) fprintf(stdout, "\t%s\n", long_prop_help(prop_num));
	(void) fprintf(stdout, "%s:\n", gettext("usage"));

	if (resource_scope) {
		for (i = 0; cmd_helptab[i].prop_num != 0; i++) {
			if (prop_num == cmd_helptab[i].prop_num) {
				(void) fprintf(stdout, "\t%s=%s\n", prop_name,
				    gettext(cmd_helptab[i].short_usage));
				break;
			}
		}
	} else  {
		for (i = 0; prof_helptab[i].prop_num != 0; i++) {
			if (prop_num == prof_helptab[i].prop_num) {
				(void) fprintf(stdout, "\t%s=%s\n", prop_name,
				    gettext(prof_helptab[i].short_usage));
				break;
			}
		}
	}
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
		(void) fprintf(fp, "\t%s [-l] [user ...] \n",
		    execname);
		(void) fprintf(fp, "\t%s -a [-l] [-S [files|ldap]] \n",
		    execname);
		(void) fprintf(fp, "\t%s -p <profile> "\
		    "[-S [files|ldap]] \n", execname);
		(void) fprintf(fp, "\t%s -p <profile> "\
		    "[-S [files|ldap]] <subcommand>\n", execname);
		(void) fprintf(fp, "\t%s -p <profile> "
		    "[-S [files|ldap]] -f <command-file>\n", execname);
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
		if (resource_scope) {
			for (i = 0; cmd_helptab[i].prop_num != 0; i++) {
				(void) fprintf(fp, "\t%s=%s\n",
				    pt_to_str(cmd_helptab[i].prop_num),
				    gettext(cmd_helptab[i].short_usage));
			}
		} else {
			for (i = 0; prof_helptab[i].prop_num != 0; i++) {
				(void) fprintf(fp, "\t%s=%s\n",
				    pt_to_str(prof_helptab[i].prop_num),
				    gettext(prof_helptab[i].short_usage));
			}
		}
	}
	if (flags & (HELP_PROPS|HELP_VALUE)) {
		if (resource_scope) {
			property_syntax(fp, cmd_helptab, cmd_num,
			    helptab[cmd_num].flags);
		} else {
			property_syntax(fp, prof_helptab, cmd_num,
			    helptab[cmd_num].flags);
		}
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
		usage(B_TRUE, cmd_num, helptab[cmd_num].flags);
	}
}

/*
 * scope_usage() is simply used when a command is called from the wrong scope.
 */

static void
scope_usage(uint_t cmd_num)
{
	zerr(gettext("The %s subcommand is only valid in the %s context."),
	    cmd_to_str(cmd_num),
	    profile_scope ?  gettext("command") : gettext("profile"));
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

static boolean_t
profile_is_read_only(int cmd_num)
{
	if (read_only_mode) {
		fprintf(stdout, "Cannot %s. Profile cannot be modified\n",
		    cmd_to_str(cmd_num));
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}

static boolean_t
command_is_read_only(int cmd_num)
{
	if (cur_command->res1 != NULL &&
	    strcmp(cur_command->res1, "RO") == 0) {
		fprintf(stdout, "%s: command is read-only\n",
		    cmd_to_str(cmd_num));
		return (B_TRUE);
	} else {
		return (B_FALSE);
	}
}

static void
print_exec_attrs(FILE *fp, char *prof)
{
	nss_XbyY_buf_t *exbuf = NULL;
	execattr_t *exec;

	if (prof == NULL || fp == NULL)
		return;

	init_nss_buffer(SEC_REP_DB_EXECATTR, &exbuf);
	exec = lk_rep->rops->get_execprof(prof, KV_COMMAND, NULL,
	    GET_ALL|__SEARCH_ALL_POLS, exbuf);
	if (exec != NULL) {
		print_profs_long(fp, exec);
		free_execattr(exec);
	}
	free_nss_buffer(&exbuf);
}

static void
print_prof_attrs(FILE *fp, const char *prof, kva_t *pa, int print_flag)
{
	char *indent = "";
	if (prof == NULL || fp == NULL)
		return;

	if ((print_flag) & (PRINT_LONG)) {
		indent = "      ";
	} else {
		indent = "          ";
	}

	(void) fprintf(fp, "%s%s", indent, prof);
	(void) fprintf(fp, "\n");

	if (print_flag  & PRINT_LONG) {
		if (pa != NULL) {
			print_profile_help_kv(fp, pa, PROFATTR_PRIVS_KW);
			print_profile_help_kv(fp, pa, PROFATTR_AUTHS_KW);
			print_profile_help_kv(fp, pa, PROFATTR_PROFS_KW);
			print_profile_help_kv(fp, pa, AUDIT_FLAGS);
		}
		print_exec_attrs(fp, (char *)prof);
	}
}

static int
show_profs_callback(const char *prof, kva_t *pa, void *opts, void *vcnt)
{
	int *pcnt = vcnt;
	print_opts_t *print_opts = opts;
	int print_flag = print_opts->print_flag;
	FILE *fp = print_opts->fp;

	(*pcnt)++;
	print_prof_attrs(fp, prof, pa, print_flag);

	return (0);
}


static int
show_profs(FILE *fp, char *user, int print_flag)
{
	int		status = EXIT_OK;
	nss_XbyY_buf_t	*buf = NULL;
	profattr_t *profiles[MAXPROFS];

	if (user == NULL) {
		if (print_flag & PRINT_ALL) {
			int i = 0, j, k;
			profattr_t *nextprof;
			/*
			 * Need to eliminate duplicates.
			 * doing this in 2 passes.
			 */
			init_nss_buffer(SEC_REP_DB_PROFATTR, &buf);
			lk_rep->rops->set_profattr();
			while ((nextprof =
			    lk_rep->rops->get_profattr(buf)) != NULL) {
				if (i >= MAXPROFS) {
					fprintf(stderr, gettext("ERROR:Exceeded"
					    " max. profiles allowed.\n"));
					return (-1);
				} else {
					profiles[i++] = nextprof;
				}

			}
			for (j = 0; j < i; j++) {
				for (k = 0; k < i; k++) {
					if (k == j || profiles[k] == NULL ||
					    profiles[j] == NULL)
						continue;
					if (strcmp(profiles[j]->name,
					    profiles[k]->name) == 0) {
						/* eliminate the duplicate */
						free_profattr(profiles[k]);
						profiles[k] = NULL;
					}
				}
			}
			for (j = 0; j < i; j++) {
				if (profiles[j] != NULL) {
					if (print_flag & PRINT_LONG) {
						print_prof_attrs(fp,
						    profiles[j]->name,
						    profiles[j]->attr,
						    print_flag);
					} else {
						fprintf(fp, "\t%s\n",
						    profiles[j]->name);
					}
					free_profattr(profiles[j]);
				}
			}
			lk_rep->rops->end_profattr();
			free_nss_buffer(&buf);
			return (0);
		} else {
			user = username;
		}
	}
	if (getpwnam(user) == NULL) {
		status = EXIT_NON_FATAL;
		(void) fprintf(stderr, "%s: %s: ", execname, user);
		(void) fprintf(stderr, gettext("No such user\n"));
		return (status);
	}

	if (print_flag >= PRINT_DEFAULT) {
		int cnt = 0;
		print_opts_t print_opts;

		print_opts.fp = fp;
		print_opts.print_flag = print_flag;
		(void) _enum_profs(user, show_profs_callback, &print_opts,
		    &cnt);
		if (cnt == 0)
			status = EXIT_NON_FATAL;
	}

	if (status == EXIT_NON_FATAL) {
		(void) fprintf(stderr, "%s: %s: ", execname, user);
		(void) fprintf(stderr, gettext("No profiles\n"));
	}

	return (status);
}

/*
 * print extended profile information.
 *
 * output is "pretty printed" like
 *   [6spaces]Profile Name1
 *   [8spaces possible profile attributes privs]
 *   [8spaces possible profile attributes auths]
 *   [8spaces possible profile attributes profiles]
 *   [8spaces possible profile attributes audit flags]
 *   [10spaces  ]execname1 [skip to ATTR_COL]exec1 attributes1
 *   [      spaces to ATTR_COL              ]exec1 attributes2
 *   [10spaces  ]execname2 [skip to ATTR_COL]exec2 attributes1
 *   [      spaces to ATTR_COL              ]exec2 attributes2
 *   [6spaces]Profile Name2
 *   etc
 */
/*
 * ATTR_COL is based on
 *   10 leading spaces +
 *   25 positions for the executable +
 *    1 space seperating the execname from the attributes
 * so attribute printing starts at column 37 (36 whitespaces)
 *
 *  25 spaces for the execname seems reasonable since currently
 *  less than 3% of the shipped exec_attr would overflow this
 */
#define	ATTR_COL	37

static void
print_profs_long(FILE *fp, execattr_t *exec)
{
	char	*curprofile;
	int	len;
	kv_t	*kv_pair;
	char	*key;
	char	*val;
	int	i;

	for (curprofile = ""; exec != NULL; exec = exec->next) {

		/* print profile name if it is a new one */
		if (strcmp(curprofile, exec->name) != 0) {
			curprofile = exec->name;
		}
		len = fprintf(fp, "          %s ", exec->id);

		if ((exec->attr == NULL || exec->attr->data == NULL)) {
			(void) fprintf(fp, "\n");
			continue;
		}

		/*
		 * if printing the name of the executable got us past the
		 * ATTR_COLth column, skip to ATTR_COL on a new line to
		 * print the attribues.
		 * else, just skip to ATTR_COL column.
		 */
		if (len >= ATTR_COL)
			(void) fprintf(fp, "\n%*s", ATTR_COL, " ");
		else
			(void) fprintf(fp, "%*s", ATTR_COL-len, " ");
		len = ATTR_COL;

		/* print all attributes of this profile */
		kv_pair = exec->attr->data;
		for (i = 0; i < exec->attr->length; i++) {
			key = kv_pair[i].key;
			val = kv_pair[i].value;
			if (key == NULL || val == NULL)
				continue;
			/* align subsequent attributes on the same column */
			if (i != 0)
				(void) fprintf(fp, "%*s", len, " ");
			(void) fprintf(fp, "%s=%s\n", key, val);
		}

	}
}

static void
print_profile_help_kv(FILE *fp, kva_t *attr, char *key)
{
	char *value;

	if (attr) {
		value = kva_match(attr, key);
		if (value)
			(void) fprintf(fp, "\t%s=%s\n", key, value);
	}
}

static execattr_t *
find_execattr(execattr_t **current, char *id)
{
	execattr_t **prev, *next;

	if (*current == NULL) {
		return (NULL);
	}
	prev = current;
	next = *prev;
	while (next) {
		if (strncmp(next->id, id, MAXPATHLEN) == 0) {
			return (next);
		}
		prev = &next->next;
		next = *prev;
	}
	return (NULL);
}

static boolean_t
add_execattr(execattr_t **current, execattr_t *new)
{
	execattr_t **prev, *next;
	boolean_t is_wildcard = B_FALSE;
	size_t	new_len, next_len;

	/*
	 * If we're adding an entry to cur_commands
	 * make sure it isn't also in the list of
	 * commands to delete.
	 */
	if (current == &cur_commands)
		(void) remove_execattr(&del_commands, new->id);

	if (*current == NULL) {
		*current = new;
		new->next = NULL;
		return (B_TRUE);
	}
	new_len = strlen(new->id);
	if (new->id[new_len - 1] == '*') {
		is_wildcard = B_TRUE;
	}

	prev = current;
	next = *prev;
	while (next) {
		if (strncmp(next->id, new->id, MAXPATHLEN) == 0) {
			/*
			 * Command is already in the profile
			 */
			return (B_FALSE);
		}
		/*
		 * Put willdcards after full names
		 * and least restrictive ones last
		 */
		next_len = strlen(next->id);
		if (next->id[next_len - 1] == '*') {
			if (!is_wildcard) {
				new->next = next;
				*prev = new;
				return (B_TRUE);
			} else if (new_len > next_len) {
				new->next = next;
				*prev = new;
				return (B_TRUE);
			}
		}
		prev = &next->next;
		next = *prev;
	}
	new->next = NULL;
	*prev = new;
	return (B_TRUE);
}

static boolean_t
remove_execattr(execattr_t **current, char *id)
{
	execattr_t **prev, *next, *new;
	boolean_t move2del = B_FALSE;

	prev = current;
	next = *prev;

	/*
	 * If an existing profile is being modified
	 * then we need to retain this exec_attr entry
	 * so that it can be explicitly deleted later.
	 */

	if ((current == &cur_commands) && (cur_profile->res1 != NULL) &&
	    ((strcmp(cur_profile->res1, "MOD") == 0) ||
	    (strcmp(cur_profile->res1, "RO") == 0)))
		move2del = B_TRUE;
	while (next) {
		if (strncmp(next->id, id, MAXPATHLEN) == 0) {
			new = next;
			*prev = next->next;
			next = *prev;
			new->next = NULL;

			if (move2del) {
				new->res1 = strdup_check("DEL");
				(void) add_execattr(&del_commands, new);
			} else {
				free_execattr(new);
			}
			return (B_TRUE);
		}
		prev = &next->next;
		next = *prev;
	}
	return (B_FALSE);
}

static  int
putexecattrent(execattr_t *exec)
{
	int flag;

	if ((exec->res1 == NULL) ||
	    (exec->res1[0] == '\0') ||
	    (strcmp(exec->res1, "RO") == 0))
		return (EXIT_OK);
	if (strcmp(exec->res1, "ADD") == 0)
		flag = ADD_MASK;
	else if (strcmp(exec->res1, "MOD") == 0)
		flag = MOD_MASK;
	else if (strcmp(exec->res1, "DEL") == 0)
		flag = DEL_MASK;

	if (mod_rep->rops->put_execattr(exec, flag) != Z_OK) {
		zerr("commit failed");
		saw_error = B_TRUE;
		return (Z_ERR);
	}
	free(exec->res1);
	exec->res1 = NULL;
	return (EXIT_OK);
}

static int
putprofattrent(profattr_t *prof)
{
	int flag;
	execattr_t *next;

	if ((need_to_commit) &&
	    (need_to_commit != pt_to_mask(PT_COMMAND))) {
		if (prof->res1 != NULL && strcmp(prof->res1, "ADD") == 0)
			flag = ADD_MASK;
		else if (prof->res1 != NULL && strcmp(prof->res1, "MOD") == 0)
			flag = MOD_MASK;
		else if (prof->res1 != NULL && strcmp(prof->res1, "DEL") == 0)
			flag = DEL_MASK;
		if (mod_rep->rops->put_profattr(prof, flag) != Z_OK) {
			zerr("commit failed");
			saw_error = B_TRUE;
			return (Z_ERR);
		}
	}

	if (need_to_commit & pt_to_mask(PT_COMMAND)) {
		next = cur_commands;
		while (next) {
			if (putexecattrent(next) == Z_ERR)
				return (Z_ERR);
			next = next->next;
		}

		next = del_commands;
		while (next) {
			if (putexecattrent(next) == Z_ERR)
				return (Z_ERR);
			next = next->next;
		}
		free_execattr(del_commands);
		del_commands = NULL;
	}
	return (EXIT_OK);
}

char *
delete_value(char *value, char *tokens)
{
	char *tmp, *cp, *lasts;
	boolean_t first = B_TRUE;

	tmp = strdup_check(tokens);
	tokens[0] = '\0';
	for (cp = strtok_r(tmp, ",", &lasts); cp != NULL;
	    cp = strtok_r(NULL, ",", &lasts)) {
		if (strcmp(cp, value) != 0) {
			if (first)
				first = B_FALSE;
			else
				strcat(tokens, ",");
			strcat(tokens, cp);
		}
	}
	free(tmp);
	return (tokens);
}

boolean_t
value_exists(char *value, char *tokens)
{
	char *tmp, *cp, *lasts;

	tmp = strdup_check(tokens);
	for (cp = strtok_r(tmp, ",", &lasts); cp != NULL;
	    cp = strtok_r(NULL, ",", &lasts)) {
		if (strcmp(cp, value) == 0) {
			free(tmp);
			return (B_TRUE);
		}
	}
	free(tmp);
	return (B_FALSE);
}

int
value_append(char *value, kv_t *data)
{
	char *tmp;
	size_t size;
	boolean_t first = B_TRUE;

	size = strlen(data->value);
	if (size != 0)
		first = B_FALSE;
	size += strlen(value) + 2;
	tmp = malloc(size);
	if (tmp == NULL)
		exit(1);
	strcpy(tmp, data->value);
	if (!first)
		strcat(tmp, ",");
	strcat(tmp, value);
	free(data->value);
	data->value = tmp;
	return (0);
}

int
key_value_append(char *key, char *value, kva_t **kva_p)
{
	int	i = 0;
	int	size;
	kva_t	*kva = *kva_p;
	kva_t	*new_kva;
	kv_t	*old_data;
	kv_t	*new_data;

	if (kva != NULL) {
		size = kva->length + 1;
	} else {
		size = 1;
	}
	if ((new_kva = _new_kva(size)) == NULL)
		exit(1);
	new_data = new_kva->data;
	if (size > 1) {
		old_data = kva->data;
		for (i = 0; i < kva->length; i++) {
			new_data[i].key = old_data[i].key;
			new_data[i].value = old_data[i].value;
		}
	}
	new_kva->length = size;
	new_data[i].key = strdup_check(key);
	new_data[i].value = strdup_check(value);
	if (kva_p != NULL && kva != NULL && kva->data != NULL) {
		free(kva->data);
		free(kva);
	}
	*kva_p = new_kva;
	return (0);
}

int
clear2kva(kva_t **kva_p, char *key)
{
	int	i;
	kv_t	*data;
	kva_t	*kva = *kva_p;

	if (kva == NULL) {
		return (0);
	}
	data = kva->data;
	for (i = 0; i < kva->length; i++) {
		if (data[i].key != NULL &&
		    strcmp(data[i].key, key) == 0) {
			if (data[i].value != NULL)
				free(data[i].value);
			data[i].value = NULL;
			return (0);
		}
	}
	return (0);
}

int
remove2kva(kva_t **kva_p, char *key, char *value)
{
	int	i;
	kv_t	*data;
	kva_t	*kva = *kva_p;

	if (kva == NULL) {
		return (0);
	}
	data = kva->data;
	for (i = 0; i < kva->length; i++) {
		if (data[i].key != NULL &&
		    strcmp(data[i].key, key) == 0) {
			if (data[i].value != NULL) {
				data[i].value =
				    delete_value(value, data[i].value);
				return (0);
			}
		}
	}
	return (0);
}

int
add2kva(kva_t **kva_p, char *key, char *value)
{
	int	i;
	kv_t	*data;
	kva_t	*kva = *kva_p;

	if (kva != NULL) {
		data = kva->data;
		for (i = 0; i < kva->length; i++) {
			if (data[i].key != NULL &&
			    strcmp(data[i].key, key) == 0) {
				if (data[i].value != NULL) {
					/* check if it exists */
					if (value_exists(value,
					    data[i].value)) {
						zerr(gettext("value '%s' is"
						    " already "
						    "specified in '%s'"),
						    value, key);
						saw_error = B_TRUE;
						return (0);
					} else {
						/*
						 * TODO:
						 * check if it is Stop or All
						 */
						value_append(value, &data[i]);
						return (0);
					}
				} else {
					data[i].value = _strdup_null(value);
					return (0);
				}
			}
		}
	}
	key_value_append(key, value, &kva);
	*kva_p = kva;
	return (0);
}

int
set2kva(kva_t **kva_p, char *key, char *value)
{
	/*
	 * First clear the ke-value setting if it exists
	 * then call the insert function
	 *
	 */
	clear2kva(kva_p, key);
	add2kva(kva_p, key, value);
	return (0);
}

static boolean_t
prop2kva(kva_t **kva_p, int prop_type, property_value_ptr_t pp,
    int val_type, int cmd)
{
	char *prop_id;
	char *key = pt_to_str(prop_type);

	if (pp->pv_type != PROP_VAL_SIMPLE &&
	    pp->pv_type != val_type) {
		zerr(gettext("A %s value was expected here."),
		    pvt_to_str(PROP_VAL_SIMPLE));
		saw_error = B_TRUE;
		return (B_FALSE);
	}
	if (pp->pv_type == PROP_VAL_SIMPLE) {
		if (pp->pv_simple == NULL) {
			long_usage(cmd, B_TRUE);
			return (B_FALSE);
		}
		prop_id = pp->pv_simple;

		if (verify(prop_type, cmd, prop_id)) {
			switch (cmd) {
			case CMD_ADD:
				add2kva(kva_p, key, prop_id);
				break;
			case CMD_SET:
				set2kva(kva_p, key, prop_id);
				break;
			case CMD_REMOVE:
				remove2kva(kva_p, key, prop_id);
				break;
			}
		} else {
			zerr("Cannot %s %s", cmd_to_str(cmd), prop_id);
			saw_error = B_TRUE;
			return (B_FALSE);
		}
	} else {
		list_property_ptr_t list;

		for (list = pp->pv_list; list != NULL;
		    list = list->lp_next) {
			prop_id = list->lp_simple;
			if (prop_id == NULL)
				break;
			if (verify(prop_type, cmd, prop_id)) {
				switch (cmd) {
				case CMD_ADD:
					add2kva(kva_p, key, prop_id);
					break;
				case CMD_SET:
					set2kva(kva_p, key, prop_id);
					cmd = CMD_ADD;
					break;
				case CMD_REMOVE:
					remove2kva(kva_p, key, prop_id);
					break;
				}
			} else {
				zerr("Cannot %s %s", cmd_to_str(cmd), prop_id);
				saw_error = B_TRUE;
				return (B_FALSE);
			}
		}
	}
	return (B_TRUE);
}

static void
separate_audit_flags()
{
	char *value;
	char *always = NULL;
	char *never = NULL;
	char *sep;
	size_t offset;

	value = (char *)_unescape(kva_match(cur_profile->attr, AUDIT_FLAGS),
	    KV_SPECIAL);
	if (value) {
		never = strchr(value, ':');
		if ((sep = strchr(value, ':')) == NULL) {
			/*
			 * Nothing to never_audit
			 */
			offset = strlen(value);
		} else {
			offset = sep - value;
		}

		always = strdup_check(value);
		always[offset] = '\0';
		add2kva(&cur_profile->attr, ALWAYSAUDIT, always);
		never = strdup_check(value);
		(void) strcpy(never, value + offset + 1);
		add2kva(&cur_profile->attr, NEVERAUDIT, never);
	}
}

static void
combine_audit_flags()
{
	char *flags;
	char *always = NULL;
	char *never = NULL;
	size_t length = 0;
	boolean_t got_always = B_FALSE;
	boolean_t got_never = B_FALSE;

	always = (char *)_escape(kva_match(cur_profile->attr, ALWAYSAUDIT),
	    KV_SPECIAL);
	if (always)
		got_always = B_TRUE;
	else
		always = "no";
	length = strlen(always);
	never = kva_match(cur_profile->attr, NEVERAUDIT);
	if (never)
		got_never = B_TRUE;
	else
		never = "no";
	length += strlen(never) + 3;
	if (got_always || got_never) {
		flags = malloc(length);
		if (flags == NULL) {
			zerr(gettext("Out of memory"));
			exit(Z_ERR);
		}
		(void) snprintf(flags, length, "%s\\:%s", always, never);
		set2kva(&cur_profile->attr, AUDIT_FLAGS, flags);
	} else {
		clear2kva(&cur_profile->attr, AUDIT_FLAGS);
	}
	clear2kva(&cur_profile->attr, ALWAYSAUDIT);
	clear2kva(&cur_profile->attr, NEVERAUDIT);
}

static boolean_t
initialize()
{
	nss_XbyY_buf_t *profbuf = NULL;


	if (!chkauthattr(PROFILE_MANAGE_AUTH, username)) {
		read_only_mode = B_TRUE;
	}

	if (!chkauthattr(PROFILE_CMD_MANAGE_AUTH, username)) {
		read_only_mode = B_TRUE;
	}
	init_nss_buffer(SEC_REP_DB_PROFATTR, &profbuf);
	mod_rep->rops->get_profnam(profname, &cur_profile, profbuf);
	if (cur_profile != NULL) {
		nss_XbyY_buf_t *exbuf = NULL;
		execattr_t *cmd;

		if ((cur_profile->res1 != NULL) &&
		    (strcmp(cur_profile->res1, "RO") == 0)) {
			read_only_mode = B_TRUE;
		} else if (chkauthattr(PROFILE_ASSIGN_AUTH, username) ||
		    check_has_profile(cur_profile->name)) {
			cur_profile->res1 = strdup_check("MOD");
		} else {
			read_only_mode = B_TRUE;
			cur_profile->res1 = strdup_check("RO");
		}

		init_nss_buffer(SEC_REP_DB_EXECATTR, &exbuf);
		cur_commands = lk_rep->rops->get_execprof(profname,
		    KV_COMMAND, NULL, GET_ALL|__SEARCH_ALL_POLS, exbuf);
		for (cmd = cur_commands; cmd != NULL; cmd = cmd->next) {
			if (cmd->res1 != NULL && strcmp(cmd->res1, "RO") != 0)
				cmd->res1 = NULL;
			if (read_only_mode)
				cmd->res1 = strdup_check("RO");
		}
		separate_audit_flags();
		return (B_TRUE);
	} else {
		free_nss_buffer(&profbuf);
		if ((cur_profile =
		    (profattr_t *)malloc(sizeof (profattr_t))) == NULL) {
			zerr(gettext("Out of memory"));
			exit(Z_ERR);
		}
		cur_profile->name = profname;
		cur_profile->res1 = strdup_check("ADD");
		cur_profile->res2 = NULL;
		cur_profile->desc = NULL;
		cur_profile->attr = _new_kva(8);
		cur_profile->attr->length = 8;
		cur_profile->attr->data[0].key =
		    strdup_check(PROFATTR_AUTHS_KW);
		cur_profile->attr->data[1].key =
		    strdup_check(PROFATTR_PROFS_KW);
		cur_profile->attr->data[2].key =
		    strdup_check(PROFATTR_PRIVS_KW);
		cur_profile->attr->data[3].key =
		    strdup_check(USERATTR_LIMPRIV_KW);
		cur_profile->attr->data[4].key =
		    strdup_check(USERATTR_DFLTPRIV_KW);
		cur_profile->attr->data[5].key =
		    strdup_check(AUTHATTR_HELP_KW);
		cur_profile->attr->data[6].key =
		    strdup_check(ALWAYSAUDIT);
		cur_profile->attr->data[7].key =
		    strdup_check(NEVERAUDIT);

		need_to_commit = pt_to_mask(PT_DESCRIPTION);
		return (B_TRUE);
	}
}


static void
export_prop(FILE *of, int prop_num, char *prop_id)
{
	char *quote_str;

	if (prop_id == NULL || strlen(prop_id) == 0)
		return;
	quote_str = quoteit(prop_id);
	(void) fprintf(of, "%s %s=%s\n", cmd_to_str(CMD_SET),
	    pt_to_str(prop_num), quote_str);
	free(quote_str);
}

static void
export_prop_kv(FILE *of, int prop_num, kva_t *attr)
{
	char *value;

	if ((value = kva_match(attr, pt_to_str(prop_num))) != NULL)
		export_prop(of, prop_num, value);
}
void
export_func(cmd_t *cmd)
{
	int arg;
	char outfile[MAXPATHLEN];
	FILE *of;
	boolean_t need_to_close = B_FALSE;
	boolean_t arg_err = B_FALSE;
	int i;
	execattr_t *next;

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

	export_prop(of, PT_PROFNAME, cur_profile->name);
	export_prop(of, PT_DESCRIPTION, cur_profile->desc);
	for (i = 2; prof_helptab[i].prop_num != PT_COMMAND; i++) {
		export_prop_kv(of, prof_helptab[i].prop_num, cur_profile->attr);
	}
	next = cur_commands;
	while (next) {
		(void) fprintf(of, "%s %s\n", cmd_to_str(CMD_ADD),
		    pt_to_str(PT_COMMAND));
		export_prop(of, PT_PATHNAME, next->id);
		for (i = 1; cmd_helptab[i].prop_num != 0; i++) {
			export_prop_kv(of, cmd_helptab[i].prop_num, next->attr);
		}
		(void) fprintf(of, "%s\n", cmd_to_str(CMD_END));
		next = next->next;
	}
	done:
		if (need_to_close) {
			(void) fchown(fileno(of), getuid(), getgid());
			(void) fclose(of);
		}
}

void
exit_func(cmd_t *cmd)
{
	int arg, answer;
	boolean_t force_exit = B_FALSE;
	boolean_t arg_err = B_FALSE;
	int commit = 0;

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
	if (resource_scope) {
		scope_usage(CMD_EXIT);
		return;
	}

	if (need_to_commit) {
		commit = 1;
		commit_func(cmd);
		if (!profile_verified && !force_exit) {
			answer = ask_yesno(B_FALSE, gettext(
			    "Profile verification failed; really quit"));
			if (answer == -1) {
				zerr(gettext("Profile verification failed, "
				    "input not from terminal and -F not "
				    "specified:\n%s command "
				    "ignored, but exiting anyway."),
				    cmd_to_str(CMD_EXIT));
				exit(Z_ERR);
			} else if (answer == 0) {
				return;
			}
		}
	}
	if (need_to_commit && commit)
		exit(EXIT_FATAL);
	else if (!need_to_commit && commit)
		exit(EXIT_OK);
	else if (saw_error)
		exit(EXIT_NON_FATAL);
	else
		exit(EXIT_OK);
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

static boolean_t
add_resource(cmd_t *cmd)
{
	char *prop_id = NULL;
	property_value_ptr_t pp;
	nss_XbyY_buf_t *buf = NULL;
	execattr_t *exec;
	/*
	 * The id (pathname) may be specified as a command value
	 */
	if (cmd->cmd_prop_nv_pairs == 1) {
		pp = cmd->cmd_property_ptr[0];
		if (((prop_id = pp->pv_simple) == NULL)) {
			zerr(gettext("A single pathname is required."));
			saw_error = B_TRUE;
			return (B_FALSE);
		}
	}
	if (!verify_pathname(profname, PT_PATHNAME, prop_id)) {
		zerr("Cannot %s command", cmd_to_str(CMD_ADD));
		saw_error = B_TRUE;
		return (B_FALSE);
	}

	if ((cur_command = (execattr_t *)malloc(sizeof (execattr_t)))
	    == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}
	cur_command->res1 = NULL;
	cur_command->res1 = strdup_check("ADD");
	if (prop_id) {
		init_nss_buffer(SEC_REP_DB_EXECATTR, &buf);
		if ((exec = mod_rep->rops->get_execprof(profname, KV_COMMAND,
		    prop_id, GET_ONE, buf)) != NULL) {
			fprintf(stderr, "exec->id:%s\n", exec->id);
			cur_command->res1 = strdup_check("MOD");
			free_execattr(exec);
		}
		free_nss_buffer(&buf);
	}
	cur_command->name = strdup_check(profname);
	if (prop_id) {
		cur_command->id = strdup_check(prop_id);
		cur_cmdname = get_execbasename(cur_command->id);
	} else {
		cur_cmdname = strdup_check(KV_COMMAND);
	}
	cur_command->type = strdup_check(KV_COMMAND);
	cur_command->policy = strdup_check(DEFAULT_POLICY);
	cur_command->next = NULL;
	cur_command->res2 = NULL;
	cur_command->attr = _new_kva(6);
	cur_command->attr->length = 6;
	cur_command->attr->data[0].key = strdup_check(EXECATTR_EUID_KW);
	cur_command->attr->data[1].key = strdup_check(EXECATTR_EGID_KW);
	cur_command->attr->data[2].key = strdup_check(EXECATTR_UID_KW);
	cur_command->attr->data[3].key = strdup_check(EXECATTR_GID_KW);
	cur_command->attr->data[4].key = strdup_check(EXECATTR_LPRIV_KW);
	cur_command->attr->data[5].key = strdup_check(EXECATTR_IPRIV_KW);
	return (B_TRUE);
}

static void
add_property(cmd_t *cmd)
{
	int res_type, prop_type;
	property_value_ptr_t pp;

	res_type = resource_scope;
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
	if (res_type == RT_COMMAND) {
		if (command_is_read_only(CMD_ADD)) {
			zerr("Value not added");
			saw_error = B_TRUE;
			return;
		}
		switch (prop_type) {
		case PT_PRIVS:
		case PT_LIMITPRIVS:
			if (prop2kva(&cur_command->attr,
			    prop_type, pp, PROP_VAL_LIST, CMD_ADD))
				need_to_commit |= pt_to_mask(PT_COMMAND);
			break;
		default:
			zerr(gettext("Cannot 'add' to '%s' property. "\
			    "Use 'set'instead."),
			    pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	} else {
		if (profile_is_read_only(CMD_ADD)) {
			saw_error = B_TRUE;
			return;
		}
		switch (prop_type) {
		case PT_AUTHORIZATION:
		case PT_SUBPROFILE:
		case PT_PRIVS:
		case PT_LIMPRIV:
		case PT_DFLTPRIV:
		case PT_ALWAYSAUDIT:
		case PT_NEVERAUDIT:
			if (prop2kva(&cur_profile->attr,
			    prop_type, pp, PROP_VAL_LIST, CMD_ADD))
			need_to_commit |= pt_to_mask(prop_type);
			return;
		default:
			zerr(gettext("Cannot 'add' to '%s' property. "\
			    "Use 'set'instead."),
			    pt_to_str(prop_type));
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

	if (profile_is_read_only(CMD_ADD)) {
		saw_error = B_TRUE;
		return;
	}


	if (profile_scope) {
		if (cmd->cmd_res_type == RT_COMMAND) {
			if (add_resource(cmd)) {
				need_to_commit |= pt_to_mask(PT_COMMAND);
				resource_scope = cmd->cmd_res_type;
				profile_scope = B_FALSE;
				end_op = CMD_ADD;
			}
			return;
		} else {
			add_property(cmd);
		}
	} else
		add_property(cmd);
}

void
delete_func(cmd_t *cmd)
{
/*
 * Need to determine if the profile is a subprofile
 * of any other profile in this repository. If so, issue
 * a warning, and check if the user is authorized to modify
 * them. Don't delete it unless the user is authorized to
 * modify all the affected profiles.
 */
	profattr_t *nextprof;
	execattr_t *next;
	boolean_t can_delete = B_TRUE;
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
	if (resource_scope) {
		scope_usage(CMD_DELETE);
		return;
	}

	if (profile_is_read_only(CMD_DELETE)) {
		saw_error = B_TRUE;
		return;
	}
	setprofattr();
	while (nextprof = getprofattr()) {
		char *subprofs;

		if (subprofs = kva_match(nextprof->attr, PROFATTR_PROFS_KW)) {
			char *subprofname;

			subprofname = strtok(subprofs, KV_SEPSTR);
			while (subprofname != NULL) {
				if (strcmp(profname,  subprofname) ==  0) {
					fprintf(stderr, "'%s' is included in "
					    "'%s'\nIt must be removed from "
					    "that profile before it can be "
					    "deleted\n",
					    profname, nextprof->name);
					can_delete = B_FALSE;
				}
				subprofname = strtok(NULL, KV_SEPSTR);
			}
		}
	}
	if (!can_delete) {
		zerr("Profile not deleted");
		saw_error = B_TRUE;
		return;
	}

	if (!force_delete) {
		if ((answer = ask_yesno(B_FALSE, gettext("Are you "
		    "sure you want to delete the profile"))) == -1) {
			zerr(gettext("Input not from terminal and -F not "
			    "specified:\n%s command ignored, exiting."),
			    cmd_to_str(CMD_DELETE));
			exit(Z_ERR);
		}
		if (answer != 1)
			return;
	}
	if (cur_profile->res1 != NULL &&
	    strcmp(cur_profile->res1, "ADD") == 0) {
		printf("Profile was never added\n");
		exit(1);
	}
	/*
	 * Revert to last saved copy so that we can delete
	 * the correct exec_attr entries.
	 */

	free_profattr(cur_profile);
	if (!initialize())
		exit(Z_ERR);
/*
 * First delete all the commands
 * Then the prof_attr entry;
 */
	next = cur_commands;
	while (next != NULL) {
		char *prop_id;

		if (next->res1 != NULL &&
		    strcmp(next->res1, "RO") == 0) {
			fprintf(stderr, gettext("%s is read-only. Not "
			    "removed\n"), next->id);
			next = next->next;
		} else {
			prop_id = next->id;
			next = next->next;
			(void) remove_execattr(&cur_commands,
			    prop_id);
			need_to_commit |= pt_to_mask(PT_COMMAND);
		}
	}
	cur_profile->res1 = strdup_check("DEL");
	need_to_commit |= pt_to_mask(PT_PROFNAME);
	if (putprofattrent(cur_profile) == EXIT_OK) {
		printf("profile was deleted\n");
		exit(0);
	} else
		exit(1);
}

static boolean_t
prompt_remove_resource(cmd_t *cmd)
{
	int answer;
	int arg;
	boolean_t force = B_FALSE;
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

	if (!force) {
		if (!interactive_mode) {
			zerr(gettext("There are multiple commands in this "
			    "profile.  Either specify the pathname to\n"
			    "remove a single command or use the -F option to "
			    "remove all commands."));
			saw_error = B_TRUE;
			return (B_FALSE);
		}
		answer = ask_yesno(B_FALSE, gettext(
		    "Are you sure you want to remove ALL commands"));
		if (answer == -1) {
			return (B_FALSE);
		}
		if (answer != 1)
			return (B_FALSE);
	}
	return (B_TRUE);
}

static void
remove_resource(cmd_t *cmd)
{
	char *prop_id;
	property_value_ptr_t pp;
	execattr_t *next;

	if (cmd->cmd_prop_nv_pairs == 1) {
		pp = cmd->cmd_property_ptr[0];
		if (((prop_id = pp->pv_simple) == NULL)) {
			zerr(gettext("The command name is not valid."));
			saw_error = B_TRUE;
			return;
		}
		next = find_execattr(&cur_commands, prop_id);
		if ((next != NULL) && (next->res1 != NULL) &&
		    (strcmp(next->res1, "RO") == 0)) {
			zerr(gettext("%s is read-only. Not "
			    "removed"), prop_id);
			saw_error = B_TRUE;
			return;
		}
		if (!remove_execattr(&cur_commands, prop_id)) {
			zerr(gettext("The command name does not match an "
			    "existing entry."));
			saw_error = B_TRUE;
			return;
		} else {
			need_to_commit |= pt_to_mask(PT_COMMAND);
		}
	} else {
		if (cur_commands != NULL && cur_commands->next != NULL) {
			if (!prompt_remove_resource(cmd)) {
				zerr(gettext("The commands were not removed."));
				saw_error = B_TRUE;
				return;
			}
		}
		next = cur_commands;
		while (next != NULL) {
			if (next->res1 != NULL &&
			    strcmp(next->res1, "RO") == 0) {
				fprintf(stderr, gettext("%s is read-only. Not "
				    "removed\n"), next->id);
				next = next->next;
			} else {
				prop_id = next->id;
				next = next->next;
				(void) remove_execattr(&cur_commands,
				    prop_id);
			}
		}
		need_to_commit |= pt_to_mask(PT_COMMAND);
	}
}

static void
remove_property(cmd_t *cmd)
{
	int res_type, prop_type;
	property_value_ptr_t pp;

	res_type = resource_scope;
	prop_type = cmd->cmd_prop_name[0];

	if (cmd->cmd_prop_nv_pairs != 1) {
		long_usage(CMD_REMOVE, B_TRUE);
		return;
	}

	pp = cmd->cmd_property_ptr[0];

	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_REMOVE, B_TRUE);
		return;
	}
	if (res_type == RT_COMMAND) {
		if (command_is_read_only(CMD_REMOVE)) {
			return;
		}
		switch (prop_type) {
		case PT_PRIVS:
		case PT_LIMITPRIVS:
			if (prop2kva(&cur_command->attr,
			    prop_type, pp, PROP_VAL_LIST,
			    CMD_REMOVE))
				need_to_commit |= pt_to_mask(PT_COMMAND);
			break;
		default:
			zerr(gettext("Cannot 'remove' from '%s' property. "
			    "Use 'set'instead."),
			    pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	} else {
		if (profile_is_read_only(CMD_REMOVE)) {
			return;
		}
		switch (prop_type) {
		case PT_AUTHORIZATION:
		case PT_SUBPROFILE:
		case PT_PRIVS:
		case PT_LIMPRIV:
		case PT_DFLTPRIV:
		case PT_ALWAYSAUDIT:
		case PT_NEVERAUDIT:
			if (prop2kva(&cur_profile->attr,
			    prop_type, pp, PROP_VAL_LIST,
			    CMD_REMOVE))
				need_to_commit |= pt_to_mask(prop_type);
			return;
		default:
			zerr(gettext("Cannot 'remove' from '%s' property. "
			    "Use 'set'instead."),
			    pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
	}
}

void
remove_func(cmd_t *cmd)
{
	assert(cmd != NULL);

	if (profile_scope) {
		if (cmd->cmd_res_type == RT_COMMAND)
			remove_resource(cmd);
		else
			remove_property(cmd);
	} else {
		if (cmd->cmd_res_type != RT_COMMAND)
			remove_property(cmd);
		else
			scope_usage(CMD_REMOVE);
	}
}

void
clear_func(cmd_t *cmd)
{
	int res_type, prop_type;

	assert(cmd != NULL);

	res_type = resource_scope;
	prop_type = cmd->cmd_res_type;
	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_CLEAR, B_TRUE);
		return;
	}

	if (!verify(prop_type, CMD_CLEAR, NULL)) {
		zerr("Cannot %s %s", cmd_to_str(CMD_CLEAR),
		    pt_to_str(prop_type));
		saw_error = B_TRUE;
		return;
	}
	if (res_type == RT_COMMAND) {
		switch (prop_type) {
		case PT_EUID:
		case PT_UID:
		case PT_EGID:
		case PT_GID:
		case PT_PRIVS:
		case PT_LIMITPRIVS:
			clear2kva(&cur_command->attr, pt_to_str(prop_type));
			need_to_commit |= pt_to_mask(PT_COMMAND);
			return;
		default:
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, CMD_CLEAR, HELP_PROPS);
			return;
		}
	} else {
		switch (prop_type) {
		case PT_AUTHORIZATION:
		case PT_PRIVS:
		case PT_LIMPRIV:
		case PT_DFLTPRIV:
		case PT_ALWAYSAUDIT:
		case PT_NEVERAUDIT:
		case PT_SUBPROFILE:
		case PT_HELPFILE:
			clear2kva(&cur_profile->attr, pt_to_str(prop_type));
			need_to_commit |= pt_to_mask(prop_type);
			return;
		default:
			long_usage(CMD_SET, B_TRUE);
			usage(B_FALSE, CMD_CLEAR, HELP_PROPS);
			return;
		}
	}
}

void
select_func(cmd_t *cmd)
{
	char *prop_id;
	property_value_ptr_t pp;

	assert(cmd != NULL);

	if (resource_scope) {
		scope_usage(CMD_SELECT);
		return;
	}

	if (cmd->cmd_res_type == RT_PROFATTR) {
		long_usage(CMD_SELECT, B_TRUE);
		return;
	}

	if (cmd->cmd_res_type != RT_COMMAND) {
		long_usage(CMD_SELECT, B_TRUE);
		usage(B_FALSE, CMD_SELECT, HELP_RESOURCES);
		return;
	}

	if (cmd->cmd_prop_nv_pairs == 1) {
		pp = cmd->cmd_property_ptr[0];
		if (((prop_id = pp->pv_simple) == NULL)) {
			zerr(gettext("The command name is not valid."));
			saw_error = B_TRUE;
			return;
		} else {
			mod_command = find_execattr(&cur_commands, prop_id);
			if (mod_command == NULL) {
				zerr(gettext("The command name does not match "
				    "an existing entry."));
				saw_error = B_TRUE;
				return;
			}

			cur_command = mod_command;
			if (!command_is_read_only(CMD_SELECT)) {
				if ((cur_command =
				    (execattr_t *)malloc(sizeof (execattr_t)))
				    == NULL) {
					zerr(gettext("Out of memory"));
					exit(Z_ERR);
				}
				cur_command->name = strdup_check(profname);
				cur_command->id = strdup_check(prop_id);
				cur_command->type = strdup_check(KV_COMMAND);
				cur_command->policy =
				    strdup_check(DEFAULT_POLICY);
				cur_command->next = NULL;
				if (mod_command->res1 != NULL)
					cur_command->res1 =
					    strdup_check(mod_command->res1);
				else
					cur_command->res1 = NULL;
				cur_command->res2 = NULL;
				cur_command->attr = _kva_dup(mod_command->attr);
			}
		}
	} else if (cur_commands != NULL &&
	    cur_commands->next == NULL) {
		cur_command = cur_commands;
	} else {
		zerr(gettext("A command name must be specified."));
		saw_error = B_TRUE;
		return;
	}

	cur_cmdname = get_execbasename(cur_command->id);
	resource_scope = cmd->cmd_res_type;
	profile_scope = B_FALSE;
	end_op = CMD_SELECT;
}

void
set_func(cmd_t *cmd)
{
	char *prop_id;
	int res_type, prop_type;
	property_value_ptr_t pp;
	execattr_t *next;

	assert(cmd != NULL);


	res_type = resource_scope;
	prop_type = cmd->cmd_prop_name[0];

	pp = cmd->cmd_property_ptr[0];
	prop_id = pp->pv_simple;

	if (prop_type == PT_UNKNOWN) {
		long_usage(CMD_SET, B_TRUE);
		return;
	}

	if (res_type == RT_COMMAND) {
		switch (prop_type) {
		case PT_PATHNAME:
			if (verify(prop_type, CMD_SET, prop_id)) {
				cur_command->id = strdup_check(prop_id);
				cur_cmdname = get_execbasename(cur_command->id);
				need_to_commit |= pt_to_mask(PT_COMMAND);
			}
			break;
		case PT_EUID:
		case PT_UID:
		case PT_EGID:
		case PT_GID:
			if (prop2kva(&cur_command->attr,
			    prop_type, pp, PROP_VAL_SIMPLE, CMD_SET))
				need_to_commit |= pt_to_mask(PT_COMMAND);
			break;
		case PT_PRIVS:
		case PT_LIMITPRIVS:
			if (prop2kva(&cur_command->attr,
			    prop_type, pp, PROP_VAL_LIST, CMD_SET))
				need_to_commit |= pt_to_mask(PT_COMMAND);
			break;
		default:
			zerr(gettext("The '%s' property only applies to "
			    "profiles."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
		}
		if (need_to_commit) {
			if (cur_command->res1 == NULL)
				cur_command->res1 = strdup_check("MOD");
			else if (strcmp(cur_command->res1, "MOD"))
				cur_command->res1 = strdup_check("ADD");
		}
	} else if (prop_type == PT_PROFNAME) {
		nss_XbyY_buf_t *buf = NULL;
		profattr_t *prof;
		/*
		 * If new profile name is different from current name
		 * and if it already exists, don't allow it.
		 */
		if (prop_id == NULL || strlen(prop_id) == 0) {
			zerr(gettext("Profile name cannot be empty string."));
			saw_error = B_TRUE;
			return;
		}
		init_nss_buffer(SEC_REP_DB_PROFATTR, &buf);
		if (cur_profile->name != NULL &&
		    (strcmp(cur_profile->name, prop_id) != 0) &&
		    (mod_rep->rops->get_profnam(prop_id, &prof, buf) == 0)) {
			zerr(gettext("Cannot switch to an existing "
			    "profile."));
			saw_error = B_TRUE;
			free_profattr(prof);
			free_nss_buffer(&buf);
			return;
		}
		free_nss_buffer(&buf);
		if (!chkauthattr(PROFILE_ASSIGN_AUTH, username) &&
		    !check_has_profile(cur_profile->name)) {
			zerr(gettext("Can only rename your own profiles."));
			saw_error = B_TRUE;
			return;
		}
		next = cur_commands;
		if (profname)
			free(profname);
		profname = strdup_check(prop_id);
		cur_profile->name = profname;
		/*
		 * Remove read-only status if present
		 */
		read_only_mode = B_FALSE;
		if (cur_profile->res1 != NULL) {
			free(cur_profile->res1);
		}
		cur_profile->res1 = strdup_check("ADD");
		/*
		 * Replace the profile name in exec_attr enties
		 */
		while (next) {
			free(next->name);
			next->res1 = strdup_check("ADD");
			next->name = strdup_check(profname);
			next = next->next;
		}
		need_to_commit |= pt_to_mask(PT_PROFNAME);
		need_to_commit |= pt_to_mask(PT_COMMAND);
		return;
	} else if (profile_is_read_only(CMD_SET)) {
		return;
	} else switch (prop_type) {
		case PT_DESCRIPTION:
			if (verify(prop_type, CMD_SET, prop_id)) {
				if ((prop_id = pp->pv_simple) == NULL) {
					list_property_ptr_t list;
					char *desc;
					int size = 0;
					for (list = pp->pv_list; list != NULL;
					    list = list->lp_next) {
						prop_id = list->lp_simple;
						if (prop_id != NULL) {
							size +=
							    strlen(prop_id)+1;
						}
					}
					if ((desc = malloc(size +1)) == NULL) {
						zerr(gettext("Out of memory"));
						exit(Z_ERR);
					}
					desc[0] = '\0';
					for (list = pp->pv_list; list != NULL;
					    list = list->lp_next) {
						prop_id = list->lp_simple;
						if (prop_id != NULL) {
							strcat(desc, prop_id);
						}
						if (list->lp_next != NULL)
							strcat(desc, ",");
					}
					cur_profile->desc = desc;
				} else {
					cur_profile->desc =
					    strdup_check(pp->pv_simple);
				}
				need_to_commit |= pt_to_mask(PT_DESCRIPTION);
			}
			break;
		case PT_HELPFILE:
			if (prop2kva(&cur_profile->attr,
			    prop_type, pp, PROP_VAL_SIMPLE, CMD_SET))
				need_to_commit |= pt_to_mask(PT_HELPFILE);
			break;
		case PT_AUTHORIZATION:
		case PT_PRIVS:
		case PT_LIMPRIV:
		case PT_DFLTPRIV:
		case PT_SUBPROFILE:
		case PT_ALWAYSAUDIT:
		case PT_NEVERAUDIT:
			if (prop2kva(&cur_profile->attr,
			    prop_type, pp, PROP_VAL_LIST, CMD_SET))
				need_to_commit |= pt_to_mask(prop_type);
			break;
		default:
			zerr(gettext("The '%s' property only applies to "
			    "commands."), pt_to_str(prop_type));
			saw_error = B_TRUE;
			return;
	}
	if (need_to_commit && cur_profile->res1 == NULL)
		cur_profile->res1 = strdup_check("MOD");
}

void
info_func(cmd_t *cmd)
{
	FILE *fp = stdout;
	execattr_t *next;
	int i;
	int prop_type;

	assert(cmd != NULL);

	if (interactive_mode) {
		setbuf(fp, NULL);
	}
	if (cmd->cmd_argc == 0) {
		prop_type = cmd->cmd_prop_name[0];
		if (resource_scope) {
			if (prop_type) {
				switch (prop_type) {
				case PT_PATHNAME:
					fprintf(fp, "\tid=%s\n",
					    cur_command->id);
					return;
				case PT_PRIVS:
				case PT_LIMITPRIVS:
				case PT_EUID:
				case PT_UID:
				case PT_EGID:
				case PT_GID:
					print_profile_help_kv(fp,
					    cur_command->attr,
					    pt_to_str(prop_type));
					return;
				default:
					zerr(gettext("%s is not a valid "
					    "command property."),
					    rt_to_str(cmd->cmd_res_type));
					return;
				}
			} else {
				fprintf(fp, "\tid=%s\n", cur_command->id);
				for (i = 1; cmd_helptab[i].prop_num != 0; i++) {
					print_profile_help_kv(fp,
					    cur_command->attr,
					    pt_to_str(cmd_helptab[i].prop_num));
				}
				return;
			}
		} else {
			if (prop_type) {
				switch (prop_type) {
				case PT_PROFNAME:
					fprintf(fp, "\t%s=%s\n",
					    pt_to_str(prop_type),
					    cur_profile->name);
					break;
				case PT_DESCRIPTION:
					fprintf(fp, "\t%s=%s\n",
					    pt_to_str(prop_type),
					    cur_profile->desc);
					break;
				case PT_AUTHORIZATION:
				case PT_PRIVS:
				case PT_LIMPRIV:
				case PT_DFLTPRIV:
				case PT_SUBPROFILE:
				case PT_HELPFILE:
				case PT_ALWAYSAUDIT:
				case PT_NEVERAUDIT:
					print_profile_help_kv(fp,
					    cur_profile->attr,
					    pt_to_str(prop_type));
					break;
				case PT_COMMAND:
					next = cur_commands;
					while (next) {
						fprintf(fp,
						    "\tcmd=%s\n", next->id);
						next = next->next;
					}
					break;
				default:
					zerr(gettext("%s is not a valid"
					    " profile property."),
					    pt_to_str(prop_type));
					break;
				}
			} else {
				fprintf(fp, "\t%s=%s\n", pt_to_str(PT_PROFNAME),
				    cur_profile->name);
				fprintf(fp, "\t%s=%s\n",
				    pt_to_str(PT_DESCRIPTION),
				    cur_profile->desc);
				for (i = 2; prof_helptab[i].prop_num !=
				    PT_COMMAND; i++) {
					print_profile_help_kv(fp,
					    cur_profile->attr,
					    pt_to_str(
					    prof_helptab[i].prop_num));
				}
				next = cur_commands;
				while (next) {
					fprintf(fp, "\tcmd=%s\n", next->id);
					next = next->next;
				}
			}
		}
	}

}

static void
free_attrs(attrs_t *attrs)
{
	int i;

	for (i = 0; i < attrs->attr_cnt; i++)
		free(attrs->attrvals[i]);
}

static void
add_attrs(const char *attrname, char **attrArray, int *attrcnt)
{
	int i;
	for (i = 0; i < *attrcnt; i++) {
		if (attrArray[i] != NULL &&
		    strcmp(attrname, attrArray[i]) == 0) {
			return; /* already in list */
		}
	}

	/* not in list, add it in */
	attrArray[*attrcnt] = strdup_check((char *)attrname);
	*attrcnt += 1;
}

static int
get_attrs(const char *name, kva_t *kva, void *ctxt, void *pres)
{
	NOTE(ARGUNUSED(name))
	attrs_t *res = pres;
	char *val;

	if (pres != NULL && ctxt != NULL && res->attrvals != NULL) {
		val = kva_match(kva, (char *)ctxt);
		if (val != NULL) {
			char *vals;
			char *token, *lasts;

			vals = strdup_check(val);
			for (token = strtok_r(vals, ",", &lasts);
			    token != NULL;
			    token = strtok_r(NULL, ",", &lasts)) {
				add_attrs(token, res->attrvals, &res->attr_cnt);
			}

		}
	}
	return (0);
}

static int
find_priv_attrs(const char *name, kva_t *kva, void *ctxt, void *pres)
{
	NOTE(ARGUNUSED(name))
	priv_attr_t *attrs = pres;
	char *val;

	if (attrs != NULL && ctxt != NULL) {
		syslog(LOG_ERR, "ctxt = %s", ctxt);
		val = kva_match(kva, (char *)ctxt);
		if (val != NULL) {
			syslog(LOG_ERR, "val = %s", val);
			attrs->privs = strdup_check(val);
		} else {
			attrs->privs = NULL;
		}
	}
	return (attrs->privs != NULL);
}

/*
 * Verifies the provided list of authorizations are all valid.
 *
 * Returns NULL if all authorization names are valid.
 * Otherwise, returns the invalid authorization name
 *
 */
static boolean_t
verify_auths(char *name, int prop_num, char *auths)
{
	char *authname;
	authattr_t *result;
	char *tmp;
	boolean_t do_auth_checks = B_TRUE;
	boolean_t verified = B_TRUE;

	if (chkauthattr(AUTHORIZATION_ASSIGN_AUTH, username)) {
		do_auth_checks = B_FALSE;
	} else if (!chkauthattr(AUTHORIZATION_DELEGATE_AUTH, username)) {
		do_auth_checks = B_FALSE;
		if (auths)
			(void) fprintf(stderr, gettext("%s: %s: %s cannot "
			    "delegate '%s'\n"),
			    name, pt_to_str(prop_num), username, auths);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    AUTHORIZATION_DELEGATE_AUTH);
		verified = B_FALSE;
		return (verified);
	}
	if (auths == NULL)
		return (verified);

	tmp = strdup_check(auths);
	if (tmp == NULL) {
		zerr(gettext("Out of memory"));
		exit(Z_ERR);
	}

	authname = strtok(tmp, KV_SEPSTR);
	while (authname != NULL) {
		char *suffix;

		/* Remove named object after slash */
		if ((suffix = index(authname, KV_OBJECTCHAR)) != NULL)
			*suffix = '\0';

		/* Find the suffix */
		if ((suffix = rindex(authname, '.')) == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: %s: Invalid authorization "
			    "'%s'\n"), name, pt_to_str(prop_num), authname);
			verified = B_FALSE;
			return (verified);
		}

		/* Check for existence in auth_attr */
		if (suffix)
			suffix++;
		if (suffix && strcmp(suffix, KV_WILDCARD)) {
			/* Not a wildcard */
			result = getauthnam(authname);
			if (result == NULL) {
			/* can't find the auth */
				(void) fprintf(stderr,
				    gettext("%s: %s: Invalid "
				    "authorization '%s'\n"),
				    name, pt_to_str(prop_num), authname);
				verified = B_FALSE;
				return (verified);
			} else {
				free_authattr(result);
			}
		}
		if (do_auth_checks) {
			/* Check if user has been granted this authorization */
			if (!chkauthattr(authname, username)) {
				(void) fprintf(stderr, gettext("%s: %s: %s "
				    "cannot delegate '%s' requires %s"
				    " authorization\n"),
				    name, pt_to_str(prop_num),
				    username, authname, authname);
				verified = B_FALSE;
				return (verified);
			}
		}
		authname = strtok(NULL, KV_SEPSTR);
	}
	free(tmp);
	return (verified);
}

static boolean_t
check_has_profile(char *prof)
{
	boolean_t found = B_FALSE;
	int i;
	attrs_t my_profs;

	if (prof) {
		my_profs.attr_cnt = 0;
		_enum_attrs(username, get_attrs, USERATTR_PROFILES_KW,
		    &my_profs);
		for (i = 0; i < my_profs.attr_cnt; i++) {
			if (my_profs.attrvals[i] != NULL &&
			    strcmp(prof, my_profs.attrvals[i]) == 0) {
				found = B_TRUE;
				break;
			}
		}
		free_attrs(&my_profs);
	}
	return (found);
}

/*
 * Verifies the provided list of profile names are valid.
 *
 * Returns NULL if all profile names are valid.
 * Otherwise, returns the invalid profile name
 *
 */
static boolean_t
verify_profiles(char *name, int prop_num, char *profs)
{
	char *subprofname;
	profattr_t *result;
	char *tmp;
	boolean_t found = B_FALSE;
	int i;
	boolean_t verified = B_TRUE;
	attrs_t my_profs;

	if (chkauthattr(PROFILE_ASSIGN_AUTH, username)) {
		my_profs.attr_cnt = -1;
	} else if (!chkauthattr(PROFILE_DELEGATE_AUTH, username)) {
		if (profs)
			(void) fprintf(stderr, gettext("%s: %s: %s cannot "
			    "delegate '%s'\n"),
			    name, pt_to_str(prop_num), username, profs);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    PROFILE_DELEGATE_AUTH);
		my_profs.attr_cnt = 0;
		verified = B_FALSE;
		return (verified);
	} else if (profs == NULL) {
		return (verified);
	} else {
		my_profs.attr_cnt = 0;
		_enum_attrs(username, get_attrs, USERATTR_PROFILES_KW,
		    &my_profs);
	}

	tmp = strdup_check(profs);

	subprofname = strtok(tmp, KV_SEPSTR);
	while (subprofname != NULL) {
		result = getprofnam(subprofname);
		if (result == NULL) {
			/* can't find the profile */
			(void) fprintf(stderr, gettext("%s: %s: '%s' is not "
			    "a valid name\n"),
			    name, pt_to_str(prop_num), subprofname);
			verified = B_FALSE;
			return (verified);
		}
		free_profattr(result);
		if (my_profs.attr_cnt  == -1)
			found = B_TRUE;
		else
			found = B_FALSE;
		for (i = 0; i < my_profs.attr_cnt; i++) {
			if (my_profs.attrvals[i] != NULL &&
			    strcmp(subprofname, my_profs.attrvals[i]) == 0) {
				found = B_TRUE;
				break;
			}
		}
		if (!found) {
			verified = B_FALSE;
			return (verified);
		}
		subprofname = strtok(NULL, KV_SEPSTR);
	}
	free(tmp);
	free_attrs(&my_profs);
	return (verified);

}

static boolean_t
verify_privs(char *name, int prop_num, char *aset_str)
{
	const char *res;
	priv_set_t *aset;
	priv_set_t *uset;
	priv_attr_t priv_attr;
	char *priv_type;
	char *priv_str;

	if (aset_str == NULL)
		return (B_TRUE);

	aset = priv_str_to_set(aset_str, ",", &res);
	if (aset != NULL) {
		res = NULL;
	} else if (res == NULL)
		res = strerror(errno);
	if (res) {
		(void) fprintf(stderr, gettext("%s: %s: '%s' is not a "
		    "valid privilege\n"), name, pt_to_str(prop_num), res);
			return (B_FALSE);
	}

	if (chkauthattr(PRIVILEGE_ASSIGN_AUTH, username))
		return (B_TRUE);
	if (!chkauthattr(PRIVILEGE_DELEGATE_AUTH, username)) {
		(void) fprintf(stderr, gettext("%s: %s: %s cannot "
		    "delegate '%s'\n"),
		    name, pt_to_str(prop_num), username, aset_str);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    PRIVILEGE_DELEGATE_AUTH);
		priv_freeset(aset);
		return (B_FALSE);
	}


	switch (prop_num) {
	case PT_PRIVS:
	case PT_DFLTPRIV:
		priv_type = USERATTR_DFLTPRIV_KW;
		break;
	case PT_LIMPRIV:
	case PT_LIMITPRIVS:
		priv_type = USERATTR_LIMPRIV_KW;
		break;
	default:
		return (B_FALSE);
	}

	/* get real user priv_set */
	_enum_attrs(username, &find_priv_attrs, priv_type,
	    &priv_attr);

	if (priv_attr.privs != NULL)
		priv_str = priv_attr.privs;
	else
		priv_str = "basic";

	uset = priv_str_to_set(priv_str, ",", &res);
	if (priv_attr.privs != NULL)
		free(priv_attr.privs);

	if (uset != NULL) {
		priv_inverse(uset);
		priv_intersect(aset, uset);
	} else if (res == NULL) {
		(void) fprintf(stderr,
		    gettext("%s: %s: %s cannot delegate '%s'\n"),
		    name, pt_to_str(prop_num), username, aset_str);
		priv_freeset(aset);
		return (B_FALSE);
	}
	if (!priv_isemptyset(uset)) {
		priv_str = priv_set_to_str(uset, ',', PRIV_STR_SHORT);
		(void) fprintf(stderr,
		    gettext("%s: %s: %s cannot delegate '%s' requires %s"
		    " privilege\n"),
		    name, pt_to_str(prop_num), username, priv_str, priv_str);
		priv_freeset(aset);
		priv_freeset(uset);
		free(priv_str);
		return (B_FALSE);
	} else {
		priv_freeset(aset);
		priv_freeset(uset);
		return (B_TRUE);
	}
}

static boolean_t
verify_audit(char *name, int prop_num, char *flags)
{
	au_mask_t mask;
	char *err = "NULL";

	if (flags != NULL) {
		if (!__chkflags(flags, &mask, B_FALSE, &err)) {
			(void) fprintf(stderr, gettext("%s: %s: '%s' is not a "
			    "valid flag\n"), name, pt_to_str(prop_num), err);
			return (B_FALSE);
		}
	}
	if (!chkauthattr(AUDIT_ASSIGN_AUTH, username)) {
		if (flags)
			(void) fprintf(stderr, gettext("%s: %s: %s cannot "
			    "assign '%s'\n"),
			    name, pt_to_str(prop_num), username, flags);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    AUDIT_ASSIGN_AUTH);
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
verify_uid(char *name, int prop_num, char *uid)
{
	struct passwd *auser;

	if (uid != NULL) {
		if (isdigit(*uid))
			auser = getpwuid(atoi(uid));
		else
			auser = getpwnam(uid);
		if (auser  == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: %s: '%s' is not a valid user\n"),
			    name, pt_to_str(prop_num), uid);
			return (B_FALSE);
		}
	}
	if (!chkauthattr(PROFILE_CMD_SETUID_AUTH, username)) {
		if (uid)
			(void) fprintf(stderr, gettext("%s: %s: %s cannot "
			    "assign '%s'\n"),
			    name, pt_to_str(prop_num), username, uid);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    PROFILE_CMD_SETUID_AUTH);
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
verify_gid(char *name, int prop_num, char *gid)
{
	struct group *agroup;

	if (gid != NULL) {
		if (isdigit(*gid))
			agroup = getgrgid(atoi(gid));
		else
			agroup = getgrnam(gid);
		if (agroup  == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: %s: '%s' is not a valid group\n"),
			    name, pt_to_str(prop_num), gid);
			return (B_FALSE);
		}
	}

	if (chkauthattr(GROUP_ASSIGN_AUTH, username))
		return (B_TRUE);

	if (!chkauthattr(GROUP_DELEGATE_AUTH, username)) {
		if (gid)
			(void) fprintf(stderr, gettext("%s: %s: %s cannot "
			    "delegate '%s'\n"),
			    name, pt_to_str(prop_num), username, gid);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    GROUP_DELEGATE_AUTH);
		return (B_FALSE);
	} else if (gid != NULL && agroup->gr_gid != getgid()) {
		(void) fprintf(stderr, gettext("%s: %s: %s cannot "
		    "delegate '%s' requires %s gid\n"),
		    name, pt_to_str(prop_num), username, gid, gid);
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
verify_help(char *name, int prop_num, char *helpfile)
{
	int fi;
	boolean_t verified = B_TRUE;

	if (helpfile == NULL)
		return (B_TRUE);

/*
 * Use the real uid when opening the source file
 * to prevent exposing the contents of a private file
 */
	(void) setreuid(geteuid(), getuid());
	fi = open(helpfile, O_RDONLY);
	if (fi < 0) {
		(void) fprintf(stderr, gettext("%s: %s: %s cannot "
		    "read '%s'\n"),
		    name, pt_to_str(prop_num), username, helpfile);
		verified = B_FALSE;
	} else {
		close(fi);
	}
	(void) setreuid(geteuid(), getuid());
	return (verified);
}

static boolean_t
verify_pathname(char *name, int prop_num, char *pathname)
{
	execattr_t *next;

	if (!chkauthattr(PROFILE_CMD_MANAGE_AUTH, username)) {
		(void) fprintf(stderr, gettext("%s: %s: %s cannot manage "
		    "execution attributes\n"),
		    profname, pt_to_str(PT_COMMAND), username);
		(void) fprintf(stderr, gettext("%s: %s: %s "
		    "requires %s authorization\n"),
		    name, pt_to_str(prop_num), username,
		    PROFILE_CMD_MANAGE_AUTH);
		return (B_FALSE);
	} else if (pathname && pathname[0] != '/' &&
	    pathname[0] != KV_WILDCHAR) {
		zerr(gettext("A fully qualified pathname "
		    "is required."));
		return (B_FALSE);
	}
	if (pathname == NULL)
		return (B_TRUE);

	next = cur_commands;
	while (next) {
		if ((next != mod_command) &&
		    (strncmp(next->id, pathname, MAXPATHLEN) == 0)) {
			/*
			 * Command is already in the profile
			 */
			if (end_op == CMD_ADD)
				zerr(gettext("Use'select' to change the "
				    "attributes of an existing command."));
			else
				zerr(gettext("The command %s is already "
				    "included in this profile"), pathname);
			saw_error = B_TRUE;
			return (B_FALSE);
		}
		next = next->next;
	}
	return (B_TRUE);
}

static boolean_t
verify(int prop_type, int cmd, char *value)
{
	char *name;

	if (profile_scope) {
		name = profname;
		if (profile_is_read_only(cmd)) {
			saw_error = B_TRUE;
			return (B_FALSE);
		}
	} else {
		name = cur_cmdname;
		if (command_is_read_only(cmd)) {
			saw_error = B_TRUE;
			return (B_FALSE);
		}
	}

	switch (prop_type) {
	case PT_PROFNAME:
		if (value == NULL || strlen(value) == 0)
			return (B_FALSE);
		else
			return (B_TRUE);
	case PT_AUTHORIZATION:
		return (verify_auths(name, prop_type, value));
	case PT_SUBPROFILE:
		return (verify_profiles(name, prop_type, value));
	case PT_PRIVS:
	case PT_LIMPRIV:
	case PT_DFLTPRIV:
	case PT_LIMITPRIVS:
		return (verify_privs(name, prop_type, value));
	case PT_DESCRIPTION:
		return (B_TRUE);
	case PT_HELPFILE:
		return (verify_help(name, prop_type, value));
	case PT_PATHNAME:
		return (verify_pathname(name, prop_type, value));
	case PT_EUID:
	case PT_UID:
		return (verify_uid(name, prop_type, value));
	case PT_EGID:
	case PT_GID:
		return (verify_gid(name, prop_type, value));
	case PT_ALWAYSAUDIT:
	case PT_NEVERAUDIT:
		return (verify_audit(name, prop_type, value));
	default:
		return (B_FALSE);
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
	execattr_t *next;
	int i;
	boolean_t verified = B_TRUE;
	char *value;
	int prop_type;

	if ((cur_profile->res1 != NULL) &&
	    (strcmp(cur_profile->res1, "RO") != 0)) {
		for (i = 0; prof_helptab[i].prop_num != 0; i++) {

			prop_type = prof_helptab[i].prop_num;
			if (!(need_to_commit & (pt_to_mask(prop_type))))
				continue;

			switch (prop_type) {
			case PT_PROFNAME:
				break;
			case PT_DESCRIPTION:
				if (cur_profile->desc == NULL) {
					zerr(gettext("Some descriptive text "
					    "is required."));
					verified = B_FALSE;
				}
				break;
			case PT_AUTHORIZATION:
				value = kva_match(cur_profile->attr,
				    pt_to_str(prop_type));
				if ((value != NULL) &&
				    (!verify_auths(profname,
				    prop_type, value)))
					verified = B_FALSE;
				break;
			case PT_SUBPROFILE:
				value = kva_match(cur_profile->attr,
				    pt_to_str(prop_type));
				if ((value != NULL) &&
				    (!verify_profiles(profname,
				    prop_type, value)))
					verified = B_FALSE;
				break;
			case PT_PRIVS:
			case PT_LIMPRIV:
			case PT_DFLTPRIV:
				value = kva_match(cur_profile->attr,
				    pt_to_str(prop_type));
				if ((value != NULL) &&
				    (!verify_privs(profname,
				    prop_type, value)))
					verified = B_FALSE;
				break;
			case PT_HELPFILE:
				value = kva_match(cur_profile->attr,
				    pt_to_str(prop_type));
				if ((value != NULL) &&
				    (!verify_help(profname,
				    prop_type, value)))
					verified = B_FALSE;
				break;
			case PT_ALWAYSAUDIT:
			case PT_NEVERAUDIT:
				value = kva_match(cur_profile->attr,
				    pt_to_str(prop_type));
				if ((value != NULL) &&
				    (!verify_audit(profname,
				    prop_type, value)))
					verified = B_FALSE;
				break;
			default:
				break;
			}
		}
	}
	next = cur_commands;
	while (next) {
		int j;

		if ((next->res1 == NULL) ||
		    (next->res1[0] == '\0') ||
		    (strcmp(next->res1, "RO") == 0)) {
			next = next->next;
			continue;
		}
		for (j = 0; cmd_helptab[j].prop_num != 0; j++) {
			prop_type = cmd_helptab[j].prop_num;
			value = kva_match(next->attr,
			    pt_to_str(prop_type));
			if (value == NULL ||
			    value[0] == '\0')
				continue;
			switch (prop_type) {
			case PT_PATHNAME:
				if (!verify_pathname(next->id,
				    prop_type, value))
					verified = B_FALSE;
				break;
			case PT_UID:
			case PT_EUID:
				if (!verify_uid(next->id,
				    prop_type, value))
					verified = B_FALSE;
				break;
			case PT_GID:
			case PT_EGID:
				if (!verify_gid(next->id,
				    prop_type, value))
					verified = B_FALSE;
				break;
			case PT_PRIVS:
			case PT_LIMITPRIVS:
				if (!verify_privs(next->id,
				    prop_type, value))
					verified = B_FALSE;
				break;
			}
		}
		next = next->next;
	}
	profile_verified = verified;
}

void
cancel_func(cmd_t *cmd)
{
	int arg;
	boolean_t arg_err = B_FALSE;

	if (profile_scope) {
		scope_usage(CMD_CANCEL);
		return;
	}

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

	free_execattr(cur_command);
	cur_command = NULL;
	mod_command = NULL;
	profile_scope = B_TRUE;
	resource_scope = RT_PROFATTR;
	end_op = -1;
}

void
end_func(cmd_t *cmd)
{
	boolean_t arg_err = B_FALSE;
	int arg;

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

	if (profile_scope) {
		scope_usage(CMD_END);
		return;
	}

	assert(end_op == CMD_ADD || end_op == CMD_SELECT);

	if (resource_scope) {
		/* First make sure everything was filled in. */

		if (end_op == CMD_ADD) {
			/* Make sure there isn't already one like this. */
			if (cur_command->id == NULL) {
				zerr(gettext("A fully qualified pathname "
				    "is required."));
				saw_error = B_TRUE;
				return;
			}
			if (add_execattr(&cur_commands,
			    cur_command) == B_FALSE) {
				zerr(gettext("Use'select' to change the "
				    "attributes of an existing command."));
				saw_error = B_TRUE;
				return;
			}
		} else { /* CMD_SELECT */
			if (!command_is_read_only(CMD_END)) {
				if (strcmp(mod_command->id,
				    cur_command->id) != 0)
					cur_command->res1 = strdup_check("ADD");
				(void) remove_execattr(&cur_commands,
				    mod_command->id);
				mod_command = NULL;
				if (add_execattr(&cur_commands,
				    cur_command) == B_FALSE) {
					zerr(gettext("%s failed"),
					    cmd_to_str(end_op));
					saw_error = B_TRUE;
					return;
				}
			}
		}
		cur_command = NULL;
	} else {
		short_usage(CMD_END);
		saw_error = B_TRUE;
		return;
	}

	profile_scope = B_TRUE;
	resource_scope = RT_PROFATTR;
	end_op = -1;
}

static int
copy_help_file(char *source, char *target)
{
	int fi, fo;
	struct stat 	s1, s2;

	/*
	 * Copy the file.  If it happens to be a
	 * symlink, copy the file referenced
	 * by the symlink.
	 */
	fi = open(source, O_RDONLY);
	if (fi < 0) {
		(void) fprintf(stderr,
		    gettext("%s: cannot read %s: "),
		    execname, source);
		perror("");
		return (1);
	}

	fo = creat(target, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fo < 0) {
		(void) fprintf(stderr,
		    gettext("%s: cannot create %s: "),
		    execname, target);
		perror("");
		(void) close(fi);
		return (1);
	} else {
		/* stat the new file, its used below */
		(void) stat(target, &s2);
	}

	/*
	 * Set target's permissions to the source
	 * before any copying so that any partially
	 * copied file will have the source's
	 * permissions (at most) or umask permissions
	 * whichever is the most restrictive.
	 *
	 */

	if (fstat(fi, &s1) < 0) {
		(void) fprintf(stderr,
		    gettext("%s: cannot access %s\n"),
		    execname, source);
		return (1);
	}

	if (writefile(fi, fo, source, target, NULL,
	    NULL, &s1, &s2) != 0) {
		return (1);
	}

	(void) close(fi);
	if (close(fo) < 0) {
		return (1);
	}
	return (0);
}

void
commit_func(cmd_t *cmd)
{
	char *help_src;

	if (resource_scope) {
		if (interactive_mode)
			scope_usage(CMD_COMMIT);
		return;
	}
	verify_func(cmd);

	if (!profile_verified)
		return;

	if ((need_to_commit & (pt_to_mask(PT_HELPFILE))) &&
	    (help_src = kva_match(cur_profile->attr,
	    pt_to_str(PT_HELPFILE))) != NULL) {
		char *help_file;
		char *locale;
		char dest_path[MAXPATHLEN];

		help_file = get_execbasename(help_src);
		if ((locale = getenv("LANG")) == NULL)
			locale = "C";
		(void) snprintf(dest_path, MAXPATHLEN,
		    "/usr/lib/help/profiles/locale/%s/%s",
		    locale, help_file);
		copy_help_file(help_src, dest_path);
		need_to_commit &= ~(pt_to_mask(PT_HELPFILE));
	}

	combine_audit_flags();
	if (putprofattrent(cur_profile) == EXIT_OK) {
		need_to_commit = 0;
		saw_error = B_FALSE;
	}

	separate_audit_flags();
}

void
revert_func(cmd_t *cmd)
{
	int arg, answer;
	boolean_t force_revert = B_FALSE;
	boolean_t arg_err = B_FALSE;

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

	if (!force_revert) {
		if ((answer = ask_yesno(B_FALSE, gettext("Are you "
		    "sure you want to revert the profile"))) == -1) {
			zerr(gettext("Input not from terminal and -F not "
			    "specified:\n%s command ignored, exiting."),
			    cmd_to_str(CMD_REVERT));
			exit(Z_ERR);
		}
		if (answer != 1)
			return;
	}
	if (need_to_commit == 0)
		return;
	if (cur_commands != NULL) {
		free_execattr(cur_commands);
		cur_commands = NULL;
	}
	if (del_commands != NULL) {
		free_execattr(del_commands);
		del_commands = NULL;
	}
	need_to_commit = 0;
	saw_error = B_FALSE;

	if (cur_profile != NULL) {
		free_profattr(cur_profile);
		cur_profile = NULL;
	}
	profname = strdup_check(orig_profname);
	if (!initialize())
		exit(Z_ERR);
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
	if (strcmp(cmd->cmd_argv[0], "help") == 0) {
		longer_prop_usage(PT_HELPFILE);
		return;
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
	return (EXIT_OK);
}
static int
cleanup()
{
	int answer;
	cmd_t *cmd;
	int commit = 0;

	if (!interactive_mode && !cmd_file_mode) {
		/*
		 * If we're not in interactive mode, and we're not in command
		 * file mode, then we must be in commands-from-the-command-line
		 * mode.  As such, we can't loop back and ask for more input.
		 * It was OK to prompt for such things as whether or not to
		 * really delete a profile in the command handler called from
		 * yyparse() above, but "really quit?" makes no sense in this
		 * context.  So disable prompting.
		 */
		ok_to_prompt = B_FALSE;
	}
	if (resource_scope) {
		if (!time_to_exit) {
			/*
			 * Just print a simple error message in the -1 case,
			 * since exit_func() already handles that case, and
			 * EOF means we are finished anyway.
			 */
			answer = ask_yesno(B_FALSE,
			    gettext("Profile incomplete; really quit"));
			if (answer == -1) {
				zerr(gettext("Profile incomplete."));
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
	if (need_to_commit) {
		commit = 1;
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
				zerr(gettext("Profile not saved."));
				return (Z_ERR);
			}
			answer = ask_yesno(B_FALSE,
			    gettext("Profile not saved; really quit"));
			if (answer == -1) {
				zerr(gettext("Profile not saved."));
				return (Z_ERR);
			}
			if (answer != 1) {
				time_to_exit = B_FALSE;
				yyin = stdin;
				return (Z_REPEAT);
			}
		} else saw_error = B_FALSE;
	}
	if (need_to_commit && commit)
		return (Z_ERR);
	else if (!need_to_commit && commit)
		return (Z_OK);
	else if (saw_error && !commit)
		return (Z_USAGE);
	return (Z_OK);

}

/*
 * read_input() is the driver of this program.  It is a wrapper around
 * yyparse(), printing appropriate prompts when needed, checking for
 * exit conditions and reacting appropriately [the latter in its cleanup()
 * helper function].
 *
 * Like most profiles functions, it returns EXIT_OK or Z_ERR, *or* Z_REPEAT
 * so do_interactive() knows that we are not really done (i.e, we asked
 * the user if we should really quit and the user said no).
 */
static int
read_input()
{
	boolean_t yyin_is_a_tty = isatty(fileno(yyin));
	/*
	 * The prompt is "e:p> " or "e:p:c> " where e is execname, p is profile
	 * and c is command name: 5 is for the two ":"s + "> " + terminator.
	 */
	char prompt[MAXPATHLEN + 5], *line;

	/* yyin should have been set to the appropriate (FILE *) if not stdin */
	newline_terminated = B_TRUE;
	for (;;) {
		if (yyin_is_a_tty) {
			if (newline_terminated) {
				if (profile_scope)
					(void) snprintf(prompt, sizeof (prompt),
					    "%s:%s> ", execname, profname);
				else
					(void) snprintf(prompt, sizeof (prompt),
					    "%s:%s:%s> ", execname, profname,
					    cur_cmdname);
			}
			/*
			 * If the user hits ^C then we want to catch it and
			 * start over.  If the user hits EOF then we want to
			 * bail out.
			 */
			if (gl == NULL)
				return (cleanup());
			line = gl_get_line(gl, prompt, NULL, -1);
			if (gl_return_status(gl) == GLR_SIGNAL) {
				gl_abandon_line(gl);
				continue;
			}
			if (line == NULL)
				break;
			(void) string_to_yyin(line);
			if (yyin == NULL)
				return (cleanup());
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
 * This function is used in the profiles-interactive-mode scenario: it just
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
	if (err != EXIT_OK)
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
	return (strdup_check(pw->pw_name));
}
static
/* ARGSUSED */
CPL_MATCH_FN(cmd_cpl_fn)
{
	if (profile_scope) {
		return (value_completion(prof_helptab, profile_scope_cmds,
		    cpl, line, word_end));
	} else {
		return (value_completion(cmd_helptab, cmd_res_scope_cmds,
		    cpl, line, word_end));
	}
}

int
main(int argc, char *argv[])
{
	int err, arg;
	int	status = EXIT_OK;
	int	print_flag = PRINT_DEFAULT;
	int	rep_count = 0;
	char	*repository = NULL;

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
		exit(EXIT_OK);
	}

	while ((arg = getopt(argc, argv, "?lap:S:f:")) != EOF) {
		switch (arg) {
		case '?':
			if (optopt == '?')
				usage(B_FALSE, CMD_HELP,
				    HELP_USAGE | HELP_SUBCMDS);
			else
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
			/* NOTREACHED */
		case 'l':
			print_flag |= PRINT_LONG;
			break;
		case 'a':
			print_flag |= PRINT_ALL;
			break;
		case 'f':
			cmd_file_name = strdup_check(optarg);
			cmd_file_mode = B_TRUE;
			break;
		case 'p':
			profname = strdup_check(optarg);
			break;
		case 'S':
			repository = optarg;
			rep_count++;
			if (rep_count > 1) {
				fprintf(stderr, "%s", gettext("ERROR:"
				    " -S option specified multiple"
				    " times.\n"));
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			}
			break;
		default:
			usage(B_FALSE, CMD_HELP, HELP_USAGE);
			exit(Z_USAGE);
		}
	}

	if ((status = get_repository_handle(repository, &lk_rep)) != 0) {
		if (status == SEC_REP_NOREP)
			usage(B_FALSE, CMD_HELP, HELP_USAGE);
		exit(Z_ERR);
	}

	if (profname == NULL) {
		FILE *fp = stdout;

		argc -= optind;
		argv += optind;

		if (*argv == NULL) {
			if (!(print_flag & PRINT_ALL) && rep_count) {
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			}
			status = show_profs(fp, NULL, print_flag);
		} else {
			if (rep_count > 0 || (print_flag & PRINT_ALL)) {
				usage(B_FALSE, CMD_HELP, HELP_USAGE);
				exit(Z_USAGE);
			}
			do {
				(void) fprintf(fp, "%s:\n", *argv);
				status = show_profs(fp, (char *)*argv,
				    (print_flag | PRINT_NAME));
				if (status == EXIT_FATAL) {
					break;
				}
				if (argv[1] != NULL) {
					/* separate users with empty line */
					(void) fprintf(fp, "\n");
				}
			} while (*++argv);
		}
		status = (status == EXIT_OK) ? status : EXIT_FATAL;

		return (status);
	}
	if (optind > argc || strcmp(profname, "") == 0) {
		usage(B_TRUE, CMD_HELP, HELP_USAGE);
		exit(Z_USAGE);
	}
	if (rep_count < 1) {
		status = find_in_nss(SEC_REP_DB_PROFATTR, profname, &mod_rep);
		if (status == SEC_REP_NOT_FOUND) {
			if (get_repository_handle(NSS_REP_FILES, &mod_rep) != 0)
				exit(Z_ERR);
		} else if (status == SEC_REP_SYSTEM_ERROR)
			exit(Z_ERR);
	} else {
		if (get_repository_handle(repository, &mod_rep) != 0)
			exit(Z_ERR);
	}
	if (!initialize())
		exit(Z_ERR);
	orig_profname = strdup_check(profname);


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
			priv_list = get_privlist();
			prof_list = get_proflist();
			auth_list = get_authlist();
			audit_list = get_auditlist();
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
