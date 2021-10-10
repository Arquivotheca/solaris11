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
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "ilomconfig.h"
#include <libnvpair.h>
#include "cli_common_log.h"
#include <libipmi.h>

static char *allowed_prefixes[] = { "set:", "Set",
							"Created", "create",
							"Deleted", "delete",
							NULL };
static char *error_prefixes[] = { "set:", "show:", "create:", "delete:", NULL};


int
does_target_exist(char *target) {
	sunoem_handle_t *localhandle = NULL;
	char command[LUAPI_MAX_OBJ_PATH_LEN];
	int j = 0, found = 0;

	/* Try showing the target to see if there is an error */
	(void) snprintf(command, LUAPI_MAX_OBJ_PATH_LEN, "show %s", target);

	for (j = 0; j < 5 && !found; j++) {
		if (localhandle) {
			sunoem_cleanup(localhandle);
			localhandle = NULL;
		}
		if (sunoem_init(&localhandle, command) == -1) {
			continue;
		} else {
			found = 1;
		}
	}

	if (sunoem_is_error(localhandle)) {
		sunoem_cleanup(localhandle);
		return (0);
	} else {
		sunoem_cleanup(localhandle);
		return (1);
	}
}

cli_errno_t
set_property(char *prefix, char *target,
	char *property, char *value, int reenter) {
	return set_property_sunoem(prefix, target,
		property, value, reenter, 1);
}

cli_errno_t
set_property_sunoem(char *prefix, char *target,
	char *property, char *value,
	int reenter, int print) {
	cli_errno_t ret = SSM_CLI_OK;
	char buf[LUAPI_MAX_OBJ_PATH_LEN];

	(void) debug_log("Setting property '%s' to '%s'\n", property, value);

	(void) snprintf(buf, LUAPI_MAX_OBJ_PATH_LEN, "set %s/%s %s=\"%s\"",
		prefix, target, property, value);
	if (reenter) {
		ret = sunoem_execute_and_print(buf, value, print);
	} else {
		ret = sunoem_execute_and_print(buf, NULL, print);
	}

	if (ret != SSM_CLI_OK) {
		(void) debug_log("Error setting prop '%s' to val '%s'\n",
			property, value);
	} else {
		(void) debug_log("Successfully set prop '%s' to val '%s'\n",
			property, value);
	}

	return (ret);
}

int
sunoem_init(sunoem_handle_t **handle, char *command)
{
	int result = 0;
	sunoem_handle_t *sh;
	ipmi_handle_t *ihp;
	int err;
	char *errmsg;
	uint_t xport_type = IPMI_TRANSPORT_BMC;
	nvlist_t *params = NULL;
	char *cmds_in[3];
	char *cmd_end = "";
	char cmd_out[SUNOEM_CLI_BUF_SIZE];

	/* Create the handle */
	sh = malloc(sizeof (sunoem_handle_t));
	if (sh == NULL) {
		(void) iprintf("Internal error\n");
		return (-1);
	}

	/* Open the temp file */
	(void) sprintf(sh->tmpfilename, "ilomconfig-temp.%d",
	    (int)getpid());
	sh->tmpfile = fopen(sh->tmpfilename, "a+");

	if (sh->tmpfile == NULL) {
		(void) iprintf("Internal error\n");
		free(sh);
		return (-1);
	} else {
		*handle = sh;
	}

	if ((ihp = ipmi_open(&err, &errmsg, xport_type,
	    params)) == NULL) {
		(void) debug_log("failed to open libipmi: %s\n",
		    errmsg);
		free(sh);
		return (-1);
	}

	/*
	 * Don't allow Ctrl-C in the middle of a sunoem command
	 * or it hangs that stack
	 */
	(void) signal(SIGINT, SIG_IGN);

	cmds_in[0] = command;
	cmds_in[1] = cmd_end;

	result = ipmi_sunoem_cli(ihp, cmds_in, cmd_out,
	    SUNOEM_CLI_BUF_SIZE, 0);

	(void) signal(SIGINT, SIG_DFL);

	/* Write output of command to file */
	(void) fprintf(sh->tmpfile, "%s", cmd_out);

	/* Close the file and then open back up for reading */
	(void) fclose(sh->tmpfile);
	sh->tmpfile = fopen(sh->tmpfilename, "r");

	/* clean repository caches */

	ipmi_close(ihp);

	return (result);
}


void
sunoem_cleanup(sunoem_handle_t *handle) {
	if (handle && handle->tmpfile) (void) fclose(handle->tmpfile);
	if (handle && handle->tmpfilename) (void) unlink(handle->tmpfilename);
	if (handle) free(handle);
}

int
sunoem_init_reenter(sunoem_handle_t **handle, char *command,
	char *reenter)
{
	int result = 0;
	sunoem_handle_t *sh;
	ipmi_handle_t *ihp;
	char *errmsg;
	int err;
	uint_t xport_type = IPMI_TRANSPORT_BMC;
	nvlist_t *params = NULL;
	char *cmds_in[4];
	char *cmd_end = "";
	char cmd_out[SUNOEM_CLI_BUF_SIZE*10];

	/* Create the handle */
	sh = malloc(sizeof (sunoem_handle_t));
	if (sh == NULL) {
		(void) iprintf("Internal error\n");
		return (-1);
	}

	/* Open the temp file */
	(void) sprintf(sh->tmpfilename, "ilomconfig-temp.%d",
	    (int)getpid());
	sh->tmpfile = fopen(sh->tmpfilename, "a+");

	if (sh->tmpfile == NULL) {
		(void) iprintf("Internal error\n");
		free(sh);
		return (-1);
	} else {
		*handle = sh;
	}

	cmds_in[0] = command;
	cmds_in[1] = reenter;
	cmds_in[2] = cmd_end;

	/* Open IPMI interface */
	if ((ihp = ipmi_open(&err, &errmsg, xport_type, params)) == NULL) {
		(void) iprintf("failed to open libipmi: %s\n",
		    errmsg);
		return (1);
	}

	/*
	 * Don't allow Ctrl-C in the middle of a sunoem command
	 * or it hangs that stack
	 */
	(void) signal(SIGINT, SIG_IGN);

	(void) debug_log(
	    "sunoem_init_reenter calling ipmi_sunoem_cli with"
	    "cmds %s %s\n",
	    command, reenter);

	result = ipmi_sunoem_cli(ihp, cmds_in, cmd_out,
	    SUNOEM_CLI_BUF_SIZE*10, 0);

	(void) debug_log(
"sunoem_init_reenter done with ipmi_sunoem_cli, cmd_out is %s\n",
	    cmd_out);

	(void) signal(SIGINT, SIG_DFL);

	/* Write output of command to file */
	(void) fprintf(sh->tmpfile, "%s", cmd_out);

	/* Close the file and then opening back up for reading */
	(void) fclose(sh->tmpfile);
	sh->tmpfile = fopen(sh->tmpfilename, "r");

	/* clean repository caches */

	ipmi_close(ihp);

	return (result);
}

cli_errno_t
sunoem_print_response(sunoem_handle_t *handle) {
	sunoem_handle_t *sh = (sunoem_handle_t *)handle;
	char tmpline[LUAPI_MAX_OBJ_PATH_LEN];
	cli_errno_t ret = SSM_CLI_OK;
	int i;

	/* Look at each line in the output */
	while (fgets(tmpline, LUAPI_MAX_OBJ_PATH_LEN, sh->tmpfile)
		!= NULL) {
		for (i = 0; allowed_prefixes[i] != NULL; i++) {
			if (strncmp(tmpline, allowed_prefixes[i],
					strlen(allowed_prefixes[i])) == 0) {
				(void) iprintf("%s", tmpline);
			}
		}
		for (i = 0; error_prefixes[i] != NULL; i++) {
			if (strncmp(tmpline, error_prefixes[i],
					strlen(error_prefixes[i])) == 0) {
				ret = ILOM_ERROR_OCCURRED;
			}
		}
	}
	return (ret);
}

cli_errno_t
sunoem_execute_and_print(char *command, char *reenter, int print) {
	sunoem_handle_t *localhandle;
	cli_errno_t ret = SSM_CLI_OK;

	if (reenter) {
		if (sunoem_init_reenter(&localhandle,
				command, reenter) == -1) {
			(void) debug_log(
				"Can't connect to BMC when setting %s\n",
				command);
			return (ILOM_CANNOT_CONNECT_BMC);
		}
	} else {
		if (sunoem_init(&localhandle, command) == -1) {
			(void) debug_log(
				"Can't connect to BMC when setting %s\n",
				command);
			return (ILOM_CANNOT_CONNECT_BMC);
		}
	}
	if (print) ret = sunoem_print_response(localhandle);
	sunoem_cleanup(localhandle);
	return (ret);
}

void
sunoem_parse_properties(sunoem_handle_t *handle) {
	sunoem_handle_t *sh = (sunoem_handle_t *)handle;
	char tmpline[LUAPI_MAX_OBJ_PATH_LEN];
	char tmpword[LUAPI_MAX_OBJ_PATH_LEN];
	char tmpvalue[LUAPI_MAX_OBJ_VAL_LEN];
	char separator[10];
	int properties = 0;

	/* Initialize numprops */
	handle->numprops = 0;

	/* Look at each line in the output */
	while (fgets(tmpline, LUAPI_MAX_OBJ_PATH_LEN, sh->tmpfile)
		!= NULL) {

		/* If you find a word on the line */
		/* LINTED - unbounded string specifier */
		if (sscanf(tmpline, "%s %s %[^\n]", tmpword,
				separator, tmpvalue) > 0) {

			/* Check to see if another section is starting */
			if (strcmp(tmpword, "Commands:") == 0) {
				properties = 0;
				*separator = 0;
			}
			/*
			 * Workaround bug in sunoem CLI for dropped data
			 * Maybe we are looking at property but the
			 * "Properties:" heading got dropped
			 */
			if (strcmp(separator, "=") == 0) {
				properties = 1;
			}

			/* If you are currently in the targets section */
			if (properties == 1) {
				(void) strncpy(
				handle->properties[handle->numprops].property,
					tmpword, LUAPI_MAX_OBJ_PATH_LEN);
				(void) strncpy(
				handle->properties[handle->numprops++].value,
					tmpvalue, LUAPI_MAX_OBJ_VAL_LEN);
			}

			/* Check to see if the targets section is starting */
			if (strcmp(tmpword, "Properties:") == 0) {
				properties = 1;
			}
		}
	}
}

int
sunoem_print_ilomversion(sunoem_handle_t *handle) {
	sunoem_handle_t *sh = (sunoem_handle_t *)handle;
	char tmpline[LUAPI_MAX_OBJ_PATH_LEN];
	char version[16];
	char buildnumber[16];
	int i;

	/* Consume the first few lines */
	for (i = 0; i < 1; i++) {
		(void) fgets(tmpline, LUAPI_MAX_OBJ_PATH_LEN, sh->tmpfile);
	}

	/* First line is the ILOM version */
	if (fgets(tmpline, LUAPI_MAX_OBJ_PATH_LEN,
			sh->tmpfile) != NULL) {
		/* LINTED - unbounded string specifier */
		(void) sscanf(tmpline, "%*s %*s %s", version);
	}
	/* Second line is the build number */
	if (fgets(tmpline, LUAPI_MAX_OBJ_PATH_LEN,
			sh->tmpfile) != NULL) {
		/* LINTED - unbounded string specifier */
		(void) sscanf(tmpline, "%*s %*s %*s %*s %s", buildnumber);
		(void) printf("ILOM Version: v%s r%s\n", version, buildnumber);
		return (1);
	}
	return (0);
}

int
sunoem_is_error(sunoem_handle_t *handle) {
	sunoem_handle_t *sh = (sunoem_handle_t *)handle;
	char tmpline[LUAPI_MAX_OBJ_PATH_LEN];
	int i;

	/* Look at each line in the output */
	while (fgets(tmpline, LUAPI_MAX_OBJ_PATH_LEN, sh->tmpfile) != NULL) {
		for (i = 0; error_prefixes[i] != NULL; i++) {
			if (strncmp(tmpline, error_prefixes[i],
					strlen(error_prefixes[i])) == 0) {
				return (1);
			}
		}
	}
	return (0);
}

cli_errno_t
get_property_ilom(char *target, char *property,
	char *value, int vallen) {
	sunoem_handle_t *localhandle = NULL;
	char command[LUAPI_MAX_OBJ_PATH_LEN];
	int i, j;
	int found = 0;

	/* Retry up to five times to workaround sunoem bugs */
	for (i = 0; i < 5 && !found; i++) {
		if (localhandle) {
				sunoem_cleanup(localhandle);
				localhandle = NULL;
		}
		(void) snprintf(command, LUAPI_MAX_OBJ_PATH_LEN,
			"show %s", target);
		if (sunoem_init(&localhandle, command) == -1) {
			(void) debug_log("get_property_ilom failed %s %s %d\n",
				target, property, i);
			continue;
		}

		sunoem_parse_properties(localhandle);

		for (j = 0; j < localhandle->numprops; j++) {
			if (strcmp(localhandle->properties[j].property,
					property) == 0) {
				(void) snprintf(value, vallen, "%s",
					localhandle->properties[j].value);
				found = 1;
				break;
			}
		}
	}

	sunoem_cleanup(localhandle);

	if (!found) {
		(void) debug_log("get_property_ilom can't connect %s %s\n",
			target, property);
		return (ILOM_CANNOT_CONNECT_BMC);
	} else {
		return (SSM_CLI_OK);
	}
}
