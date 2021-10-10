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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This module contains smbadm CLI which offers smb configuration
 * functionalities.
 */
#include <errno.h>
#include <err.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <strings.h>
#include <limits.h>
#include <getopt.h>
#include <libintl.h>
#include <zone.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>
#include <netinet/in.h>
#include <auth_attr.h>
#include <locale.h>
#include <smb/smb.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libntsvcs.h>
#include <netsmb/smb_lib.h>
#include <netsmb/smb_keychain.h>
#include <netsmb/netbios.h>
#include <netsmb/nb_lib.h>

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif

#define	SMBADM_EXIT_SUCCESS	0
#define	SMBADM_EXIT_ERROR	1

#define	SMBADM_ARGBUFSIZ	SMBIOC_MAX_NAME * 2
#define	SMBADM_PROMPTBUFSIZ	64 + SMBADM_ARGBUFSIZ
#define	SMBADM_PWDBUFSIZ	256

typedef enum {
	HELP_ADD_KEY,
	HELP_ADD_MEMBER,
	HELP_CREATE_GROUP,
	HELP_CRYPT,
	HELP_DELETE_GROUP,
	HELP_DEL_KEY,
	HELP_DEL_MEMBER,
	HELP_GET_GROUP,
	HELP_JOIN,
	HELP_SHOW_DOMAINS,
	HELP_LOOKUP_SERVER,
	HELP_LOOKUP_USER,
	HELP_PRINT,
	HELP_RENAME_GROUP,
	HELP_SET_GROUP,
	HELP_SHOW_GROUPS,
	HELP_DISABLE_USER,
	HELP_ENABLE_USER,
	HELP_SHOW_SHARES,
	HELP_SHOW_SESSIONS,
	HELP_SHOW_CONNECTIONS,
	HELP_SHOW_FILES
} smbadm_help_t;

#define	SMBADM_CMDF_NONE	0x00
#define	SMBADM_CMDF_USER	0x01
#define	SMBADM_CMDF_GROUP	0x02
#define	SMBADM_CMDF_TYPEMASK	0x0F

#define	SMBADM_ANSBUFSIZ	64
#define	SMBADM_TIMEBUFSIZ	30

/*
 * The auth field can be NULL for no auth required or checking is done
 * elsewhere.
 */
typedef struct smbadm_cmdinfo {
	char *name;
	int (*func)(int, char **);
	smbadm_help_t usage;
	uint32_t flags;
	char *auth;
} smbadm_cmdinfo_t;

smbadm_cmdinfo_t *curcmd;
static char *progname;

#define	SMBADM_ACTION_AUTH	"solaris.smf.manage.smb"
#define	SMBADM_VALUE_AUTH	"solaris.smf.value.smb"

#define	SMBADM_FMT_SESSIONS_TITLE	"%-30s%-23s%-5s%-6s%s\n"
#define	SMBADM_FMT_SESSIONS		"%-30s%-23s%-5d%-6s%s\n"
#define	SMBADM_FMT_CONNECT_TITLE	"%-30s%-30s%-5s%s\n"
#define	SMBADM_FMT_CONNECT		"%-30s%-30s%-5d%s\n"
#define	SMBADM_FMT_FILES		"%-30s%-30s%s\n"
#define	SMBADM_FMT_SHARES		"%-20s%s\n"
#define	SMBADM_FMT_SHOW_DOMAIN		"%-10s: %s\n"
#define	SMBADM_FMT_SHOW_DOMAIN_DC	"%20s: %s\n"
#define	SMBADM_FMT_SHOW_DOMAIN_SID	"%-10s: %s [%s]\n"

/*
 * Show domain types
 */
#define	SMBADM_SHOW_DOMAIN_WKGRP	"Workgroup"
#define	SMBADM_SHOW_DOMAIN_PRIMARY	"Primary"
#define	SMBADM_SHOW_DOMAIN_LOCAL	"Local"
#define	SMBADM_SHOW_DOMAIN_OTHER	"Other"
#define	SMBADM_SHOW_DOMAIN_SEL_DC	"Domain controller"

static boolean_t smbadm_checkauth(const char *);

static void smbadm_usage(boolean_t);
static int smbadm_join_workgroup(const char *);
static int smbadm_join_domain(const char *, const char *);
static int smbadm_set_joininfo(const char *, smb_joininfo_t *);
static int smbadm_krb_config(smb_joininfo_t *, char *, size_t);
static void smbadm_log_krberr(krb5_context, krb5_error_code,
    const char *, ...);
static void smbadm_extract_domain(char *, char **, char **);
static void smbadm_domain_show(list_t *);

static int smbadm_join(int, char **);
static int smbadm_domains_show(int, char **);
static int smbadm_group_create(int, char **);
static int smbadm_group_delete(int, char **);
static int smbadm_group_rename(int, char **);
static int smbadm_groups_show(int, char **);
static void smbadm_group_show_name(const char *, const char *);
static int smbadm_group_getprop(int, char **);
static int smbadm_group_setprop(int, char **);
static int smbadm_group_addmember(int, char **);
static int smbadm_group_delmember(int, char **);
static int smbadm_user_disable(int, char **);
static int smbadm_user_enable(int, char **);
static int smbadm_shares_show(int, char **);
static int smbadm_sessions_show(int, char **);
static int smbadm_connections_show(int, char **);
static int smbadm_files_show(int, char **);
static int smbadm_user_lookup(int, char **);
static int smbadm_crypt(int, char **);
static int smbadm_key_add(int, char **);
static int smbadm_key_remove(int, char **);
static int smbadm_server_lookup(int, char **);
static int smbadm_print(int, char **);

static smbadm_cmdinfo_t smbadm_cmdtable[] =
{
	{ "add-key",		smbadm_key_add,		HELP_ADD_KEY,
		SMBADM_CMDF_NONE,	NULL },
	{ "add-member",		smbadm_group_addmember,	HELP_ADD_MEMBER,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "create-group",	smbadm_group_create,	HELP_CREATE_GROUP,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "crypt",		smbadm_crypt,		HELP_CRYPT,
		SMBADM_CMDF_NONE,	NULL },
	{ "delete-group",	smbadm_group_delete,	HELP_DELETE_GROUP,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "disable-user",	smbadm_user_disable,	HELP_DISABLE_USER,
		SMBADM_CMDF_USER,	SMBADM_ACTION_AUTH },
	{ "enable-user",	smbadm_user_enable,	HELP_ENABLE_USER,
		SMBADM_CMDF_USER,	SMBADM_ACTION_AUTH },
	{ "get-group",		smbadm_group_getprop,	HELP_GET_GROUP,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "join",		smbadm_join,		HELP_JOIN,
		SMBADM_CMDF_NONE,	SMBADM_VALUE_AUTH },
	{ "lookup-server",	smbadm_server_lookup,	HELP_LOOKUP_SERVER,
		SMBADM_CMDF_NONE,	NULL },
	{ "lookup-user",	smbadm_user_lookup,	HELP_LOOKUP_USER,
		SMBADM_CMDF_NONE,	NULL },
	{ "print",		smbadm_print,		HELP_PRINT,
		SMBADM_CMDF_NONE,	NULL },
	{ "remove-key",		smbadm_key_remove,	HELP_DEL_KEY,
		SMBADM_CMDF_NONE,	NULL },
	{ "remove-member",	smbadm_group_delmember,	HELP_DEL_MEMBER,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "rename-group",	smbadm_group_rename,	HELP_RENAME_GROUP,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "set-group",		smbadm_group_setprop,	HELP_SET_GROUP,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "show-domains",	smbadm_domains_show,	HELP_SHOW_DOMAINS,
		SMBADM_CMDF_NONE,	NULL },
	{ "show-groups",	smbadm_groups_show,	HELP_SHOW_GROUPS,
		SMBADM_CMDF_GROUP,	SMBADM_ACTION_AUTH },
	{ "show-shares",	smbadm_shares_show,	HELP_SHOW_SHARES,
		SMBADM_CMDF_NONE,	NULL },
	{ "show-sessions",	smbadm_sessions_show,	HELP_SHOW_SESSIONS,
		SMBADM_CMDF_NONE,	SMBADM_ACTION_AUTH },
	{ "show-connections",	smbadm_connections_show, HELP_SHOW_CONNECTIONS,
		SMBADM_CMDF_NONE,	SMBADM_ACTION_AUTH },
	{ "show-files",		smbadm_files_show,	HELP_SHOW_FILES,
		SMBADM_CMDF_NONE,	SMBADM_ACTION_AUTH },
};

#define	SMBADM_NCMD	(sizeof (smbadm_cmdtable) / sizeof (smbadm_cmdtable[0]))

typedef struct smbadm_prop {
	char *p_name;
	char *p_value;
} smbadm_prop_t;

typedef struct smbadm_prop_handle {
	char *p_name;
	char *p_dispvalue;
	int (*p_setfn)(char *, smbadm_prop_t *);
	int (*p_getfn)(char *, smbadm_prop_t *);
	boolean_t (*p_chkfn)(smbadm_prop_t *);
} smbadm_prop_handle_t;

typedef struct smbadm_svc_info {
	char ss_domain[MAXHOSTNAMELEN];
	char ss_username[MAXNAMELEN];
	char ss_passwd[SMB_PASSWD_MAXLEN + 1];
	char ss_server[MAXHOSTNAMELEN];
	char ss_qualifier[MAXNAMELEN];
	boolean_t ss_qual_computer;
	boolean_t ss_qual_share;
	boolean_t ss_showtitle;
	boolean_t ss_anon_connect;
} smbadm_svc_info_t;

static int smbadm_getuserinfo(const char *, smbadm_svc_info_t *);
static boolean_t smbadm_prop_validate(smbadm_prop_t *prop, boolean_t chkval);
static int smbadm_prop_parse(char *arg, smbadm_prop_t *prop);
static smbadm_prop_handle_t *smbadm_prop_gethandle(char *pname);

static boolean_t smbadm_chkprop_priv(smbadm_prop_t *prop);
static int smbadm_setprop_tkowner(char *gname, smbadm_prop_t *prop);
static int smbadm_getprop_tkowner(char *gname, smbadm_prop_t *prop);
static int smbadm_setprop_backup(char *gname, smbadm_prop_t *prop);
static int smbadm_getprop_backup(char *gname, smbadm_prop_t *prop);
static int smbadm_setprop_restore(char *gname, smbadm_prop_t *prop);
static int smbadm_getprop_restore(char *gname, smbadm_prop_t *prop);
static int smbadm_setprop_desc(char *gname, smbadm_prop_t *prop);
static int smbadm_getprop_desc(char *gname, smbadm_prop_t *prop);

static smbadm_prop_handle_t smbadm_ptable[] = {
	{"backup",	"on | off", 	smbadm_setprop_backup,
	smbadm_getprop_backup,	smbadm_chkprop_priv 	},
	{"restore",	"on | off",	smbadm_setprop_restore,
	smbadm_getprop_restore,	smbadm_chkprop_priv	},
	{"take-ownership", "on | off",	smbadm_setprop_tkowner,
	smbadm_getprop_tkowner,	smbadm_chkprop_priv	},
	{"description",	"<string>",	smbadm_setprop_desc,
	smbadm_getprop_desc,	NULL			},
};

static int smbadm_init(void);
static void smbadm_fini(void);
static const char *smbadm_pwd_strerror(int error);

/*
 * Number of supported properties
 */
#define	SMBADM_NPROP	(sizeof (smbadm_ptable) / sizeof (smbadm_ptable[0]))

static void
smbadm_cmdusage(FILE *fp, smbadm_cmdinfo_t *cmd)
{
	switch (cmd->usage) {
	case HELP_ADD_KEY:
		(void) fprintf(fp, gettext("\t%s [-u username]\n"), cmd->name);
		return;

	case HELP_ADD_MEMBER:
		(void) fprintf(fp,
		    gettext("\t%s -m member [[-m member] ...] group\n"),
		    cmd->name);
		return;

	case HELP_CREATE_GROUP:
		(void) fprintf(fp, gettext("\t%s [-d description] group\n"),
		    cmd->name);
		return;

	case HELP_CRYPT:
		(void) fprintf(fp, gettext("\t%s\n"), cmd->name);
		return;

	case HELP_DELETE_GROUP:
		(void) fprintf(fp, gettext("\t%s group\n"), cmd->name);
		return;

	case HELP_DISABLE_USER:
	case HELP_ENABLE_USER:
		(void) fprintf(fp, gettext("\t%s user\n"), cmd->name);
		return;

	case HELP_GET_GROUP:
		(void) fprintf(fp, gettext("\t%s [[-p property] ...] group\n"),
		    cmd->name);
		return;

	case HELP_JOIN:
		(void) fprintf(fp, gettext("\t%s -u username domain\n"
		    "\t%s -w workgroup\n"), cmd->name, cmd->name);
		return;

	case HELP_SHOW_DOMAINS:
		(void) fprintf(fp, gettext("\t%s\n"), cmd->name);
		return;

	case HELP_LOOKUP_SERVER:
		(void) fprintf(fp, gettext("\t%s [-w host] "
		    "//server\n"), cmd->name);
		return;

	case HELP_LOOKUP_USER:
		(void) fprintf(fp, gettext("\t%s [-u username] "
		    "name | SID\n"), cmd->name);
		return;

	case HELP_PRINT:
		(void) fprintf(fp, gettext("\t%s [-u username] "
		    "//server/share  {print_file|-}\n"), cmd->name);
		return;

	case HELP_DEL_KEY:
		(void) fprintf(fp, gettext("\t%s [-u username]\n"), cmd->name);
		return;

	case HELP_DEL_MEMBER:
		(void) fprintf(fp,
		    gettext("\t%s -m member [[-m member] ...] group\n"),
		    cmd->name);
		return;

	case HELP_RENAME_GROUP:
		(void) fprintf(fp, gettext("\t%s group new-group\n"),
		    cmd->name);
		return;

	case HELP_SET_GROUP:
		(void) fprintf(fp, gettext("\t%s -p property=value "
		    "[[-p property=value] ...] group\n"), cmd->name);
		return;

	case HELP_SHOW_GROUPS:
		(void) fprintf(fp, gettext("\t%s [-m] [-p] [group]\n"),
		    cmd->name);
		return;

	case HELP_SHOW_SHARES:
		(void) fprintf(fp,
		    gettext("\t%s [-t] [-A | -u username] server\n"),
		    cmd->name);
		return;
	case HELP_SHOW_SESSIONS:
	case HELP_SHOW_FILES:
		(void) fprintf(fp, gettext("\t%s [-t] [-u username] server\n"),
		    cmd->name);
		return;

	case HELP_SHOW_CONNECTIONS:
		(void) fprintf(fp,
		    gettext("\t%s [-t] [-u username] "
		    "[-c computername | -s sharename] server\n"), cmd->name);
		return;
	default:
		break;
	}

	abort();
	/* NOTREACHED */
}

static void
smbadm_usage(boolean_t requested)
{
	FILE *fp = requested ? stdout : stderr;
	boolean_t show_props = B_FALSE;
	int i;

	if (curcmd == NULL) {
		(void) fprintf(fp,
		    gettext("usage: %s [-h | <command> [options]]\n"),
		    progname);
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < SMBADM_NCMD; i++)
			smbadm_cmdusage(fp, &smbadm_cmdtable[i]);

		(void) fprintf(fp,
		    gettext("\nFor property list, run %s %s|%s\n"),
		    progname, "get-group", "set-group");

		exit(requested ? 0 : 2);
	}

	(void) fprintf(fp, gettext("usage:\n"));
	smbadm_cmdusage(fp, curcmd);

	if (strcmp(curcmd->name, "get-group") == 0 ||
	    strcmp(curcmd->name, "set-group") == 0)
		show_props = B_TRUE;

	if (show_props) {
		(void) fprintf(fp,
		    gettext("\nThe following properties are supported:\n"));

		(void) fprintf(fp, "\n\t%-16s   %s\n\n",
		    "PROPERTY", "VALUES");

		for (i = 0; i < SMBADM_NPROP; i++) {
			(void) fprintf(fp, "\t%-16s   %s\n",
			    smbadm_ptable[i].p_name,
			    smbadm_ptable[i].p_dispvalue);
		}
	}

	exit(requested ? 0 : 2);
}

/*
 * smbadm_strcasecmplist
 *
 * Find a string 's' within a list of strings.
 *
 * Returns the index of the matching string or -1 if there is no match.
 */
static int
smbadm_strcasecmplist(const char *s, ...)
{
	va_list ap;
	char *p;
	int ndx;

	va_start(ap, s);

	for (ndx = 0; ((p = va_arg(ap, char *)) != NULL); ++ndx) {
		if (strcasecmp(s, p) == 0) {
			va_end(ap);
			return (ndx);
		}
	}

	va_end(ap);
	return (-1);
}

/*
 * smbadm_answer_prompt
 *
 * Prompt for the answer to a question.  A default response must be
 * specified, which will be used if the user presses <enter> without
 * answering the question.
 */
static int
smbadm_answer_prompt(const char *prompt, char *answer, const char *dflt)
{
	char buf[SMBADM_ANSBUFSIZ];
	char *p;

	(void) printf(gettext("%s [%s]: "), prompt, dflt);

	if (fgets(buf, SMBADM_ANSBUFSIZ, stdin) == NULL)
		return (-1);

	if ((p = strchr(buf, '\n')) != NULL)
		*p = '\0';

	if (*buf == '\0')
		(void) strlcpy(answer, dflt, SMBADM_ANSBUFSIZ);
	else
		(void) strlcpy(answer, buf, SMBADM_ANSBUFSIZ);

	return (0);
}

/*
 * smbadm_confirm
 *
 * Ask a question that requires a yes/no answer.
 * A default response must be specified.
 */
static boolean_t
smbadm_confirm(const char *prompt, const char *dflt)
{
	char buf[SMBADM_ANSBUFSIZ];

	for (;;) {
		if (smbadm_answer_prompt(prompt, buf, dflt) < 0)
			return (B_FALSE);

		if (smbadm_strcasecmplist(buf, "n", "no", 0) >= 0)
			return (B_FALSE);

		if (smbadm_strcasecmplist(buf, "y", "yes", 0) >= 0)
			return (B_TRUE);

		(void) printf(gettext("Please answer yes or no.\n"));
	}
}

static boolean_t
smbadm_join_prompt(const char *domain)
{
	(void) printf(gettext("After joining %s the smb service will be "
	    "restarted automatically.\n"), domain);

	return (smbadm_confirm("Would you like to continue?", "no"));
}

static void
smbadm_restart_service(void)
{
	if (smb_smf_restart_service() != 0) {
		(void) fprintf(stderr,
		    gettext("Unable to restart smb service. "
		    "Run 'svcs -xv smb/server' for more information."));
	}
}

static boolean_t
smbadm_krbcfg_drealm_update_prompt(smb_krb5_cfg_t *cfg)
{
	char buf[SMB_LOG_LINE_SZ];

	(void) snprintf(buf, sizeof (buf), "The current default realm is %s.\n"
	    "Do you want to change the default realm to %s?",
	    cfg->kc_orig_drealm, cfg->kc_realm);

	return (smbadm_confirm(buf, "yes"));
}

/*
 * smbadm_join
 *
 * Join a domain or workgroup.
 *
 * When joining a domain, we may receive the username, password and
 * domain name in any of the following combinations.  Note that the
 * password is optional on the command line: if it is not provided,
 * we will prompt for it later.
 *
 *	username+password domain
 *	domain\username+password
 *	domain/username+password
 *	username@domain
 *
 * We allow domain\name+password or domain/name+password but not
 * name+password@domain because @ is a valid password character.
 *
 * If the username and domain name are passed as separate command
 * line arguments, we process them directly.  Otherwise we separate
 * them and continue as if they were separate command line arguments.
 */
static int
smbadm_join(int argc, char **argv)
{
	char buf[MAXHOSTNAMELEN * 2];
	char *domain = NULL;
	char *username = NULL;
	uint32_t mode = 0;
	int option;

	while ((option = getopt(argc, argv, "u:w:")) != -1) {
		switch (option) {
		case 'w':
			if (mode != 0) {
				(void) fprintf(stderr,
				    gettext("-u and -w must only appear "
				    "once and are mutually exclusive\n"));
				smbadm_usage(B_FALSE);
			}

			mode = SMB_SECMODE_WORKGRP;
			domain = optarg;
			break;

		case 'u':
			if (mode != 0) {
				(void) fprintf(stderr,
				    gettext("-u and -w must only appear "
				    "once and are mutually exclusive\n"));
				smbadm_usage(B_FALSE);
			}

			mode = SMB_SECMODE_DOMAIN;
			username = optarg;

			if ((domain = argv[optind]) == NULL) {
				/*
				 * The domain was not specified as a separate
				 * argument, check for the combination forms.
				 */
				(void) strlcpy(buf, username, sizeof (buf));
				smbadm_extract_domain(buf, &username, &domain);
			}

			if ((username == NULL) || (*username == '\0')) {
				(void) fprintf(stderr,
				    gettext("missing username\n"));
				smbadm_usage(B_FALSE);
			}
			break;

		default:
			smbadm_usage(B_FALSE);
			break;
		}
	}

	if ((domain == NULL) || (*domain == '\0')) {
		(void) fprintf(stderr, gettext("missing %s name\n"),
		    (mode == SMB_SECMODE_WORKGRP) ? "workgroup" : "domain");
		smbadm_usage(B_FALSE);
	}

	if (mode == SMB_SECMODE_WORKGRP)
		return (smbadm_join_workgroup(domain));
	else
		return (smbadm_join_domain(domain, username));
}

/*
 * Workgroups comprise a collection of standalone, independently administered
 * computers that use a common workgroup name.  This is a peer-to-peer model
 * with no formal membership mechanism.
 */
static int
smbadm_join_workgroup(const char *workgroup)
{
	smb_joininfo_t jdi;
	uint32_t status;

	bzero(&jdi, sizeof (jdi));
	jdi.mode = SMB_SECMODE_WORKGRP;
	(void) strlcpy(jdi.domain_name, workgroup, sizeof (jdi.domain_name));
	(void) strtrim(jdi.domain_name, " \t\n");

	if (smb_name_validate_workgroup(jdi.domain_name) != ERROR_SUCCESS) {
		(void) fprintf(stderr, gettext("workgroup name is invalid\n"));
		smbadm_usage(B_FALSE);
	}

	if (!smbadm_join_prompt(jdi.domain_name))
		return (0);

	if (smb_join(&jdi, &status) != 0) {
		(void) fprintf(stderr,
		    gettext("failed to join %s\n"),  jdi.domain_name);
		(void) fprintf(stderr,
		    gettext("failed to contact smbd - %s\n"), strerror(errno));
		return (1);
	}
	if (status != NT_STATUS_SUCCESS) {
		(void) fprintf(stderr, gettext("failed to join %s: %s\n"),
		    jdi.domain_name, xlate_nt_status(status));
		return (1);
	} else {
		(void) printf(gettext("Successfully joined %s\n"),
		    jdi.domain_name);
		smbadm_restart_service();
	}
	return (0);
}

/*
 * Domains comprise a centrally administered group of computers and accounts
 * that share a common security and administration policy and database.
 * Computers must join a domain and become domain members, which requires
 * an administrator level account name.
 *
 * The '+' character is invalid within a username.  We allow the password
 * to be appended to the username using '+' as a scripting convenience.
 *
 * If the system is currently operating in domain mode, join workgroup prior to
 * attempting the domain join.  This will ensure that upon domain join failure:
 * - the system is in a well-defined state, and
 * - previous domain membership will not be displayed to confuse end-users.
 *
 * On exit, smb_joininfo_t instance is bzero'd to avoid leaving behind
 * confidential information (i.e. user's password) on the stack.
 */
static int
smbadm_join_domain(const char *domain, const char *username)
{
	smb_joininfo_t	jdi;
	uint32_t	status;
	char		*prompt;
	char		*p;
	int		len;
	char		nb_domain[MAXHOSTNAMELEN];
	char		drealm[MAXHOSTNAMELEN];
	errcode_t	err;
	smb_joininfo_t	wkgrp = {"WORKGROUP", "", "", "", SMB_SECMODE_WORKGRP};

	bzero(&jdi, sizeof (jdi));
	jdi.mode = SMB_SECMODE_DOMAIN;
	(void) strlcpy(jdi.domain_name, domain, sizeof (jdi.domain_name));
	(void) strtrim(jdi.domain_name, " \t\n");

	if (smb_name_validate_domain(jdi.domain_name) != ERROR_SUCCESS) {
		(void) fprintf(stderr, gettext("domain name is invalid\n"));
		smbadm_usage(B_FALSE);
	}

	if (!smbadm_join_prompt(jdi.domain_name))
		return (0);

	if ((p = strchr(username, '+')) != NULL) {
		++p;

		len = (int)(p - username);
		if (len > sizeof (jdi.domain_name))
			len = sizeof (jdi.domain_name);

		(void) strlcpy(jdi.domain_username, username, len);
		(void) strlcpy(jdi.domain_passwd, p,
		    sizeof (jdi.domain_passwd));
	} else {
		(void) strlcpy(jdi.domain_username, username,
		    sizeof (jdi.domain_username));
	}

	if (smb_name_validate_account(jdi.domain_username) != ERROR_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("username contains invalid characters\n"));
		smbadm_usage(B_FALSE);
	}

	if (*jdi.domain_passwd == '\0') {
		prompt = gettext("Enter domain password: ");

		if ((p = getpassphrase(prompt)) == NULL) {
			(void) fprintf(stderr, gettext("missing password\n"));
			smbadm_usage(B_FALSE);
		}

		(void) strlcpy(jdi.domain_passwd, p,
		    sizeof (jdi.domain_passwd));
	}

	if (!SMB_IS_FQDN(jdi.domain_name))
		(void) strlcpy(nb_domain, jdi.domain_name, sizeof (nb_domain));
	else
		*nb_domain = '\0';

	/* Switch to workgroup */
	if (smb_config_get_secmode() == SMB_SECMODE_DOMAIN) {
		if (smb_join(&wkgrp, &status) != 0) {
			(void) fprintf(stderr,
			    gettext("failed to join %s\n"),  wkgrp.domain_name);
			(void) fprintf(stderr,
			    gettext("failed to contact smbd - %s\n"),
			    strerror(errno));
			bzero(&jdi, sizeof (jdi));
			return (1);
		}
	}

	/*
	 * DC discovery
	 */
	(void) printf(gettext("Locating DC in %s ... this may take a "
	    "minute ...\n"), jdi.domain_name);
	if ((status = smb_discover_dc(&jdi)) != NT_STATUS_SUCCESS) {
		switch (status) {
		case NT_STATUS_DOMAIN_CONTROLLER_NOT_FOUND:
			(void) fprintf(stderr, gettext(
			    "failed to find any domain controllers for %s\n"),
			    jdi.domain_name);
			bzero(&jdi, sizeof (jdi));
			return (1);

		default:
			(void) fprintf(stderr, gettext(
			    "failed to locate domain controllers for %s: %s\n"),
			    jdi.domain_name, xlate_nt_status(status));
			bzero(&jdi, sizeof (jdi));
			return (1);
		}
	}

	/*
	 * If NetBIOS name of the domain is specified, the corresponding fully
	 * qualified name should be looked up in order to determine whether
	 * there is an existing Kerberos configuration for the domain.
	 */
	if (smbadm_set_joininfo(nb_domain, &jdi) != 0) {
		bzero(&jdi, sizeof (jdi));
		return (1);
	}

	/* Set up Kerberos config file prior to domain join. */
	if (smbadm_krb_config(&jdi, drealm, sizeof (drealm)) != 0) {
		bzero(&jdi, sizeof (jdi));
		return (1);
	}

	(void) printf(gettext("Joining %s ... this may take a minute"
	    " ...\n"), jdi.domain_name);

	if (smb_join(&jdi, &status) != 0) {
		(void) fprintf(stderr,
		    gettext("failed to join %s\n"), jdi.domain_name);
		(void) fprintf(stderr,
		    gettext("failed to contact smbd - %s\n"), strerror(errno));
		bzero(&jdi, sizeof (jdi));
		return (1);
	}

	switch (status) {
	case NT_STATUS_SUCCESS:
		(void) printf(gettext("Successfully joined %s\n"),
		    jdi.domain_name);
		bzero(&jdi, sizeof (jdi));
		smbadm_restart_service();
		return (0);

	default:
		(void) fprintf(stderr, gettext("failed to join %s: %s\n"),
		    jdi.domain_name, xlate_nt_status(status));
		(void) fprintf(stderr, gettext("Please refer to the system log"
		    " for more information.\n"));
		bzero(&jdi, sizeof (jdi));
		/*
		 * On error, restore the default realm in krb5.conf to avoid
		 * disruptions to Kerberized applications.
		 */
		if (*drealm != '\0') {
			if ((err = smb_krb5_cfg_set_drealm(drealm)) != 0) {
				smbadm_log_krberr(NULL, err,
				    "Failed to restore the default realm %s in"
				    " %s.", drealm, smb_krb5_cfg_getpath());
			}
		}
		return (1);
	}
}

/*
 * We want to process the user and domain names as separate strings.
 * Check for names of the forms below and separate the components as
 * required.
 *
 *	name@domain
 *	domain\name
 *	domain/name
 *
 * If we encounter any of the forms above in arg, the @, / or \
 * separator is replaced by \0 and the username and domain pointers
 * are changed to point to the appropriate components (in arg).
 *
 * If none of the separators are encountered, the username and domain
 * pointers remain unchanged.
 */
static void
smbadm_extract_domain(char *arg, char **username, char **domain)
{
	char *p;

	if ((p = strpbrk(arg, "/\\@")) != NULL) {
		if (*p == '@') {
			*p = '\0';
			++p;

			if (strchr(arg, '+') != NULL)
				return;

			*domain = p;
			*username = arg;
		} else {
			*p = '\0';
			++p;
			*username = p;
			*domain = arg;
		}
	}
}

/*
 * smbadm_domains_show
 *
 * Displays current security mode and domain/workgroup name.
 */
/*ARGSUSED*/
static int
smbadm_domains_show(int argc, char **argv)
{
	char nb_domain[NETBIOS_NAME_SZ];
	char ad_domain[MAXHOSTNAMELEN];
	char modename[16];
	smb_domains_info_t domains_info;
	int rc;

	rc = smb_config_getstr(SMB_CI_SECURITY, modename, sizeof (modename));
	if (rc != SMBD_SMF_OK) {
		(void) fprintf(stderr,
		    gettext("cannot determine the operational mode\n"));
		return (1);
	}

	if (smb_getdomainname_nb(nb_domain, sizeof (nb_domain)) != 0) {
		(void) fprintf(stderr, gettext("failed to get the %s name\n"),
		    modename);
		return (1);
	}

	if (strcmp(modename, "workgroup") == 0) {
		(void) printf(SMBADM_FMT_SHOW_DOMAIN,
		    gettext(SMBADM_SHOW_DOMAIN_WKGRP), nb_domain);
		return (0);
	}

	(void) printf(SMBADM_FMT_SHOW_DOMAIN,
	    gettext(SMBADM_SHOW_DOMAIN_PRIMARY), nb_domain);
	if ((smb_getdomainname_ad(ad_domain, sizeof (ad_domain)) == 0) &&
	    (*ad_domain != '\0'))
		(void) printf(SMBADM_FMT_SHOW_DOMAIN,
		    gettext(SMBADM_SHOW_DOMAIN_PRIMARY), ad_domain);

	if (smb_get_domains_info(&domains_info) != 0)
		return (0);

	if (domains_info.d_status == NT_STATUS_SUCCESS) {

		if (*domains_info.d_dc_name != '\0')
			(void) printf(SMBADM_FMT_SHOW_DOMAIN_DC,
			    gettext(SMBADM_SHOW_DOMAIN_SEL_DC),
			    domains_info.d_dc_name);

		smbadm_domain_show(&domains_info.d_domain_list);
	}
	smb_domains_info_free(&domains_info.d_domain_list);

	return (0);
}

/*
 * Display the domains (local, primary, and trusted/untrusted).
 */
static void
smbadm_domain_show(list_t *domain_list)
{
	char			*tag;
	smb_domain_info_t	*domain;

	domain = list_head(domain_list);
	while (domain) {
		switch (domain->i_type) {
		case SMB_DOMAIN_PRIMARY:
			tag = SMBADM_SHOW_DOMAIN_PRIMARY;
			break;

		case SMB_DOMAIN_TRUSTED:
		case SMB_DOMAIN_UNTRUSTED:
			tag = SMBADM_SHOW_DOMAIN_OTHER;
			break;

		case SMB_DOMAIN_LOCAL:
			tag = SMBADM_SHOW_DOMAIN_LOCAL;
			break;
		default:
			domain = list_next(domain_list, domain);
			continue;
		}

		(void) printf(SMBADM_FMT_SHOW_DOMAIN_SID,
		    tag, domain->i_nbname, domain->i_sid);

		domain = list_next(domain_list, domain);
	}
}

/*
 * smbadm_group_create
 *
 * Creates a local SMB group
 */
static int
smbadm_group_create(int argc, char **argv)
{
	char *gname = NULL;
	char *desc = NULL;
	int option;
	int status;

	while ((option = getopt(argc, argv, "d:")) != -1) {
		switch (option) {
		case 'd':
			desc = optarg;
			break;

		default:
			smbadm_usage(B_FALSE);
		}
	}

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == '\0') {
		(void) fprintf(stderr, gettext("missing group name\n"));
		smbadm_usage(B_FALSE);
	}

	status = smb_lgrp_add(gname, desc);
	if (status != SMB_LGRP_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("failed to create %s (%s)\n"), gname,
		    smb_lgrp_strerror(status));
	} else {
		(void) printf(gettext("%s created\n"), gname);
	}

	return (status);
}

/*
 * smbadm_group_dump_members
 *
 * Dump group members details.
 */
static void
smbadm_group_dump_members(smb_gsid_t *members, int num)
{
	char		sidstr[SMB_SID_STRSZ];
	lsa_account_t	acct;
	int		i;

	if (num == 0) {
		(void) printf(gettext("\tNo members\n"));
		return;
	}

	(void) printf(gettext("\tMembers:\n"));
	for (i = 0; i < num; i++) {
		smb_sid_tostr(members[i].gs_sid, sidstr);

		if (smb_lookup_sid(sidstr, &acct) == 0) {
			if (acct.a_status == NT_STATUS_SUCCESS)
				smbadm_group_show_name(acct.a_domain,
				    acct.a_name);
			else
				(void) printf(gettext("\t\t%s [%s]\n"),
				    sidstr, xlate_nt_status(acct.a_status));
		} else {
			(void) printf(gettext("\t\t%s\n"), sidstr);
		}
	}
}

static void
smbadm_group_show_name(const char *domain, const char *name)
{
	if (strchr(domain, '.') != NULL)
		(void) printf(gettext("\t\t%s@%s\n"), name, domain);
	else
		(void) printf(gettext("\t\t%s\\%s\n"), domain, name);
}

/*
 * smbadm_group_dump_privs
 *
 * Dump group privilege details.
 */
static void
smbadm_group_dump_privs(smb_privset_t *privs)
{
	smb_privinfo_t *pinfo;
	char *pstatus;
	int i;

	(void) printf(gettext("\tPrivileges: \n"));

	for (i = 0; i < privs->priv_cnt; i++) {
		pinfo = smb_priv_getbyvalue(privs->priv[i].luid.lo_part);
		if ((pinfo == NULL) || (pinfo->flags & PF_PRESENTABLE) == 0)
			continue;

		switch (privs->priv[i].attrs) {
		case SE_PRIVILEGE_ENABLED:
			pstatus = "On";
			break;
		case SE_PRIVILEGE_DISABLED:
			pstatus = "Off";
			break;
		default:
			pstatus = "Unknown";
			break;
		}
		(void) printf(gettext("\t\t%s: %s\n"), pinfo->name, pstatus);
	}

	if (privs->priv_cnt == 0)
		(void) printf(gettext("\t\tNo privileges\n"));
}

/*
 * smbadm_group_dump
 *
 * Dump group details.
 */
static void
smbadm_group_dump(smb_group_t *grp, boolean_t show_mem, boolean_t show_privs)
{
	char sidstr[SMB_SID_STRSZ];

	(void) printf(gettext("%s (%s)\n"), grp->sg_name, grp->sg_cmnt);

	smb_sid_tostr(grp->sg_id.gs_sid, sidstr);
	(void) printf(gettext("\tSID: %s\n"), sidstr);

	if (show_privs)
		smbadm_group_dump_privs(grp->sg_privs);

	if (show_mem)
		smbadm_group_dump_members(grp->sg_members, grp->sg_nmembers);
}

/*
 * smbadm_group_show
 *
 */
static int
smbadm_groups_show(int argc, char **argv)
{
	char *gname = NULL;
	boolean_t show_privs;
	boolean_t show_members;
	int option;
	int status;
	smb_group_t grp;
	smb_giter_t gi;

	show_privs = show_members = B_FALSE;

	while ((option = getopt(argc, argv, "mp")) != -1) {
		switch (option) {
		case 'm':
			show_members = B_TRUE;
			break;
		case 'p':
			show_privs = B_TRUE;
			break;

		default:
			smbadm_usage(B_FALSE);
		}
	}

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == '\0')
		gname = "*";

	if (strcmp(gname, "*")) {
		status = smb_lgrp_getbyname(gname, &grp);
		if (status == SMB_LGRP_SUCCESS) {
			smbadm_group_dump(&grp, show_members, show_privs);
			smb_lgrp_free(&grp);
		} else {
			(void) fprintf(stderr,
			    gettext("failed to find %s (%s)\n"),
			    gname, smb_lgrp_strerror(status));
		}
		return (status);
	}

	if ((status = smb_lgrp_iteropen(&gi)) != SMB_LGRP_SUCCESS) {
		(void) fprintf(stderr, gettext("failed to list groups (%s)\n"),
		    smb_lgrp_strerror(status));
		return (status);
	}

	while ((status = smb_lgrp_iterate(&gi, &grp)) == SMB_LGRP_SUCCESS) {
		smbadm_group_dump(&grp, show_members, show_privs);
		smb_lgrp_free(&grp);
	}

	smb_lgrp_iterclose(&gi);

	if ((status != SMB_LGRP_NO_MORE) || smb_lgrp_itererror(&gi)) {
		if (status != SMB_LGRP_NO_MORE)
			syslog(LOG_ERR, "smb_lgrp_iterate: %s",
			    smb_lgrp_strerror(status));

		(void) fprintf(stderr,
		    gettext("\nAn error occurred while retrieving group data.\n"
		    "Check the system log for more information.\n"));
		return (status);
	}

	return (0);
}

/*
 * smbadm_group_delete
 */
static int
smbadm_group_delete(int argc, char **argv)
{
	char *gname = NULL;
	int status;

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == '\0') {
		(void) fprintf(stderr, gettext("missing group name\n"));
		smbadm_usage(B_FALSE);
	}

	status = smb_lgrp_delete(gname);
	if (status != SMB_LGRP_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("failed to delete %s (%s)\n"), gname,
		    smb_lgrp_strerror(status));
	} else {
		(void) printf(gettext("%s deleted\n"), gname);
	}

	return (status);
}

/*
 * smbadm_group_rename
 */
static int
smbadm_group_rename(int argc, char **argv)
{
	char *gname = NULL;
	char *ngname = NULL;
	int status;

	gname = argv[optind];
	if (optind++ >= argc || gname == NULL || *gname == '\0') {
		(void) fprintf(stderr, gettext("missing group name\n"));
		smbadm_usage(B_FALSE);
	}

	ngname = argv[optind];
	if (optind >= argc || ngname == NULL || *ngname == '\0') {
		(void) fprintf(stderr, gettext("missing new group name\n"));
		smbadm_usage(B_FALSE);
	}

	status = smb_lgrp_rename(gname, ngname);
	if (status != SMB_LGRP_SUCCESS) {
		if (status == SMB_LGRP_EXISTS)
			(void) fprintf(stderr,
			    gettext("failed to rename '%s' (%s already "
			    "exists)\n"), gname, ngname);
		else
			(void) fprintf(stderr,
			    gettext("failed to rename '%s' (%s)\n"), gname,
			    smb_lgrp_strerror(status));
	} else {
		(void) printf(gettext("'%s' renamed to '%s'\n"), gname, ngname);
	}

	return (status);
}

/*
 * smbadm_group_setprop
 *
 * Set the group properties.
 */
static int
smbadm_group_setprop(int argc, char **argv)
{
	char *gname = NULL;
	smbadm_prop_t props[SMBADM_NPROP];
	smbadm_prop_handle_t *phandle;
	int option;
	int pcnt = 0;
	int ret;
	int p;

	bzero(props, SMBADM_NPROP * sizeof (smbadm_prop_t));

	while ((option = getopt(argc, argv, "p:")) != -1) {
		switch (option) {
		case 'p':
			if (pcnt >= SMBADM_NPROP) {
				(void) fprintf(stderr,
				    gettext("exceeded number of supported"
				    " properties\n"));
				smbadm_usage(B_FALSE);
			}

			if (smbadm_prop_parse(optarg, &props[pcnt++]) != 0)
				smbadm_usage(B_FALSE);
			break;

		default:
			smbadm_usage(B_FALSE);
		}
	}

	if (pcnt == 0) {
		(void) fprintf(stderr,
		    gettext("missing property=value argument\n"));
		smbadm_usage(B_FALSE);
	}

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == '\0') {
		(void) fprintf(stderr, gettext("missing group name\n"));
		smbadm_usage(B_FALSE);
	}

	for (p = 0; p < pcnt; p++) {
		phandle = smbadm_prop_gethandle(props[p].p_name);
		if (phandle) {
			if (phandle->p_setfn(gname, &props[p]) != 0)
				ret = 1;
		}
	}

	return (ret);
}

/*
 * smbadm_group_getprop
 *
 * Get the group properties.
 */
static int
smbadm_group_getprop(int argc, char **argv)
{
	char *gname = NULL;
	smbadm_prop_t props[SMBADM_NPROP];
	smbadm_prop_handle_t *phandle;
	int option;
	int pcnt = 0;
	int ret;
	int p;

	bzero(props, SMBADM_NPROP * sizeof (smbadm_prop_t));

	while ((option = getopt(argc, argv, "p:")) != -1) {
		switch (option) {
		case 'p':
			if (pcnt >= SMBADM_NPROP) {
				(void) fprintf(stderr,
				    gettext("exceeded number of supported"
				    " properties\n"));
				smbadm_usage(B_FALSE);
			}

			if (smbadm_prop_parse(optarg, &props[pcnt++]) != 0)
				smbadm_usage(B_FALSE);
			break;

		default:
			smbadm_usage(B_FALSE);
		}
	}

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == '\0') {
		(void) fprintf(stderr, gettext("missing group name\n"));
		smbadm_usage(B_FALSE);
	}

	if (pcnt == 0) {
		/*
		 * If no property has be specified then get
		 * all the properties.
		 */
		pcnt = SMBADM_NPROP;
		for (p = 0; p < pcnt; p++)
			props[p].p_name = smbadm_ptable[p].p_name;
	}

	for (p = 0; p < pcnt; p++) {
		phandle = smbadm_prop_gethandle(props[p].p_name);
		if (phandle) {
			if (phandle->p_getfn(gname, &props[p]) != 0)
				ret = 1;
		}
	}

	return (ret);
}

/*
 * smbadm_group_addmember
 *
 */
static int
smbadm_group_addmember(int argc, char **argv)
{
	lsa_account_t	acct;
	char *gname = NULL;
	char **mname;
	int option;
	smb_gsid_t msid;
	int status;
	int mcnt = 0;
	int ret = 0;
	int i;


	mname = (char **)malloc(argc * sizeof (char *));
	if (mname == NULL) {
		warn(gettext("failed to add group member"));
		return (1);
	}
	bzero(mname, argc * sizeof (char *));

	while ((option = getopt(argc, argv, "m:")) != -1) {
		switch (option) {
		case 'm':
			mname[mcnt++] = optarg;
			break;

		default:
			free(mname);
			smbadm_usage(B_FALSE);
		}
	}

	if (mcnt == 0) {
		(void) fprintf(stderr, gettext("missing member name\n"));
		free(mname);
		smbadm_usage(B_FALSE);
	}

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == 0) {
		(void) fprintf(stderr, gettext("missing group name\n"));
		free(mname);
		smbadm_usage(B_FALSE);
	}


	for (i = 0; i < mcnt; i++) {
		ret = 0;
		if (mname[i] == NULL)
			continue;

		ret = smb_lookup_name(mname[i], SidTypeUnknown, &acct);
		if ((ret != 0) || (acct.a_status != NT_STATUS_SUCCESS)) {
			(void) fprintf(stderr,
			    gettext("failed to add %s: unable to obtain SID\n"),
			    mname[i]);
			continue;
		}

		msid.gs_type = acct.a_sidtype;

		if ((msid.gs_sid = smb_sid_fromstr(acct.a_sid)) == NULL) {
			(void) fprintf(stderr,
			    gettext("failed to add %s: no memory\n"), mname[i]);
			continue;
		}

		status = smb_lgrp_add_member(gname, msid.gs_sid, msid.gs_type);
		smb_sid_free(msid.gs_sid);
		if (status != SMB_LGRP_SUCCESS) {
			(void) fprintf(stderr,
			    gettext("failed to add %s (%s)\n"),
			    mname[i], smb_lgrp_strerror(status));
			ret = 1;
		} else {
			(void) printf(gettext("'%s' is now a member of '%s'\n"),
			    mname[i], gname);
		}
	}

	free(mname);
	return (ret);
}

/*
 * smbadm_group_delmember
 */
static int
smbadm_group_delmember(int argc, char **argv)
{
	lsa_account_t	acct;
	char *gname = NULL;
	char **mname;
	int option;
	smb_gsid_t msid;
	int status;
	int mcnt = 0;
	int ret = 0;
	int i;

	mname = (char **)malloc(argc * sizeof (char *));
	if (mname == NULL) {
		warn(gettext("failed to delete group member"));
		return (1);
	}
	bzero(mname, argc * sizeof (char *));

	while ((option = getopt(argc, argv, "m:")) != -1) {
		switch (option) {
		case 'm':
			mname[mcnt++] = optarg;
			break;

		default:
			free(mname);
			smbadm_usage(B_FALSE);
		}
	}

	if (mcnt == 0) {
		(void) fprintf(stderr, gettext("missing member name\n"));
		free(mname);
		smbadm_usage(B_FALSE);
	}

	gname = argv[optind];
	if (optind >= argc || gname == NULL || *gname == 0) {
		(void) fprintf(stderr, gettext("missing group name\n"));
		free(mname);
		smbadm_usage(B_FALSE);
	}


	for (i = 0; i < mcnt; i++) {
		ret = 0;
		if (mname[i] == NULL)
			continue;

		ret = smb_lookup_name(mname[i], SidTypeUnknown, &acct);
		if ((ret != 0) || (acct.a_status != NT_STATUS_SUCCESS)) {
			(void) fprintf(stderr,
			    gettext("failed to remove %s: "
			    "unable to obtain SID\n"),
			    mname[i]);
			continue;
		}

		msid.gs_type = acct.a_sidtype;

		if ((msid.gs_sid = smb_sid_fromstr(acct.a_sid)) == NULL) {
			(void) fprintf(stderr,
			    gettext("failed to remove %s: no memory\n"),
			    mname[i]);
			continue;
		}

		status = smb_lgrp_del_member(gname, msid.gs_sid, msid.gs_type);
		smb_sid_free(msid.gs_sid);
		if (status != SMB_LGRP_SUCCESS) {
			(void) fprintf(stderr,
			    gettext("failed to remove %s (%s)\n"),
			    mname[i], smb_lgrp_strerror(status));
			ret = 1;
		} else {
			(void) printf(
			    gettext("'%s' has been removed from %s\n"),
			    mname[i], gname);
		}
	}

	return (ret);
}

static int
smbadm_user_disable(int argc, char **argv)
{
	int error;
	char *user = NULL;

	user = argv[optind];
	if (optind >= argc || user == NULL || *user == '\0') {
		(void) fprintf(stderr, gettext("missing user name\n"));
		smbadm_usage(B_FALSE);
	}

	error = smb_pwd_setcntl(user, SMB_PWC_DISABLE);
	if (error == SMB_PWE_SUCCESS)
		(void) printf(gettext("%s is disabled.\n"), user);
	else
		(void) fprintf(stderr, "%s\n", smbadm_pwd_strerror(error));

	return (error);
}

static int
smbadm_user_enable(int argc, char **argv)
{
	int error;
	char *user = NULL;

	user = argv[optind];
	if (optind >= argc || user == NULL || *user == '\0') {
		(void) fprintf(stderr, gettext("missing user name\n"));
		smbadm_usage(B_FALSE);
	}

	error = smb_pwd_setcntl(user, SMB_PWC_ENABLE);
	if (error == SMB_PWE_SUCCESS)
		(void) printf(gettext("%s is enabled.\n"), user);
	else
		(void) fprintf(stderr, "%s\n", smbadm_pwd_strerror(error));

	return (error);
}

/*
 * Format the time as 'N days HH:MM:SS'.
 */
static void
smbadm_format_time(uint32_t time, char *buf, int len)
{
	int days, hrs, mins, secs;

	days = time / (60*60*24);
	time %= (60*60*24);
	hrs = time / (60*60);
	time %= (60*60);
	mins = time / 60;
	time %= (60);
	secs = time;

	if (days > 0)
		(void) snprintf(buf, len,
		    "%d day%s %d:%d:%d", days, days > 1 ? "s" : "",
		    hrs, mins, secs);
	else
		(void) snprintf(buf, len, "%d:%d:%d",
		    hrs, mins, secs);
}

/*
 * Gets the solaris username and the domain name. The domain name is retrieved
 * from the SMF configuration. This will be the default, if the username and
 * domain are not provided from the command line.
 * The user will be prompted for a password.
 */
static int
smbadm_getuserinfo_default(smbadm_svc_info_t *enuminfo)
{
	struct passwd *pw;
	char	*p, *prompt;

	pw = getpwuid(getuid());
	if (pw == NULL)
		return (-1);

	(void) strlcpy(enuminfo->ss_username, pw->pw_name, MAXNAMELEN);
	smb_config_getdomaininfo(enuminfo->ss_domain, NULL, NULL, NULL, NULL);

	if (smb_name_validate_domain(enuminfo->ss_domain) != ERROR_SUCCESS) {
		(void) fprintf(stderr, gettext("invalid domain name\n"));
		return (-1);
	}

	prompt = gettext("Enter password: ");

	if ((p = getpassphrase(prompt)) == NULL) {
		(void) fprintf(stderr, gettext("missing password\n"));
		return (-1);
	}
	(void) strlcpy(enuminfo->ss_passwd, p, SMB_PASSWD_MAXLEN + 1);

	return (0);
}

/*
 * Process an account name into its constituent components.
 *
 *	name@domain
 *	domain\name[+password]
 *	domain/name[+password]
 *
 * The user will be prompted for a password if one is not provided
 * in optarg.
 */
static int
smbadm_getuserinfo(const char *optarg, smbadm_svc_info_t *enuminfo)
{
	char	buf[MAXNAMELEN * 2];
	char	*domain = NULL;
	char	*username = NULL;
	char	*prompt;
	char	*p;

	(void) strlcpy(buf, optarg, sizeof (buf));
	smb_name_parse(buf, &username, &domain);

	if ((username == NULL) || (*username == '\0')) {
		(void) fprintf(stderr, gettext("missing username\n"));
		return (-1);
	}

	(void) strlcpy(enuminfo->ss_domain, domain, MAXHOSTNAMELEN);
	(void) strlcpy(enuminfo->ss_username, username, MAXNAMELEN);

	if ((p = strchr(enuminfo->ss_username, '+')) != NULL) {
		*p = '\0';
		++p;
		(void) strlcpy(enuminfo->ss_passwd, p, SMB_PASSWD_MAXLEN + 1);
	}

	if (smb_name_validate_domain(domain) != ERROR_SUCCESS) {
		(void) fprintf(stderr, gettext("invalid domain name\n"));
		return (-1);
	}

	if (smb_name_validate_account(enuminfo->ss_username) != ERROR_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("username contains invalid characters\n"));
		return (-1);
	}

	if (*enuminfo->ss_passwd == '\0') {
		prompt = gettext("Enter password: ");

		if ((p = getpassphrase(prompt)) == NULL) {
			(void) fprintf(stderr, gettext("missing password\n"));
			return (-1);
		}

		(void) strlcpy(enuminfo->ss_passwd, p, SMB_PASSWD_MAXLEN + 1);
	}

	return (0);
}

/*
 * Parse the options for the Server Service enumerate commands.
 */
static void
smbadm_srvsvc_show_getopt(int argc, char **argv, uint32_t infotype,
    smbadm_svc_info_t *enuminfo)
{
	char	*server;
	char	qualifier[MAXNAMELEN];
	char	*p;
	int	option;
	struct	hostent *h;
	int	err;

	bzero(enuminfo, sizeof (smbadm_svc_info_t));
	while ((option = getopt(argc, argv, "tAu:c:s:")) != -1) {
		switch (option) {
		case 'u':
			if (enuminfo->ss_username[0] != '\0') {
				(void) fprintf(stderr,
				    gettext("-u must only appear once\n"));
				smbadm_usage(B_FALSE);
			}

			if (smbadm_getuserinfo(optarg, enuminfo) != 0)
				smbadm_usage(B_FALSE);
			break;
		case 'c':
			if (infotype != SMB_SVCENUM_TYPE_TREE)
				smbadm_usage(B_FALSE);

			if (enuminfo->ss_qual_computer) {
				(void) fprintf(stderr,
				    gettext("-c must only appear once\n"));
				smbadm_usage(B_FALSE);
			}

			(void) strlcpy(qualifier, optarg, MAXNAMELEN);
			enuminfo->ss_qual_computer = B_TRUE;
			break;
		case 's':
			if (infotype != SMB_SVCENUM_TYPE_TREE)
				smbadm_usage(B_FALSE);

			if (enuminfo->ss_qual_share) {
				(void) fprintf(stderr,
				    gettext("-s must only appear once\n"));
				smbadm_usage(B_FALSE);
			}

			(void) strlcpy(qualifier, optarg, MAXNAMELEN);
			enuminfo->ss_qual_share = B_TRUE;
			break;
		case 't':
			enuminfo->ss_showtitle = B_TRUE;
			break;
		case 'A':
			enuminfo->ss_anon_connect = B_TRUE;
			break;
		default:
			smbadm_usage(B_FALSE);
			break;
		}
	}

	if (enuminfo->ss_anon_connect && enuminfo->ss_username[0] != '\0') {
		(void) fprintf(stderr,
		    gettext("-A and -u are mutually exclusive\n"));
		smbadm_usage(B_FALSE);
	}

	if (!enuminfo->ss_anon_connect && enuminfo->ss_username[0] == '\0') {
		if (smbadm_getuserinfo_default(enuminfo) != 0) {
			(void) fprintf(stderr,
			    gettext("cannot get user information\n"));
			smbadm_usage(B_FALSE);
		}
	}

	if (infotype == SMB_SVCENUM_TYPE_TREE) {
		if (enuminfo->ss_qual_computer && enuminfo->ss_qual_share) {
			(void) fprintf(stderr,
			    gettext("-c and -s are mutually exclusive\n"));
			smbadm_usage(B_FALSE);
		}

		/*
		 * If a qualifier has not been specified,
		 * assume the request is for this computer.
		 */
		if (!enuminfo->ss_qual_computer && !enuminfo->ss_qual_share) {
			enuminfo->ss_qual_computer = B_TRUE;
			(void) gethostname(qualifier, MAXNAMELEN);
		}

		if (qualifier[0] == '\0') {
			(void) fprintf(stderr,
			    gettext("missing qualifier\n"));
			smbadm_usage(B_FALSE);
		}

		if (enuminfo->ss_qual_computer) {
			p = qualifier;
			p += strspn(p, "/\\");

			if (*p == '\0') {
				(void) fprintf(stderr,
				    gettext("missing qualifier\n"));
				smbadm_usage(B_FALSE);
			}

			(void) snprintf(enuminfo->ss_qualifier, MAXNAMELEN,
			    "\\\\%s", p);
		} else {
			(void) snprintf(enuminfo->ss_qualifier, MAXNAMELEN,
			    "%s", qualifier);
		}
	}

	server = argv[optind];
	if ((++optind) != argc || server == NULL || *server == '\0') {
		(void) fprintf(stderr, gettext("invalid parameter\n"));
		smbadm_usage(B_FALSE);
	}

	server += strspn(server, "/\\");
	if (*server == '\0') {
		(void) fprintf(stderr, gettext("missing server name\n"));
		smbadm_usage(B_FALSE);
	}

	if ((h = smb_gethostbyaddr(server, &err)) != NULL) {
		(void) strlcpy(enuminfo->ss_server, h->h_name, MAXHOSTNAMELEN);
		freehostent(h);
	} else {
		(void) strlcpy(enuminfo->ss_server, server, MAXHOSTNAMELEN);
	}
}

/*
 * Initialize the services and set the IPC username and password.
 */
static int
smbadm_ipc_init(smbadm_svc_info_t *enuminfo)
{
	uint8_t	passwd_hash[SMBAUTH_HASH_SZ];
	int	rc;

	ntsvcs_init();

	rc = smb_auth_ntlm_hash(enuminfo->ss_passwd, passwd_hash);
	if (rc != SMBAUTH_SUCCESS) {
		(void) fprintf(stderr, "initialization failed for '%s'\n",
		    enuminfo->ss_username);
		ntsvcs_fini();
		return (-1);
	}

	smb_ipc_set(enuminfo->ss_username, passwd_hash);
	return (0);
}

/*
 * Finalize the services and roll back the IPC username.
 */
static void
smbadm_ipc_fini(void)
{
	smb_ipc_rollback();
	ntsvcs_fini();
}

/*
 * Display the list of shares on the specified server.
 */
static int
smbadm_shares_show(int argc, char **argv)
{
	srvsvc_list_t		sl;
	srvsvc_info_t		*info;
	srvsvc_share_info_t	*si;
	smbadm_svc_info_t	enuminfo;
	int			rc;

	smbadm_srvsvc_show_getopt(argc, argv,
	    SMB_SVCENUM_TYPE_SHARE, &enuminfo);

	if ((rc = smbadm_ipc_init(&enuminfo)) != 0)
		return (rc);

	srvsvc_net_enum_init(&sl);

	rc = srvsvc_net_share_enum(enuminfo.ss_server,
	    enuminfo.ss_domain, 1, &sl);
	if (rc == 0) {
		info = list_head(&sl.sl_list);
		if ((info != NULL) && enuminfo.ss_showtitle)
			(void) printf(gettext(SMBADM_FMT_SHARES),
			    "SHARE", "DESCRIPTION");

		while (info != NULL) {
			si = &info->l_list.ul_shr;
			(void) printf(SMBADM_FMT_SHARES,
			    si->si_netname, si->si_comment);
			info = list_next(&sl.sl_list, info);
		}

		(void) printf(gettext("%d shares (total=%d, read=%d)\n"),
		    sl.sl_count, sl.sl_totalentries, sl.sl_entriesread);
	}

	srvsvc_net_enum_fini(&sl);
	smbadm_ipc_fini();
	return ((rc == 0) ? 0 : -1);
}

/*
 * Display the files open over SMB on the specified server.
 */
static int
smbadm_files_show(int argc, char **argv)
{
	srvsvc_list_t		sl;
	srvsvc_file_info_t	*si;
	srvsvc_info_t		*info;
	char			*fperm;
	smbadm_svc_info_t	enuminfo;
	int			rc;

	smbadm_srvsvc_show_getopt(argc, argv, SMB_SVCENUM_TYPE_FILE, &enuminfo);

	if ((rc = smbadm_ipc_init(&enuminfo)) != 0)
		return (rc);

	srvsvc_net_enum_init(&sl);

	rc = srvsvc_net_files_enum(enuminfo.ss_server, enuminfo.ss_domain,
	    NULL, NULL, 3, &sl);
	if (rc == 0) {
		info = list_head(&sl.sl_list);
		if ((info != NULL) && enuminfo.ss_showtitle)
			(void) printf(gettext(SMBADM_FMT_FILES),
			    "FILES", "USERS", "MODE");

		while (info != NULL) {
			si = &info->l_list.ul_file;
			switch (si->fi_permissions) {
			case FILE_READ_DATA:
				fperm = "Read";
				break;
			case FILE_WRITE_DATA:
				fperm = "Write";
				break;
			case (FILE_WRITE_DATA | FILE_READ_DATA | FILE_EXECUTE):
				fperm = "Read+Write";
				break;
			default:
				fperm = "Read";
			}

			(void) printf(SMBADM_FMT_FILES,
			    si->fi_path, si->fi_username, fperm);
			info = list_next(&sl.sl_list, info);
		}

		(void) printf(gettext("%d open files (total=%d, read=%d)\n"),
		    sl.sl_count, sl.sl_totalentries, sl.sl_entriesread);
	}

	srvsvc_net_enum_fini(&sl);
	smbadm_ipc_fini();
	return ((rc == 0) ? 0 : -1);
}

/*
 * Display the SMB tree connections on the specified server.
 */
static int
smbadm_connections_show(int argc, char **argv)
{
	srvsvc_list_t		sl;
	srvsvc_connect_info_t	*si;
	srvsvc_info_t		*info;
	smbadm_svc_info_t	enuminfo;
	char			timebuf[SMBADM_TIMEBUFSIZ];
	int			rc;

	smbadm_srvsvc_show_getopt(argc, argv, SMB_SVCENUM_TYPE_TREE, &enuminfo);

	if ((rc = smbadm_ipc_init(&enuminfo)) != 0)
		return (rc);

	srvsvc_net_enum_init(&sl);

	rc = srvsvc_net_connect_enum(enuminfo.ss_server, enuminfo.ss_domain,
	    enuminfo.ss_qualifier, 1, &sl);
	if (rc == 0) {
		info = list_head(&sl.sl_list);
		if ((info != NULL) && enuminfo.ss_showtitle)
			(void) printf(gettext(SMBADM_FMT_CONNECT_TITLE),
			    "USERS", "RESOURCE", "OPEN", "TIME");

		while (info != NULL) {
			si = &info->l_list.ul_connection;
			smbadm_format_time(si->ci_time,
			    timebuf, SMBADM_TIMEBUFSIZ);
			(void) printf(SMBADM_FMT_CONNECT,
			    si->ci_username, si->ci_share, si->ci_numopens,
			    timebuf);
			info = list_next(&sl.sl_list, info);
		}

		(void) printf(gettext("%d connections (total=%d, read=%d)\n"),
		    sl.sl_count, sl.sl_totalentries, sl.sl_entriesread);
	}

	srvsvc_net_enum_fini(&sl);
	smbadm_ipc_fini();
	return ((rc == 0) ? 0 : -1);
}

/*
 * Display the SMB user sessions on the specified server.
 */
static int
smbadm_sessions_show(int argc, char **argv)
{
	srvsvc_list_t		sl;
	srvsvc_session_info_t	*si;
	srvsvc_info_t		*info;
	smbadm_svc_info_t	enuminfo;
	char			timebuf[SMBADM_TIMEBUFSIZ];
	boolean_t		is_guest = B_FALSE;
	int			rc;

	smbadm_srvsvc_show_getopt(argc, argv, SMB_SVCENUM_TYPE_USER, &enuminfo);

	if ((rc = smbadm_ipc_init(&enuminfo)) != 0)
		return (rc);

	srvsvc_net_enum_init(&sl);

	rc = srvsvc_net_session_enum(enuminfo.ss_server, enuminfo.ss_domain,
	    NULL, NULL, 1, &sl);
	if (rc == 0) {
		info = list_head(&sl.sl_list);
		if ((info != NULL) && enuminfo.ss_showtitle)
			(void) printf(gettext(SMBADM_FMT_SESSIONS_TITLE),
			    "USERS", "CLIENT", "OPEN", "GUEST", "TIME");

		while (info != NULL) {
			si = &info->l_list.ul_session;
			is_guest = (si->ui_flags & SMB_ATF_GUEST) ?
			    B_TRUE : B_FALSE;
			smbadm_format_time(si->ui_logon_time,
			    timebuf, SMBADM_TIMEBUFSIZ);

			(void) printf(SMBADM_FMT_SESSIONS,
			    si->ui_account, si->ui_workstation, si->ui_numopens,
			    (is_guest) ? "Yes" : "No", timebuf);
			info = list_next(&sl.sl_list, info);
		}

		(void) printf(gettext("%d sessions (total=%d, read=%d)\n"),
		    sl.sl_count, sl.sl_totalentries, sl.sl_entriesread);
	}

	srvsvc_net_enum_fini(&sl);
	smbadm_ipc_fini();
	return ((rc == 0) ? 0 : -1);
}

/*
 * Display the list of shares on the specified server.
 */
static int
smbadm_user_lookup(int argc, char **argv)
{
	smb_account_t		acct;
	char			*name;
	char			buf[MAXNAMELEN];
	smb_sid_t		*sid;
	uint32_t		status;
	smbadm_svc_info_t	userinfo;
	int			option;

	bzero(&userinfo, sizeof (smbadm_svc_info_t));
	while ((option = getopt(argc, argv, "u:")) != -1) {
		switch (option) {
		case 'u':
			if (userinfo.ss_username[0] != '\0') {
				(void) fprintf(stderr,
				    gettext("-u must only appear once\n"));
				smbadm_usage(B_FALSE);
			}

			if (smbadm_getuserinfo(optarg, &userinfo) != 0)
				smbadm_usage(B_FALSE);
			break;
		default:
			break;
		}
	}

	name = argv[optind];
	if ((++optind) != argc || name == NULL || *name == '\0') {
		(void) fprintf(stderr, gettext("missing name/SID\n"));
		smbadm_usage(B_FALSE);
	}

	if (smbadm_ipc_init(&userinfo) != 0)
		return (-1);

	if (strncmp(name, "S-1-", 4) == 0) {
		sid = smb_sid_fromstr(name);
		status = lsa_lookup_sid(sid, &acct);
		smb_sid_free(sid);

		if (status == NT_STATUS_SUCCESS) {
			if (strchr(acct.a_domain, '.') != NULL)
				(void) snprintf(buf, MAXNAMELEN, "%s@%s",
				    acct.a_name, acct.a_domain);
			else
				(void) snprintf(buf, MAXNAMELEN, "%s\\%s",
				    acct.a_domain, acct.a_name);
		}
	} else {
		status = lsa_lookup_name(name, SidTypeUnknown, &acct);
		if (status == NT_STATUS_SUCCESS)
			smb_sid_tostr(acct.a_sid, buf);
	}

	if (status == NT_STATUS_SUCCESS) {
		(void) printf("%s: %s\n", name, buf);
		smb_account_free(&acct);
	} else {
		(void) fprintf(stderr, gettext("LsaLookup failed: %s\n"),
		    xlate_nt_status(status));
	}

	smbadm_ipc_fini();
	return (0);
}

/*
 * Creates a hash of a password.
 */
static int
smbadm_crypt(int argc, char **argv)
{
	char *cp, *psw;

	if (argc < 2)
		psw = getpassphrase(gettext("Password:"));
	else
		psw = argv[1];

	/*
	 * smb_simplecrypt() requires buffer that is twice the size of
	 * given password + 3 bytes for "$$1" + NULL terminator
	 */
	cp = malloc(4 + 2 * strlen(psw));
	if (cp == NULL)
		errx(SMBADM_EXIT_ERROR, gettext("out of memory"));
	smb_simplecrypt(cp, psw);
	(void) printf("%s\n", cp);
	free(cp);
	return (0);
}

/*
 * Adds password information to the password keychain and
 * /var/smb/smbfspasswd.
 */
static int
smbadm_key_add(int argc, char **argv)
{
	static char prompt[SMBADM_PROMPTBUFSIZ];
	char pwbuf[SMBADM_PWDBUFSIZ];
	char buf[SMBADM_PWDBUFSIZ];
	char tmp_arg[SMBADM_ARGBUFSIZ];
	char def_dom[SMBIOC_MAX_NAME];
	char def_usr[SMBIOC_MAX_NAME];
	char *dom, *usr, *pass, *p;
	int err, opt;

	if (argc == 2 || argc > 3)
		smbadm_usage(B_FALSE);

	dom = usr = pass = NULL;
	while ((opt = getopt(argc, argv, "u:")) != EOF) {
		switch (opt) {

		case 'u':
			(void) strlcpy(tmp_arg, optarg, sizeof (tmp_arg));
			err = smb_ctx_parsedomuser(tmp_arg, &dom, &usr);
			if (err)
				errx(SMBADM_EXIT_ERROR,
				    gettext("failed to parse %s"), optarg);
			break;

		default:
			smbadm_usage(B_FALSE);
			break;
		}
	}

	if (optind != argc)
		smbadm_usage(B_FALSE);

	if (dom == NULL || usr == NULL) {
		err = smbfs_default_dom_usr(def_dom, sizeof (def_dom),
		    def_usr, sizeof (def_usr));
		if (err)
			errx(SMBADM_EXIT_ERROR,
			    gettext("failed to get defaults"));
	}
	if (dom == NULL)
		dom = def_dom;
	else
		(void) nls_str_upper(dom, dom);
	if (usr == NULL)
		usr = def_usr;

	if (isatty(STDIN_FILENO)) {
		(void) snprintf(prompt, sizeof (prompt),
		    gettext("Password for %s/%s:"), dom, usr);
		pass = getpassphrase(prompt);
	} else {
		pass = fgets(pwbuf, sizeof (pwbuf), stdin);
		if (pass != NULL) {
			if ((p = strchr(pwbuf, '\n')) != NULL)
				*p = '\0';
			if (strncmp(pass, "$$1", 3) == 0) {
				(void) smb_simpledecrypt(buf, pass);
				pass = buf;
			}
		}
	}

	err = smbfs_keychain_add((uid_t)-1, dom, usr, pass);
	if (err)
		errx(SMBADM_EXIT_ERROR,
		    gettext("failed to add keychain entry"));

	return (0);
}

/*
 * Removes password information from the password keychain and
 * /var/smb/smbfspasswd.
 */
static int
smbadm_key_remove(int argc, char **argv)
{
	char *dom, *usr;
	char tmp_arg[SMBADM_ARGBUFSIZ];
	char def_dom[SMBIOC_MAX_NAME];
	char def_usr[SMBIOC_MAX_NAME];
	int err, opt;

	if (argc == 2 || argc > 3)
		smbadm_usage(B_FALSE);

	if (argc == 1) {
		err = smbfs_keychain_del_owner();
		if (err)
			errx(SMBADM_EXIT_ERROR,
			    gettext("failed to delete keychain entries"));
		return (0);
	}

	dom = usr = NULL;
	while ((opt = getopt(argc, argv, "u:")) != EOF) {
		switch (opt) {

		case 'u':
			(void) strlcpy(tmp_arg, optarg, sizeof (tmp_arg));
			err = smb_ctx_parsedomuser(tmp_arg, &dom, &usr);
			if (err)
				errx(SMBADM_EXIT_ERROR,
				    gettext("failed to parse %s"), optarg);
			break;
		default:
			smbadm_usage(B_FALSE);
			break;
		}
	}

	if (optind != argc)
		smbadm_usage(B_FALSE);

	if (dom == NULL || usr == NULL) {
		err = smbfs_default_dom_usr(def_dom, sizeof (def_dom),
		    def_usr, sizeof (def_usr));
		if (err)
			errx(SMBADM_EXIT_ERROR,
			    gettext("failed to get defaults"));
	}
	if (dom == NULL)
		dom = def_dom;
	else
		(void) nls_str_upper(dom, dom);
	if (usr == NULL)
		usr = def_usr;

	err = smbfs_keychain_del((uid_t)-1, dom, usr);
	if (err)
		errx(SMBADM_EXIT_ERROR,
		    gettext("failed to delete keychain entry"));

	return (0);
}

/*
 * Displays NetBIOS domain, NetBIOS name, and IP address for
 * NetBIOS or DNS host.  An IP address can be used.
 */
static int
smbadm_server_lookup(int argc, char **argv)
{
	struct nb_ctx *ctx;
	struct sockaddr *sap;
	struct in_addr ina;
	char *hostname;
	char servername[NB_NAMELEN];
	char workgroupname[NB_NAMELEN];
	int error, opt;

	if (argc < 2)
		smbadm_usage(B_FALSE);
	error = nb_ctx_create(&ctx);
	if (error) {
		smb_error(gettext("unable to create nbcontext"), error);
		exit(SMBADM_EXIT_ERROR);
	}
	if (nb_ctx_getconfigs(ctx) != 0)
		exit(SMBADM_EXIT_ERROR);
	if ((ctx->nb_flags & NBCF_NS_ENABLE) == 0) {
		(void) fprintf(stderr,
		    gettext("nbns_enable=false, cannot do lookup\n"));
		exit(SMBADM_EXIT_ERROR);
	}
	while ((opt = getopt(argc, argv, "w:")) != EOF) {
		switch (opt) {
		case 'w':
			(void) nb_ctx_setns(ctx, optarg);
			break;
		default:
			smbadm_usage(B_FALSE);
			/*NOTREACHED*/
		}
	}
	if (optind >= argc)
		smbadm_usage(B_FALSE);
	if (nb_ctx_resolve(ctx) != 0)
		exit(SMBADM_EXIT_ERROR);
	hostname = argv[argc - 1];

	if (hostname[0] == '\\') {
		if (hostname[1] != '\\')
			smbadm_usage(B_FALSE);
	} else if (hostname[0] != '/' || hostname[1] != '/') {
		smbadm_usage(B_FALSE);
	}

	hostname += 2;

	if (hostname[0] == '\0')
		smbadm_usage(B_FALSE);

	/*
	 * First, try to resolve via name service
	 */
	error = nb_resolvehost_in(hostname, &ina);
	if (error != 0) {
		/*
		 * Then try to resolve via NetBIOS lookup
		 */
		error = nbns_resolvename(hostname, ctx, &sap);
		if (error) {
			(void) printf(gettext("unable to resolve %s\n"),
			    hostname);
			exit(SMBADM_EXIT_ERROR);
		}
		/*LINTED E_BAD_PTR_CAST_ALIGN*/
		ina = ((struct sockaddr_in *)sap)->sin_addr;
	}

	servername[0] = (char)0;
	workgroupname[0] = (char)0;
	error = nbns_getnodestatus(ctx, &ina, servername, workgroupname);
	if (error) {
		smb_error(
		    gettext("unable to get status from %s"), error, hostname);
		exit(SMBADM_EXIT_ERROR);
	}

	if (workgroupname[0])
		(void) printf(gettext("Workgroup: %s\n"), workgroupname);

	if (servername[0])
		(void) printf(gettext("Server: %s\n"), servername);

	(void) printf(gettext("IP address: %s\n"), inet_ntoa(ina));

	return (0);
}

/*
 * Prints a file or data from standard input.
 */
static int
smbadm_print(int argc, char **argv)
{
	char *username = NULL, *share_unc, *filename;
	int opt;

	/* last arg is the print file. */
	if (argc < 3)
		smbadm_usage(B_FALSE);

	while ((opt = getopt(argc, argv, "u:")) != EOF) {
		switch (opt) {
		case 'u':
			username = optarg;
			break;

		default:
			smbadm_usage(B_FALSE);
			break;
		}
	}

	if (optind != argc-2)
		smbadm_usage(B_FALSE);

	share_unc = argv[argc-2];
	filename = argv[argc-1];

	return (smbfs_print(username, share_unc, filename));
}

int
main(int argc, char **argv)
{
	int ret;
	int i;
	char *cmdname;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	(void) malloc(0);	/* satisfy libumem dependency */

	progname = basename(argv[0]);

	if (getzoneid() != GLOBAL_ZONEID) {
		(void) fprintf(stderr,
		    gettext("cannot execute in non-global zone\n"));
		return (0);
	}

	if (is_system_labeled()) {
		(void) fprintf(stderr,
		    gettext("Trusted Extensions not supported\n"));
		return (0);
	}

	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		smbadm_usage(B_FALSE);
	}

	/*
	 * Special case "cmd --help/-?"
	 */
	if (strcmp(argv[1], "-?") == 0 ||
	    strcmp(argv[1], "--help") == 0 ||
	    strcmp(argv[1], "-h") == 0)
		smbadm_usage(B_TRUE);

	/*
	 * Special case "list" alias for "show-domains"
	 */
	if (strcasecmp(argv[1], "list") == 0)
		cmdname = "show-domains";
	else
		cmdname = argv[1];

	for (i = 0; i < SMBADM_NCMD; ++i) {
		curcmd = &smbadm_cmdtable[i];
		if (strcasecmp(cmdname, curcmd->name) == 0) {
			if (argc > 2) {
				/* cmd subcmd --help/-? */
				if (strcmp(argv[2], "-?") == 0 ||
				    strcmp(argv[2], "--help") == 0 ||
				    strcmp(argv[2], "-h") == 0)
					smbadm_usage(B_TRUE);
			}

			if (!smbadm_checkauth(curcmd->auth)) {
				(void) fprintf(stderr,
				    gettext("%s: %s: authorization denied\n"),
				    progname, curcmd->name);
				return (1);
			}

			if ((ret = smbadm_init()) != 0)
				return (ret);

			ret = curcmd->func(argc - 1, &argv[1]);

			smbadm_fini();
			return (ret);
		}
	}

	curcmd = NULL;
	(void) fprintf(stderr, gettext("unknown subcommand (%s)\n"), argv[1]);
	smbadm_usage(B_FALSE);
	return (2);
}

static int
smbadm_init(void)
{
	int rc;

	switch (curcmd->flags & SMBADM_CMDF_TYPEMASK) {
	case SMBADM_CMDF_GROUP:
		if ((rc = smb_lgrp_start()) != SMB_LGRP_SUCCESS) {
			(void) fprintf(stderr,
			    gettext("failed to initialize (%s)\n"),
			    smb_lgrp_strerror(rc));
			return (1);
		}
		break;

	case SMBADM_CMDF_USER:
		smb_pwd_init(B_FALSE);
		break;

	default:
		break;
	}

	return (0);
}

static void
smbadm_fini(void)
{
	switch (curcmd->flags & SMBADM_CMDF_TYPEMASK) {
	case SMBADM_CMDF_GROUP:
		smb_lgrp_stop();
		break;

	case SMBADM_CMDF_USER:
		smb_pwd_fini();
		break;

	default:
		break;
	}
}

static boolean_t
smbadm_checkauth(const char *auth)
{
	struct passwd *pw;

	if (auth == NULL)
		return (B_TRUE);

	if ((pw = getpwuid(getuid())) == NULL)
		return (B_FALSE);

	if (chkauthattr(auth, pw->pw_name) == 0)
		return (B_FALSE);

	return (B_TRUE);
}

static boolean_t
smbadm_prop_validate(smbadm_prop_t *prop, boolean_t chkval)
{
	smbadm_prop_handle_t *pinfo;
	int i;

	for (i = 0; i < SMBADM_NPROP; i++) {
		pinfo = &smbadm_ptable[i];
		if (strcmp(pinfo->p_name, prop->p_name) == 0) {
			if (pinfo->p_chkfn && chkval)
				return (pinfo->p_chkfn(prop));
			return (B_TRUE);
		}
	}

	(void) fprintf(stderr, gettext("unrecognized property '%s'\n"),
	    prop->p_name);

	return (B_FALSE);
}

static int
smbadm_prop_parse(char *propname, smbadm_prop_t *prop)
{
	boolean_t parse_value;
	char *equal;

	if ((prop->p_name = propname) == NULL)
		return (2);

	if (strcmp(curcmd->name, "set-group") == 0) {
		equal = strchr(propname, '=');
		if (equal == NULL)
			return (2);

		*equal++ = '\0';
		prop->p_value = equal;
		parse_value = B_TRUE;
	} else {
		prop->p_value = NULL;
		parse_value = B_FALSE;
	}

	if (smbadm_prop_validate(prop, parse_value) == B_FALSE)
		return (2);

	return (0);
}

static smbadm_prop_handle_t *
smbadm_prop_gethandle(char *pname)
{
	int i;

	for (i = 0; i < SMBADM_NPROP; i++)
		if (strcmp(pname, smbadm_ptable[i].p_name) == 0)
			return (&smbadm_ptable[i]);

	return (NULL);
}

static int
smbadm_setprop_desc(char *gname, smbadm_prop_t *prop)
{
	int status;

	status = smb_lgrp_setcmnt(gname, prop->p_value);
	if (status != SMB_LGRP_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("failed to modify the group description (%s)\n"),
		    smb_lgrp_strerror(status));
		return (1);
	}

	(void) printf(gettext("%s: description modified\n"), gname);
	return (0);
}

static int
smbadm_getprop_desc(char *gname, smbadm_prop_t *prop)
{
	char *cmnt = NULL;
	int status;

	status = smb_lgrp_getcmnt(gname, &cmnt);
	if (status != SMB_LGRP_SUCCESS) {
		(void) fprintf(stderr,
		    gettext("failed to get the group description (%s)\n"),
		    smb_lgrp_strerror(status));
		return (1);
	}

	(void) printf(gettext("\t%s: %s\n"), prop->p_name, cmnt);
	free(cmnt);
	return (0);
}

static int
smbadm_group_setpriv(char *gname, uint8_t priv_id, smbadm_prop_t *prop)
{
	boolean_t enable;
	int status;
	int ret;

	if (strcasecmp(prop->p_value, "on") == 0) {
		(void) printf(gettext("Enabling %s privilege "), prop->p_name);
		enable = B_TRUE;
	} else {
		(void) printf(gettext("Disabling %s privilege "), prop->p_name);
		enable = B_FALSE;
	}

	status = smb_lgrp_setpriv(gname, priv_id, enable);
	if (status == SMB_LGRP_SUCCESS) {
		(void) printf(gettext("succeeded\n"));
		ret = 0;
	} else {
		(void) printf(gettext("failed: %s\n"),
		    smb_lgrp_strerror(status));
		ret = 1;
	}

	return (ret);
}

static int
smbadm_group_getpriv(char *gname, uint8_t priv_id, smbadm_prop_t *prop)
{
	boolean_t enable;
	int status;

	status = smb_lgrp_getpriv(gname, priv_id, &enable);
	if (status != SMB_LGRP_SUCCESS) {
		(void) fprintf(stderr, gettext("failed to get %s (%s)\n"),
		    prop->p_name, smb_lgrp_strerror(status));
		return (1);
	}

	(void) printf(gettext("\t%s: %s\n"), prop->p_name,
	    (enable) ? "On" : "Off");

	return (0);
}

static int
smbadm_setprop_tkowner(char *gname, smbadm_prop_t *prop)
{
	return (smbadm_group_setpriv(gname, SE_TAKE_OWNERSHIP_LUID, prop));
}

static int
smbadm_getprop_tkowner(char *gname, smbadm_prop_t *prop)
{
	return (smbadm_group_getpriv(gname, SE_TAKE_OWNERSHIP_LUID, prop));
}

static int
smbadm_setprop_backup(char *gname, smbadm_prop_t *prop)
{
	return (smbadm_group_setpriv(gname, SE_BACKUP_LUID, prop));
}

static int
smbadm_getprop_backup(char *gname, smbadm_prop_t *prop)
{
	return (smbadm_group_getpriv(gname, SE_BACKUP_LUID, prop));
}

static int
smbadm_setprop_restore(char *gname, smbadm_prop_t *prop)
{
	return (smbadm_group_setpriv(gname, SE_RESTORE_LUID, prop));
}

static int
smbadm_getprop_restore(char *gname, smbadm_prop_t *prop)
{
	return (smbadm_group_getpriv(gname, SE_RESTORE_LUID, prop));
}

static boolean_t
smbadm_chkprop_priv(smbadm_prop_t *prop)
{
	if (prop->p_value == NULL || *prop->p_value == '\0') {
		(void) fprintf(stderr,
		    gettext("missing value for '%s'\n"), prop->p_name);
		return (B_FALSE);
	}

	if (strcasecmp(prop->p_value, "on") == 0)
		return (B_TRUE);

	if (strcasecmp(prop->p_value, "off") == 0)
		return (B_TRUE);

	(void) fprintf(stderr,
	    gettext("%s: unrecognized value for '%s' property\n"),
	    prop->p_value, prop->p_name);

	return (B_FALSE);
}

static const char *
smbadm_pwd_strerror(int error)
{
	switch (error) {
	case SMB_PWE_SUCCESS:
		return (gettext("Success."));

	case SMB_PWE_USER_UNKNOWN:
		return (gettext("User does not exist."));

	case SMB_PWE_USER_DISABLE:
		return (gettext("User is disabled."));

	case SMB_PWE_CLOSE_FAILED:
	case SMB_PWE_OPEN_FAILED:
	case SMB_PWE_WRITE_FAILED:
	case SMB_PWE_UPDATE_FAILED:
		return (gettext("Unexpected failure. "
		    "SMB password database unchanged."));

	case SMB_PWE_STAT_FAILED:
		return (gettext("stat of SMB password file failed."));

	case SMB_PWE_BUSY:
		return (gettext("SMB password database busy. "
		    "Try again later."));

	case SMB_PWE_DENIED:
		return (gettext("Operation not permitted."));

	case SMB_PWE_SYSTEM_ERROR:
		return (gettext("System error."));

	default:
		break;
	}

	return (gettext("Unknown error code."));
}

/*
 * Enable libumem debugging by default on DEBUG builds.
 */
#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

/*
 * Set the fully-qualified domain name and domain controller
 * fields of the specified smb_joininfo_t instance.
 */
static int
smbadm_set_joininfo(const char *nb_domain, smb_joininfo_t *jdi)
{
	if (*nb_domain != '\0' &&
	    smb_get_fqdn(jdi->domain_name, sizeof (jdi->domain_name)) !=
	    NT_STATUS_SUCCESS) {
		(void) fprintf(stderr, gettext("Can't access "
		    "fully qualified domain name\n"));
		(void) printf(gettext("Please specify a fully "
		    "qualified domain name for %s"), nb_domain);
		return (1);
	}

	if (smb_get_dcinfo(jdi->dc, sizeof (jdi->dc)) != NT_STATUS_SUCCESS) {
		(void) fprintf(stderr, gettext(
		    "Can't access domain controller info\n"));
		return (1);
	}

	return (0);
}

/*
 * Configure Kerberos
 *
 * If krb5.conf doesn't exist, create one.  Otherwise, validate
 * the existing config file and programmatically recover from
 * any mis-configurations if possible.
 *
 * Aside from auto-recovery, the following will be updated in
 * an existing krb5.conf:
 *  - kpasswd server/admin server will be set to discovered DC
 *  - the list of KDC entries
 *  - default realm based on user's choice.
 *
 * On success, default realm name is returned via drealm argument.
 * Upon domain join failure, the caller should restore the default
 * realm to prevent any Kerberized service disruptions.
 */
static int
smbadm_krb_config(smb_joininfo_t *jdi, char *drealm, size_t len)
{
	smb_krb5_cfg_t	krbcfg;
	char		errmsg[SMB_LOG_LINE_SZ];
	char		**kdcs = NULL;
	errcode_t	err;

	bzero(drealm, len);
	kdcs = smb_ads_get_kdcs(jdi->domain_name);
	if (kdcs == NULL) {
		(void) fprintf(stderr, gettext(
		    "Failed to configure %s: can't discover KDCs"),
		    smb_krb5_cfg_getpath());
		return (1);
	}

	err = smb_krb5_cfg_init(&krbcfg, jdi->domain_name, jdi->dc, kdcs);
	if (err != 0) {
		smbadm_log_krberr(krbcfg.kc_ctx, err,
		    "Error preparing Kerberos configuration");
		return (1);
	}

	if (krbcfg.kc_orig_drealm != NULL)
		(void) strlcpy(drealm, krbcfg.kc_orig_drealm, len);

	if (!SMB_KRB5_CFG_DEFAULT_REALM_EXIST(&krbcfg) ||
	    SMB_KRB5_CFG_IS_DEFAULT_REALM(&krbcfg))
		krbcfg.kc_default = B_TRUE;
	else
		krbcfg.kc_default = smbadm_krbcfg_drealm_update_prompt(
		    &krbcfg);

	if (krbcfg.kc_exist)
		err = smb_krb5_cfg_update(&krbcfg, errmsg, sizeof (errmsg));
	else
		err = smb_krb5_cfg_add(&krbcfg, errmsg, sizeof (errmsg));

	if (err != 0) {
		smbadm_log_krberr(krbcfg.kc_ctx, err, errmsg);
		(void) smb_krb5_cfg_fini(&krbcfg, B_FALSE);
		return (1);
	}

	/* Commit changes to krb5.conf. */
	if ((err = smb_krb5_cfg_fini(&krbcfg, B_TRUE)) != 0) {
		smbadm_log_krberr(krbcfg.kc_ctx, err,
		    "Failed to commit changes to %s", krbcfg.kc_path);
	}

	return (0);
}

/*
 * Writes Kerberos error messages to both stderr and system log.
 */
static void
smbadm_log_krberr(krb5_context ctx, krb5_error_code code,
    const char *fmt, ...)
{
	va_list		ap;
	char		msg[SMB_LOG_LINE_SZ];
	const char	*krbmsg;

	va_start(ap, fmt);
	(void) vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	if (ctx != NULL)
		krbmsg = krb5_get_error_message(ctx, code);
	else
		krbmsg = error_message(code);

	(void) fprintf(stderr, gettext("%s (%s)\n"), msg, krbmsg);
	smb_krb5_log_errmsg(ctx, code, msg);

	if (ctx != NULL)
		krb5_free_error_message(ctx, krbmsg);
}
