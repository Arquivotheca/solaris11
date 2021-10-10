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

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <auth_attr.h>
#include <auth_list.h>
#include <pwd.h>
#include "ilomconfig.h"
#include "cli_common_log.h"
#include <libnvpair.h>
#include <libipmi.h>

/* Globals options visible to all commands */
disp_options_t dopt;
static int wants_help = 0;

static struct option long_options[] = {
	/* These options return the short option value */
	{"quiet",   no_argument, 0, 'q'},	   /* 0 */
	{"verbose",	 no_argument, 0, 'v'},
	{"help",	 no_argument, 0, '?'},
	{"version",	 no_argument, 0, 'V'},
	/*
	 * These options return their required place on the command line:
	 * 1 means it must be the first arg
	 * 2 means it must be the second arg
	 * 0 means it doesn't matter.
	 * We distinguish them by their indices.
	 */
	{"list",  no_argument, 0, 1},
	{"modify",  no_argument, 0, 1},    /* 5 */
	{"enable",  no_argument, 0, 1},
	{"disable",  no_argument, 0, 1},
	/* These options are long-only and require the -- */
	{"ipaddress",  required_argument, 0, 0},
	{"netmask",  required_argument, 0, 0},
	{"interconnect",  no_argument, 0, 2}, /* 10 */
	{"hostipaddress",  required_argument, 0, 0},
	{"system-summary",  no_argument, 0, 2},
	{0, 0, 0, 0}
};

static boolean_t
check_auth(const char *auth)
{
	struct passwd	*pw;

	if ((pw = getpwuid(getuid())) == NULL)
		return (B_FALSE);

	return (chkauthattr(auth, pw->pw_name) != 0);
}

cli_errno_t
process_options(int argc, char **argv, int *command,
	int *subcommand, nvlist_t *option_list)
{
	/* getopt_long stores the option index here. */
	int option_index;
	int c;
	int argnum;
	int arglen;

	/*
	 * Suppress printing of error messages by getopt so that this
	 * code can print them as needed
	 */
	opterr = 0;

	/* optstring starts with - in order to catch non-options */
	for (argnum = 1; (c = getopt_long(argc, argv, ":qv?V",
	    long_options, &option_index)) != -1; argnum++) {
		/* Short options get have the '=' preceding the argument */
		if (optarg && optarg[0] == '=') {
			optarg++;
		}

		/* Treat options ending in an = as if they had no argument */
		arglen = strlen(argv[argnum]);
		if (argv[argnum][arglen-1] == '=') {
			optarg = NULL;
		}

		switch (c) {
			case 'v':
				dopt.opt_verbose = B_TRUE;
				opterr = 1;
				break;

			case 'V':
				(void) printf("ilomconfig version %s\n\n",
				    ILOMCONFIG_VERSION);
				exit(0);
				break;

			case 'q':
				dopt.opt_quiet = B_TRUE;
				break;

			case 0:
				(void) debug_log("index is %d\n", option_index);
				if ((optarg == NULL) || (optarg[0] == '-')) {
					usage(*command, *subcommand);
					return (SSM_CLI_INVALID_ARG);
				}
				switch (option_index) {
				case OPT_IPADDRESS:
					if (nvlist_add_string(
					    option_list,
					    "ipaddress",
					    optarg) != 0) {
						return (SSM_CLI_INTERNAL_ERROR);
					}
					if (*command != OPT_MODIFY &&
					    *command != OPT_ENABLE) {
						return (SSM_CLI_INVALID_ARG);
					}
					break;
				case OPT_IPNETMASK:
					if (nvlist_add_string(
					    option_list,
					    "netmask",
					    optarg) != 0) {
						return (SSM_CLI_INTERNAL_ERROR);
					}
					if (*command != OPT_MODIFY &&
					    *command != OPT_ENABLE) {
						return (SSM_CLI_INVALID_ARG);
					}
					break;
				case OPT_HOSTIPADDRESS:
					if (nvlist_add_string(option_list,
					    "hostipaddress",
					    optarg) != 0) {
						return (SSM_CLI_INTERNAL_ERROR);
					}
					if ((*command != OPT_ENABLE &&
					    *command != OPT_MODIFY) ||
					    *subcommand !=
					    OPT_INTERCONNECT) {
						return (SSM_CLI_INVALID_ARG);
					}
					break;
				default:
					usage(*command, *subcommand);
					return (SSM_CLI_INVALID_OPTION);
				}
				break;
			case ':':
				usage(*command, *subcommand);
		return (SSM_CLI_INVALID_OPTION);
				break;
			case 1:
				if (argnum != 1) {
					usage(*command, *subcommand);
			return (SSM_CLI_INVALID_OPTION);
				}
				switch (option_index) {
					case OPT_LIST:
					case OPT_MODIFY:
					case OPT_ENABLE:
					case OPT_DISABLE:
						*command = option_index;
						break;
					default:
					return (SSM_CLI_INVALID_SUBCOMMAND);
				}
				break;
			case 2:
				if (argnum != 2) {
					usage(*command, *subcommand);
			return (SSM_CLI_INVALID_OPTION);
				}
				switch (option_index) {

				case OPT_SYSTEMSUMMARY:
				case OPT_INTERCONNECT:
					*subcommand = option_index;
					break;
				default:
					usage(*command, 0);
					return (SSM_CLI_INVALID_OPTION);
				}
				break;
			case '?':
				wants_help = 1;
				usage(*command, *subcommand);
				break;
			default:
				usage(*command, *subcommand);
				return (SSM_CLI_INVALID_OPTION);
				break;
		} /* end switch */

	}

	/* If there were extra args, then it's a problem */
	if (argc != argnum) {
		usage(*command, *subcommand);
		return (SSM_CLI_INVALID_OPTION);
	}

	return (SSM_CLI_OK);
}

void
preprocess_options(int argc, char **argv) {
	int i, j;
	char *temp;
	int len;

	/*
	 * Look for options that are part of the long_option array
	 * and prefix with -- if necessary
	 */
	for (i = 1; i < argc; i++) {
		for (j = 0; long_options[j].name != 0; j++) {
			if (strcmp(argv[i], long_options[j].name) == 0) {
				len = strlen(argv[i]) + 3;
				temp = malloc(len);
				if (temp != NULL) {
					(void) snprintf(temp, len, "--%s",
						argv[i]);
					argv[i] = temp;
				} else {
					exit_and_log(
					    SSM_CLI_INSUFFICIENT_MEMORY);
				}
			}
		}
	}
}

cli_errno_t
execute_command(int command, int subcommand, nvlist_t *option_list) {
	int ret = SSM_CLI_OK;

	switch (command) {
		case OPT_LIST:
			if (subcommand == OPT_SYSTEMSUMMARY) {
				ret = list_systemsummary();
			} else if (subcommand == OPT_INTERCONNECT) {
				ret = list_interconnect();
			} else {
				ret = SSM_CLI_INVALID_OPTION;
				usage(OPT_LIST, 0);
			}
			break;
		case OPT_ENABLE:
			if (subcommand == OPT_INTERCONNECT) {
				ret = modify_interconnect(option_list, 1);
			} else {
				ret = SSM_CLI_INVALID_OPTION;
				usage(OPT_ENABLE, 0);
			}
			break;
		case OPT_DISABLE:
			if (subcommand == OPT_INTERCONNECT) {
				ret = disable_interconnect();
			} else {
				ret = SSM_CLI_INVALID_OPTION;
				usage(OPT_DISABLE, 0);
			}
			break;
		case OPT_MODIFY:
			if (subcommand == OPT_INTERCONNECT) {
				ret = modify_interconnect(option_list, 0);
			} else {
				ret = SSM_CLI_INVALID_OPTION;
				usage(OPT_MODIFY, 0);
			}
			break;

		default:
			ret = SSM_CLI_INVALID_OPTION;
			usage(OPT_MODIFY, 0);
	}
	return (ret);
}

int
main(int argc, char **argv)
{
	cli_errno_t ret = SSM_CLI_OK;
	int command = 0, subcommand = 0;
	nvlist_t *option_list = NULL;
	char tmpfilename[FILENAME_MAX_LEN];

	/* Initialize booleans */
	dopt.opt_verbose = B_FALSE;
	dopt.opt_quiet = B_FALSE;

	if ((check_auth(DEVICE_CONFIG_AUTH) != B_TRUE) ||
	    (check_auth("solaris.network.interface.config") != B_TRUE) ||
	    (check_auth(LINK_SEC_AUTH) != B_TRUE)) {
		(void) fprintf(stderr,
		    "ilomconfig: Insufficient user authorizations\n");
		exit(SSM_CLI_INSUFFICIENT_PRIVILEGE);
	}

	if ((ret = init_debug_log(ILOMCONFIG_DEBUG_DEFAULT_FILE))
	    != SSM_CLI_OK) {
		exit_and_log(ret);
	}

	/* Allocate nvlist to be used for entered options. */
	if (nvlist_alloc(&option_list, NV_UNIQUE_NAME, 0)) {
		exit_and_log(SSM_CLI_INSUFFICIENT_MEMORY);
	}

	/* Preprocess args to convert to getopts style */
	preprocess_options(argc, argv);

	/* Process the options on the command line */
	ret = process_options(argc, argv, &command, &subcommand,
	    option_list);

	/* Execute command */
	if (ret == SSM_CLI_OK && wants_help == 0) {
		if (command == 0 || subcommand == 0) {
			usage(command, subcommand);
			ret = SSM_CLI_INVALID_SUBCOMMAND;
		} else {
			ret = execute_command(command, subcommand, option_list);
		}
	}

	/* Cleanup any leftover files */
	(void) snprintf(tmpfilename, FILENAME_MAX_LEN,
	    "ilomconfig-temp.%d", getpid());
	(void) unlink(tmpfilename);

	close_debug_log();

	if (ret == ILOM_ERROR_OCCURRED) {
		(void) iprintf("\n");
		exit(ret);
	} else if (ret != SSM_CLI_OK) {
		exit_and_log(ret);
	} else {
		(void) iprintf("\n");
		exit(0);
	}

	return (0);
}
