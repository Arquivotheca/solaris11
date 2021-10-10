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
 * AUTOMOUNT specific functions
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <zone.h>
#include <errno.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <pwd.h>
#include <limits.h>
#include <libscf.h>
#include <strings.h>
#include <libdlpi.h>

#include <libshare.h>
#include <libshare_impl.h>
#include "smfcfg.h"

/*
 * protocol plugin op routines
 */
static int sa_autofs_init(void);
static void sa_autofs_fini(void);

/* protocol property op routines */
static int sa_autofs_proto_get_features(uint64_t *);
static int sa_autofs_proto_get_proplist(nvlist_t **);
static int sa_autofs_proto_get_status(char **);
static int sa_autofs_proto_get_property(const char *, const char *, char **);
static int sa_autofs_proto_set_property(const char *, const char *,
    const char *);

/* protocol property validator routines */
static int range_check_validator(int, char *);
static int true_false_validator(int, char *);
static int strlen_validator(int, char *);

static void autofs_free_proto_proplist(void);

sa_proto_ops_t sa_plugin_ops = {
	.sap_hdr = {
		.pi_ptype = SA_PLUGIN_PROTO,
		.pi_type = SA_PROT_AUTOFS,
		.pi_name = "autofs",
		.pi_version = SA_LIBSHARE_VERSION,
		.pi_flags = 0,
		.pi_init = sa_autofs_init,
		.pi_fini = sa_autofs_fini
	},
	.sap_share_parse = NULL,
	.sap_share_merge = NULL,
	.sap_share_set_def_proto = NULL,
	.sap_share_validate_name = NULL,
	.sap_share_validate = NULL,
	.sap_share_publish = NULL,
	.sap_share_unpublish = NULL,
	.sap_share_unpublish_byname = NULL,
	.sap_fs_publish = NULL,
	.sap_fs_unpublish = NULL,
	.sap_share_prop_format = NULL,

	.sap_proto_get_features = sa_autofs_proto_get_features,
	.sap_proto_get_proplist = sa_autofs_proto_get_proplist,
	.sap_proto_get_status = sa_autofs_proto_get_status,
	.sap_proto_get_property = sa_autofs_proto_get_property,
	.sap_proto_set_property = sa_autofs_proto_set_property,
	.sap_proto_rem_section = NULL
};

#define	AUTOMOUNT_VERBOSE_DEFAULT	0
#define	AUTOMOUNTD_VERBOSE_DEFAULT	0
#define	AUTOMOUNT_NOBROWSE_DEFAULT	0
#define	AUTOMOUNT_TIMEOUT_DEFAULT	600
#define	AUTOMOUNT_TRACE_DEFAULT		0

/*
 * Protocol Management functions
 */
struct proto_option_defs {
	char *tag;
	char *name;	/* display name -- remove protocol identifier */
	int index;
	scf_type_t type;
	union {
	    int intval;
	    char *string;
	} defvalue;
	int32_t minval;
	int32_t maxval;
	int (*validator)(int, char *);
} autofs_proto_options[] = {
#define	PROTO_OPT_AUTOMOUNT_TIMEOUT	0
	{ "timeout",
	    "timeout",	PROTO_OPT_AUTOMOUNT_TIMEOUT,
	    SCF_TYPE_INTEGER, AUTOMOUNT_TIMEOUT_DEFAULT,
	    1, INT32_MAX, range_check_validator},
#define	PROTO_OPT_AUTOMOUNT_VERBOSE	1
	{ "automount_verbose",
	    "automount_verbose", PROTO_OPT_AUTOMOUNT_VERBOSE,
	    SCF_TYPE_BOOLEAN, AUTOMOUNT_VERBOSE_DEFAULT, 0, 1,
	    true_false_validator},
#define	PROTO_OPT_AUTOMOUNTD_VERBOSE	2
	{ "automountd_verbose",
	    "automountd_verbose", PROTO_OPT_AUTOMOUNTD_VERBOSE,
	    SCF_TYPE_BOOLEAN, AUTOMOUNTD_VERBOSE_DEFAULT, 0, 1,
	    true_false_validator},
#define	PROTO_OPT_AUTOMOUNTD_NOBROWSE	3
	{ "nobrowse",
	    "nobrowse", PROTO_OPT_AUTOMOUNTD_NOBROWSE, SCF_TYPE_BOOLEAN,
	    AUTOMOUNT_NOBROWSE_DEFAULT, 0, 1, true_false_validator},
#define	PROTO_OPT_AUTOMOUNTD_TRACE	4
	{ "trace",
	    "trace", PROTO_OPT_AUTOMOUNTD_TRACE,
	    SCF_TYPE_INTEGER, AUTOMOUNT_TRACE_DEFAULT,
	    0, 20, range_check_validator},
#define	PROTO_OPT_AUTOMOUNTD_ENV	5
	{ "environment",
	    "environment", PROTO_OPT_AUTOMOUNTD_ENV, SCF_TYPE_ASTRING,
	    NULL, 0, 1024, strlen_validator},
	{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};

#define	AUTOFS_OPT_MAX	PROTO_OPT_AUTOMOUNTD_ENV

static nvlist_t *autofs_proto_proplist = NULL;

static int
sa_autofs_init(void)
{
	return (SA_OK);
}

static void
sa_autofs_fini(void)
{
	autofs_free_proto_proplist();
}

/*
 * service_in_state(service, chkstate)
 *
 * Want to know if the specified service is in the desired state
 * (chkstate) or not. Return true (1) if it is and false (0) if it
 * isn't.
 */
static int
service_in_state(char *service, const char *chkstate)
{
	char *state;
	int ret = B_FALSE;

	state = smf_get_state(service);
	if (state != NULL) {
		/* got the state so get the equality for the return value */
		ret = strcmp(state, chkstate) == 0 ? B_TRUE : B_FALSE;
		free(state);
	}
	return (ret);
}

/*
 * Only attempt to restart the service if it is
 * currently running. In the future, it may be
 * desirable to use smf_refresh_instance if the NFS
 * services ever implement the refresh method.
 */
static void
autofs_restart_service(void)
{
	char *service = AUTOFS_DEFAULT_FMRI;
	int ret = -1;

	if (service_in_state(service, SCF_STATE_STRING_ONLINE)) {
		ret = smf_restart_instance(service);
		/*
		 * There are only a few SMF errors at this point, but
		 * it is also possible that a bad value may have put
		 * the service into maintenance if there wasn't an
		 * SMF level error.
		 */
		if (ret != 0) {
			(void) fprintf(stderr,
			    dgettext(TEXT_DOMAIN,
			    "%s failed to restart: %s\n"),
			    service, scf_strerror(scf_error()));
		} else {
			/*
			 * Check whether it has gone to "maintenance"
			 * mode or not. Maintenance implies something
			 * went wrong.
			 */
			if (service_in_state(service, SCF_STATE_STRING_MAINT)) {
				(void) fprintf(stderr,
				    dgettext(TEXT_DOMAIN,
				    "%s failed to restart\n"), service);
			}
		}
	}
}

static int
findprotoopt(const char *propname)
{
	int i;

	for (i = 0; autofs_proto_options[i].tag != NULL; i++)
		if (strcmp(autofs_proto_options[i].name, propname) == 0)
			return (i);
	return (-1);
}

static int
is_a_number(char *number)
{
	int ret = 1;
	int hex = 0;

	if (strncmp(number, "0x", 2) == 0) {
		number += 2;
		hex = 1;
	} else if (*number == '-') {
		number++; /* skip the minus */
	}

	while (ret == 1 && *number != '\0') {
		if (hex) {
			ret = isxdigit(*number++);
		} else {
			ret = isdigit(*number++);
		}
	}

	return (ret);
}

static int
range_check_validator(int index, char *value)
{
	int ret = SA_OK;
	if (!is_a_number(value)) {
		ret = SA_INVALID_PROP_VAL;
	} else {
		int val;
		val = strtoul(value, NULL, 0);
		if (val < autofs_proto_options[index].minval ||
		    val > autofs_proto_options[index].maxval)
			ret = SA_INVALID_PROP_VAL;
	}
	return (ret);
}

/*ARGSUSED*/
static int
true_false_validator(int index, char *value)
{
	if ((strcasecmp(value, "true") == 0) ||
	    (strcasecmp(value, "false") == 0) ||
	    (strcmp(value, "1") == 0) ||
	    (strcmp(value, "0") == 0)) {
		return (SA_OK);
	}
	return (SA_INVALID_PROP_VAL);
}

static int
strlen_validator(int index, char *value)
{
	if (value == NULL) {
		if (autofs_proto_options[index].minval == 0)
			return (SA_OK);
		else
			return (SA_INVALID_PROP_VAL);
	}

	if (strlen(value) > autofs_proto_options[index].maxval ||
	    strlen(value) < autofs_proto_options[index].minval)
		return (SA_INVALID_PROP_VAL);

	return (SA_OK);
}

/*
 * autofs_validate_proto_prop(index, name, value)
 *
 * Verify that the property specifed by name can take the new
 * value. This is a sanity check to prevent bad values getting into
 * the default files.
 */
static int
autofs_validate_proto_prop(int index, const char *value)
{
	if (index < 0 || index > AUTOFS_OPT_MAX)
		return (SA_INVALID_PROP);

	if (autofs_proto_options[index].validator == NULL)
		return (SA_OK);

	return (autofs_proto_options[index].validator(index, (char *)value));
}

static void
autofs_free_proto_proplist(void)
{
	if (autofs_proto_proplist != NULL) {
		nvlist_free(autofs_proto_proplist);
		autofs_proto_proplist = NULL;
	}
}

static int
autofs_add_default_value(int index)
{
	char value[MAXDIGITS];
	int ret;

	if (index < 0 || index > AUTOFS_OPT_MAX)
		return (SA_INVALID_PROP);

	switch (autofs_proto_options[index].type) {
	case SCF_TYPE_INTEGER:
		(void) snprintf(value, sizeof (value), "%d",
		    autofs_proto_options[index].defvalue.intval);
		break;

	case SCF_TYPE_BOOLEAN:
		if (autofs_proto_options[index].defvalue.intval == 0)
			(void) snprintf(value, sizeof (value), "false");
		else
			(void) snprintf(value, sizeof (value), "true");
		break;

	case SCF_TYPE_ASTRING:
		(void) snprintf(value, sizeof (value), "%s",
		    autofs_proto_options[index].defvalue.string != NULL ?
		    autofs_proto_options[index].defvalue.string : "");
		break;

	default:
		return (SA_INVALID_PROP);
	}

	ret = nvlist_add_string(autofs_proto_proplist,
	    autofs_proto_options[index].name, value);

	return ((ret == 0) ? SA_OK : SA_NO_MEMORY);
}

static int
autofs_init_proto_proplist(void)
{
	int i;
	char name[PATH_MAX];
	char value[PATH_MAX];
	char *instance = NULL;
	int bufsz = 0;
	scf_type_t sctype;
	int ret = SA_OK;

	autofs_free_proto_proplist();

	if (nvlist_alloc(&autofs_proto_proplist, NV_UNIQUE_NAME, 0) != 0)
		return (SA_NO_MEMORY);

	for (i = 0; autofs_proto_options[i].tag != NULL; i++) {
		bzero(value, PATH_MAX);
		(void) strlcpy(name, autofs_proto_options[i].name, PATH_MAX);
		sctype = autofs_proto_options[i].type;
		bufsz = PATH_MAX;
		ret = autofs_smf_get_prop(name, value, instance,
		    sctype, AUTOFS_FMRI, &bufsz);
		if (ret == 0) {
			/* add property to list */
			ret = nvlist_add_string(autofs_proto_proplist,
			    name, value);
		} else {
			/* add default value to list */
			ret = autofs_add_default_value(i);
		}

		if (ret != 0) {
			autofs_free_proto_proplist();
			return (SA_NO_MEMORY);
		}
	}

	return (ret);
}

/*
 * sa_autofs_get_features
 *
 * return supported features
 */
static int
sa_autofs_proto_get_features(uint64_t *features)
{
	*features = SA_FEATURE_NONE;
	return (SA_OK);
}

static int
sa_autofs_proto_get_proplist(nvlist_t **proplist)
{
	int ret;

	if (autofs_proto_proplist == NULL) {
		if ((ret = autofs_init_proto_proplist()) != SA_OK) {
			*proplist = NULL;
			return (ret);
		}
	}

	if (nvlist_dup(autofs_proto_proplist, proplist, 0) == 0)
		return (SA_OK);
	else
		return (SA_NO_MEMORY);
}

/*
 * sa_autofs_proto_get_status()
 *
 * What is the current status of the AUTOFS_DEFAULT_FMRI?
 * We use the SMF state here.
 * Caller must free the returned value.
 */
static int
sa_autofs_proto_get_status(char **status_str)
{
	*status_str = smf_get_state(AUTOFS_DEFAULT_FMRI);
	if (*status_str == NULL &&
	    (*status_str = strdup("-")) == NULL)
		return (SA_NO_MEMORY);
	else
		return (SA_OK);
}

/*
 * sa_autofs_proto_get_property
 */
/*ARGSUSED*/
static int
sa_autofs_proto_get_property(const char *sectname, const char *propname,
    char **propval)
{
	char *val;

	if (autofs_proto_proplist == NULL) {
		if (autofs_init_proto_proplist() != SA_OK) {
			*propval = NULL;
			return (SA_NO_MEMORY);
		}
	}

	if (nvlist_lookup_string(autofs_proto_proplist, propname, &val) == 0) {
		if ((*propval = strdup(val)) != NULL)
			return (SA_OK);
		else
			return (SA_NO_MEMORY);
	} else {
		return (SA_NO_SUCH_PROP);
	}
}

/*
 * sa_autofs_proto_set_property
 */
/*ARGSUSED*/
static int
sa_autofs_proto_set_property(const char *sectname, const char *propname,
    const char *propval)
{
	int index;
	scf_type_t sctype;
	int ret = SA_OK;
	char *value = (char *)propval;

	if (propname == NULL)
		return (SA_INVALID_PROP);

	if (propval == NULL)
		return (SA_INVALID_PROP_VAL);

	if ((index = findprotoopt(propname)) < 0)
		return (SA_INVALID_PROP);

	/* test for valid value */
	ret = autofs_validate_proto_prop(index, propval);
	if (ret == SA_OK) {
		sctype = autofs_proto_options[index].type;
		if (sctype == SCF_TYPE_BOOLEAN)
			value = (string_to_boolean(propval)) ? "1" : "0";
		ret = autofs_smf_set_prop((char *)propname, value, NULL, sctype,
		    AUTOFS_FMRI);

		if (ret == 0) {
			autofs_restart_service();
			(void) autofs_init_proto_proplist();
		} else {
			ret = SA_SCF_ERROR;
		}
	}

	return (ret);
}
