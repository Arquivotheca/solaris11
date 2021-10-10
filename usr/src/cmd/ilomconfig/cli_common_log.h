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

#ifndef _CLI_COMMON_LOG_H
#define	_CLI_COMMON_LOG_H

#include <limits.h>

/*
 * Any file using cli_common_log MUST declare a structure
 * of disp_options_t with the name dopt.
 */

typedef struct disp_options {
    boolean_t opt_verbose;
    boolean_t opt_quiet;
    char opt_filename[PATH_MAX];
} disp_options_t;

typedef struct log_options {
    char log_filename[PATH_MAX];
} log_options_t;

/*
 * Definition of error codes for CLI applications.
 *
 */

typedef enum {
	/* Common codes start here */
    SSM_CLI_OK = 0,
    SSM_CLI_INVALID_OPTION = 1,
    SSM_CLI_INVALID_SUBCOMMAND,
    SSM_CLI_INVALID_ARG,
    SSM_CLI_SUBCOMMAND_NOT_SUPPORTED,
    SSM_CLI_NAME_TOO_LONG,
    SSM_CLI_INTERNAL_ERROR,
    SSM_CLI_INSUFFICIENT_MEMORY,
    SSM_CLI_OPTION_NOT_SUPPORTED,
    SSM_CLI_INSUFFICIENT_PRIVILEGE,

	/* ILOM specific codes */
    ILOM_CANNOT_CONNECT_BMC = 50,
    ILOM_MUST_SPECIFY_OPTION,
    ILOM_NO_SUCH_PROPERTY,
    ILOM_INVALID_IPADDRESS_VALUE,
    ILOM_ERROR_OCCURRED,
    ILOM_INTERCONNECT_DISABLED,
    ILOM_NOT_REACHABLE_NETWORK,

    CLI_LAST_ERRNO
} cli_errno_t;

cli_errno_t init_debug_log(char *filepath);

void close_debug_log(void);

cli_errno_t debug_log(const char *fmt, ...);
int iprintf(const char *fmt, ...);

cli_errno_t log_and_print(boolean_t print_to_screen, const char *fmt, ...);

void exit_and_log(cli_errno_t err);

#endif /* _CLI_COMMON_LOG_H */
