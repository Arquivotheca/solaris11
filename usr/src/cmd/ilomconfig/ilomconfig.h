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

#ifndef _ILOMCONFIG_H
#define	_ILOMCONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <libnvpair.h>
#include "cli_common_log.h"
#include <arpa/inet.h>
#include <libipmi.h>

#define	ILOMCONFIG_VERSION "2.1.0"

#define	ILOMCONFIG_DEBUG_DEFAULT_FILE "/var/log/ilomconfig/ilomconfig.log"

#define	LUAPI_OBJ_VAL char *
#define	LUAPI_RC_ERROR -1
#define	LUAPI_RC_OK 0
#define	FILENAME_MAX_LEN 64

#define	SUNOEM_CLI_BUF_SIZE   1024

#define	LUAPI_MAX_OBJ_PATH_LEN 1024
#define	LUAPI_MAX_OBJ_VAL_LEN 1024
#define	MAX_NUM_TARGETS 20
#define	MAX_NUM_PROPS 300


/* indices for options which don't have a short option */
#define	OPT_LIST 4
#define	OPT_MODIFY 5
#define	OPT_ENABLE 6
#define	OPT_DISABLE 7
#define	OPT_IPADDRESS 8
#define	OPT_IPNETMASK 9
#define	OPT_INTERCONNECT 10
#define	OPT_HOSTIPADDRESS 11
#define	OPT_SYSTEMSUMMARY 12
#define	OPT_LAST OPT_SYSTEMSUMMARY

typedef struct _target_t {
    char target[LUAPI_MAX_OBJ_PATH_LEN];
} target_t;

typedef struct _propval_t {
    char property[LUAPI_MAX_OBJ_PATH_LEN];
    char value[LUAPI_MAX_OBJ_VAL_LEN];
} propval_t;

typedef struct _sunoem_handle_t {
	FILE *tmpfile;
	char tmpfilename[64];
	propval_t properties[MAX_NUM_PROPS];
	target_t targets[MAX_NUM_TARGETS];
	int numprops;
	int numtargets;
} sunoem_handle_t;


/* List Functions */
extern cli_errno_t list_systemsummary();
extern cli_errno_t list_interconnect();

/* Modify Functions */
extern cli_errno_t modify_interconnect(nvlist_t *option_list, int enable);

/* Enable Functions */
extern cli_errno_t enable_interconnect(nvlist_t *option_list);

/* Disable Functions */
extern cli_errno_t disable_interconnect();

/* SunOEM Util functions */
extern int sunoem_init(sunoem_handle_t **handle, char *command);
extern int sunoem_init_reenter(sunoem_handle_t **handle, char *command,
	char *reenter);
extern cli_errno_t sunoem_execute_and_print(char *command, char *reenter,
	int print);
extern int sunoem_is_error(sunoem_handle_t *handle);
extern void sunoem_parse_properties(sunoem_handle_t *handle);
extern cli_errno_t sunoem_print_response(sunoem_handle_t *handle);
extern int sunoem_print_ilomversion(sunoem_handle_t *handle);
extern void sunoem_cleanup(sunoem_handle_t *handle);
extern cli_errno_t get_property_ilom(char *target, char *property,
	char *value, int vallen);
extern cli_errno_t set_property(char *prefix, char *target,
	char *property, char *value, int reenter);
extern cli_errno_t set_property_sunoem(char *prefix, char *target,
	char *property, char *value, int reenter, int print);
extern int does_target_exist(char *target);

/* Usage functions */
extern void usage(int command, int subcommand);

/* Interconnect functions */
extern cli_errno_t get_host_interconnect(char *macaddress,
	char *hostipaddress, int ipbuflen, char *hostnetmask, int nmbuflen);
extern cli_errno_t set_host_interconnect(char *macaddress,
	char *hostipaddress, char *hostnetmask);
extern cli_errno_t unset_host_interconnect(char *macaddress);
extern cli_errno_t ping_ilom(char *ipaddress);

#endif /* _ILOMCONFIG_H */
