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

#include <auth_list.h>
#include <errno.h>
#include <libintl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>
#include <zone.h>
#include "allocate.h"
#include "allocate_audit.h"


/* Static functions */

static char *get_auth_list(char *, uint32_t);
static void set_error_codes(int *, int *, int *);

/* Static objects */

static adt_session_data_t   *as;
static ae_ticket_t	    init_data;

/*
 * General audit functions
 */

/* Initialize auditing */
void
audit_init(char *dev_path, char *zone, uid_t uid)
{
	/* Initialize the audit session */
	if (adt_start_session(&as, NULL, ADT_USE_PROC_DATA) != 0) {
		(void) fprintf(stderr, "Solaris Auditing: %s",
		    gettext("Failed to create the audit session.\n"));
		return;
	}

	/* Store initialization data */
	init_data.dev_path = dev_path;
	init_data.uid = uid;
	if (zone != NULL && strncmp(GLOBAL_ZONENAME, zone, strlen(zone)) != 0) {
		init_data.zonename = zone;
	}
}

/*
 * Set default authorization expected for certain command-line options. auth is
 * either AUDIT_DEVICE_REVOKE_AUTH (solaris.device.revoke) or
 * AUDIT_DEV_ALLOC_AUTH (solaris.device.allocate).
 */
void
audit_set_auth(uint32_t auth)
{
	init_data.auths |= auth;
}

/* Finish auditing */
void
audit_finish(void)
{
	(void) adt_end_session(as);
}

/*
 * Event related functions
 */

/* Initialize audit event ticket */
void
audit_event_init(au_event_t ae_type, ae_ticket_t *ticket)
{
	if (ticket == NULL) {
		return;
	}
	*ticket = init_data;
	ticket->event_type = ae_type;
}

/* Submit audit event */
void
audit_event_submit(ae_ticket_t *ticket, int status)
{
	adt_event_data_t *ae;
	struct stat64 *dev_attr;
	struct passwd pw;
	char pwb[1024];
	int error, errmsg;

	if (ticket == NULL) {
		return;
	}

	/* Allocate appropriate audit event */
	if ((ae = adt_alloc_event(as, ticket->event_type)) == NULL) {
		(void) fprintf(stderr, "Solaris Auditing: %s",
		    gettext("Failed to allocate the audit event.\n"));
		goto cleanup;
	}

	/*
	 * Do not record empty device attributes (e.g. stat64() failed, device
	 * does not exist).
	 */
	dev_attr = (ticket->dev_attr.st_ctime == 0L) ? NULL : &ticket->dev_attr;

	/*
	 * Assemble (success and failure) authorization lists. XOR is used to
	 * see which default authorization was expected, but not granted.
	 */
	ticket->auth_list_s = get_auth_list(ticket->auth_list_s, ticket->auths);
	ticket->auth_list_f = get_auth_list(ticket->auth_list_f,
	    (init_data.auths ^ ticket->auths));

	/*
	 * Record the username only if the action failed and we tried to
	 * allocate on behalf of someone else or in case of devices listing.
	 * Otherwise, requested ownership will be clear from device attributes.
	 */
	pw.pw_name = NULL;
	if ((status != 0 || ticket->event_type == ADT_da_list_devices) &&
	    ticket->uid != getuid()) {
		(void) getpwuid_r(ticket->uid, &pw, pwb, sizeof (pwb));
	}

	/* Get error codes and the error message. */
	set_error_codes(&status, &error, &errmsg);

	switch (ticket->event_type) {
	case ADT_da_allocate:
	case ADT_da_allocate_forced:
		ae->adt_da_allocate.dev_path = ticket->dev_path;
		ae->adt_da_allocate.dev_attr = dev_attr;
		ae->adt_da_allocate.dev_list = ticket->dev_list;
		ae->adt_da_allocate.uid = ticket->uid;
		ae->adt_da_allocate.uname = pw.pw_name;
		ae->adt_da_allocate.zonename = ticket->zonename;
		ae->adt_da_allocate.auth_used_s = ticket->auth_list_s;
		ae->adt_da_allocate.auth_used_f = ticket->auth_list_f;
		ae->adt_da_allocate.errmsg = errmsg;
		break;
	case ADT_da_deallocate:
	case ADT_da_deallocate_forced:
		ae->adt_da_deallocate.dev_path = ticket->dev_path;
		ae->adt_da_deallocate.dev_attr = dev_attr;
		ae->adt_da_deallocate.dev_list = ticket->dev_list;
		ae->adt_da_deallocate.zonename = ticket->zonename;
		ae->adt_da_deallocate.auth_used_s = ticket->auth_list_s;
		ae->adt_da_deallocate.auth_used_f = ticket->auth_list_f;
		ae->adt_da_deallocate.errmsg = errmsg;
		break;
	case ADT_da_list_devices:
		ae->adt_da_list_devices.dev_path = ticket->dev_path;
		ae->adt_da_list_devices.dev_attr = dev_attr;
		ae->adt_da_list_devices.zonename = ticket->zonename;
		ae->adt_da_list_devices.uid = ticket->uid;
		ae->adt_da_list_devices.uname = pw.pw_name;
		ae->adt_da_list_devices.zonename = ticket->zonename;
		ae->adt_da_list_devices.auth_used_s = ticket->auth_list_s;
		ae->adt_da_list_devices.auth_used_f = ticket->auth_list_f;
		ae->adt_da_list_devices.errmsg = errmsg;
		break;
	default:
		goto cleanup;
	}

	if (adt_put_event(ae, status, error) != 0) {
		(void) fprintf(stderr, "Solaris Auditing: %s",
		    gettext("Failed to submit the audit event.\n"));
	}

cleanup:
	adt_free_event(ae);
	audit_event_reset_auth_lists(ticket);
}

void
audit_event_set_dev_attr(ae_ticket_t *ticket)
{
	if (ticket == NULL || ticket->dev_path == NULL) {
		return;
	}
	(void) stat64(ticket->dev_path, &ticket->dev_attr);
}

void
audit_event_set_def_auth(ae_ticket_t *ticket, uint32_t auth)
{
	if (ticket == NULL) {
		return;
	}
	ticket->auths |= auth;
}

void
audit_event_reset_auth_lists(ae_ticket_t *ticket)
{
	if (ticket == NULL) {
		return;
	}

	free(ticket->auth_list_s);
	ticket->auth_list_s = NULL;
	free(ticket->auth_list_f);
	ticket->auth_list_f = NULL;
}

void
audit_event_set_auth_list(ae_ticket_t *ticket, const char *auth_list,
    boolean_t success)
{
	char **list;

	if (ticket == NULL || auth_list == NULL) {
		return;
	}

	/* Pick correct authorization list */
	list = (success) ? &ticket->auth_list_s : &ticket->auth_list_f;

	/*
	 * If device authorization field is equal to '@' or '*', we do not need
	 * to store device authorizations. In the first case no authorization is
	 * required. In the latter case the device cannot be allocated by anyone
	 * (including superuser) no matter which authorizations the user posses.
	 */

	if (strcmp("@", auth_list) == 0 ||
	    strcmp("*", auth_list) == 0) {
		return;
	}

	free(*list);
	*list = strdup(auth_list);
}

static char *
get_auth_list(char *authlist, uint32_t auths)
{
	size_t	    len = 0;
	uint32_t    lauths = 0;

	/*
	 * Prevent from adding duplicates of DEFAULT_DEV_ALLOC_AUTH,
	 * DEVICE_REVOKE_AUTH. Count correct list length.
	 */
	if (auths & AUDIT_DEV_ALLOC_AUTH) {
		if (authlist == NULL || (authlist != NULL &&
		    strstr(authlist, DEFAULT_DEV_ALLOC_AUTH) == NULL)) {
			lauths |= AUDIT_DEV_ALLOC_AUTH;
			len += strlen(DEFAULT_DEV_ALLOC_AUTH);
		}
	}
	if (auths & AUDIT_DEVICE_REVOKE_AUTH) {
		if (authlist == NULL || (authlist != NULL &&
		    strstr(authlist, DEVICE_REVOKE_AUTH) == NULL)) {
			lauths |= AUDIT_DEVICE_REVOKE_AUTH;
			len += strlen(DEVICE_REVOKE_AUTH);
		}
	}

	/* Update the list of authorizations */
	if (len > 0) {
		size_t olen = (authlist != NULL) ? strlen(authlist) : 0;
		char *ptr;

		/*
		 * Extend the memory reserved for the authorization list. Magic
		 * value 3 always adds space for two commas and for a trailing
		 * byte.
		 */
		if ((ptr = (char *)realloc(authlist, olen + len + 3)) == NULL) {
			return (authlist);
		}

		authlist = ptr;
		ptr += olen;

		if (lauths & AUDIT_DEV_ALLOC_AUTH) {
			if (ptr != authlist) {
				*ptr++ = ',';
			}
			(void) strncpy(ptr, DEFAULT_DEV_ALLOC_AUTH,
			    strlen(DEFAULT_DEV_ALLOC_AUTH));
			ptr += strlen(DEFAULT_DEV_ALLOC_AUTH);
		}
		if (lauths & AUDIT_DEVICE_REVOKE_AUTH) {
			if (ptr != authlist) {
				*ptr++ = ',';
			}
			(void) strncpy(ptr, DEVICE_REVOKE_AUTH,
			    strlen(DEVICE_REVOKE_AUTH));
			ptr += strlen(DEVICE_REVOKE_AUTH);
		}
		*ptr = '\0';
	}

	return (authlist);
}

static void
set_error_codes(int *status, int *error, int *errmsg)
{
	if (*status == 0) {
		*status = ADT_SUCCESS;
		*error = ADT_SUCCESS;
		*errmsg = ADT_DA_ERROR_NO_MSG;
		return;
	}

	*error = ADT_FAIL_VALUE_PROGRAM;

	switch (*status) {
	case ALLOCUERR:
		*errmsg = ADT_DA_ERROR_ALLOCUERR;
		break;
	case CHOWNERR:
		*errmsg = ADT_DA_ERROR_CHOWNERR;
		break;
	case CNTDEXECERR:
		*errmsg = ADT_DA_ERROR_CNTDEXECERR;
		break;
	case CNTFRCERR:
		*errmsg = ADT_DA_ERROR_CNTFRCERR;
		break;
	case DACACCERR:
		*errmsg = ADT_DA_ERROR_DACACCERR;
		break;
	case DAUTHERR:
		*errmsg = ADT_DA_ERROR_DAUTHERR;
		break;
	case UAUTHERR:
		*errmsg = ADT_DA_ERROR_NO_MSG;
		*error = ADT_FAIL_VALUE_AUTH;
		break;
	case DEFATTRSERR:
		*errmsg = ADT_DA_ERROR_DEFATTRSERR;
		break;
	case DEVLKERR:
		*errmsg = ADT_DA_ERROR_DEVLKERR;
		break;
	case DEVLONGERR:
		*errmsg = ADT_DA_ERROR_NO_MSG;
		*error = ENAMETOOLONG;
		break;
	case DEVNALLOCERR:
		*errmsg = ADT_DA_ERROR_DEVNALLOCERR;
		break;
	case DEVNAMEERR:
		*errmsg = ADT_DA_ERROR_NO_MSG;
		*error = EINVAL;
		break;
	case DEVSTATEERR:
		*errmsg = ADT_DA_ERROR_DEVSTATEERR;
		break;
	case DEVZONEERR:
		*errmsg = ADT_DA_ERROR_DEVZONEERR;
		break;
	case DSPMISSERR:
		*errmsg = ADT_DA_ERROR_DSPMISSERR;
		break;
	case LABELRNGERR:
		*errmsg = ADT_DA_ERROR_LABELRNGERR;
		break;
	case LOGINDEVPERMERR:
		*errmsg = ADT_DA_ERROR_LOGINDEVPERMERR;
		break;
	case NODAERR:
	case NODMAPERR:
		*errmsg = ADT_DA_ERROR_NODMAPERR;
		break;
	case PREALLOCERR:
		*errmsg = ADT_DA_ERROR_PREALLOCERR;
		break;
	case SETACLERR:
		*errmsg = ADT_DA_ERROR_SETACLERR;
		break;
	case ZONEERR:
		*errmsg = ADT_DA_ERROR_ZONEERR;
		break;
	case CLEANERR:
		*errmsg = ADT_DA_ERROR_CLEANERR;
		break;
	default:
		*error = ADT_FAIL_VALUE_UNKNOWN;
		*errmsg = ADT_DA_ERROR_NO_MSG;
		break;
	}

	*status = ADT_FAILURE;
}
