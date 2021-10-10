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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <libintl.h>
#include <alloca.h>
#include <getopt.h>
#include <libhotplug.h>
#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/ddi_hp.h>

#if !defined(TEXT_DOMAIN)		/* should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it wasn't */
#endif

/*
 * Define internal commands.
 */
typedef enum {
	CMD_LIST,
	CMD_ONLINE,
	CMD_OFFLINE,
	CMD_ENABLE,
	CMD_DISABLE,
	CMD_POWERON,
	CMD_POWEROFF,
	CMD_GET,
	CMD_SET,
	CMD_INSTALL,
	CMD_UNINSTALL,
	CMD_CHANGESTATE
} cmd_t;

typedef struct target_elem {
	hp_node_t		node;
	int			dupnum;
	struct target_elem	*next;
} target_elem_t;

/*
 * Define callback argument used by open_target().
 */
typedef struct {
	const char	*path;
	hp_node_t	root;
	hp_node_t	target;
	int		target_num;
	target_elem_t	*target_list;
} open_cbarg_t;

/*
 * Function prototypes.
 */
static int	cmd_list(cmd_t, int, char **, const char *);
static int	cmd_changestate(cmd_t, int, char **, const char *);
static int	cmd_private(cmd_t, int, char **, const char *);
static int	cmd_install(cmd_t, int, char **, const char *);
static int	cmd_changestate_hidden(cmd_t, int, char **, const char *);
static void	parse_common(int, char **, const char *);
static void	parse_flags(int, char **, int *, const char *);
static void	parse_target(int, char **, char **, char **, const char *);
static void	bad_option(int, int, const char *);
static void	usage(const char *);
static int	open_target(char *, char *, int, open_cbarg_t *);
static int	choose_target(char *, open_cbarg_t *);
static int	list_cb(hp_node_t, void *);
static int	list_long_cb(hp_node_t, void *);
static int	find_connector_cb(hp_node_t, void *);
static void	destroy_target_list(open_cbarg_t *);
static void	list_table_print(open_cbarg_t *, int);
static int	list_table_usage_cb(hp_node_t, void *);
static int	error_cb(hp_node_t, void *);
static void	print_indent(hp_node_t);
static void	print_options(const char *);
static void	print_error(int);
static int	state_atoi(char *);
static char	*state_itoa(int);
static short	valid_target(int);

/*
 * Define a conversion table for hotplug states.
 */
typedef struct {
	int	state;
	char	*state_str;
	short	valid_target;
} hpstate_t;

static hpstate_t hpstates[] = {
	{ DDI_HP_CN_STATE_EMPTY,		"EMPTY",		  0 },
	{ DDI_HP_CN_STATE_PRESENT,		"PRESENT",		  1 },
	{ DDI_HP_CN_STATE_POWERED,		"POWERED",		  1 },
	{ DDI_HP_CN_STATE_ENABLED,		"ENABLED",		  1 },
	{ DDI_HP_CN_STATE_PORT_EMPTY,		"PORT-EMPTY",		  0 },
	{ DDI_HP_CN_STATE_PORT_PRESENT,		"PORT-PRESENT",		  1 },
	{ DDI_HP_CN_STATE_OFFLINE,		"OFFLINE",		  1 },
	{ DDI_HP_CN_STATE_ATTACHED,		"ATTACHED",		  0 },
	{ DDI_HP_CN_STATE_MAINTENANCE,		"MAINTENANCE",		  0 },
	{ DDI_HP_CN_STATE_MAINTENANCE_SUSPENDED, "MAINTENANCE-SUSPENDED", 1 },
	{ DDI_HP_CN_STATE_ONLINE,		"ONLINE",		  1 },
	{ 0, 0, 0 }
};

/*
 * Define tables of supported subcommands.
 */
typedef struct {
	char		*cmd_str;
	cmd_t		cmd;
	int		(*func)(cmd_t cmd, int argc, char *argv[],
			    const char *usage_str);
	char		*usage_str;
} subcmd_t;

static subcmd_t subcmds[] = {
	{ "list", CMD_LIST, cmd_list,
	    "list         [-c] [-d] [-l] [-v] [<path>] [<connection>]" },
	{ "online", CMD_ONLINE, cmd_changestate,
	    "online       <path> <port>" },
	{ "offline", CMD_OFFLINE, cmd_changestate,
	    "offline      [-f] [-q] <path> <port>" },
	{ "enable", CMD_ENABLE, cmd_changestate,
	    "enable       [<path>] <connector>" },
	{ "disable", CMD_DISABLE, cmd_changestate,
	    "disable      [-f] [-q] [<path>] <connector>" },
	{ "poweron", CMD_POWERON, cmd_changestate,
	    "poweron      [<path>] <connector>" },
	{ "poweroff", CMD_POWEROFF, cmd_changestate,
	    "poweroff     [-f] [-q] [<path>] <connector>" },
	{ "get", CMD_GET, cmd_private,
	    "get          -o <options> [<path>] <connector>" },
	{ "set", CMD_SET, cmd_private,
	    "set          -o <options> [<path>] <connector>" },
	{ "install", CMD_INSTALL, cmd_install,
	    "install      <path> <port>" },
	{ "uninstall", CMD_UNINSTALL, cmd_install,
	    "uninstall    [-f] [-q] <path> <port>" }
};

static subcmd_t hidden_subcmds[] = {
	{ "changestate", CMD_CHANGESTATE, cmd_changestate_hidden,
	    "changestate  [-f] [-q] -s <state> [-o <state-priv>] "
	    "<path> <connection>" }
};

/*
 * Define tables of command line options.
 */
static const struct option common_opts[] = {
	{ "help",	no_argument,		0, '?' },
	{ "version",	no_argument,		0, 'V' },
	{ 0, 0, 0, 0 }
};

static const struct option list_opts[] = {
	{ "connectors",	no_argument,		0, 'c' },
	{ "drivers",	no_argument,		0, 'd' },
	{ "list-path",	no_argument,		0, 'l' },
	{ "verbose",	no_argument,		0, 'v' },
	{ 0, 0,	0, 0 }
};

static const struct option flag_opts[] = {
	{ "force",	no_argument,		0, 'f' },
	{ "query",	no_argument,		0, 'q' },
	{ 0, 0,	0, 0 }
};

static const struct option private_opts[] = {
	{ "options",	required_argument,	0, 'o' },
	{ 0, 0,	0, 0 }
};

static const struct option changestate_opts[] = {
	{ "force",	no_argument,		0, 'f' },
	{ "query",	no_argument,		0, 'q' },
	{ "state",	required_argument,	0, 's' },
	{ "state-priv", required_argument,	0, 'o' },
	{ 0, 0,	0, 0 }
};


/*
 * Define flags for 'list' subcommand.
 */
#define	LIST_HEADER	0x01
#define	LIST_PATHS	0x02
#define	LIST_DRIVERS	0x04
#define	LIST_VERBOSE	0x08
#define	LIST_CONNECTORS	0x10

/*
 * Define exit codes.
 */
#define	EXIT_OK		0
#define	EXIT_EINVAL	1	/* invalid arguments */
#define	EXIT_ENOENT	2	/* path or connection doesn't exist */
#define	EXIT_FAILED	3	/* operation failed */
#define	EXIT_UNAVAIL	4	/* service not available */
#define	EXIT_AMBIGUOUS	5	/* ambiguous user input */

/*
 * Global variables.
 */
static char 	*prog;
static char	version[] = "1.0";
extern int	errno;

/*
 * main()
 *
 *	The main routine determines which subcommand is used,
 *	and dispatches control to the corresponding function.
 */
int
main(int argc, char *argv[])
{
	int 		i, rv;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((prog = strrchr(argv[0], '/')) == NULL)
		prog = argv[0];
	else
		prog++;

	if (argc < 2) {
		usage(NULL);
		return (EXIT_EINVAL);
	}

	parse_common(argc, argv, NULL);

	/* Check the list of defined subcommands. */
	for (i = 0; i < (sizeof (subcmds) / sizeof (subcmd_t)); i++) {
		if (strcmp(argv[1], subcmds[i].cmd_str) == 0) {
			rv = subcmds[i].func(subcmds[i].cmd, argc - 1, &argv[1],
			    subcmds[i].usage_str);
			goto finished;
		}
	}

	/* Check the list of hidden subcommands. */
	for (i = 0; i < (sizeof (hidden_subcmds) / sizeof (subcmd_t)); i++) {
		if (strcmp(argv[1], hidden_subcmds[i].cmd_str) == 0) {
			rv = hidden_subcmds[i].func(hidden_subcmds[i].cmd,
			    argc - 1, &argv[1], hidden_subcmds[i].usage_str);
			goto finished;
		}
	}

	/* No matching subcommand found. */
	(void) fprintf(stderr, gettext("ERROR: %s: unknown subcommand '%s'\n"),
	    prog, argv[1]);
	usage(NULL);
	exit(EXIT_EINVAL);

finished:
	/* Determine exit code */
	switch (rv) {
	case 0:
		break;
	case EINVAL:
		return (EXIT_EINVAL);
	case ENXIO:
	case ENOENT:
		return (EXIT_ENOENT);
	case EBADF:
		return (EXIT_UNAVAIL);
	case ENOTTY:
		return (EXIT_AMBIGUOUS);
	default:
		return (EXIT_FAILED);
	}

	return (EXIT_OK);
}

/*
 * cmd_list()
 *
 *	Subcommand to list hotplug information.
 */
/*ARGSUSED*/
static int
cmd_list(cmd_t cmd, int argc, char *argv[], const char *usage_str)
{
	char		*path = NULL;
	char		*connection = NULL;
	int		list_flags = 0;
	int		flags = 0;
	int		opt, rv;
	open_cbarg_t	cbarg;

	/* Parse command line options */
	parse_common(argc, argv, usage_str);
	while ((opt = getopt_clip(argc, argv, "cdlv", list_opts, NULL)) != -1) {
		switch (opt) {
		case 'c':
			list_flags |= LIST_CONNECTORS;
			break;
		case 'd':
			list_flags |= LIST_DRIVERS;
			break;
		case 'l':
			list_flags |= LIST_PATHS;
			break;
		case 'v':
			list_flags |= LIST_VERBOSE;
			flags |= HPINFOUSAGE;
			break;
		default:
			bad_option(opt, optopt, usage_str);
			break;
		}
	}

	parse_target(argc, argv, &path, &connection, usage_str);

	/* Default path is "/" */
	if (path == NULL)
		path = "/";

	/* Open hotplug information */
	if ((rv = open_target(path, connection, flags, &cbarg)) != 0) {
		print_error(rv);
		return (rv);
	}

	/* Display hotplug information */
	if (list_flags & LIST_CONNECTORS) {
		/*
		 * If cbarg.target_list is NULL, it means path is NULL or
		 * the specified connection is port, then it prints all
		 * connectors under "/" or the specified port.
		 * Otherwise, it means that path != NULL, then prints the
		 * target list got from find_connector_cb().
		 */
		if (cbarg.target_list == NULL) {
			(void) hp_traverse(cbarg.root, &cbarg,
			    find_connector_cb);
		}

		if (list_flags & LIST_DRIVERS)
			flags |= HPINFOUSAGE;

		list_table_print(&cbarg, list_flags);

	} else {
		int (* func_cb)(hp_node_t node, void *arg);
		func_cb = list_cb;
		if (list_flags & LIST_PATHS)
			func_cb = list_long_cb;

		if (cbarg.target_list == NULL) {
			(void) hp_traverse(cbarg.target, &list_flags,
			    func_cb);
		} else {
			target_elem_t *tmp;
			tmp = cbarg.target_list;
			while (tmp != NULL) {
				(void) hp_traverse(tmp->node,
				    &list_flags, func_cb);
				tmp = tmp->next;
			}
		}
	}

	/* Discard hotplug information snapshot and target list */
	destroy_target_list(&cbarg);
	hp_fini(cbarg.root);

	return (0);
}

/*
 * cmd_changestate()
 *
 *      Main subcommand to perform a change of state operation.
 */
static int
cmd_changestate(cmd_t cmd, int argc, char *argv[], const char *usage_str)
{
	hp_node_t	results = NULL;
	char		*path = NULL;
	char		*connection = NULL;
	int		flags = 0;
	int		rv, state;
	open_cbarg_t	cbarg;

	/* Parse command line options */
	parse_common(argc, argv, usage_str);
	if ((cmd == CMD_OFFLINE) ||
	    (cmd == CMD_DISABLE) ||
	    (cmd == CMD_POWEROFF))
		parse_flags(argc, argv, &flags, usage_str);
	parse_target(argc, argv, &path, &connection, usage_str);

	/* Path is always required */
	if (path == NULL) {
		(void) fprintf(stderr, gettext("ERROR: too few arguments.\n"));
		usage(usage_str);
		return (EINVAL);
	}

	/* Select target state */
	switch (cmd) {
	case CMD_ONLINE:
		state = DDI_HP_CN_STATE_ONLINE;
		break;
	case CMD_OFFLINE:
		state = DDI_HP_CN_STATE_OFFLINE;
		break;
	case CMD_ENABLE:
		state = DDI_HP_CN_STATE_ENABLED;
		break;
	case CMD_DISABLE:
	case CMD_POWERON:
		state = DDI_HP_CN_STATE_POWERED;
		break;
	case CMD_POWEROFF:
		state = DDI_HP_CN_STATE_PRESENT;
		break;
	default:
		return (EINVAL);
	}

	/* Open the target hotplug node */
	if ((rv = open_target(path, connection, flags, &cbarg)) != 0) {
		print_error(rv);
		return (rv);
	}

	if (cbarg.target_num > 1) {
		if (!isatty(0)) {
			hp_fini(cbarg.root);
			return (ENOTTY);
		}

		if ((rv = choose_target(path, &cbarg)) != 0) {
			print_error(rv);
			hp_fini(cbarg.root);
			return (rv);
		}
	}
	destroy_target_list(&cbarg);

	/* Verify target */
	switch (cmd) {
	case CMD_ONLINE:
	case CMD_OFFLINE:
		if (hp_type(cbarg.target) == HP_NODE_PORT)
			break;
		(void) fprintf(stderr,
		    gettext("ERROR: invalid target (must be a port).\n"));
		hp_fini(cbarg.root);
		return (EINVAL);
	case CMD_ENABLE:
	case CMD_DISABLE:
	case CMD_POWERON:
	case CMD_POWEROFF:
		if (hp_type(cbarg.target) == HP_NODE_CONNECTOR)
			break;
		(void) fprintf(stderr,
		    gettext("ERROR: invalid target (must be a connector).\n"));
		hp_fini(cbarg.root);
		return (EINVAL);
	}

	/* Avoid invalid transitions */
	if (((cmd == CMD_DISABLE) &&
	    (hp_state(cbarg.target) != DDI_HP_CN_STATE_ENABLED)) ||
	    ((cmd == CMD_POWERON) &&
	    (hp_state(cbarg.target) >= DDI_HP_CN_STATE_POWERED))) {
		hp_fini(cbarg.root);
		return (0);
	}

	/* Do state change */
	rv = hp_set_state(cbarg.target, flags, state, NULL, &results);

	/* Display results */
	if ((cmd == CMD_ONLINE) && (rv == EIO))
		(void) fprintf(stderr, gettext("ERROR: failed to attach device "
		    "drivers or other internal errors.\n"));
	else if (rv != 0)
		print_error(rv);

	if (results != NULL) {
		(void) hp_traverse(results, NULL, error_cb);
		hp_fini(results);
	}

	/* Discard hotplug information snapshot */
	hp_fini(cbarg.root);

	return (rv);
}

/*
 * cmd_private()
 *
 *      Subcommand to get or set bus private options.
 */
static int
cmd_private(cmd_t cmd, int argc, char *argv[], const char *usage_str)
{
	char		*path = NULL;
	char		*connection = NULL;
	char		*options = NULL;
	char		*results = NULL;
	int		opt, rv;
	open_cbarg_t	cbarg;

	/* Parse command line options */
	parse_common(argc, argv, usage_str);
	while ((opt = getopt_clip(argc, argv, "o:", private_opts, NULL)) != -1)
		switch (opt) {
		case 'o':
			options = optarg;
			break;
		default:
			bad_option(opt, optopt, usage_str);
			break;
		}
	parse_target(argc, argv, &path, &connection, usage_str);

	/* Options, path, and connection are all required */
	if ((options == NULL) || (path == NULL) || (connection == NULL)) {
		(void) fprintf(stderr, gettext("ERROR: too few arguments.\n"));
		usage(usage_str);
		return (EINVAL);
	}

	/* Open the target hotplug node */
	if ((rv = open_target(path, connection, 0, &cbarg)) != 0) {
		print_error(rv);
		return (rv);
	}

	if (cbarg.target_num > 1) {
		if (!isatty(0)) {
			hp_fini(cbarg.root);
			return (ENOTTY);
		}

		if ((rv = choose_target(path, &cbarg)) != 0) {
			print_error(rv);
			hp_fini(cbarg.root);
			return (rv);
		}
	}
	destroy_target_list(&cbarg);

	/* Verify target is a connector */
	if (hp_type(cbarg.target) != HP_NODE_CONNECTOR) {
		(void) fprintf(stderr,
		    gettext("ERROR: invalid target (must be a connector).\n"));
		hp_fini(cbarg.root);
		return (EINVAL);
	}

	/* Do the operation */
	if (cmd == CMD_SET)
		rv = hp_set_private(cbarg.target, options, &results);
	else
		rv = hp_get_private(cbarg.target, options, &results);

	/* Display results */
	if (rv == ENOTSUP) {
		(void) fprintf(stderr,
		    gettext("ERROR: unsupported property name or value.\n"));
		(void) fprintf(stderr,
		    gettext("(Properties may depend upon connector state.)\n"));
	} else if (rv != 0) {
		print_error(rv);
	}
	if (results != NULL) {
		print_options(results);
		free(results);
	}

	/* Discard hotplug information snapshot */
	hp_fini(cbarg.root);

	return (rv);
}

/*
 * cmd_install()
 *
 *	Subcommand to install or uninstall dependents.
 */
static int
cmd_install(cmd_t cmd, int argc, char *argv[], const char *usage_str)
{
	hp_node_t	results = NULL;
	char		*path = NULL;
	char		*connection = NULL;
	int		flags = 0;
	int		rv;
	open_cbarg_t	cbarg;

	/* Parse command line options */
	parse_common(argc, argv, usage_str);
	if (cmd == CMD_UNINSTALL)
		parse_flags(argc, argv, &flags, usage_str);
	parse_target(argc, argv, &path, &connection, usage_str);

	/* Path and connection are required */
	if ((path == NULL) || (connection == NULL)) {
		(void) fprintf(stderr, gettext("ERROR: too few arguments.\n"));
		usage(usage_str);
		return (EINVAL);
	}

	/* Open the target hotplug node */
	if ((rv = open_target(path, connection, flags, &cbarg)) != 0) {
		print_error(rv);
		return (rv);
	}

	/* Verify target is a port */
	if (hp_type(cbarg.target) != HP_NODE_PORT) {
		(void) fprintf(stderr,
		    gettext("ERROR: invalid target (must be a port).\n"));
		hp_fini(cbarg.root);
		return (EINVAL);
	}

	/* Do the install or uninstall */
	if (cmd == CMD_INSTALL)
		rv = hp_install(cbarg.target, 0, &results);
	else
		rv = hp_uninstall(cbarg.target, flags, &results);

	/* Display results */
	print_error(rv);
	if (results != NULL) {
		(void) hp_traverse(results, NULL, error_cb);
		hp_fini(results);
	}

	/* Discard hotplug information snapshot */
	hp_fini(cbarg.root);

	return (rv);
}

/*
 * cmd_changestate_hidden()
 *
 *	Subcommand to initiate a state change operation.  This is
 *	a hidden subcommand to directly set a connector or port to
 *	a specific target state.
 */
/*ARGSUSED*/
static int
cmd_changestate_hidden(cmd_t cmd, int argc, char *argv[], const char *usage_str)
{
	hp_node_t	results = NULL;
	char		*path = NULL;
	char		*connection = NULL;
	char		*state_priv = NULL;
	int		state = -1;
	int		flags = 0;
	int		opt, rv;
	open_cbarg_t	cbarg;

	/* Parse command line options */
	parse_common(argc, argv, usage_str);
	while ((opt = getopt_clip(argc, argv, "fqs:o:", changestate_opts,
	    NULL)) != -1) {
		switch (opt) {
		case 'f':
			flags |= HPFORCE;
			break;
		case 'q':
			flags |= HPQUERY;
			break;
		case 's':
			if ((state = state_atoi(optarg)) == -1) {
				(void) printf("ERROR: invalid target state\n");
				return (EINVAL);
			}
			break;
		case 'o':
			state_priv = optarg;
			break;
		default:
			bad_option(opt, optopt, usage_str);
			break;
		}
	}
	parse_target(argc, argv, &path, &connection, usage_str);

	/* State, path, and connection are all required */
	if ((state == -1) || (path == NULL) || (connection == NULL)) {
		(void) fprintf(stderr, gettext("ERROR: too few arguments.\n"));
		usage(usage_str);
		return (EINVAL);
	}

	/* Check that target state is valid */
	if (valid_target(state) == 0) {
		(void) fprintf(stderr,
		    gettext("ERROR: invalid target state\n"));
		return (EINVAL);
	}

	/* Open the target hotplug node */
	if ((rv = open_target(path, connection, flags, &cbarg)) != 0) {
		print_error(rv);
		return (rv);
	}

	if (cbarg.target_num > 1) {
		if (!isatty(0)) {
			hp_fini(cbarg.root);
			return (ENOTTY);
		}

		if ((rv = choose_target(path, &cbarg)) != 0) {
			print_error(rv);
			hp_fini(cbarg.root);
			return (rv);
		}
	}
	destroy_target_list(&cbarg);

	/* Initiate state change operation on root of snapshot */
	rv = hp_set_state(cbarg.target, flags, state, state_priv, &results);

	/* Display results */
	print_error(rv);
	if (results) {
		(void) hp_traverse(results, NULL, error_cb);
		hp_fini(results);
	}

	/* Discard hotplug information snapshot */
	hp_fini(cbarg.root);

	return (rv);
}

/*
 * parse_common()
 *
 *	Parse command line options that are common to the
 *	entire program, and to each of its subcommands.
 */
static void
parse_common(int argc, char *argv[], const char *usage_str)
{
	int		opt;
	extern int	opterr;
	extern int	optind;

	/* Turn off error reporting */
	opterr = 0;

	while ((opt = getopt_clip(argc, argv, "?V", common_opts, NULL)) != -1)
		switch (opt) {
		case '?':
			if (optopt == '?') {
				usage(usage_str);
				exit(0);
			}
			break;
		case 'V':
			(void) printf(gettext("%s: Version %s\n"),
			    prog, version);
			exit(0);
		}

	/* Reset option index */
	optind = 1;
}

/*
 * parse_flags()
 *
 *	Parse command line flags common to all downward state
 *	change operations (offline, disable, poweoff).
 */
static void
parse_flags(int argc, char *argv[], int *flagsp, const char *usage_str)
{
	int	opt;
	int	flags = 0;

	while ((opt = getopt_clip(argc, argv, "fq", flag_opts, NULL)) != -1)
		switch (opt) {
		case 'f':
			flags |= HPFORCE;
			break;
		case 'q':
			flags |= HPQUERY;
			break;
		default:
			bad_option(opt, optopt, usage_str);
			break;
		}

	*flagsp = flags;
}

/*
 * parse_target()
 *
 *	Parse the target path and connection name from the command line.
 */
static void
parse_target(int argc, char *argv[], char **pathp, char **connectionp,
    const char *usage_str)
{
	extern int	optind;

	if (optind < argc)
		*pathp = argv[optind++];

	if (optind < argc)
		*connectionp = argv[optind++];

	if (optind < argc) {
		(void) fprintf(stderr, gettext("ERROR: too many arguments.\n"));
		usage(usage_str);
		exit(EINVAL);
	}
}

/*
 * bad_option()
 *
 *	Routine to handle bad command line options.
 */
static void
bad_option(int opt, int optopt, const char *usage_str)
{
	switch (opt) {
	case ':':
		(void) fprintf(stderr,
		    gettext("ERROR: option '%c' requires an argument.\n"),
		    optopt);
		break;
	default:
		if (optopt == '?') {
			usage(usage_str);
			exit(EXIT_OK);
		}
		(void) fprintf(stderr,
		    gettext("ERROR: unrecognized option '%c'.\n"), optopt);
		break;
	}

	usage(usage_str);

	exit(EXIT_EINVAL);
}

/*
 * usage()
 *
 *	Display general usage of the command.  Including
 *	the usage synopsis of each defined subcommand.
 */
static void
usage(const char *usage_str)
{
	int	i;

	if (usage_str != NULL) {
		(void) fprintf(stderr, gettext("Usage:   %s  %s\n\n"),
		    prog, usage_str);
		return;
	}

	(void) fprintf(stderr, gettext("Usage:  %s  <subcommand> [<args>]\n\n"),
	    prog);

	(void) fprintf(stderr, gettext("Subcommands:\n\n"));

	for (i = 0; i < (sizeof (subcmds) / sizeof (subcmd_t)); i++)
		(void) fprintf(stderr, "   %s\n\n", subcmds[i].usage_str);
}

/*
 * open_target()
 *
 *    Given a path and connection name, select and open the target
 *    node of a hotplug information snapshot.
 */
static int
open_target(char *path, char *connection, int flags, open_cbarg_t *cbarg)
{
	hp_node_t	root;
	target_elem_t	*new;
	int		lerrno;

	/* Initialize results */
	(void) memset(cbarg, 0, sizeof (open_cbarg_t));

	/* Try to open the specified path and connection */
	if ((root = hp_init(path, connection, flags)) != NULL) {

		if (hp_type(root) == HP_NODE_CONNECTOR) {
			new = (target_elem_t *)malloc(sizeof (target_elem_t));
			new->node = root;
			new->dupnum = 1;
			new->next = NULL;
			cbarg->target_list = new;
		}

		cbarg->root = root;
		cbarg->target = root;
		return (0);
	}

	/* Save errno */
	lerrno = errno;

	/*
	 * If only path is specified, attempt to resolve
	 * it as a physical connector with a unique name.
	 */
	if ((path != NULL) && (connection == NULL)) {

		/* Open a full snapshot */
		if ((root = hp_init("/", NULL, flags)) == NULL) {
			print_error(errno);
			return (errno);
		}

		/* Search for a match */
		cbarg->path = path;
		(void) hp_traverse(root, cbarg, find_connector_cb);

		/* If match was found, return it. */
		if (cbarg->target != NULL) {
			cbarg->root = root;
			return (0);
		}

		/* Otherwise, search failed. */
		hp_fini(root);
	}

	return (lerrno);
}

/*
 * choose_target()
 *
 *    Choose the target by the user if multiple connectors with the same
 *    name exist in the system.
 */
static int
choose_target(char *name, open_cbarg_t *cbarg)
{
	target_elem_t	*elem;
	char		path[MAXPATHLEN];
	char		connection[MAXPATHLEN];
	int 		choice;

	/*
	 * If multiple connectors with the same name exist in the system,
	 * the state change operations interact with the user to specify
	 * which connector need to be operated.
	 */
	(void) printf(gettext("There are multiple connectors with the same"
	    " name:\n"));

	elem = cbarg->target_list;
	while (elem != NULL) {
		if (elem->dupnum > 0 && (strcmp(hp_name(elem->node), name)
		    == 0)) {
			(void) hp_path(elem->node, path, connection);
			(void) printf("[%d] %s  %s \n", elem->dupnum,
			    connection, path);
		}
		elem = elem->next;
	}
	(void) printf(gettext("Please select a connector, then press ENTER:"));

	if ((scanf("%d", &choice) != 1) ||
	    (choice < 1) || choice > cbarg->target_num)
		return (EINVAL);

	elem = cbarg->target_list;
	while (elem != NULL) {
		if (elem->dupnum == choice && (strcmp(hp_name(elem->node),
		    name) == 0)) {
			cbarg->target = elem->node;
			break;
		}
		elem = elem->next;
	}

	return (0);
}

/*
 * list_cb()
 *
 *	Callback function for hp_traverse(), to display nodes
 *	of a hotplug information snapshot.  (Short version.)
 */
static int
list_cb(hp_node_t node, void *arg)
{
	int		*flags_p = (int *)arg;
	hp_node_t	parent, child;
	char		*driver;
	int		instance;

	switch (hp_type(node)) {
	case HP_NODE_DEVICE:
		print_indent(node);
		(void) printf("%s", hp_name(node));
		parent = hp_parent(node);
		if (hp_type(parent) == HP_NODE_PORT) {
			(void) printf("  <%s>", hp_name(parent));
			(void) printf("  %s", state_itoa(hp_state(parent)));
		}
		if (*flags_p & LIST_DRIVERS) {
			(void) printf("  ");
			driver = hp_driver(node);
			if (driver != NULL)
				(void) printf("%s", driver);
			instance = hp_instance(node);
			if (instance >= 0)
				(void) printf("#%d", instance);
		}
		(void) printf("\n");
		break;

	case HP_NODE_CONNECTOR:
		print_indent(node);
		(void) printf("[%s]", hp_name(node));
		(void) printf("  %s", state_itoa(hp_state(node)));
		(void) printf("\n");
		break;

	case HP_NODE_PORT:
		if (((child = hp_child(node)) == NULL) ||
		    (hp_type(child) != HP_NODE_DEVICE)) {
			print_indent(node);
			(void) printf("  <%s>  %s\n", hp_name(node),
			    state_itoa(hp_state(node)));
		}
		break;

	case HP_NODE_USAGE:
		print_indent(node);
		(void) printf("{ %s }\n", hp_usage(node));
		break;
	}

	return (HP_WALK_CONTINUE);
}

/*
 * list_long_cb()
 *
 *	Callback function for hp_traverse(), to display nodes
 *	of a hotplug information snapshot.  (Long version.)
 */
static int
list_long_cb(hp_node_t node, void *arg)
{
	int		*flags_p = (int *)arg;
	char		*driver;
	int		instance;
	hp_node_t	parent;
	char		*state_priv;
	char		path[MAXPATHLEN];
	char		connection[MAXPATHLEN];

	if (hp_type(node) != HP_NODE_USAGE) {
		if (hp_path(node, path, connection) != 0)
			return (HP_WALK_CONTINUE);
	}

	switch (hp_type(node)) {
	case HP_NODE_CONNECTOR:
		(void) printf("%s", path);
		(void) printf(" [%s]", connection);
		(void) printf("  %s", state_itoa(hp_state(node)));
		(void) printf("\n");
		break;

	case HP_NODE_PORT:
		break;

	case HP_NODE_DEVICE:
		(void) printf("%s", path);
		parent = hp_parent(node);
		if (parent && hp_type(parent) == HP_NODE_PORT) {
			(void) printf("  <%s>", hp_name(parent));
			state_priv = hp_state_priv(parent);
			if (state_priv && (*flags_p & LIST_VERBOSE)) {
				(void) printf(" %s  (\"%s\")",
				    state_itoa(hp_state(parent)),
				    state_priv);
			} else
				(void) printf("  %s",
				    state_itoa(hp_state(parent)));
		}

		if (*flags_p & LIST_DRIVERS) {
			(void) printf("  ");
			driver = hp_driver(node);
			if (driver != NULL)
				(void) printf("%s", driver);
			instance = hp_instance(node);
			if (instance >= 0)
				(void) printf("#%d", instance);
		}
		(void) printf("\n");
		break;

	case HP_NODE_USAGE:
		(void) printf("    { %s }", hp_usage(node));
		(void) printf("\n");
		break;
	}


	return (HP_WALK_CONTINUE);
}

/*
 * list_table_print()
 *
 *	Callback function for hp_traverse(), to display nodes
 *	of a hotplug information snapshot.  (Table version.)
 */
static void
list_table_print(open_cbarg_t *cbarg, int list_flags)
{
	hp_node_t	node, child;
	static char	path[MAXPATHLEN];
	static char	connection[MAXPATHLEN];
	target_elem_t 	*tmp;
	int		i;

	tmp = cbarg->target_list;

	/* Display header on first iteration */
	if (cbarg->target_num > 1) {
		(void) printf("%-20s %-10s %-15s %s\n", gettext("Connection"),
		    gettext("Note"), gettext("State"), gettext("Description"));
	} else {
		(void) printf("%-20s %-15s %s\n", gettext("Connection"),
		    gettext("State"), gettext("Description"));
	}

	if (list_flags & LIST_PATHS) {
		(void) printf("%-30s\n", gettext("Path"));
	}

	for (i = 0; i < 80; i++)
		(void) printf("_");
	(void) printf("\n");

	while (tmp != NULL) {
		node = tmp->node;
		if (cbarg->target_num > 1) {
			if (tmp->dupnum == 0)
				(void) printf("%-20s %-25s %s\n", hp_name(node),
				    state_itoa(hp_state(node)),
				    hp_description(node));
			else
				(void) printf("%-20s *%-10d %-15s %s\n",
				    hp_name(node), tmp->dupnum,
				    state_itoa(hp_state(node)),
				    hp_description(node));
		} else {
			(void) printf("%-20s %-15s %s\n", hp_name(node),
			    state_itoa(hp_state(node)), hp_description(node));
		}

		if ((list_flags & LIST_PATHS) &&
		    (hp_path(node, path, connection) == 0))
			(void) printf("%s\n", path);

		/* In verbose mode, traverse children for usage */
		if (((list_flags & LIST_VERBOSE) ||
		    (list_flags & LIST_DRIVERS)) &&
		    ((child = hp_child(node)) != NULL)) {
			list_flags &= ~(LIST_HEADER);
			(void) hp_traverse(child, (void *)&list_flags,
			    list_table_usage_cb);
		}
		tmp = tmp->next;
	}

	if (cbarg->target_num > 1) {
		(void) printf(gettext("\nNote:\nThere are multiple connectors"
		    " with the same name:\n"));
		tmp = cbarg->target_list;
		while (tmp != NULL) {
			node = tmp->node;
			if ((tmp->dupnum > 0) &&
			    (hp_path(node, path, connection) == 0))
				(void) printf("[%d]   %-15s %s\n", tmp->dupnum,
				    hp_name(node), path);
			tmp = tmp->next;
		}
	}
}

/*
 * find_connector_cb()
 *
 *	Callback function for hp_traverse(), to setup the list for hotplug
 *	connectors.
 */
static int
find_connector_cb(hp_node_t node, void *arg)
{
	target_elem_t 	*tmp, *prev, *new;
	open_cbarg_t	*cbarg = (open_cbarg_t *)arg;
	char		*name;

	if (hp_type(node) == HP_NODE_CONNECTOR) {

		/*
		 * If cbarg->path != NULL, then setup the connector
		 * list for all connectors with same name.
		 * Otherwise, setup list for all connectors.
		 */
		if (cbarg->path && ((name = hp_name(node)) != NULL) &&
		    (strcmp(name, cbarg->path) != 0))
			return (HP_WALK_CONTINUE);

		new = (target_elem_t *)malloc(sizeof (target_elem_t));
		new->node = node;
		new->dupnum = 1;
		new->next = NULL;

		if (cbarg->target_list == NULL) {
			cbarg->target = node;
			cbarg->target_list = new;
		} else {
			tmp = cbarg->target_list;
			do {
				prev = tmp;
				if (strcmp(hp_name(new->node),
				    hp_name(tmp->node)) == 0) {
					while ((tmp != NULL) &&
					    strcmp(hp_name(new->node),
					    hp_name(tmp->node)) == 0) {
						prev = tmp;
						tmp = tmp->next;
					}
					new->dupnum += prev->dupnum;
					cbarg->target_num = new->dupnum;
					break;
				}
				tmp = tmp->next;
			} while (tmp != NULL);

			new->next = prev->next;
			prev->next = new;
		}
	}

	return (HP_WALK_CONTINUE);
}

/*
 * destroy_target_list()
 *
 *	Destroy the target list.
 */
static void
destroy_target_list(open_cbarg_t *cbarg)
{
	target_elem_t 	*head, *tmp;
	head = cbarg->target_list;
	while (head != NULL) {
		tmp = head;
		head = head->next;
		free(tmp);
	}
	cbarg->target_list = NULL;
}

/*
 * list_table_usage_cb()
 *
 *	Callback function for hp_traverse(), called by list_table_print()
 *	to print a sub-table of dependents of a hotplug connection.
 */
static int
list_table_usage_cb(hp_node_t node, void *arg)
{
	int		*flags_p = (int *)arg;
	hp_node_t	usage;
	char		*driver;
	int		i, instance;
	char		device[25];

	/* Stop traversing at next connector */
	if (hp_type(node) == HP_NODE_CONNECTOR)
		return (HP_WALK_TERMINATE);

	if (hp_type(node) == HP_NODE_DEVICE) {

		/* Display a header on first iteration */
		if ((*flags_p & LIST_HEADER) == 0) {
			(void) printf("     %-25s  %s\n", gettext("Device"),
			    gettext("Usage"));
			(void) printf("     ");
			for (i = 0; i < 75; i++)
				(void) printf("_");
			(void) printf("\n");
		}

		/* Display current device */
		if ((*flags_p & LIST_DRIVERS) &&
		    ((driver = hp_driver(node)) != NULL)) {
			if ((instance = hp_instance(node)) >= 0)
				(void) snprintf(device, 25, "%s#%d", driver,
				    instance);
			else
				(void) snprintf(device, 25, "%s", driver);
			(void) printf("     %-25s  ", device);
		} else {
			(void) printf("     %-25s  ", hp_name(node));
		}

		/* Iterate through usage */
		usage = hp_child(node);
		if (hp_type(usage) == HP_NODE_USAGE) {
			(void) printf("%s\n", hp_usage(usage));
			while (hp_type((usage = hp_sibling(usage))) ==
			    HP_NODE_USAGE) {
				(void) printf("     %25s  %s\n", " ",
				    hp_usage(usage));
			}
		} else {
			(void) printf("-\n");
		}

		/* Suppress header on next iteration */
		*flags_p |= LIST_HEADER;
	}

	return (HP_WALK_CONTINUE);
}

/*
 * error_cb()
 *
 *	Callback function for hp_traverse(), to display
 *	error results from a state change operation.
 */
/*ARGSUSED*/
static int
error_cb(hp_node_t node, void *arg)
{
	hp_node_t	child;
	char		*usage_str;
	static char	path[MAXPATHLEN];
	static char	connection[MAXPATHLEN];

	if (((child = hp_child(node)) != NULL) &&
	    (hp_type(child) == HP_NODE_USAGE)) {
		if (hp_path(node, path, connection) == 0)
			(void) printf("%s:\n", path);
		return (HP_WALK_CONTINUE);
	}

	if ((hp_type(node) == HP_NODE_USAGE) &&
	    ((usage_str = hp_usage(node)) != NULL))
		(void) printf("   { %s }\n", usage_str);

	return (HP_WALK_CONTINUE);
}

/*
 * print_indent()
 *
 *	Print the proper indentation for the specified node.
 *	Called from list_cb().
 */
static void
print_indent(hp_node_t node)
{
	hp_node_t	parent;

	for (parent = hp_parent(node); parent; parent = hp_parent(parent))
		if (hp_type(parent) == HP_NODE_DEVICE)
			(void) printf("    ");
}


/*
 * print_options()
 *
 *	Parse and display bus private options.  The options are
 *	formatted as a string which conforms to the getsubopt(3C)
 *	format.  This routine only splits the string elements as
 *	separated by commas, and displays each portion on its own
 *	separate line of output.
 */
static void
print_options(const char *options)
{
	char	*buf, *curr, *next;
	size_t	len;

	/* Do nothing if options string is empty */
	if ((len = strlen(options)) == 0)
		return;

	/* To avoid modifying the input string, make a copy on the stack */
	if ((buf = (char *)alloca(len + 1)) == NULL) {
		(void) printf("%s\n", options);
		return;
	}
	(void) strlcpy(buf, options, len + 1);

	/* Iterate through each comma-separated name/value pair */
	curr = buf;
	do {
		if ((next = strchr(curr, ',')) != NULL) {
			*next = '\0';
			next++;
		}
		(void) printf("%s\n", curr);
	} while ((curr = next) != NULL);
}

/*
 * print_error()
 *
 *	Common routine to print error numbers in an appropriate way.
 *	Prints nothing if error code is 0.
 */
static void
print_error(int error)
{
	switch (error) {
	case 0:
		/* No error */
		return;
	case EACCES:
		(void) fprintf(stderr,
		    gettext("ERROR: operation not authorized.\n"));
		break;
	case EBADF:
		(void) fprintf(stderr,
		    gettext("ERROR: hotplug service is not available.\n"));
		break;
	case EBUSY:
		(void) fprintf(stderr,
		    gettext("ERROR: devices or resources are busy.\n"));
		break;
	case EEXIST:
		(void) fprintf(stderr,
		    gettext("ERROR: resource already exists.\n"));
		break;
	case EFAULT:
		(void) fprintf(stderr,
		    gettext("ERROR: internal failure in hotplug service.\n"));
		break;
	case EINVAL:
		(void) fprintf(stderr,
		    gettext("ERROR: invalid arguments.\n"));
		break;
	case ENODEV:
		(void) fprintf(stderr,
		    gettext("ERROR: no device is present.\n"));
		break;
	case ENOENT:
		(void) fprintf(stderr,
		    gettext("ERROR: there are no connections to display.\n"));
		(void) fprintf(stderr,
		    gettext("(See hotplug(1m) for more information.)\n"));
		break;
	case ENOTTY:
		(void) fprintf(stderr,
		    gettext("ERROR: STDIN isn't TTY.\n"));
		break;
	case ENXIO:
		(void) fprintf(stderr,
		    gettext("ERROR: no such path or connection.\n"));
		break;
	case ENOMEM:
		(void) fprintf(stderr,
		    gettext("ERROR: not enough memory.\n"));
		break;
	case ENOTSUP:
		(void) fprintf(stderr,
		    gettext("ERROR: operation not supported.\n"));
		break;
	case EIO:
		(void) fprintf(stderr,
		    gettext("ERROR: hardware or driver specific failure.\n"));
		break;
	case ECANCELED :
		(void) fprintf(stderr,
		    gettext("ERROR: The operation is cancelled.\n"));
		break;
	default:
		(void) fprintf(stderr, gettext("ERROR: operation failed: %s\n"),
		    strerror(error));
		break;
	}
}

/*
 * state_atoi()
 *
 *	Convert a hotplug state from a string to an integer.
 */
static int
state_atoi(char *state)
{
	int	i;

	for (i = 0; hpstates[i].state_str != NULL; i++)
		if (strcasecmp(state, hpstates[i].state_str) == 0)
			return (hpstates[i].state);

	return (-1);
}

/*
 * state_itoa()
 *
 *	Convert a hotplug state from an integer to a string.
 */
static char *
state_itoa(int state)
{
	static char	unknown[] = "UNKNOWN";
	int		i;

	for (i = 0; hpstates[i].state_str != NULL; i++)
		if (state == hpstates[i].state)
			return (hpstates[i].state_str);

	return (unknown);
}

/*
 * valid_target()
 *
 *	Check if a state is a valid target for a changestate command.
 */
static short
valid_target(int state)
{
	int	i;

	for (i = 0; hpstates[i].state_str != NULL; i++)
		if (state == hpstates[i].state)
			return (hpstates[i].valid_target);

	return (0);
}
