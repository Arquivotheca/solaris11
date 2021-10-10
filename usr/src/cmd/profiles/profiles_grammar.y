%{
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

#include <stdio.h>
#include <strings.h>

#include "profiles.h"

static cmd_t *cmd = NULL;		/* Command being processed */
static list_property_ptr_t new_list = NULL, tmp_list, last,
    list[MAX_EQ_PROP_PAIRS];
static property_value_t property[MAX_EQ_PROP_PAIRS];

extern boolean_t newline_terminated;
extern int num_prop_vals;		/* # of property values */

/* yacc externals */
extern int yydebug;
extern void yyerror(char *s);

/*
 * This function is used by the simple_prop_val reduction rules to set up
 * a list_property_ptr_t and adjust the above global variables appropriately.
 * Note that this function duplicates the specified string and makes
 * the new list's lp_simple field point to the duplicate.  This function does
 * not free the original string.
 *
 * This function returns a pointer to the duplicated string or NULL if an error
 * occurred.  The simple_prop_val reduction rules that invoke this function
 * should set $$ to the returned pointer.
 */
extern void free(void *);

static char *
simple_prop_val_func(const char *str)
{
	char *retstr = NULL, *tokenstr;
	char *lasts, *cp;

	if ((tokenstr = strdup(str)) == NULL) {
		return (NULL);
	}
	for (cp = strtok_r(tokenstr, ",", &lasts); cp != NULL;
	    cp = strtok_r(NULL, ",", &lasts)) {
		if ((new_list = alloc_list()) == NULL) {
			free(tokenstr);
			return (NULL);
		}
		retstr = new_list->lp_simple = strdup(cp);
		if (retstr == NULL) {
			free(tokenstr);
			free_list(new_list);
			return (NULL);
		}
		new_list->lp_next = NULL;
		if (list[num_prop_vals] == NULL) {
			list[num_prop_vals] = new_list;
		} else {
			for (tmp_list = list[num_prop_vals]; tmp_list != NULL;
			    tmp_list = tmp_list->lp_next)
				last = tmp_list;
			last->lp_next = new_list;
		}
	}
	if (retstr)
		free(tokenstr);
	else 
		retstr = strdup(tokenstr);
	return (retstr);
}

%}

%union {
	int ival;
	char *strval;
	cmd_t *cmd;
	list_property_ptr_t list;
}

%start commands

%token HELP EXPORT ADD DELETE REMOVE SELECT SET INFO CANCEL END VERIFY
%token EQUAL OPEN_SQ_BRACKET CLOSE_SQ_BRACKET OPEN_PAREN CLOSE_PAREN COMMA
%token COMMIT REVERT EXIT SEMICOLON TOKEN PROFNAME CLEAR
%token AUTHORIZATION SUBPROFILE COMMAND
%token DESCRIPTION HELPFILE PATHNAME EUID UID EGID GID PRIVS LIMITPRIVS
%token LIMPRIV DFLTPRIV ALWAYSAUDIT NEVERAUDIT

%type <strval> TOKEN EQUAL OPEN_SQ_BRACKET CLOSE_SQ_BRACKET
    property_value OPEN_PAREN CLOSE_PAREN COMMA simple_prop_val
%type <ival> resource_type  COMMAND
%type <ival> property_name PROFNAME AUTHORIZATION SUBPROFILE
    DESCRIPTION HELPFILE PATHNAME EUID UID EGID GID PRIVS LIMITPRIVS
    LIMPRIV DFLTPRIV ALWAYSAUDIT NEVERAUDIT COMMAND
%type <cmd> command
%type <cmd> add_command ADD
%type <cmd> cancel_command CANCEL
%type <cmd> commit_command COMMIT
%type <cmd> delete_command DELETE
%type <cmd> end_command END
%type <cmd> exit_command EXIT
%type <cmd> export_command EXPORT
%type <cmd> help_command HELP
%type <cmd> info_command INFO
%type <cmd> remove_command REMOVE
%type <cmd> revert_command REVERT
%type <cmd> select_command SELECT
%type <cmd> set_command SET
%type <cmd> clear_command CLEAR
%type <cmd> verify_command VERIFY
%type <cmd> terminator

%%

commands: command terminator
	{
		if ($1 != NULL) {
			if ($1->cmd_handler != NULL)
				$1->cmd_handler($1);
			free_cmd($1);
			bzero(list, sizeof (list_property_t));
			num_prop_vals = 0;
		}
		return (0);
	}
	| command error terminator
	{
		if ($1 != NULL) {
			free_cmd($1);
			bzero(list, sizeof (list_property_t));
			num_prop_vals = 0;
		}
		if (YYRECOVERING())
			YYABORT;
		yyclearin;
		yyerrok;
	}
	| error terminator
	{
		if (YYRECOVERING())
			YYABORT;
		yyclearin;
		yyerrok;
	}
	| terminator
	{
		return (0);
	}

command: add_command
	| cancel_command
	| clear_command
	| commit_command
	| delete_command
	| end_command
	| exit_command
	| export_command
	| help_command
	| info_command
	| remove_command
	| revert_command
	| select_command
	| set_command
	| verify_command

terminator:	'\n'	{ newline_terminated = B_TRUE; }
	|	';'	{ newline_terminated = B_FALSE; }

add_command: ADD
	{
		short_usage(CMD_ADD);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_ADD, HELP_RES_PROPS);
		YYERROR;
	}
	| ADD TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &add_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}
	| ADD resource_type
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &add_func;
		$$->cmd_argc = 0;
		$$->cmd_res_type = $2;
		$$->cmd_prop_nv_pairs = 0;
	}
	| ADD resource_type EQUAL property_value
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &add_func;
		$$->cmd_argc = 0;
		$$->cmd_res_type = $2;
		$$->cmd_prop_nv_pairs = 1;
		$$->cmd_prop_name[0] = PT_PATHNAME;
		$$->cmd_property_ptr[0] = &property[0];
	}
	| ADD property_name EQUAL property_value
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &add_func;
		$$->cmd_argc = 0;
		$$->cmd_prop_nv_pairs = 1;
		$$->cmd_prop_name[0] = $2;
		$$->cmd_property_ptr[0] = &property[0];
	}

cancel_command: CANCEL
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &cancel_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| CANCEL TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &cancel_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

commit_command: COMMIT
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &commit_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| COMMIT TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &commit_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

delete_command: DELETE
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &delete_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	|	DELETE TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &delete_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

end_command: END
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &end_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| END TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &end_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

exit_command: EXIT
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &exit_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| EXIT TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &exit_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

export_command: EXPORT
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &export_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| EXPORT TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &export_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}
	| EXPORT TOKEN TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &export_func;
		$$->cmd_argc = 2;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = $3;
		$$->cmd_argv[2] = NULL;
	}

help_command:	HELP
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &help_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	|	HELP property_name
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &help_func;
		$$->cmd_prop_name[0] = $2;
		$$->cmd_prop_nv_pairs = 0;
	}
	|	HELP TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &help_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

info_command:	INFO
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &info_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	|	INFO property_name
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &info_func;
		$$->cmd_prop_name[0] = $2;
		$$->cmd_prop_nv_pairs = 0;
	}
	|	INFO TOKEN
	{
		short_usage(CMD_INFO);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_INFO, HELP_RES_PROPS);
		free($2);
		YYERROR;
	}

remove_command: REMOVE
	{
		short_usage(CMD_REMOVE);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_REMOVE, HELP_RES_PROPS);
		YYERROR;
	}
	| REMOVE TOKEN
	{
		short_usage(CMD_REMOVE);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_REMOVE, HELP_RES_PROPS);
		YYERROR;
	}
	| REMOVE resource_type
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &remove_func;
		$$->cmd_res_type = $2;
	}
	| REMOVE TOKEN resource_type
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &remove_func;
		$$->cmd_res_type = $3;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}
	| REMOVE property_name EQUAL property_value
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &remove_func;
		$$->cmd_prop_nv_pairs = 1;
		$$->cmd_prop_name[0] = $2;
		$$->cmd_property_ptr[0] = &property[0];
	}
	| REMOVE resource_type EQUAL property_value
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &remove_func;
		$$->cmd_argc = 0;
		$$->cmd_res_type = $2;
		$$->cmd_prop_nv_pairs = 1;
		$$->cmd_prop_name[0] = PT_PATHNAME;
		$$->cmd_property_ptr[0] = &property[0];
	}

revert_command: REVERT
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &revert_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| REVERT TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &revert_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

select_command: SELECT
	{
		short_usage(CMD_SELECT);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_SELECT, HELP_RES_PROPS);
		YYERROR;
	}
	| SELECT resource_type
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &select_func;
		$$->cmd_res_type = $2;
	}
	| SELECT resource_type EQUAL property_value
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &select_func;
		$$->cmd_argc = 0;
		$$->cmd_res_type = $2;
		$$->cmd_prop_nv_pairs = 1;
		$$->cmd_prop_name[0] = PT_PATHNAME;
		$$->cmd_property_ptr[0] = &property[0];
	}

set_command: SET
	{
		short_usage(CMD_SET);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_SET, HELP_PROPS);
		YYERROR;
	}
	| SET property_name EQUAL OPEN_SQ_BRACKET CLOSE_SQ_BRACKET
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &set_func;
		$$->cmd_prop_nv_pairs = 0;
		$$->cmd_prop_name[0] = $2;
		property[0].pv_type = PROP_VAL_LIST;
		property[0].pv_list = NULL;
		$$->cmd_property_ptr[0] = &property[0];
	}
	| SET property_name EQUAL property_value
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &set_func;
		$$->cmd_prop_nv_pairs = 1;
		$$->cmd_prop_name[0] = $2;
		$$->cmd_property_ptr[0] = &property[0];
	}

clear_command: CLEAR
	{
		short_usage(CMD_CLEAR);
		(void) fputs("\n", stderr);
		usage(B_FALSE, CMD_CLEAR, HELP_PROPS);
		YYERROR;
	}
	| CLEAR property_name
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &clear_func;
		$$->cmd_res_type = $2;
	}

verify_command: VERIFY
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &verify_func;
		$$->cmd_argc = 0;
		$$->cmd_argv[0] = NULL;
	}
	| VERIFY TOKEN
	{
		if (($$ = alloc_cmd()) == NULL)
			YYERROR;
		cmd = $$;
		$$->cmd_handler = &verify_func;
		$$->cmd_argc = 1;
		$$->cmd_argv[0] = $2;
		$$->cmd_argv[1] = NULL;
	}

resource_type: COMMAND	{ $$ = RT_COMMAND; }

property_name: PROFNAME	{ $$ = PT_PROFNAME; }
	| AUTHORIZATION	{ $$ = PT_AUTHORIZATION; }
	| SUBPROFILE	{ $$ = PT_SUBPROFILE; }
	| DESCRIPTION	{ $$ = PT_DESCRIPTION; }
	| HELPFILE	{ $$ = PT_HELPFILE; }
	| PATHNAME	{ $$ = PT_PATHNAME; }
	| EUID		{ $$ = PT_EUID; }
	| UID		{ $$ = PT_UID; }
	| EGID		{ $$ = PT_EGID; }
	| GID		{ $$ = PT_GID; }
	| PRIVS		{ $$ = PT_PRIVS; }
	| LIMPRIV	{ $$ = PT_LIMPRIV; }
	| DFLTPRIV	{ $$ = PT_DFLTPRIV; }
	| LIMITPRIVS	{ $$ = PT_LIMITPRIVS; }
	| ALWAYSAUDIT	{ $$ = PT_ALWAYSAUDIT; }
	| NEVERAUDIT	{ $$ = PT_NEVERAUDIT; }
	| COMMAND	{ $$ = PT_COMMAND; }

/*
 * The grammar builds data structures from the bottom up.  Thus various
 * strings are lexed into TOKENs or commands or resource or property values.
 * Below is where the resource and property values are built up into more
 * complex data structures.
 *
 * There are two kinds of properties: simple (single valued) and list 
 * (concatenation of one or more simple properties).
 *
 * So the property structure has a type which is one of these, and the
 * corresponding _simple  or_list is set to the corresponding
 * lower-level data structure.
 */

property_value: simple_prop_val
	{
		if (list[num_prop_vals] == NULL) {
			property[num_prop_vals].pv_type = PROP_VAL_SIMPLE;
			property[num_prop_vals].pv_simple = $1;
		} else if (list[num_prop_vals]->lp_next == NULL) {
			free_outer_list(list[num_prop_vals]);
			list[num_prop_vals] = NULL;
			property[num_prop_vals].pv_type = PROP_VAL_SIMPLE;
			property[num_prop_vals].pv_simple = $1;
		} else {
			property[num_prop_vals].pv_type = PROP_VAL_LIST;
			property[num_prop_vals].pv_list = list[num_prop_vals];
		}
		num_prop_vals++;
	}
	| list_prop_val
	{
		property[num_prop_vals].pv_type = PROP_VAL_LIST;
		property[num_prop_vals].pv_list = list[num_prop_vals];
		num_prop_vals++;
	}

/*
 * One level lower, lists are made up of simple values, so
 * simple_prop_val fill in a list structure and
 * insert it into the linked list which is built up.
 *
 * The list structures for the linked lists are allocated
 * below, and freed by recursive functions which are ultimately called
 * by free_cmd(), which is called from the top-most "commands" part of
 * the grammar.
 *
 * NOTE: simple_prop_val piece needs reduction rules for
 * property_name and resource_type so that the parser will accept property names
 * and resource type names as property values.
 */

simple_prop_val: TOKEN
	{
		$$ = simple_prop_val_func($1);
		free($1);
		if ($$ == NULL)
			YYERROR;
	}
	| resource_type
	{
		if (($$ = simple_prop_val_func(res_types[$1])) == NULL)
			YYERROR;
	}
	| property_name
	{
		if (($$ = simple_prop_val_func(prop_types[$1])) == NULL)
			YYERROR;
	}

list_piece: simple_prop_val
	| simple_prop_val COMMA list_piece

list_prop_val: OPEN_SQ_BRACKET list_piece CLOSE_SQ_BRACKET
%%
