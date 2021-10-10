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

/*
 * CUI for administering power management properties
 */

#include <sys/types.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <libintl.h>
#include <locale.h>
#include <libuutil.h>
#include <libpower.h>
#include <libscf.h>

#define	POWERADM_CMD_GET	"get"
#define	POWERADM_CMD_SET	"set"
#define	POWERADM_CMD_START	"start"
#define	POWERADM_CMD_LIST	"list"
#define	POWERADM_CMD_UPDATE	"update"
#define	POWERADM_CMD_HELP	"help"

#define	POWERADM_EXIT_SUCCESS	0
#define	POWERADM_EXIT_ERROR	1
#define	POWERADM_EXIT_USAGE	2
#define	POWERADM_LOG_LEVEL	UU_DPRINTF_FATAL

#define	POWERADM_SHORTOPTS	"?v"
#define	POWERADM_GET_SHORTOPTS	"a:"

typedef pm_error_t (*poweradm_func_t)(int, char *[], int *);
struct poweradm_cmd_s {
	poweradm_func_t	c_func;
	const char	*c_name;
	const char	*c_usage;
};
typedef struct poweradm_cmd_s poweradm_cmd_t;

static uu_dprintf_t		*log;
static uu_dprintf_severity_t	log_level = POWERADM_LOG_LEVEL;

/* global poweradm options */
static struct option poweradm_options[] = {
	{"verbose",	no_argument,		NULL,	'v'},
	{"help",	no_argument,		NULL,	'?'},
	{0, 0, 0, 0}
};

/* sub-command arguments for the "get" command */
static struct option poweradm_get_options[] = {
	{"authority",	required_argument,	NULL, 'a'},
	{0, 0, 0, 0}
};


static pm_error_t poweradm_do_get(int, char *[], int *);
static pm_error_t poweradm_do_list(int, char *[], int *);
static pm_error_t poweradm_do_set(int, char *[], int *);
static pm_error_t poweradm_do_start(int, char *[], int *);
static pm_error_t poweradm_do_update(int, char *[], int *);
static pm_error_t poweradm_usage(int, char *[], int *);

static void poweradm_set_warn(nvlist_t *);
static pm_error_t poweradm_set_trimvalue(char *, char **);
static pm_error_t emit_result(nvlist_t *, pm_authority_t);
static pm_error_t emit_nvpair_value(nvpair_t *, pm_authority_t, boolean_t);

/*
 * Define a structure with all the possible sub-command arguments to poweradm
 * and the usage message for each command.
 *
 * Note: do not forget to update the message file if changes are made to
 * this structure.
 */
static const poweradm_cmd_t poweradm_cmd_get = {poweradm_do_get,
	POWERADM_CMD_GET, "[-a all | current | platform | smf] name [name...]"};
static const poweradm_cmd_t poweradm_cmd_help = {poweradm_usage,
	POWERADM_CMD_HELP, NULL};
static const poweradm_cmd_t poweradm_cmd_list = {poweradm_do_list,
	POWERADM_CMD_LIST, NULL};
static const poweradm_cmd_t poweradm_cmd_set = {poweradm_do_set,
	POWERADM_CMD_SET, "name=value [name=value...]"};
static const poweradm_cmd_t poweradm_cmd_start = {poweradm_do_start,
	POWERADM_CMD_START, NULL};
static const poweradm_cmd_t poweradm_cmd_update = {poweradm_do_update,
	POWERADM_CMD_UPDATE, NULL};
static const poweradm_cmd_t *poweradm_cmds[] = {
	&poweradm_cmd_get,
	&poweradm_cmd_help,
	&poweradm_cmd_list,
	&poweradm_cmd_set,
	&poweradm_cmd_start,
	&poweradm_cmd_update,
	NULL
};


int
main(int argc, char *argv[])
{
	int		exitcode;
	int		c;
	int		optidx;
	pm_error_t	err;

	const poweradm_cmd_t	**cpp;
	const poweradm_cmd_t	*cp;

	(void) setlocale(LC_ALL, "C");
	(void) textdomain(TEXT_DOMAIN);

	optidx = 0;
	exitcode = POWERADM_EXIT_SUCCESS;
	while ((c = getopt_clip(argc, argv, POWERADM_SHORTOPTS,
	    poweradm_options, &optidx)) != -1) {
		switch (c) {
		case 0:
			break;

		case 'v':
			log_level++;
#ifdef DEBUG
			if (log_level > UU_DPRINTF_DEBUG) {
				/*
				 * In debug builds, allow increased verbosity
				 * levels to emit debug information while
				 * maintaining the ability to see what the
				 * customer will see in non-debug builds.
				 */
				log_level = UU_DPRINTF_DEBUG;
			}
#else
			if (log_level > UU_DPRINTF_INFO) {
				log_level = UU_DPRINTF_INFO;
			}
#endif
			break;

		case '?': /* FALL-THROUGH */
		default:
			(void) poweradm_usage(0, NULL, &exitcode);
			exit(exitcode);
			break;
		}
	}
	if (optind >= argc) {
		/*
		 * There is no sub-command argument to process. This is a
		 * usage error.
		 */
		(void) poweradm_usage(0, NULL, &exitcode);
		exit(exitcode);
	}

	/* Create the logging handle at the requested level */
	log = uu_dprintf_create(uu_setpname(argv[0]), log_level, 0);

	/*
	 * The command is now the next argument as specified by optind.
	 * Scan the list of available commands to find a match, if any.
	 */
	err = PM_SUCCESS;
	cp = NULL;
	for (cpp = poweradm_cmds; *cpp != NULL; cpp++) {
		cp = *cpp;

		uu_dprintf(log, UU_DPRINTF_DEBUG,
		    "comparing \"%s\" with \"%s\"\n", argv[optind], cp->c_name);
		if (strncmp(argv[optind], cp->c_name,
		    strlen(cp->c_name)) == 0) {
			uu_dprintf(log, UU_DPRINTF_DEBUG, "matched %s\n",
			    cp->c_name);
			break;
		}
	}

	/* Assume failure. The sub-command routines will reset the exit code */
	exitcode = POWERADM_EXIT_ERROR;
	if (*cpp != NULL) {
		cp = *cpp;

		/*
		 * Call the found command and pass in the remaining options
		 * vector so the command can parse it's own arguments.
		 *
		 * The sub-command routines are responsible for setting the
		 * appropriate exit code and returning an appropriate error
		 * code. The action to take on the error code is determined
		 * here.
		 */
		err = (cp->c_func)(argc - optind, &argv[optind], &exitcode);
		switch (err) {
		case PM_SUCCESS: /* success */
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "%s succeeded\n", cp->c_name);
			break;

		case PM_ERROR_INVALID_ARGUMENT:
		case PM_ERROR_MISSING_PROPERTY_NAME:
		case PM_ERROR_MISSING_PROPERTY_VALUE:
		case PM_ERROR_USAGE:
			uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
			    pm_strerror(err));

			/* Emit a usage message and set the exit code */
			(void) poweradm_usage(argc - optind, &argv[optind],
			    &exitcode);
			break;

		case PM_ERROR_PROPERTY_NOT_FOUND:
		case PM_ERROR_PROPERTY_GROUP_NOT_FOUND:
			/* This is a non-fatal error, but inform the user */
			uu_dprintf(log, UU_DPRINTF_NOTICE, "%s\n",
			    pm_strerror(err));
			err = PM_SUCCESS;
			break;

		case PM_ERROR_INVALID_AUTHORITY:
		case PM_ERROR_INVALID_BOOLEAN:
		case PM_ERROR_INVALID_INTEGER:
		case PM_ERROR_INVALID_PROPERTY_NAME:
		case PM_ERROR_INVALID_PROPERTY_VALUE:
		case PM_ERROR_INVALID_TYPE:
		case PM_ERROR_NVLIST:
		case PM_ERROR_SCF:
		case PM_ERROR_SYSTEM:
			uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
			    pm_strerror(err));
			break;

		default:
			/*
			 * A fatal error has occurred. The routine will emit
			 * an appropriate error message with context not
			 * available here.
			 */
			uu_dprintf(log, UU_DPRINTF_FATAL,
			    gettext("an internal error occurred: %s\n"),
			    pm_strerror(err));
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "command result %d exit code %d\n", err, exitcode);
			break;
		}
	} else {
		/* No valid command was found */
		(void) poweradm_usage(0, NULL, &exitcode);
	}

	/* Clean up and exit with the specified code */
	uu_dprintf_destroy(log);

	return (exitcode);
}


/*ARGSUSED*/
static pm_error_t
poweradm_usage(int argc, char *argv[], int *exitcode)
{
	size_t			maxwidth;
	const poweradm_cmd_t	**cpp;

	(void) fprintf(stderr, gettext("usage: %s "), getexecname());
	(void) fprintf(stderr, gettext(" [-?]"));
	(void) fprintf(stderr, gettext(" [--verbose | -v]"));
	(void) fprintf(stderr, gettext(" command [args]\n"));

	/* Calculate the maximum field width of the command names */
	for (maxwidth = 0, cpp = poweradm_cmds; *cpp != NULL; cpp++) {
		const poweradm_cmd_t *cp = *cpp;
		size_t len = strlen(cp->c_name);
		if (len > maxwidth) {
			maxwidth = len;
		}
	}
	for (cpp = poweradm_cmds; *cpp != NULL; cpp++) {
		const poweradm_cmd_t *cp = *cpp;
		(void) fprintf(stderr, "\t%-*s", maxwidth, cp->c_name);
		if (cp->c_usage != NULL) {
			(void) fprintf(stderr, "\t%s", gettext(cp->c_usage));
		}
		(void) fprintf(stderr, "\n");
	}

	/* Re-set the exit code to indicate a usage error occurred */
	*exitcode = POWERADM_EXIT_USAGE;

	return (PM_SUCCESS);
}


static pm_error_t
poweradm_do_get(int argc, char *argv[], int *exitcode)
{
	int		arg;
	int		optidx;
	const char	*authname;
	pm_error_t	err;
	pm_authority_t	authority;
	nvlist_t	*result;
	nvlist_t	*query;

	uu_dprintf(log, UU_DPRINTF_DEBUG,
	    "entering %s with %d argc\n", POWERADM_CMD_GET, argc);

	/* Assume failure initially */
	*exitcode = POWERADM_EXIT_ERROR;
	if (argc < 2) {
		/* There were no property names listed for retrieval */
		errno = EINVAL;
		uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
		    gettext("one or more property names are required for this "
		    "command"));
		return (PM_ERROR_MISSING_PROPERTY_NAME);
	}

	/* Check the current value by default */
	authority = PM_AUTHORITY_INVALID;

	err = PM_SUCCESS;
	optind = 0;
	optidx = 0;
	while ((arg = getopt_clip(argc, argv, POWERADM_GET_SHORTOPTS,
	    poweradm_get_options, &optidx)) != -1) {
		switch (arg) {
		case 0:
			break;

		case 'a':
			if (authority != PM_AUTHORITY_INVALID) {
				/*
				 * -a was specified multiple times, which is
				 * not allowed. Return a usage message.
				 */
				uu_dprintf(log, UU_DPRINTF_DEBUG,
				    "option -a was specified multiple times\n");

				return (PM_ERROR_USAGE);
			}

			/* Validate the given authority name */
			authority = pm_authority_get(optarg);
			if (authority == PM_AUTHORITY_INVALID) {
				/*
				 * This is a usage error instead of an invalid
				 * authority error because it is in an option
				 * specifier and not a possible value set.
				 */
				uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
				    pm_strerror(PM_ERROR_INVALID_AUTHORITY));
				return (PM_ERROR_USAGE);
			}
			break;

		default:
			uu_dprintf(log, UU_DPRINTF_FATAL,
			    gettext("\"%c\" is an invalid option for this "
			    "command\n"), arg);
			return (PM_ERROR_USAGE);
			break;
		}
	}
	if (authority == PM_AUTHORITY_INVALID) {
		/*
		 * No authority was specified on the command line. Use the
		 * current authority instead.
		 */
		authority = PM_AUTHORITY_CURRENT;
	}
	uu_dprintf(log, UU_DPRINTF_DEBUG, "do_getprop: authority %s\n",
	    pm_authority_getname(authority));

	if (optind >= argc) {
		return (PM_ERROR_MISSING_PROPERTY_NAME);
	}

	/*
	 * Create the property query, which consists of 2 properties:
	 *
	 *	Name			Description
	 *	-----------------------	--------------------------------------
	 *	PM_PROP_AUTHORITY	The name of the authority to query
	 *
	 *	PM_PROP_QUERY		An array of strings. Each string is a
	 *				property name to retrieve.  Property
	 *				names may be qualified with a property
	 *				group name.
	 */
	errno = 0;
	authname = pm_authority_getname(authority);
	if ((errno = nvlist_alloc(&query, NV_UNIQUE_NAME, 0)) != 0 ||
	    (errno = nvlist_add_string(query, PM_PROP_AUTHORITY,
	    authname)) != 0 ||
	    (errno = nvlist_add_string_array(query, PM_PROP_QUERY,
	    &(argv[optind]), argc - optind)) != 0) {
		/*
		 * Something went wrong inside nvlist. All of the properties
		 * were validated above.
		 */
		uu_dprintf(log, UU_DPRINTF_DEBUG,
		    "%s encountered a fatal error %d (%s)\n",
		    __FUNCTION__, errno, strerror(errno));
		return (PM_ERROR_NVLIST);
	}

	/* Query for the desired properties */
	result = NULL;
	errno = 0;
	err = pm_getprop(&result, query);
	if (err == PM_SUCCESS) {
		/*
		 * Print the result of the get operation and reset the exit
		 * code to indicate success.
		 */
		err = emit_result(result, authority);
		if (err == PM_SUCCESS) {
			*exitcode = POWERADM_EXIT_SUCCESS;
		}
	}

	nvlist_free(query);
	if (result != NULL) {
		nvlist_free(result);
	}

	return (err);
}


static pm_error_t
emit_result(nvlist_t *result, pm_authority_t authority)
{
	size_t		maxwidth;
	boolean_t	emit_sep;
	char		*pgname;
	nvpair_t	*elp;
	nvlist_t	*nvl;
	nvpair_t	*nvp;
	pm_error_t	err;

	/*
	 * Scan the result list and calculate the maximum length of the
	 * fully qualified property names.
	 */
	maxwidth = 0;
	elp = nvlist_next_nvpair(result, NULL);
	while (elp != NULL) {
		size_t len = 0;

		pgname = NULL;
		len = strlen(nvpair_name(elp));
		len += strlen(PM_SEP_STR);
		if (nvpair_value_nvlist(elp, &nvl) == 0 &&
		    nvlist_lookup_string(nvl, PM_PROP_PGNAME, &pgname) == 0 &&
		    pgname != NULL) {
			len += strlen(pgname);
		}
		if (len > maxwidth) {
			maxwidth = len;
		}

		/* Check the next property */
		elp = nvlist_next_nvpair(result, elp);
	}

	/*
	 * Scan the result list and print each element.  Output will change
	 * based on the specified log_level and the requested authority.
	 */
	err = PM_SUCCESS;
	errno = 0;
	elp = nvlist_next_nvpair(result, NULL);
	while (err == PM_SUCCESS && elp != NULL) {
		/* Get the list of values for the current property */
		if ((errno = nvpair_value_nvlist(elp, &nvl)) != 0) {
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "%s: %s value not found\n", __FUNCTION__,
			    nvpair_name(elp));
			return (PM_ERROR_NVLIST);
		}

		/* Get the property group name, if any, from the list */
		pgname = NULL;
		(void) nvlist_lookup_string(nvl, PM_PROP_PGNAME, &pgname);

		if (authority == PM_AUTHORITY_ALL ||
		    log_level > POWERADM_LOG_LEVEL) {
			char	buf[maxwidth + 1];

			/*
			 * Multiple values may be printed. Emit the full
			 * property name specifier as well.
			 */
			if (pgname != NULL) {
				/*
				 * Print the fully-qualified property name,
				 * including the property group.  Align for
				 * the length of power service properties.
				 */
				(void) snprintf(buf, sizeof (buf), "%s%s%s",
				    pgname, PM_SEP_STR, nvpair_name(elp));
			} else {
				(void) snprintf(buf, sizeof (buf), "%s",
				    nvpair_name(elp));
			}
			(void) printf("%-*s ", maxwidth, buf);
		}

		/*
		 * The value list consists of authorities and property values,
		 * with a possible property group name. Find the valid
		 * authorities and print the values with a possible separator
		 * if there are multiple values to emit.
		 */
		emit_sep = B_FALSE;
		for (nvp = nvlist_next_nvpair(nvl, NULL);
		    err == PM_SUCCESS && nvp != NULL;
		    nvp = nvlist_next_nvpair(nvl, nvp)) {
			pm_authority_t	curauth;

			curauth = pm_authority_get(nvpair_name(nvp));
			if (curauth == PM_AUTHORITY_INVALID) {
				/* This is not a value to emit */
				continue;
			}
			if (authority == PM_AUTHORITY_ALL ||
			    authority == curauth) {
				err = emit_nvpair_value(nvp, authority,
				    emit_sep);
				emit_sep = B_TRUE;
			}
		}
		(void) printf("\n");

		/* Emit the next nvpair */
		elp = nvlist_next_nvpair(result, elp);
	}

	return (err);
}


static pm_error_t
emit_nvpair_value(nvpair_t *nvp, pm_authority_t authority, boolean_t emit_sep)
{
	pm_error_t	err;
	data_type_t	ptype;
	boolean_t	valb;
	char		*vals;
	uint64_t	valu;

	/* A separator is required between the previous value and this value */
	if (emit_sep) {
		(void) printf(", ");
	}

	/*
	 * Emit the authority if multiple authorities are requested, or in
	 * the case where the verbose option was specified on the command line
	 */
	if (authority == PM_AUTHORITY_ALL ||
	    log_level > POWERADM_LOG_LEVEL) {
		(void) printf("%s=", nvpair_name(nvp));
	}

	err = PM_SUCCESS;
	ptype = nvpair_type(nvp);
	switch (ptype) {
	case DATA_TYPE_BOOLEAN_VALUE:
		errno = nvpair_value_boolean_value(nvp, &valb);
		if (errno != 0) {
			err = PM_ERROR_NVLIST;
		} else {
			if (valb == B_TRUE) {
				(void) printf("%s", PM_TRUE_STR);
			} else {
				(void) printf("%s", PM_FALSE_STR);
			}
		}
		break;

	case DATA_TYPE_STRING:
		errno = nvpair_value_string(nvp, &vals);
		if (errno != 0) {
			err = PM_ERROR_NVLIST;
		} else {
			(void) printf("%s", vals);
		}
		break;

	case DATA_TYPE_UINT64:
		errno = nvpair_value_uint64(nvp, &valu);
		if (errno != 0) {
			err = PM_ERROR_NVLIST;
		} else {
			(void) printf("%llu", valu);
		}
		break;

	default:
		uu_dprintf(log, UU_DPRINTF_DEBUG,
		    "unexpected nvlist type %d encountered\n", (int)ptype);
		err = PM_ERROR_INVALID_TYPE;
		break;
	}

	return (err);
}


/*ARGSUSED1*/
static pm_error_t
poweradm_do_list(int argc, char *argv[], int *exitcode)
{
	pm_error_t	err;
	nvlist_t	*result = NULL;

	uu_dprintf(log, UU_DPRINTF_DEBUG, "%s enter with argc %d\n",
	    __FUNCTION__, argc);

	/* Set the exit code to assume failure */
	*exitcode = POWERADM_EXIT_ERROR;
	if (argc > 1) {
		/* Additional invalid options were specified */
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	/* Retrieve all of the properties from libpower */
	err = pm_listprop(&result);
	if (err == PM_SUCCESS) {
		/* Print the result and set the exit code for success */
		err = emit_result(result, PM_AUTHORITY_ALL);
		if (err == PM_SUCCESS) {
			*exitcode = POWERADM_EXIT_SUCCESS;
		}
	}
	if (result != NULL) {
		nvlist_free(result);
	}

	return (err);
}


static pm_error_t
poweradm_do_set(int argc, char *argv[], int *exitcode)
{
	int		i;
	char		*cp;
	char		*argp;
	char 		*valp;
	nvlist_t	*nvl;
	pm_error_t	err;
	char		buf[ARG_MAX];

	uu_dprintf(log, UU_DPRINTF_DEBUG, "%s enter with argc %d\n",
	    __FUNCTION__, argc);

	/* Set the exit code to assume failure */
	*exitcode = POWERADM_EXIT_ERROR;
	if (argc < 2) {
		/* There were no property names listed for retrieval */
		errno = EINVAL;
		uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
		    gettext("one or more property names are required for this "
		    "command"));
		return (PM_ERROR_MISSING_PROPERTY_NAME);
	}

	/*
	 * We know there is at least 1 property name, so allocate a list
	 * to contain the new properties and values.
	 */
	nvl = NULL;
	errno = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0);
	if (errno != 0) {
		uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
		    gettext("failed to allocate memory"));
		uu_dprintf(log, UU_DPRINTF_DEBUG,
		    "%s encountered a fatal error %d (%s)\n",
		    __FUNCTION__, errno, strerror(errno));
		err = PM_ERROR_NVLIST;
	}

	/*
	 * Each entry contains a "name=value" argument. Go through
	 * them all and build a list of properties to set.
	 */
	err = PM_SUCCESS;
	i = 1;
	while (err == PM_SUCCESS && i < argc) {
		/* Make a copy of the option because we may need to modify it */
		bzero(buf, sizeof (buf));
		(void) strncpy(buf, argv[i], sizeof (buf));

		/*
		 * Validate the argument is not an option specifier and
		 * is not going to cause an overflow problem.
		 */
		if (buf[0] == '-') {
			/* An option was specified and none are valid */
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "an option was specified and none are allowed\n");
			err = PM_ERROR_USAGE;
			break;
		}
		if (strnlen(argv[i], sizeof (buf) + 1) >= sizeof (buf)) {
			errno = E2BIG;
			uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
			    gettext("property name and value too long"));
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "%s encountered a fatal error %d (%s)\n",
			    __FUNCTION__, errno, strerror(errno));

			err = PM_ERROR_INVALID_PROPERTY_VALUE;
			break;
		}

		/*
		 * Separate the value from the property and eliminate
		 * excess whitespace from the value.
		 */
		valp = NULL;
		argp = buf;
		cp = strchr(buf, '=');
		if (cp == NULL) {
			errno = EINVAL;
			err = PM_ERROR_MISSING_PROPERTY_VALUE;
			break;
		}
		err = poweradm_set_trimvalue(cp, &valp);
		if (err != PM_SUCCESS || valp == NULL || strlen(valp) == 0) {
			errno = EINVAL;
			err = PM_ERROR_INVALID_PROPERTY_VALUE;
			break;
		}
		*cp = '\0';

		/*
		 * Add the property and value to the list to send to libpower
		 */
		uu_dprintf(log, UU_DPRINTF_DEBUG, "adding %s value %s\n",
		    argp, valp);
		errno = nvlist_add_string(nvl, argp, valp);
		if (errno != 0) {
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "%s add to %p failed\n", __FUNCTION__, (void *)nvl);
			uu_dprintf(log, UU_DPRINTF_DEBUG,
			    "%s encountered a fatal error %d (%s)\n",
			    __FUNCTION__, errno, strerror(errno));
			err = PM_ERROR_NVLIST;
			break;
		}

		/* Check the next argument */
		i++;
	}

	if (err == PM_SUCCESS) {
		/*
		 * The property list was built correctly. Submit the property
		 * names and values to libpower to send to SMF.
		 */
		err = pm_setprop(nvl);
		if (err == PM_SUCCESS) {
			/*
			 * Emit a warning about adminstrative authority if
			 * necessary
			 */
			poweradm_set_warn(nvl);

			/*
			 * The set was successful. Reset the exit code to
			 * success
			 */
			*exitcode = POWERADM_EXIT_SUCCESS;
		}
	}

	/* Clean up and return the error code */
	if (nvl != NULL) {
		nvlist_free(nvl);
	}

	return (err);
}


/*
 * Strip leading and trailing white space from the given value string and
 * return a pointer to the new beginning of the string or and appropriate
 * error indication.
 */
static pm_error_t
poweradm_set_trimvalue(char *cp, char **result)
{
	size_t	len;

	*result = NULL;
	if (cp == NULL) {
		return (PM_ERROR_MISSING_PROPERTY_VALUE);
	}

	/* Strip leading white space */
	cp++;
	while (cp != '\0' && isspace(*cp)) {
		cp++;
	}

	/* Strip trailing white space */
	len = strlen(cp);
	if (len == 0) {
		return (PM_ERROR_INVALID_PROPERTY_VALUE);
	}

	while (cp != '\0' && len > 0 && isspace(cp[len])) {
		cp[len] = '\0';
		len = strlen(cp);
	}
	if (len == 0) {
		return (PM_ERROR_INVALID_PROPERTY_VALUE);
	}
	*result = cp;

	return (PM_SUCCESS);
}


/*
 * Values set by poweradm will only take effect if the authority is set to
 * SMF.  When running in verbose mode, print a warning to the user to indicate
 * this if the authority is not set to SMF.
 */
static void
poweradm_set_warn(nvlist_t *nvl)
{
	pm_error_t	err;
	const char	*authname;
	char		*authstr;
	char		*propname;
	nvlist_t	*result = NULL;
	nvlist_t	*vallist;
	nvlist_t	*query;

	if (log_level < UU_DPRINTF_WARNING) {
		/*
		 * An administrative warning is only required if verbose
		 * output is requested.
		 */
		return;
	}

	if (nvlist_lookup_string(nvl, PM_PROP_AUTHORITY, &authstr) == 0 &&
	    pm_authority_get(authstr) == PM_AUTHORITY_SMF) {
		/*
		 * This set operation changed the adminstrative authority to
		 * SMF. No warning is required.
		 */
		uu_dprintf(log, UU_DPRINTF_DEBUG, "%s authority is SMF\n",
		    __FUNCTION__);
		return;
	}

	/* Get the current administrative authority from libpower */
	authname = pm_authority_getname(PM_AUTHORITY_CURRENT);
	propname = PM_PROP_AUTHORITY;
	errno = 0;
	if (nvlist_alloc(&query, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_string(query, PM_PROP_AUTHORITY, authname) != 0 ||
	    nvlist_add_string_array(query, PM_PROP_QUERY, &propname, 1) != 0) {
		/*
		 * Something went wrong inside nvlist. All of the properties
		 * were validated above.
		 */
		if (query != NULL) {
			nvlist_free(query);
		}
		uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
		    gettext("failed to create query"));
		return;
	}
	err = pm_getprop(&result, query);

	authstr = NULL;
	if (err == PM_SUCCESS &&
	    nvlist_lookup_nvlist(result, PM_PROP_AUTHORITY, &vallist) == 0 &&
	    nvlist_lookup_string(vallist, PM_AUTHORITY_CURRENT_STR,
	    &authstr) == 0 && pm_authority_get(authstr) != PM_AUTHORITY_SMF) {
		/*
		 * The administrative authority is not SMF. Warn the user that
		 * the change will not take effect.
		 */
		uu_dprintf(log, UU_DPRINTF_DEBUG, "%s current authority %s\n",
		    __FUNCTION__, authstr);
		uu_dprintf(log, UU_DPRINTF_WARNING, "%s\n",
		    gettext("the administrative-authority is not \"smf\"."));
	}
	if (query != NULL) {
		nvlist_free(query);
	}
	if (result != NULL) {
		nvlist_free(result);
	}
}


static pm_error_t
poweradm_do_start(int argc, char *argv[], int *exitcode)
{
	pm_error_t	err;

	uu_dprintf(log, UU_DPRINTF_DEBUG, "%s enter with argc %d\n",
	    __FUNCTION__, argc);

	/* Set the exit code to assume failure */
	*exitcode = POWERADM_EXIT_ERROR;
	if (argc > 1) {
		/* Additional invalid options were specified */
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	/*
	 * Attempt to initialize the suspend enable property for this machine
	 * using the machine properties and whitelist.
	 */
	err = pm_init_suspend();
	if (err == PM_SUCCESS) {
		/*
		 * We have initialized the service. Now push the values to
		 * the kernel to start the service.
		 */
		err = poweradm_do_update(argc, argv, exitcode);
	}
	switch (err) {
	case PM_SUCCESS:
		*exitcode = SMF_EXIT_OK;
		break;

	case PM_ERROR_SCF:
		/*
		 * The initialization failed. Exit with the appropriate code to
		 * have SMF take the appropriate action.
		 */
		*exitcode = SMF_EXIT_ERR_FATAL;
		if (errno == EPERM) {
			uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
			    strerror(errno));
			*exitcode = SMF_EXIT_ERR_PERM;
		}
		break;

	default:
		/* Leave the exit code at failure and return the error */
		break;
	}

	return (err);
}


/*ARGSUSED1*/
static pm_error_t
poweradm_do_update(int argc, char *argv[], int *exitcode)
{
	pm_error_t	err;

	uu_dprintf(log, UU_DPRINTF_DEBUG, "%s enter with argc %d\n",
	    __FUNCTION__, argc);

	/* Set the default exit code to a fatal error */
	*exitcode = SMF_EXIT_ERR_FATAL;
	if (argc > 1) {
		/* Additional invalid options were specified */
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	/* The libpower update routine handles all update operations */
	err = pm_update();
	uu_dprintf(log, UU_DPRINTF_DEBUG, "%s pm_update returned %d (%s)\n",
	    __FUNCTION__, err, pm_strerror(err));
	switch (err) {
	case PM_SUCCESS:
		*exitcode = SMF_EXIT_OK;
		break;

	case PM_ERROR_SYSTEM:
		if (errno == ENOTSUP) {
			/*
			 * This will be returned by the pm driver if
			 * suspend-enable fails to activate. This is considered
			 * a non-fatal error and we will not place the power
			 * service in to maintenance mode when this occurs.
			 */
			*exitcode = SMF_EXIT_OK;
			break;
		}
		/*
		 * The errno is not ENOTSUP, so we will place the service
		 * in to maintenance mode in the same was as we would for
		 * an SCF error.
		 */

	/* FALLTHROUGH */
	case PM_ERROR_SCF:
		/*
		 * The initialization failed. Exit with the appropriate code to
		 * put the service in to maintenance mode or not.
		 */
		*exitcode = SMF_EXIT_ERR_FATAL;
		if (errno == EPERM) {
			uu_dprintf(log, UU_DPRINTF_FATAL, "%s\n",
			    strerror(errno));
			*exitcode = SMF_EXIT_ERR_PERM;
		}
		break;

	default:
		/* Leave the exit code at failure and return the error */
		break;
	}

	return (err);
}
