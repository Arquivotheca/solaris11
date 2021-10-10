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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#include <libnvpair.h>

#include "cli_common_log.h"

/*
 * Any file using cli_common_log MUST declare
 * a global varaiable
 * disp_optiosn_t dopt;
 */
extern disp_options_t dopt;

typedef struct error_msg {
	cli_errno_t error_code;
	const char *error_msg;
	const char *error_name;
} error_msg_t;

error_msg_t error_msg_table[] = {
	{ SSM_CLI_OK, "", "SSM_CLI_OK"},
	{ SSM_CLI_INVALID_OPTION, "Invalid option",
	"SSM_CLI_INVALID_OPTION" },
	{ SSM_CLI_INVALID_ARG, "Invalid argument", "SSM_CLI_INVALID_ARG" },
	{ SSM_CLI_INVALID_SUBCOMMAND, "Invalid subcommand",
	"SSM_CLI_INVALID_SUBCOMMAND" },
	{ SSM_CLI_SUBCOMMAND_NOT_SUPPORTED, "Subcommand not supported",
	"SSM_CLI_SUBCOMMAND_NOT_SUPPORTED" },
	{ SSM_CLI_NAME_TOO_LONG, "Entered name is too long",
	"SSM_CLI_NAME_TOO_LONG" },
	{ SSM_CLI_INTERNAL_ERROR, "Internal error", "SSM_CLI_INTERNAL_ERROR" },
	{ SSM_CLI_INSUFFICIENT_MEMORY, "Insufficient memory",
	"SSM_CLI_INSUFFICIENT_MEMORY" },
	{ SSM_CLI_INSUFFICIENT_PRIVILEGE,
	"Insufficent privilege to execute command",
	"SSM_CLI_INSUFFICIENT_PRIVILEGE" },
	{ SSM_CLI_OPTION_NOT_SUPPORTED, "Option not supported",
	"SSM_CLI_OPTION_NOT_SUPPORTED" },
	{ ILOM_CANNOT_CONNECT_BMC, "Cannot connect to BMC",
	"ILOM_CANNOT_CONNECT_BMC" },
	{ ILOM_MUST_SPECIFY_OPTION,  "Must specify option to modify",
	"ILOM_MUST_SPECIFY_OPTION" },
	{ ILOM_NO_SUCH_PROPERTY,  "No such property", "ILOM_NO_SUCH_PROPERTY" },
	{ ILOM_INVALID_IPADDRESS_VALUE, "Invalid IP address",
	"ILOM_INVALID_IPADDRESS_VALUE" },
	{ ILOM_ERROR_OCCURRED, "ILOM error occurred", "ILOM_ERROR_OCCURRED" },
	{ ILOM_INTERCONNECT_DISABLED, "Cannot modify interconnect when \
disabled (use enable command)", "ILOM_INTERCONNECT_DISABLED" },
	{ ILOM_NOT_REACHABLE_NETWORK, "ILOM not reachable over internal LAN",
"ILOM_NOT_REACHABLE_NETWORK" },

};

static log_options_t log_opt;

static FILE *debug_fp = NULL;

cli_errno_t
init_debug_log(char *filepath)
{
	int rc;
	char *name;
	char dir[PATH_MAX];
	struct stat stat_buf;

	if (strlen(filepath) > PATH_MAX) {
		return (SSM_CLI_NAME_TOO_LONG);
	}

	(void) strncpy(log_opt.log_filename, filepath,
	    sizeof (log_opt.log_filename));

	if ((name = strrchr(filepath, '/')) != (char *)NULL) {
		int index = filepath-name;

		if (index < 0)
			index = -index;

		(void) strncpy(dir, filepath, index);
		dir[index] = '\0';

		rc = stat(dir, &stat_buf);
		if (rc == -1 && errno == ENOENT) {
			if (mkdir(dir, 0755) != 0) {
				(void) fprintf(stderr, "failed to mkdir %s\n",
				    dir);
				return (SSM_CLI_INTERNAL_ERROR);
			}
		}
	}

	/* Open the debug log in append mode */
	debug_fp = fopen(log_opt.log_filename, "a");
	if (debug_fp == NULL) {
		(void) fprintf(stderr, "Failure opening %s\n",
		    log_opt.log_filename);
		return (SSM_CLI_INTERNAL_ERROR);
	}

	return (SSM_CLI_OK);

}

void
close_debug_log(void) {

	if (debug_fp != NULL)
		(void) fclose(debug_fp);

}

cli_errno_t
debug_log(const char *fmt, ...)
{
	va_list ap;
	time_t now = time(NULL);
	char	time_buf[50];	/* array to hold day and time */

	if (debug_fp != NULL) {
		va_start(ap, fmt);
		(void) strftime(time_buf, sizeof (time_buf),
		    NULL, localtime(&now));
		(void) fprintf(debug_fp, "%s:(CLI) ", time_buf);
		(void) vfprintf(debug_fp, fmt, ap);
		va_end(ap);
		(void) fflush(debug_fp);

	}

	return (SSM_CLI_OK);
}

void
exit_and_log(cli_errno_t err)
{
	int i;
	const char *out = "Error could not be determined";

	/* Get error message corresponding to error code */
	int tbl_size = sizeof (error_msg_table) / sizeof (error_msg_t);
	error_msg_t *msgs = error_msg_table;

	/* log message to file */
	for (i = 0; i < tbl_size; i++, msgs++) {
		if (err == msgs->error_code) {
			out = msgs->error_msg;
		}
	}

	/* output to screen if not suppressed */
	if (!dopt.opt_quiet) {
		(void) printf("ERROR: %s\n", out);
	}

	(void) debug_log("ERROR: %s\n", out);

	/* Extra space for good luck */
	(void) printf("\n");

	close_debug_log();

	exit(err);
}

int
iprintf(const char *format, ...) {
	int ret = 0;

	va_list args;
	va_start(args, format);

	if (dopt.opt_quiet != B_TRUE) {
		ret = vprintf(format, args);
	}
	va_end(args);

	return (ret);
}
