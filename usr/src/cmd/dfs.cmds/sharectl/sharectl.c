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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>

#include "libshare.h"

#include <libintl.h>
#include <locale.h>

/* sharectl specific usage message values */
typedef enum {
	USAGE_CTL_GET,
	USAGE_CTL_SET,
	USAGE_CTL_STATUS,
	USAGE_CTL_DELSECT
} sc_usage_t;

typedef struct sa_command {
	char	*cmdname;
	int	(*cmdfunc)(int, char **);
	int	cmdidx;
	int	priv;	/* requires RBAC authorizations */
} sa_command_t;

/*
 * functions/values for manipulating options
 */
#define	OPT_ADD_OK		0
#define	OPT_ADD_SYNTAX		-1
#define	OPT_ADD_MEMORY		-2

/* option list structure */
struct options {
	struct options *next;
	char *optname;
	char *optvalue;
};

static int sc_get(int, char **);
static int sc_set(int, char **);
static int sc_status(int, char **);
#ifdef HAVE_SECTIONS
static int sc_delsect(int, char **);
#endif
static int sc_cache(int, char **);

static sa_command_t commands[] = {
	{"get", sc_get, USAGE_CTL_GET},
	{"set", sc_set, USAGE_CTL_SET},
	{"status", sc_status, USAGE_CTL_STATUS},
#ifdef HAVE_SECTIONS
	/*
	 * Disable this command since there are no protocol supporting
	 * sections.  Keeping the command in case someone might want to
	 * use sections in the future.
	 */
	{"delsect", sc_delsect, USAGE_CTL_DELSECT},
#endif
	{NULL, NULL, 0},
};

static int run_command(char *, int, char **);
static sa_command_t *sa_lookup(char *);
static char *sc_get_usage(sc_usage_t);
static void global_help(void);
static void free_optlist(struct options *);
static int add_opt(struct options **, char *, int);
static void show_status(sa_proto_t);
static void print_share(nvlist_t *);

int
main(int argc, char *argv[])
{
	int c;
	int help = 0;
	int rval;
	char *command;

	/*
	 * make sure locale and gettext domain is setup
	 */
	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "h?")) != EOF) {
		switch (c) {
		case '?':
		case 'h':
			help = 1;
			break;
		default:
			(void) printf(gettext("Invalid option: %c\n"), c);
		}
	}
	if (optind == argc || help) {
		/* no subcommand */
		global_help();
		exit(0);
	}
	optind = 1;

	/*
	 * now have enough to parse rest of command line
	 */
	command = argv[optind];
	rval = run_command(command, argc - optind, argv + optind);

	return (rval);
}

static int
run_command(char *command, int argc, char *argv[])
{
	sa_command_t *cmdvec;
	int ret;

	/*
	 * To get here, we know there should be a command due to the
	 * preprocessing done earlier.  Need to find the protocol
	 * that is being affected. If no protocol, then it is ALL
	 * protocols.
	 *
	 * ??? do we really need the protocol at this level? it may be
	 * sufficient to let the commands look it up if needed since
	 * not all commands do proto specific things
	 *
	 * Known sub-commands are handled at this level. An unknown
	 * command will be passed down to the shared object that
	 * actually implements it. We can do this since the semantics
	 * of the common sub-commands is well defined.
	 */

	cmdvec = sa_lookup(command);
	if (cmdvec == NULL) {
		/*
		 * hidden command to dump shared cache
		 */
		if (strncmp(command, "cache", strlen(command)) == 0)
			return (sc_cache(argc, argv));

		(void) printf(gettext("command %s not found\n"), command);
		exit(1);
	}
	/*
	 * need to check priviledges and restrict what can be done
	 * based on least priviledge and sub-command.
	 */
	ret = cmdvec->cmdfunc(argc, argv);
	return (ret);
}

static int
sc_get(int argc, char *argv[])
{
	int c;
	char *proto_str = NULL;
	sa_proto_t proto;
	struct options *optlist = NULL;
	nvlist_t *proplist;
	nvlist_t *sectlist;
	char *propname;
	char *propval;
	char *sectname;
	nvpair_t *nvp;
	nvpair_t *sect_nvp;
	int ret = SA_OK;
	int first = 1;

	while ((c = getopt(argc, argv, "?hp:")) != EOF) {
		switch (c) {
		case 'p':
			ret = add_opt(&optlist, optarg, 1);
			if (ret != OPT_ADD_OK) {
				(void) printf(gettext(
				    "Problem with property: %s\n"), optarg);
				free_optlist(optlist);
				return (SA_NO_MEMORY);
			}
			break;
		case '?':
		case 'h':
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_GET));
			free_optlist(optlist);
			return (SA_OK);
		default:
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_GET));
			free_optlist(optlist);
			return (SA_SYNTAX_ERR);
		}
	}

	if (optind >= argc) {
		(void) printf(gettext("usage: %s\n"),
		    sc_get_usage(USAGE_CTL_GET));
		(void) printf(gettext("\tprotocol must be specified.\n"));
		free_optlist(optlist);
		return (SA_NO_SUCH_PROTO);
	}

	proto_str = argv[optind];
	if ((proto = sa_val_to_proto(proto_str)) == SA_PROT_NONE) {
		(void) printf(gettext("Invalid protocol specified: %s\n"),
		    proto_str);
		free_optlist(optlist);
		return (SA_NO_SUCH_PROTO);
	}

	proplist = sa_proto_get_proplist(proto);
	if (proplist == NULL) {
		free_optlist(optlist);
		return (ret);
	}

	/*
	 * Properties are stored as a list of nvpairs.
	 * Sections are stored as embedded nvlists within the
	 * property list. The name of the embedded section nvlist
	 * is the scf property group name which is not displayed here.
	 * The section name is stored as the nvpair "section="
	 */
	if (optlist == NULL) {
		/*
		 * Display all known properties for this protocol
		 *
		 * First display all non section properties
		 */
		for (nvp = nvlist_next_nvpair(proplist, NULL);
		    nvp != NULL;
		    nvp = nvlist_next_nvpair(proplist, nvp)) {
			if (nvpair_type(nvp) == DATA_TYPE_NVLIST)
				continue;

			propname = nvpair_name(nvp);
			(void) nvpair_value_string(nvp, &propval);
			(void) printf(gettext("%s=%s\n"), propname, propval);
		}

		/*
		 * Now display all sections.
		 */
		for (nvp = nvlist_next_nvpair(proplist, NULL);
		    nvp != NULL;
		    nvp = nvlist_next_nvpair(proplist, nvp)) {
			/*
			 * ignore nvpairs that are not nvlists
			 */
			if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
				continue;

			/* obtain the nvlist from the nvpair */
			if (nvpair_value_nvlist(nvp, &sectlist) != 0)
				continue;

			/* lookup the section name */
			if (nvlist_lookup_string(sectlist, "section",
			    &sectname) != 0)
				sectname = "";

			if (!first)
				(void) printf("\n");
			else
				first = 0;
			/* display the section name */
			(void) printf("[%s]\n", sectname);

			/*
			 * now display all properties for this section
			 */
			for (sect_nvp = nvlist_next_nvpair(sectlist, NULL);
			    sect_nvp != NULL;
			    sect_nvp = nvlist_next_nvpair(sectlist, sect_nvp)) {
				if (nvpair_type(sect_nvp) != DATA_TYPE_STRING)
					continue;

				propname = nvpair_name(sect_nvp);
				/* ignore section name, already displayed */
				if (strcmp(propname, "section") == 0)
					continue;
				(void) nvpair_value_string(sect_nvp, &propval);
				(void) printf(gettext("%s=%s\n"),
				    propname, propval);
			}
		}
	} else {
		struct options *opt;

		/* list the specified option(s) */
		for (opt = optlist; opt != NULL; opt = opt->next) {
			int found = 0;

			/*
			 * first look for global property
			 */
			if (nvlist_lookup_string(proplist, opt->optname,
			    &propval) == 0) {
				(void) printf(gettext("%s=%s\n"), opt->optname,
				    propval);
				found++;
			}

			/*
			 * now search all sections for a match
			 */
			for (nvp = nvlist_next_nvpair(proplist, NULL);
			    nvp != NULL;
			    nvp = nvlist_next_nvpair(proplist, nvp)) {
				if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
					continue;
				if (nvpair_value_nvlist(nvp, &sectlist) != 0)
					continue;

				if (nvlist_lookup_string(sectlist, opt->optname,
				    &propval) != 0)
					continue;

				if (nvlist_lookup_string(sectlist, "section",
				    &sectname) != 0)
					sectname = "";

				(void) printf(gettext("[%s] %s=%s\n"),
				    sectname, opt->optname, propval);
				found++;
			}

			if (!found) {
				(void) printf(gettext("%s: not defined\n"),
				    opt->optname);
				ret = SA_NO_SUCH_PROP;
			}
		}
	}

	nvlist_free(proplist);

	return (ret);
}

static int
sc_set(int argc, char *argv[])
{
	int c;
	int ret = SA_OK;
	char *proto_str = NULL;
	sa_proto_t proto;
	struct options *optlist = NULL;
	struct options *opt;
	char *sectname = NULL;
	char *propval;

	while ((c = getopt(argc, argv, "?hp:")) != EOF) {
		switch (c) {
		case 'p':
			ret = add_opt(&optlist, optarg, 0);
			if (ret != SA_OK) {
				(void) printf(gettext(
				    "Problem with property: %s\n"), optarg);
				free_optlist(optlist);
				return (SA_NO_MEMORY);
			}
			break;
		case '?':
		case 'h':
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_SET));
			free_optlist(optlist);
			return (SA_OK);
		default:
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_SET));
			free_optlist(optlist);
			return (SA_SYNTAX_ERR);
		}
	}

	if (optind >= argc) {
		(void) printf(gettext("usage: %s\n"),
		    sc_get_usage(USAGE_CTL_SET));
		(void) printf(gettext("\tprotocol must be specified.\n"));
		free_optlist(optlist);
		return (SA_NO_SUCH_PROTO);
	}

	proto_str = argv[optind];
	if ((proto = sa_val_to_proto(proto_str)) == SA_PROT_NONE) {
		(void) printf(gettext("Invalid protocol specified: %s\n"),
		    proto_str);
		free_optlist(optlist);
		return (SA_NO_SUCH_PROTO);
	}

	if (optlist == NULL) {
		(void) printf(gettext("usage: %s\n"),
		    sc_get_usage(USAGE_CTL_SET));
		(void) printf(gettext(
		    "\tat least one property and value "
		    "must be specified\n"));
		free_optlist(optlist);
		return (ret);
	}

	/*
	 * fetch and change the specified option(s)
	 */
	for (opt = optlist; opt != NULL; opt = opt->next) {
		if (strncmp("section", opt->optname, 7) == 0) {
			if ((sa_proto_get_featureset(proto) &
			    SA_FEATURE_HAS_SECTIONS) == 0) {
				(void) printf(gettext("Protocol %s "
				    "does not have sections\n"),
				    proto_str);
				ret = SA_NOT_SUPPORTED;
				break;
			} else {
				if (sectname != NULL)
					free(sectname);
				sectname = strdup(opt->optvalue);
				if (sectname == NULL) {
					(void) printf(
					    gettext("no memory\n"));
					ret = SA_NO_MEMORY;
					break;
				}
				continue;
			}
		}

		propval = sa_proto_get_property(proto, sectname,
		    opt->optname);

		if ((propval == NULL) &&
		    ((sa_proto_get_featureset(proto) &
		    SA_FEATURE_ADD_PROPERTIES) == 0)) {
			(void) printf(gettext("%s: not defined\n"),
			    opt->optname);
			ret = SA_NO_SUCH_PROP;
			break;
		}
		free(propval);

		ret = sa_proto_set_property(proto, sectname, opt->optname,
		    opt->optvalue);

		if (ret != SA_OK) {
			(void) printf(gettext(
			    "Could not set property %s: %s\n"),
			    opt->optname, sa_strerror(ret));
			break;
		}
	}

	if (sectname != NULL)
		free(sectname);

	free_optlist(optlist);

	return (ret);
}

static int
sc_status(int argc, char *argv[])
{
	int c;
	int i;
	int verbose = 0;
	int num_proto;
	sa_proto_t proto;
	sa_proto_t *protos;
	int ret = SA_OK;

	while ((c = getopt(argc, argv, "?hv")) != EOF) {
		switch (c) {
		case 'v':
			verbose++;
			break;
		case '?':
		case 'h':
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_STATUS));
			return (SA_OK);
		default:
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_STATUS));
			return (SA_SYNTAX_ERR);
		}
	}

	if (optind == argc) {
		/* status for all protocols */
		if ((num_proto = sa_get_protocols(&protos)) > 0) {
			for (i = 0; i < num_proto; i++) {
				show_status(protos[i]);
			}
			free(protos);
		}
	} else {
		for (i = optind; i < argc; i++) {
			proto = sa_val_to_proto(argv[i]);
			if (proto == SA_PROT_NONE) {
				(void) printf(gettext("Invalid protocol: %s\n"),
				    argv[i]);
				ret = SA_NO_SUCH_PROTO;
			} else {
				show_status(proto);
			}
		}
	}
	return (ret);
}

#ifdef HAVE_SECTIONS
static int
sc_delsect(int argc, char *argv[])
{
	int c;
	int ret = SA_OK;
	char *proto_str = NULL;
	sa_proto_t proto;
	char *sectname = NULL;

	while ((c = getopt(argc, argv, "?h")) != EOF) {
		switch (c) {
		default:
			ret = SA_SYNTAX_ERR;
			/*FALLTHROUGH*/
		case '?':
		case 'h':
			(void) printf(gettext("usage: %s\n"),
			    sc_get_usage(USAGE_CTL_DELSECT));
			return (ret);
		}
		/*NOTREACHED*/
	}

	sectname = argv[optind++];

	if (optind >= argc) {
		(void) printf(gettext("usage: %s\n"),
		    sc_get_usage(USAGE_CTL_DELSECT));
		(void) printf(gettext(
		    "\tsection and protocol must be specified.\n"));
		return (SA_NO_SUCH_PROTO);
	}

	proto_str = argv[optind];
	if ((proto = sa_val_to_proto(proto_str)) == SA_PROT_NONE) {
		(void) printf(gettext("Invalid protocol specified: %s\n"),
		    proto_str);
		return (SA_NO_SUCH_PROTO);
	}

	if ((sa_proto_get_featureset(proto) & SA_FEATURE_HAS_SECTIONS) == 0) {
		(void) printf(gettext("Protocol %s does not have sections\n"),
		    proto_str);
		return (SA_NOT_SUPPORTED);
	}

	ret = sa_proto_rem_section(proto, sectname);
	if (ret != SA_OK) {
		(void) printf(gettext("Cannot delete section %s: %s\n"),
		    sectname, sa_strerror(ret));
		return (ret);
	}

	return (ret);
}
#endif

/*ARGSUSED*/
static int
sc_cache(int argc, char *argv[])
{
	nvlist_t *share;
	void *hdl;
	int rc;

	if ((rc = sa_share_find_init(NULL, SA_PROT_ANY, &hdl)) == SA_OK) {
		while (sa_share_find_next(hdl, &share) == SA_OK) {
			print_share(share);
			sa_share_free(share);
		}
		sa_share_find_fini(hdl);
	}

	return (rc);
}

static sa_command_t *
sa_lookup(char *cmd)
{
	int i;
	size_t len;

	len = strlen(cmd);
	for (i = 0; commands[i].cmdname != NULL; i++) {
		if (strncmp(cmd, commands[i].cmdname, len) == 0)
			return (&commands[i]);
	}
	return (NULL);
}

static char *
sc_get_usage(sc_usage_t index)
{
	char *ret = NULL;

	switch (index) {
	case USAGE_CTL_GET:
		ret = gettext("get [-h | -p property ...] proto");
		break;
	case USAGE_CTL_SET:
		ret = gettext("set [-h] -p property=value ... proto");
		break;
	case USAGE_CTL_STATUS:
		ret = gettext("status [-h | proto ...]");
		break;
	case USAGE_CTL_DELSECT:
		ret = gettext("delsect [-h] section proto");
		break;
	}
	return (ret);
}

static void
global_help(void)
{
	int i;

	(void) printf(gettext("usage: sharectl <command> [options]\n"));

	(void) printf("\tsub-commands:\n");
	for (i = 0; commands[i].cmdname != NULL; i++) {
		(void) printf("\t%s\n",
		    sc_get_usage((sc_usage_t)commands[i].cmdidx));
	}
}

static void
free_optlist(struct options *optlist)
{
	struct options *opt, *next;

	for (opt = optlist; opt != NULL; opt = next) {
		next = opt->next;
		free(opt);
	}
}

static int
add_opt(struct options **optlistp, char *optarg, int unset)
{
	struct options *newopt, *tmp, *optlist;
	char *optname;
	char *optvalue;

	optlist = *optlistp;
	newopt = (struct options *)malloc(sizeof (struct options));
	if (newopt == NULL)
		return (OPT_ADD_MEMORY);

	/* extract property/value pair */
	optname = optarg;
	if (!unset) {
		optvalue = strchr(optname, '=');
		if (optvalue == NULL) {
			free(newopt);
			return (OPT_ADD_SYNTAX);
		}
		*optvalue++ = '\0'; /* separate the halves */
	} else {
		optvalue = NULL;
	}

	newopt->optname = optname;
	newopt->optvalue = optvalue;
	newopt->next = NULL;
	if (optlist == NULL) {
		optlist = newopt;
	} else {
		for (tmp = optlist; tmp->next != NULL;
		    tmp = tmp->next) {
			/*
			 * Check to see if this is a duplicate
			 * value. We want to replace the first
			 * instance with the second.
			 */
			if (strcmp(tmp->optname, optname) == 0) {
				tmp->optvalue = optvalue;
				free(newopt);
				goto done;
			}
		}
		tmp->next = newopt;
	}
done:
	*optlistp = optlist;
	return (OPT_ADD_OK);
}

static void
show_status(sa_proto_t proto)
{
	char *status;
	uint64_t features;

	status = sa_proto_get_status(proto);
	features = sa_proto_get_featureset(proto);
	(void) printf("%s\t%s", sa_proto_to_val(proto),
	    status ? gettext(status) : "-");
	if (status != NULL)
		free(status);
	/*
	 * Need to flag a client only protocol so test suites can
	 * remove it from consideration.
	 */
	if (!(features & SA_FEATURE_SERVER))
		(void) printf(" client");
	(void) printf("\n");
}

static int
pack_security(nvlist_t *sec_nvl, const char *sec_name, char *buf,
size_t buflen)
{
	nvpair_t	*nvp;
	char		*propname;
	char		*propval;
	size_t		cnt = 0;

	cnt += snprintf(&buf[cnt], buflen-cnt, ",sec=%s", sec_name);
	if (cnt >= buflen)
		return (cnt);

	for (nvp = nvlist_next_nvpair(sec_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(sec_nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_STRING)
			continue;

		propname = nvpair_name(nvp);

		if (nvpair_value_string(nvp, &propval) != 0)
			continue;
		cnt += snprintf(&buf[cnt], buflen-cnt, ",%s=%s",
		    propname, propval);
		if (cnt >= buflen)
			return (cnt);
	}

	return (cnt);
}

static int
pack_proto(nvlist_t *prot_nvl, const char *prot, char *buf,
size_t buflen)
{
	nvpair_t	*nvp;
	nvlist_t	*sec_nvl;
	char		*propname;
	char		*propval;
	size_t		cnt = 0;

	cnt += snprintf(&buf[cnt], buflen-cnt, ",prot=%s", prot);
	if (cnt >= buflen)
		return (cnt);

	/*
	 * first print out all global protocol properties
	 */

	for (nvp = nvlist_next_nvpair(prot_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(prot_nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_STRING)
			continue;

		propname = nvpair_name(nvp);
		(void) nvpair_value_string(nvp, &propval);

		cnt += snprintf(&buf[cnt], buflen-cnt, ",%s=%s",
		    propname, propval);
		if (cnt >= buflen)
			return (cnt);
	}

	/*
	 * now print out security properties
	 */
	for (nvp = nvlist_next_nvpair(prot_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(prot_nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		(void) nvpair_value_nvlist(nvp, &sec_nvl);
		propname = nvpair_name(nvp);
		cnt += pack_security(sec_nvl, propname, &buf[cnt],
		    buflen-cnt);
		if (cnt >= buflen)
			return (cnt);
	}

	return (cnt);
}

static int
pack_share(nvlist_t *nvl, char *buf, size_t buflen)
{
	char		*sname;
	char		*propname;
	char		*propval;
	nvpair_t	*nvp;
	nvlist_t	*prot_nvl = 0;
	size_t		cnt = 0;

	if ((sname = sa_share_get_name(nvl)) == NULL)
		return (0);

	/* always print share name first */
	cnt += snprintf(&buf[cnt], buflen-cnt, "name=%s", sname);
	if (cnt >= buflen)
		return (cnt);

	/*
	 * first print out all global properties
	 */
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_STRING)
			continue;

		propname = nvpair_name(nvp);

		/* skip share name, already added */
		if (strcasecmp(propname, "name") == 0)
			continue;
		/* ignore 'mntpnt' property */
		if (strcasecmp(propname, "mntpnt") == 0)
			continue;

		(void) nvpair_value_string(nvp, &propval);
		cnt += snprintf(&buf[cnt], buflen-cnt, ",%s=%s",
		    propname, propval);
		if (cnt >= buflen)
			return (cnt);
	}

	/*
	 * now print out protocol properties
	 */
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		propname = nvpair_name(nvp);
		if (nvpair_value_nvlist(nvp, &prot_nvl) != 0)
			continue;

		if (strcasecmp(propname, "nfs") == 0 ||
		    strcasecmp(propname, "smb") == 0) {
			cnt += pack_proto(prot_nvl, propname, &buf[cnt],
			    buflen-cnt);
		} else {
			cnt += snprintf(&buf[cnt], buflen-cnt,
			    ",prot=%s", propname);
		}
		if (cnt >= buflen)
			return (cnt);
	}

	return (cnt);
}

static char *
share_to_str(nvlist_t *share_nvl)
{
	size_t buflen;
	char *share_buf;

	if (nvlist_size(share_nvl, &buflen, NV_ENCODE_NATIVE) != 0)
		return (NULL);
	if ((share_buf = malloc(buflen)) == NULL)
		return (NULL);
	share_buf[0] = '\0';

	(void) pack_share(share_nvl, share_buf, buflen);

	return (share_buf);
}

static void
print_share(nvlist_t *share_nvl)
{
	char *share_str;

	share_str = share_to_str(share_nvl);
	if (share_str != NULL) {
		(void) printf("%s\n", share_str);
		free(share_str);
	}
}
