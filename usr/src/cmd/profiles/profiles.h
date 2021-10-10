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

#ifndef _PROFILES_H
#define	_PROFILES_H

/*
 * header file for zonecfg command
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>

#define	Z_OK		0
#define	Z_ERR		1
#define	Z_USAGE		2
#define	Z_REPEAT	3

#define	CMD_ADD		0
#define	CMD_CANCEL	1
#define	CMD_CLEAR	2
#define	CMD_COMMIT	3
#define	CMD_DELETE	4
#define	CMD_END		5
#define	CMD_EXIT	6
#define	CMD_EXPORT	7
#define	CMD_HELP	8
#define	CMD_INFO	9
#define	CMD_REMOVE	10
#define	CMD_REVERT	11
#define	CMD_SELECT	12
#define	CMD_SET		13
#define	CMD_VERIFY	14

#define	CMD_MIN		CMD_ADD
#define	CMD_MAX		CMD_VERIFY

/* resource types: increment RT_MAX when expanding this list */
#define	RT_PROFATTR	0
#define	RT_COMMAND	1

#define	RT_MIN		RT_PROFATTR
#define	RT_MAX		RT_COMMAND

/* property types: increment PT_MAX when expanding this list */
#define	PT_UNKNOWN	0
#define	PT_PROFNAME	1
#define	PT_AUTHORIZATION	2
#define	PT_SUBPROFILE	3
#define	PT_PRIVS	4
#define	PT_LIMPRIV	5
#define	PT_DFLTPRIV	6
#define	PT_DESCRIPTION	7
#define	PT_HELPFILE	8
#define	PT_PATHNAME	9
#define	PT_EUID		10
#define	PT_UID		11
#define	PT_EGID		12
#define	PT_GID		13
#define	PT_LIMITPRIVS	14
#define	PT_ALWAYSAUDIT	15
#define	PT_NEVERAUDIT	16
#define	PT_COMMAND	17

#define	PT_MIN		PT_UNKNOWN
#define	PT_MAX		PT_COMMAND

#define	MAX_EQ_PROP_PAIRS	3

#define	PROP_VAL_SIMPLE		0
#define	PROP_VAL_LIST		1

#define	PROP_VAL_MIN		PROP_VAL_SIMPLE
#define	PROP_VAL_MAX		PROP_VAL_LIST

/*
 * If any subcommand is ever modified to take more than three arguments,
 * this will need to be incremented.
 */
#define	MAX_SUBCMD_ARGS		3

typedef struct list_property {
	char	*lp_simple;
	struct list_property	*lp_next;
} list_property_t, *list_property_ptr_t;

typedef struct property_value {
	int	pv_type;	/* from the PROP_VAL_* list above */
	char	*pv_simple;
	list_property_ptr_t	pv_list;
} property_value_t, *property_value_ptr_t;

typedef struct cmd {
	char	*cmd_name;
	void	(*cmd_handler)(struct cmd *);
	int	cmd_res_type;
	int	cmd_prop_nv_pairs;
	int	cmd_prop_name[MAX_EQ_PROP_PAIRS];
	property_value_ptr_t	cmd_property_ptr[MAX_EQ_PROP_PAIRS];
	int	cmd_argc;
	char	*cmd_argv[MAX_SUBCMD_ARGS + 1];
} cmd_t;

#define	HELP_USAGE	0x01
#define	HELP_SUBCMDS	0x02
#define	HELP_PROPERTIES	0x04
#define	HELP_RESOURCES	0x08
#define	HELP_PROPS	0x10
#define	HELP_META	0x20
#define	HELP_RES_SCOPE	0x40
#define	HELP_LIST	0x80
#define	HELP_SIMPLE	0x100
#define	HELP_REQUIRED	0x200
#define	HELP_CMDMODE	0x400

#define	HELP_RES_PROPS	(HELP_RESOURCES | HELP_PROPS)
#define	HELP_VALUE	(HELP_LIST | HELP_SIMPLE)
#define	HELP_PRESERVE	(HELP_REQUIRED | HELP_SIMPLE)
#define	HELP_ADDREM	(HELP_LIST | HELP_CMDMODE)
#define	HELP_COMMAND	(HELP_REQUIRED | HELP_CMDMODE)

extern void add_func(cmd_t *);
extern void cancel_func(cmd_t *);
extern void commit_func(cmd_t *);
extern void delete_func(cmd_t *);
extern void end_func(cmd_t *);
extern void exit_func(cmd_t *);
extern void export_func(cmd_t *);
extern void help_func(cmd_t *);
extern void info_func(cmd_t *);
extern void remove_func(cmd_t *);
extern void revert_func(cmd_t *);
extern void select_func(cmd_t *);
extern void set_func(cmd_t *);
extern void verify_func(cmd_t *);
extern void clear_func(cmd_t *);

extern cmd_t *alloc_cmd(void);
extern list_property_ptr_t alloc_list(void);
extern void free_cmd(cmd_t *);
extern void free_list(list_property_ptr_t);
extern void free_outer_list(list_property_ptr_t);

extern void usage(boolean_t, uint_t, uint_t);

extern FILE *yyin;
extern char *res_types[];
extern char *prop_types[];

#ifdef __cplusplus
}
#endif

#endif	/* _PROFILES_H */
