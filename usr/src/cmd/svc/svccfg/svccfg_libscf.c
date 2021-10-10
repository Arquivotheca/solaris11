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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <door.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libscf.h>
#include <libscf_priv.h>
#include <libtecla.h>
#include <libuutil.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wait.h>
#include <poll.h>
#include <libgen.h>

#include <libxml/tree.h>

#include <sys/param.h>

#include <sys/stat.h>
#include <sys/mman.h>

#ifndef NATIVE_BUILD
#include <libcontract_priv.h>
#endif

#include "svccfg.h"
#include "notify_params.h"
#include "manifest_hash.h"
#include "manifest_find.h"

/* The colon namespaces in each entity (each followed by a newline). */
#define	COLON_NAMESPACES	":properties\n"

#define	TEMP_FILE_PATTERN	"/tmp/svccfg-XXXXXX"

/* These are characters which the lexer requires to be in double-quotes. */
#define	CHARS_TO_QUOTE		" \t\n\\>=\"()"

#define	HASH_SIZE		16
#define	HASH_PG_TYPE		"framework"
#define	HASH_PG_FLAGS		0
#define	HASH_PROP		"md5sum"

/*
 * Indentation used in the output of the describe subcommand.
 */
#define	TMPL_VALUE_INDENT	"  "
#define	TMPL_INDENT		"    "
#define	TMPL_INDENT_2X		"        "
#define	TMPL_CHOICE_INDENT	"      "

/*
 * Directory locations for manifests
 */
#define	VARSVC_PR		"var_svc_manifest"
#define	LIBSVC_PR		"lib_svc_manifest"
#define	MFSTFILEPR		"manifestfile"

/*
 * System and site profile directories
 */
#define	ETCSYSPROFILE_DIR	"/etc/svc/profile/"
#define	ETCSITEPROFILE_DIR	"/etc/svc/profile/site/"
#define	ETCSVCPROFILE_DIR	ETCSYSPROFILE_DIR

/*
 * Supported system profiles
 */
#define	ETCSYSPROFILE_GENERIC	"/etc/svc/profile/generic.xml"
#define	ETCSYSPROFILE_PLATFORM	"/etc/svc/profile/platform.xml"

/*
 * Contract information
 */
#define	ORCL_TESTING		"ORCL_testing"

/*
 * Supported site.xml profiles
 */
#define	ETCSITEPROFILE_FILE	"/etc/svc/profile/site.xml"
#define	VARSITEPROFILE_FILE	"/var/svc/profile/site.xml"

#define	SUPPORTPROP		"support"

#define	MFSTHISTFILE		"/lib/svc/share/mfsthistory"

#define	MFSTFILE_MAX		16

/* Bit representation for decoration layers */
#define	SVCCFG_CURRENT		0x00
#define	SVCCFG_MANIFEST		0x01
#define	SVCCFG_SYSTEM_PROFILE	0x02
#define	SVCCFG_SITE_PROFILE	0x04
#define	SVCCFG_ADMIN		0x08

/*
 * Decoration
 */
static const struct {
	scf_decoration_layer_t li_layer;
	int layer_flag;
} svccfg_layer_info[] = {
	{SCF_DECORATION_MANIFEST, SVCCFG_MANIFEST},
	{SCF_DECORATION_SYSTEM_PROFILE, SVCCFG_SYSTEM_PROFILE},
	{SCF_DECORATION_SITE_PROFILE, SVCCFG_SITE_PROFILE},
	{SCF_DECORATION_ADMIN, SVCCFG_ADMIN},
	{SCF_DECORATION_TOP, SVCCFG_CURRENT}
};

#define	SVCCFG_LAYER_INFO_COUNT (sizeof (svccfg_layer_info) / \
	    sizeof (*svccfg_layer_info))

#define	IS_LAYER_BACKED_BY_FILE(l) \
	((l) > SCF_DECORATION_INVALID && (l) < SCF_DECORATION_ADMIN)

/*
 * These are the classes of elements which may appear as children of service
 * or instance elements in XML manifests.
 */
struct entity_elts {
	xmlNodePtr	create_default_instance;
	xmlNodePtr	single_instance;
	xmlNodePtr	restarter;
	xmlNodePtr	dependencies;
	xmlNodePtr	dependents;
	xmlNodePtr	method_context;
	xmlNodePtr	exec_methods;
	xmlNodePtr	notify_params;
	xmlNodePtr	property_groups;
	xmlNodePtr	instances;
	xmlNodePtr	stability;
	xmlNodePtr	template;
};

/*
 * Likewise for property_group elements.
 */
struct pg_elts {
	xmlNodePtr	stability;
	xmlNodePtr	propvals;
	xmlNodePtr	properties;
};

/*
 * Likewise for template elements.
 */
struct template_elts {
	xmlNodePtr	common_name;
	xmlNodePtr	description;
	xmlNodePtr	documentation;
};

/*
 * Likewise for type (for notification parameters) elements.
 */
struct params_elts {
	xmlNodePtr	paramval;
	xmlNodePtr	parameter;
};

/*
 * This structure is for snaplevel lists.  They are convenient because libscf
 * only allows traversing snaplevels in one direction.
 */
struct snaplevel {
	uu_list_node_t	list_node;
	scf_snaplevel_t	*sl;
};

/*
 * This is used for communication between lscf_service_export and
 * export_callback.
 */
struct export_args {
	const char	*filename;
	int 		flags;
};

/*
 * The service_manifest structure is used by the upgrade process
 * to create a list of service to manifest linkages from the manifests
 * in a set of given directories.
 */
typedef struct service_manifest {
	const char 	*servicename;
	uu_list_t	*mfstlist;
	size_t	mfstlist_sz;

	uu_avl_node_t	svcmfst_node;
} service_manifest_t;

/*
 * Structure to track the manifest file property group
 * and the manifest file associated with that property
 * group.  Also, a flag to keep the access once it has
 * been checked.
 */
struct mpg_mfile {
	char	*mpg;
	char	*mfile;
	int	access;
	int	nosvcsupport;   /* set if file doesn't deliver service */
};

const char * const scf_pg_general = SCF_PG_GENERAL;
const char * const scf_group_framework = SCF_GROUP_FRAMEWORK;
const char * const scf_property_enabled = SCF_PROPERTY_ENABLED;
const char * const scf_property_complete = SCF_PROPERTY_COMPLETE;
const char * const scf_property_external = "external";

const char * const snap_initial = "initial";
const char * const snap_previous = "previous";
const char * const snap_running = "running";

scf_handle_t *g_hndl = NULL;	/* only valid after lscf_prep_hndl() */
boolean_t g_hndl_noreset = B_FALSE;
boolean_t cur_selection_is_masked = B_FALSE;
char *cur_selection_pattern = NULL;

ssize_t max_scf_fmri_len;
ssize_t max_scf_name_len;
ssize_t max_scf_pg_type_len;
ssize_t max_scf_value_len;
ssize_t max_scf_selection_str_len;
static size_t max_scf_len;

static scf_scope_t *cur_scope;
static scf_service_t *cur_svc = NULL;
static scf_instance_t *cur_inst = NULL;
static scf_snapshot_t *cur_snap = NULL;
static scf_snaplevel_t *cur_level = NULL;

static uu_list_pool_t *snaplevel_pool;
/* cur_levels is the snaplevels of cur_snap, from least specific to most. */
static uu_list_t *cur_levels;
static struct snaplevel *cur_elt;		/* cur_elt->sl == cur_level */

static FILE *tempfile = NULL;
static char tempfilename[sizeof (TEMP_FILE_PATTERN)] = "";

static const char *emsg_entity_not_selected;
static const char *emsg_permission_denied;
static const char *emsg_create_xml;
static const char *emsg_cant_modify_snapshots;
static const char *emsg_invalid_for_snapshot;
static const char *emsg_read_only;
static const char *emsg_deleted;
static const char *emsg_invalid_pg_name;
static const char *emsg_invalid_prop_name;
static const char *emsg_no_such_pg;
static const char *emsg_fmri_invalid_pg_name;
static const char *emsg_fmri_invalid_pg_name_type;
static const char *emsg_pg_deleted;
static const char *emsg_pg_mod_perm;
static const char *emsg_pg_add_perm;
static const char *emsg_pg_del_perm;
static const char *emsg_snap_perm;
static const char *emsg_masked_selection;

static const char *msg_delcust_svc;
static const char *msg_delcust_inst;
static const char *msg_delcust_pg;
static const char *msg_delcust_prop;
static const char *msg_delcust_process;
static const char *emsg_delcust_svc;

static int no_refresh = 0;
static int do_print_layer = 0;

/* import globals, to minimize allocations */
static scf_scope_t *imp_scope = NULL;
static scf_service_t *imp_svc = NULL, *imp_tsvc = NULL;
static scf_instance_t *imp_inst = NULL, *imp_tinst = NULL;
static scf_snapshot_t *imp_snap = NULL, *imp_lisnap = NULL, *imp_tlisnap = NULL;
static scf_snapshot_t *imp_rsnap = NULL;
static scf_snaplevel_t *imp_snpl = NULL, *imp_rsnpl = NULL;
static scf_propertygroup_t *imp_pg = NULL, *imp_pg2 = NULL;
static scf_property_t *imp_prop = NULL;
static scf_iter_t *imp_iter = NULL;
static scf_iter_t *imp_rpg_iter = NULL;
static scf_iter_t *imp_up_iter = NULL;
static scf_transaction_t *imp_tx = NULL;	/* always reset this */
static char *imp_str = NULL;
static size_t imp_str_sz;
static char *imp_tsname = NULL;
static char *imp_fe1 = NULL;			/* for fmri_equal() */
static char *imp_fe2 = NULL;
static uu_list_t *imp_deleted_dpts = NULL;	/* pgroup_t's to refresh */
static char *imp_bundle = NULL;			/* bundle for import/apply */

/* upgrade_dependents() globals */
static scf_instance_t *ud_inst = NULL;
static scf_snaplevel_t *ud_snpl = NULL;
static scf_propertygroup_t *ud_pg = NULL;
static scf_propertygroup_t *ud_cur_depts_pg = NULL;
static scf_propertygroup_t *ud_run_dpts_pg = NULL;
static scf_property_t *ud_prop = NULL;
static scf_property_t *ud_dpt_prop = NULL;
static scf_value_t *ud_val = NULL;
static scf_iter_t *ud_iter = NULL, *ud_iter2 = NULL;
static scf_transaction_t *ud_tx = NULL;
static char *ud_ctarg = NULL;
static char *ud_oldtarg = NULL;
static char *ud_name = NULL;

/* export globals */
static scf_instance_t *exp_inst;
static scf_propertygroup_t *exp_pg;
static scf_property_t *exp_prop;
static scf_decoration_t *exp_dec;
static scf_value_t *exp_val;
static scf_iter_t *exp_inst_iter, *exp_pg_iter, *exp_prop_iter, *exp_val_iter;
static scf_iter_t *exp_dec_iter;
static char *exp_str;
static size_t exp_str_sz;
static int exp_dec_layer_map = SVCCFG_CURRENT;

/* cleanup globals */
static uu_avl_pool_t *service_manifest_pool = NULL;
static uu_avl_t *service_manifest_tree = NULL;

/* sets of layers */
static scf_decoration_layer_t layers_top[] = {
	SCF_DECORATION_TOP,
	SCF_DECORATION_INVALID
};
static scf_decoration_layer_t layers_admin_customizations[] = {
	SCF_DECORATION_ADMIN,
	SCF_DECORATION_INVALID
};
static scf_decoration_layer_t layers_local_customizations[] = {
	SCF_DECORATION_SITE_PROFILE,
	SCF_DECORATION_ADMIN,
	SCF_DECORATION_INVALID
};
static scf_decoration_layer_t layers_all[] = {
	SCF_DECORATION_TOP,
	SCF_DECORATION_MANIFEST,
	SCF_DECORATION_SYSTEM_PROFILE,
	SCF_DECORATION_SITE_PROFILE,
	SCF_DECORATION_ADMIN,
	SCF_DECORATION_INVALID
};

typedef enum fname {    /* enumeration of field names */
	F_INVALID = 0,
	F_PROPNAME = 1,	/* property name */
	F_PGNAME,	/* property group name */
	F_INSTNAME,	/* instance name */
	F_SVCNAME,	/* service name */
	F_LAYER,	/* layer */
	F_PROPTYPE,	/* property type */
	F_VALUE,	/* property values */
	F_FILE,		/* manifest file */
	F_MASKED,	/* masked */
	F_TIME		/* modification time */
} fname_t;

#define	MAX_FARRAY_SIZE (sizeof (fname_t) * F_TIME * 2)

typedef struct fnames {
	int	f_size;
	fname_t f_array[MAX_FARRAY_SIZE];
} fnames_t;

static void scfdie_lineno(int lineno) __NORETURN;

static char *start_method_names[] = {
	"start",
	"inetd_start",
	NULL
};

static struct uri_scheme {
	const char *scheme;
	const char *protocol;
} uri_scheme[] = {
	{ "mailto", "smtp" },
	{ "snmp", "snmp" },
	{ "syslog", "syslog" },
	{ NULL, NULL }
};
#define	URI_SCHEME_NUM ((sizeof (uri_scheme) / \
    sizeof (struct uri_scheme)) - 1)

static int
check_uri_scheme(const char *scheme)
{
	int i;

	for (i = 0; uri_scheme[i].scheme != NULL; ++i) {
		if (strcmp(scheme, uri_scheme[i].scheme) == 0)
			return (i);
	}

	return (-1);
}

static int
check_uri_protocol(const char *p)
{
	int i;

	for (i = 0; uri_scheme[i].protocol != NULL; ++i) {
		if (strcmp(p, uri_scheme[i].protocol) == 0)
			return (i);
	}

	return (-1);
}

/*
 * For unexpected libscf errors.
 */
#ifdef NDEBUG

static void scfdie(void) __NORETURN;

static void
scfdie(void)
{
	scf_error_t err = scf_error();

	if (err == SCF_ERROR_CONNECTION_BROKEN)
		uu_die(gettext("Repository connection broken.  Exiting.\n"));

	uu_die(gettext("Unexpected fatal libscf error: %s.  Exiting.\n"),
	    scf_strerror(err));
}

#else

#define	scfdie()	scfdie_lineno(__LINE__)

static void
scfdie_lineno(int lineno)
{
	scf_error_t err = scf_error();

	if (err == SCF_ERROR_CONNECTION_BROKEN)
		uu_die(gettext("Repository connection broken.  Exiting.\n"));

	uu_die(gettext("Unexpected libscf error on line %d of " __FILE__
	    ": %s.\n"), lineno, scf_strerror(err));
}

#endif

static void
scfwarn(void)
{
	warn(gettext("Unexpected libscf error: %s.\n"),
	    scf_strerror(scf_error()));
}

/*
 * Clear a field of a structure.
 */
static int
clear_int(void *a, void *b)
{
	/* LINTED */
	*(int *)((char *)a + (size_t)b) = 0;

	return (UU_WALK_NEXT);
}

static int
scferror2errno(scf_error_t err)
{
	switch (err) {
	case SCF_ERROR_BACKEND_ACCESS:
		return (EACCES);

	case SCF_ERROR_BACKEND_READONLY:
		return (EROFS);

	case SCF_ERROR_CONNECTION_BROKEN:
		return (ECONNABORTED);

	case SCF_ERROR_CONSTRAINT_VIOLATED:
	case SCF_ERROR_INVALID_ARGUMENT:
		return (EINVAL);

	case SCF_ERROR_DELETED:
		return (ECANCELED);

	case SCF_ERROR_EXISTS:
		return (EEXIST);

	case SCF_ERROR_NO_MEMORY:
		return (ENOMEM);

	case SCF_ERROR_NO_RESOURCES:
		return (ENOSPC);

	case SCF_ERROR_NOT_FOUND:
		return (ENOENT);

	case SCF_ERROR_PERMISSION_DENIED:
		return (EPERM);

	default:
#ifndef NDEBUG
		(void) fprintf(stderr, "%s:%d: Unknown libscf error %d.\n",
		    __FILE__, __LINE__, err);
#else
		(void) fprintf(stderr, "Unknown libscf error %d.\n", err);
#endif
		abort();
		/* NOTREACHED */
	}
}

static int
entity_get_pg(void *ent, int issvc, const char *name,
    scf_propertygroup_t *pg)
{
	if (issvc)
		return (scf_service_get_pg(ent, name, pg));
	else
		return (scf_instance_get_pg(ent, name, pg));
}

static void
entity_destroy(void *ent, int issvc)
{
	if (issvc)
		scf_service_destroy(ent);
	else
		scf_instance_destroy(ent);
}

static int
get_pg(const char *pg_name, scf_propertygroup_t *pg)
{
	int ret;

	if (cur_level != NULL)
		ret = scf_snaplevel_get_pg(cur_level, pg_name, pg);
	else if (cur_inst != NULL)
		ret = scf_instance_get_pg(cur_inst, pg_name, pg);
	else
		ret = scf_service_get_pg(cur_svc, pg_name, pg);

	return (ret);
}

/*
 * Find a snaplevel in a snapshot.  If get_svc is true, find the service
 * snaplevel.  Otherwise find the instance snaplevel.
 *
 * Returns
 *   0 - success
 *   ECONNABORTED - repository connection broken
 *   ECANCELED - instance containing snap was deleted
 *   ENOENT - snap has no snaplevels
 *	    - requested snaplevel not found
 */
static int
get_snaplevel(scf_snapshot_t *snap, int get_svc, scf_snaplevel_t *snpl)
{
	if (scf_snapshot_get_base_snaplevel(snap, snpl) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_DELETED:
		case SCF_ERROR_NOT_FOUND:
			return (scferror2errno(scf_error()));

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_snapshot_get_base_snaplevel",
			    scf_error());
		}
	}

	for (;;) {
		ssize_t ssz;

		ssz = scf_snaplevel_get_instance_name(snpl, NULL, 0);
		if (ssz >= 0) {
			if (!get_svc)
				return (0);
		} else {
			switch (scf_error()) {
			case SCF_ERROR_CONSTRAINT_VIOLATED:
				if (get_svc)
					return (0);
				break;

			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				return (scferror2errno(scf_error()));

			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error("scf_snaplevel_get_instance_name",
				    scf_error());
			}
		}

		if (scf_snaplevel_get_next_snaplevel(snpl, snpl) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
			case SCF_ERROR_CONNECTION_BROKEN:
			case SCF_ERROR_DELETED:
				return (scferror2errno(scf_error()));

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_INVALID_ARGUMENT:
			default:
				bad_error("scf_snaplevel_get_next_snaplevel",
				    scf_error());
			}
		}
	}
}

/*
 * If issvc is 0, take ent to be a pointer to an scf_instance_t.  If it has
 * a running snapshot, and that snapshot has an instance snaplevel, set pg to
 * the property group named name in it.  If it doesn't have a running
 * snapshot, set pg to the instance's current property group named name.
 *
 * If issvc is nonzero, take ent to be a pointer to an scf_service_t, and walk
 * its instances.  If one has a running snapshot with a service snaplevel, set
 * pg to the property group named name in it.  If no such snaplevel could be
 * found, set pg to the service's current property group named name.
 *
 * iter, inst, snap, and snpl are required scratch objects.
 *
 * Returns
 *   0 - success
 *   ECONNABORTED - repository connection broken
 *   ECANCELED - ent was deleted
 *   ENOENT - no such property group
 *   EINVAL - name is an invalid property group name
 *   EBADF - found running snapshot is missing a snaplevel
 */
static int
entity_get_running_pg(void *ent, int issvc, const char *name,
    scf_propertygroup_t *pg, scf_iter_t *iter, scf_instance_t *inst,
    scf_snapshot_t *snap, scf_snaplevel_t *snpl)
{
	int r;

	if (issvc) {
		/* Search for an instance with a running snapshot. */
		if (scf_iter_service_instances(iter, ent) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				return (scferror2errno(scf_error()));

			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_HANDLE_MISMATCH:
			default:
				bad_error("scf_iter_service_instances",
				    scf_error());
			}
		}

		for (;;) {
			r = scf_iter_next_instance(iter, inst);
			if (r == 0) {
				if (scf_service_get_pg(ent, name, pg) == 0)
					return (0);

				switch (scf_error()) {
				case SCF_ERROR_DELETED:
				case SCF_ERROR_NOT_FOUND:
				case SCF_ERROR_INVALID_ARGUMENT:
				case SCF_ERROR_CONNECTION_BROKEN:
					return (scferror2errno(scf_error()));

				case SCF_ERROR_NOT_BOUND:
				case SCF_ERROR_HANDLE_MISMATCH:
				case SCF_ERROR_NOT_SET:
				default:
					bad_error("scf_service_get_pg",
					    scf_error());
				}
			}
			if (r != 1) {
				switch (scf_error()) {
				case SCF_ERROR_DELETED:
				case SCF_ERROR_CONNECTION_BROKEN:
					return (scferror2errno(scf_error()));

				case SCF_ERROR_INVALID_ARGUMENT:
				case SCF_ERROR_NOT_SET:
				case SCF_ERROR_NOT_BOUND:
				case SCF_ERROR_HANDLE_MISMATCH:
				default:
					bad_error("scf_iter_next_instance",
					    scf_error());
				}
			}

			if (scf_instance_get_snapshot(inst, snap_running,
			    snap) == 0)
				break;

			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
			case SCF_ERROR_DELETED:
				continue;

			case SCF_ERROR_CONNECTION_BROKEN:
				return (ECONNABORTED);

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error("scf_instance_get_snapshot",
				    scf_error());
			}
		}
	} else {
		if (scf_instance_get_snapshot(ent, snap_running, snap) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
				break;

			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				return (scferror2errno(scf_error()));

			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_instance_get_snapshot",
				    scf_error());
			}

			if (scf_instance_get_pg(ent, name, pg) == 0)
				return (0);

			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_NOT_FOUND:
			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_CONNECTION_BROKEN:
				return (scferror2errno(scf_error()));

			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_instance_get_pg", scf_error());
			}
		}
	}

	r = get_snaplevel(snap, issvc, snpl);
	switch (r) {
	case 0:
		break;

	case ECONNABORTED:
	case ECANCELED:
		return (r);

	case ENOENT:
		return (EBADF);

	default:
		bad_error("get_snaplevel", r);
	}

	if (scf_snaplevel_get_pg(snpl, name, pg) == 0)
		return (0);

	switch (scf_error()) {
	case SCF_ERROR_DELETED:
	case SCF_ERROR_INVALID_ARGUMENT:
	case SCF_ERROR_CONNECTION_BROKEN:
	case SCF_ERROR_NOT_FOUND:
		return (scferror2errno(scf_error()));

	case SCF_ERROR_NOT_BOUND:
	case SCF_ERROR_HANDLE_MISMATCH:
	case SCF_ERROR_NOT_SET:
	default:
		bad_error("scf_snaplevel_get_pg", scf_error());
		/* NOTREACHED */
	}
}

/*
 * To be registered with atexit().
 */
static void
remove_tempfile(void)
{
	int ret;

	if (tempfile != NULL) {
		if (fclose(tempfile) == EOF)
			(void) warn(gettext("Could not close temporary file"));
		tempfile = NULL;
	}

	if (tempfilename[0] != '\0') {
		do {
			ret = remove(tempfilename);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1)
			warn(gettext("Could not remove temporary file"));
		tempfilename[0] = '\0';
	}
}

/*
 * Launch private svc.configd(1M) for manipulating alternate repositories.
 */
static void
start_private_repository(engine_state_t *est)
{
	int fd, stat;
	struct door_info info;
	pid_t pid;

	/*
	 * 1.  Create a temporary file for the door.
	 */
	if (est->sc_repo_doorname != NULL)
		free((void *)est->sc_repo_doorname);

	est->sc_repo_doorname = tempnam(est->sc_repo_doordir, "scfdr");
	if (est->sc_repo_doorname == NULL)
		uu_die(gettext("Could not acquire temporary filename"));

	fd = open(est->sc_repo_doorname, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0)
		uu_die(gettext("Could not create temporary file for "
		    "repository server"));

	(void) close(fd);

	/*
	 * 2.  Launch a configd with that door, using the specified
	 * repository.
	 */
	if ((est->sc_repo_pid = fork()) == 0) {
		(void) execlp(est->sc_repo_server, est->sc_repo_server, "-p",
		    "-d", est->sc_repo_doorname, "-r", est->sc_repo_filename,
		    NULL);
		uu_die(gettext("Could not execute %s"), est->sc_repo_server);
	} else if (est->sc_repo_pid == -1)
		uu_die(gettext("Attempt to fork failed"));

	do {
		pid = waitpid(est->sc_repo_pid, &stat, 0);
	} while (pid == -1 && errno == EINTR);

	if (pid == -1)
		uu_die(gettext("Could not waitpid() for repository server"));

	if (!WIFEXITED(stat)) {
		uu_die(gettext("Repository server failed (status %d).\n"),
		    stat);
	} else if (WEXITSTATUS(stat) != 0) {
		uu_die(gettext("Repository server failed (exit %d).\n"),
		    WEXITSTATUS(stat));
	}

	/*
	 * See if it was successful by checking if the door is a door.
	 */

	fd = open(est->sc_repo_doorname, O_RDWR);
	if (fd < 0)
		uu_die(gettext("Could not open door \"%s\""),
		    est->sc_repo_doorname);

	if (door_info(fd, &info) < 0)
		uu_die(gettext("Unexpected door_info() error"));

	if (close(fd) == -1)
		warn(gettext("Could not close repository door"),
		    strerror(errno));

	est->sc_repo_pid = info.di_target;
}

static int
scf_layer_to_flag(scf_decoration_layer_t t)
{
	int i;

	for (i = 0; i < SVCCFG_LAYER_INFO_COUNT; i++)
		if (svccfg_layer_info[i].li_layer == t)
			return (svccfg_layer_info[i].layer_flag);

	return (SVCCFG_CURRENT);
}

void
lscf_cleanup(void)
{
	/*
	 * In the case where we've launched a private svc.configd(1M)
	 * instance, we must terminate our child and remove the temporary
	 * rendezvous point.
	 */
	if (est->sc_repo_pid > 0) {
		(void) kill(est->sc_repo_pid, SIGTERM);
		(void) waitpid(est->sc_repo_pid, NULL, 0);
		(void) unlink(est->sc_repo_doorname);

		est->sc_repo_pid = 0;
	}
}

void
unselect_cursnap(void)
{
	void *cookie;

	cur_level = NULL;

	cookie = NULL;
	while ((cur_elt = uu_list_teardown(cur_levels, &cookie)) != NULL) {
		scf_snaplevel_destroy(cur_elt->sl);
		free(cur_elt);
	}

	scf_snapshot_destroy(cur_snap);
	cur_snap = NULL;
}

/*
 * Returns 0 for success, 1 for failure.
 *
 * The only failure mode is when the current selection is masked, but the caller
 * has requested naive binding.
 */
int
lscf_prep_hndl(int flags)
{
	static int bound_flags;
	char *cur_fmri = NULL;

	if (g_hndl == NULL) {
		g_hndl = scf_handle_create(SCF_VERSION);
		if (g_hndl == NULL)
			scfdie();

		if (est->sc_repo_filename != NULL)
			start_private_repository(est);

		if (est->sc_repo_doorname != NULL) {
			scf_value_t *repo_value;
			int ret;

			repo_value = scf_value_create(g_hndl);
			if (repo_value == NULL)
				scfdie();

			ret = scf_value_set_astring(repo_value,
			    est->sc_repo_doorname);
			assert(ret == 0);

			if (scf_handle_decorate(g_hndl, "door_path",
			    repo_value) != 0)
			scfdie();

			scf_value_destroy(repo_value);
		}
	} else {
		if (bound_flags == flags || g_hndl_noreset == B_TRUE)
			return (0);

		if ((flags & LSCF_HNDL_MASKED) == 0 &&
		    cur_selection_is_masked) {
			/*
			 * If we're running interactively, then the svccfg
			 * select subcommand has already succeeded.  We need a
			 * descriptive message that the current subcommand is
			 * unsupported, but we shouldn't fail.
			 *
			 * Otherwise, for noninteractive use, we print the same
			 * message that select prints when it can't match a
			 * pattern.
			 *
			 * Using semerr() works in both cases, because it only
			 * exits in the noninteractive case.
			 */
			if (est->sc_cmd_flags & SC_CMD_IACTIVE) {
				semerr(emsg_masked_selection);
			} else {
				semerr(scf_get_msg(SCF_MSG_PATTERN_NOINSTSVC),
				    cur_selection_pattern);
			}

			return (1);
		}

		cur_fmri = safe_malloc(max_scf_selection_str_len + 1);
		lscf_get_selection_str(cur_fmri, max_scf_selection_str_len + 1);
		if (scf_handle_unbind(g_hndl) != 0)
			scfdie();

		scf_instance_destroy(cur_inst);
		scf_service_destroy(cur_svc);
		scf_scope_destroy(cur_scope);

		cur_inst = NULL;
		cur_svc = NULL;
		cur_scope = NULL;
	}

	bound_flags = flags;

	if (scf_handle_decorate(g_hndl, SCF_SHOW_MASKED,
	    (flags & LSCF_HNDL_MASKED) ?
	    SCF_DECORATE_TRUE : SCF_DECORATE_CLEAR) != 0)
		scfdie();

	if (scf_handle_bind(g_hndl) != 0)
		uu_die(gettext("Could not connect to repository server: %s.\n"),
		    scf_strerror(scf_error()));

	cur_scope = scf_scope_create(g_hndl);
	if (cur_scope == NULL)
		scfdie();

	if (scf_handle_get_local_scope(g_hndl, cur_scope) != 0)
		scfdie();

	if (cur_fmri != NULL) {
		if (cur_snap) {
			char *cur_snap_str, *tmp_ptr, *real_cur_fmri;
			int unseli = 0;

			real_cur_fmri = cur_fmri;

			cur_snap_str = strchr(cur_fmri, '[') + 1;
			tmp_ptr = strchr(cur_fmri, ']') + 1;
			cur_fmri = tmp_ptr;
			tmp_ptr--;
			*tmp_ptr = '\0';

			if ((tmp_ptr = strchr(cur_fmri, '[')) != NULL) {
				while (*tmp_ptr != ']') {
					*tmp_ptr = *(tmp_ptr + 1);
					tmp_ptr++;
				}

				tmp_ptr--;
				*tmp_ptr = '\0';

				unseli = 1;
			}

			unselect_cursnap();

			lscf_select(cur_fmri);

			lscf_selectsnap(cur_snap_str);

			if (unseli)
				lscf_unselect();

			cur_fmri = real_cur_fmri;
		} else {
			lscf_select(cur_fmri);
		}

		free(cur_fmri);
	}

	return (0);
}

static void
repository_teardown(void)
{
	if (g_hndl != NULL) {
		if (cur_snap != NULL)
			unselect_cursnap();
		scf_instance_destroy(cur_inst);
		scf_service_destroy(cur_svc);
		scf_scope_destroy(cur_scope);
		scf_handle_destroy(g_hndl);
		cur_inst = NULL;
		cur_svc = NULL;
		cur_scope = NULL;
		cur_selection_is_masked = B_FALSE;
		g_hndl = NULL;
		lscf_cleanup();
	}
}

int
lscf_set_repository(const char *repfile, int force, char *prefix)
{
	struct stat st;
	repository_teardown();

	if (est->sc_repo_filename != NULL) {
		free((void *)est->sc_repo_filename);
		est->sc_repo_filename = NULL;
	}

	if (prefix) {
		if (lstat(prefix, &st) != 0 || (st.st_mode & S_IFDIR) == 0 ||
		    access(prefix, R_OK|X_OK) != 0) {
			semerr(gettext("prefix \"%s\" is inaccessable or "
			    "is a file: %s\n"), prefix, strerror(errno));

			return (0);
		}

		est->sc_bundle_prefix = safe_strdup(prefix);
	} else {
		est->sc_bundle_prefix = NULL;
	}

	if ((force == 0) && (access(repfile, R_OK) != 0)) {
		/*
		 * Repository file does not exist
		 * or has no read permission.
		 */
		semerr(gettext("Cannot access \"%s\": %s\n"),
		    repfile, strerror(errno));
	} else {
		est->sc_repo_filename = safe_strdup(repfile);
	}

	return (0);
}

void
lscf_init()
{
	if ((max_scf_fmri_len = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH)) < 0 ||
	    (max_scf_name_len = scf_limit(SCF_LIMIT_MAX_NAME_LENGTH)) < 0 ||
	    (max_scf_pg_type_len = scf_limit(SCF_LIMIT_MAX_PG_TYPE_LENGTH)) <
	    0 ||
	    (max_scf_value_len = scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH)) < 0)
		scfdie();


	max_scf_selection_str_len = 1 + max_scf_name_len + 1 +
	    max_scf_fmri_len + 2 + max_scf_name_len + 1;

	max_scf_len = max_scf_fmri_len;
	if (max_scf_name_len > max_scf_len)
		max_scf_len = max_scf_name_len;
	if (max_scf_pg_type_len > max_scf_len)
		max_scf_len = max_scf_pg_type_len;
	/*
	 * When a value of type opaque is represented as a string, the
	 * string contains 2 characters for every byte of data.  That is
	 * because the string contains the hex representation of the opaque
	 * value.
	 */
	if (2 * max_scf_value_len > max_scf_len)
		max_scf_len = 2 * max_scf_value_len;

	if (atexit(remove_tempfile) != 0)
		uu_die(gettext("Could not register atexit() function"));

	emsg_entity_not_selected = gettext("An entity is not selected.\n");
	emsg_permission_denied = gettext("Permission denied.\n");
	emsg_create_xml = gettext("Could not create XML node.\n");
	emsg_cant_modify_snapshots = gettext("Cannot modify snapshots.\n");
	emsg_invalid_for_snapshot =
	    gettext("Invalid operation on a snapshot.\n");
	emsg_read_only = gettext("Backend read-only.\n");
	emsg_deleted = gettext("Current selection has been deleted.\n");
	emsg_invalid_pg_name =
	    gettext("Invalid property group name \"%s\".\n");
	emsg_invalid_prop_name = gettext("Invalid property name \"%s\".\n");
	emsg_no_such_pg = gettext("No such property group \"%s\".\n");
	emsg_fmri_invalid_pg_name = gettext("Service %s has property group "
	    "with invalid name \"%s\".\n");
	emsg_fmri_invalid_pg_name_type = gettext("Service %s has property "
	    "group with invalid name \"%s\" or type \"%s\".\n");
	emsg_pg_deleted = gettext("%s changed unexpectedly "
	    "(property group \"%s\" or an ancestor was deleted).\n");
	emsg_pg_mod_perm = gettext("Could not modify property group \"%s\" "
	    "in %s (permission denied).\n");
	emsg_pg_add_perm = gettext("Could not create property group \"%s\" "
	    "in %s (permission denied).\n");
	emsg_pg_del_perm = gettext("Could not delete property group \"%s\" "
	    "in %s (permission denied).\n");
	emsg_snap_perm = gettext("Could not take \"%s\" snapshot of %s "
	    "(permission denied).\n");
	emsg_masked_selection = gettext("This subcommand is not supported "
	    "when the current selection is masked.\n");

	msg_delcust_process = gettext("Processing: %s\n");
	msg_delcust_svc = gettext(" Deleting customizations for service: %s\n");
	msg_delcust_inst = gettext(" Deleting customizations for instance: "
	    "%s\n");
	msg_delcust_pg = gettext(" Deleting customizations for pg: %s\n");
	msg_delcust_prop = gettext(" Deleting customizations for property: "
	    "%s/%s\n");
	emsg_delcust_svc = gettext(" Operation not supported: %s\n");

	string_pool = uu_list_pool_create("strings", sizeof (string_list_t),
	    offsetof(string_list_t, node), NULL, 0);
	snaplevel_pool = uu_list_pool_create("snaplevels",
	    sizeof (struct snaplevel), offsetof(struct snaplevel, list_node),
	    NULL, 0);
}

static const char *
prop_to_typestr(const scf_property_t *prop)
{
	scf_type_t ty;

	if (scf_property_type(prop, &ty) != SCF_SUCCESS)
		scfdie();

	return (scf_type_to_string(ty));
}

static const char *
dec_to_typestr(const scf_decoration_t *dec)
{
	scf_type_t ty;

	if (scf_decoration_type(dec, &ty) != SCF_SUCCESS)
		scfdie();

	return (scf_type_to_string(ty));
}

static scf_type_t
string_to_type(const char *type)
{
	size_t len = strlen(type);
	char *buf;

	if (len == 0 || type[len - 1] != ':')
		return (SCF_TYPE_INVALID);

	buf = (char *)alloca(len + 1);
	(void) strlcpy(buf, type, len + 1);
	buf[len - 1] = 0;

	return (scf_string_to_type(buf));
}

static scf_value_t *
string_to_value(const char *str, scf_type_t ty, boolean_t require_quotes)
{
	scf_value_t *v;
	char *dup, *nstr;
	size_t len;

	v = scf_value_create(g_hndl);
	if (v == NULL)
		scfdie();

	len = strlen(str);
	if (require_quotes &&
	    (len < 2 || str[0] != '\"' || str[len - 1] != '\"')) {
		semerr(gettext("Multiple string values or string values "
		    "with spaces must be quoted with '\"'.\n"));
		scf_value_destroy(v);
		return (NULL);
	}

	nstr = dup = safe_strdup(str);
	if (dup[0] == '\"') {
		/*
		 * Strip out the first and the last quote.
		 */
		dup[len - 1] = '\0';
		nstr = dup + 1;
	}

	if (scf_value_set_from_string(v, ty, (const char *)nstr) != 0) {
		assert(scf_error() == SCF_ERROR_INVALID_ARGUMENT);
		semerr(gettext("Invalid \"%s\" value \"%s\".\n"),
		    scf_type_to_string(ty), nstr);
		scf_value_destroy(v);
		v = NULL;
	}
	free(dup);
	return (v);
}

/*
 * Print str to strm, quoting double-quotes and backslashes with backslashes.
 * Optionally append a comment prefix ('#') to newlines ('\n').
 */
static int
quote_and_print(const char *str, FILE *strm, int commentnl)
{
	const char *cp;

	for (cp = str; *cp != '\0'; ++cp) {
		if (*cp == '"' || *cp == '\\')
			(void) putc('\\', strm);

		(void) putc(*cp, strm);

		if (commentnl && *cp == '\n') {
			(void) putc('#', strm);
		}
	}

	return (ferror(strm));
}

/*
 * These wrappers around lowlevel functions provide consistent error checking
 * and warnings.
 */
static int
prop_get_dec(scf_property_t *prop, scf_decoration_t *dec)
{
	scf_decoration_layer_t lval;
	int r;

	/*
	 * We don't process non-persistent property groups so
	 * failing to get decoration is a real failure.
	 */
	if (scf_iter_property_decorations(exp_dec_iter, prop, 0))
		scfdie();

	/*
	 * Since decorations are ordered in the repcache, we iterate
	 * (high to low) and return the first decoration that was requested.
	 *
	 * If 'current' layer is requested, simply return the first decoration.
	 */
	while ((r = scf_iter_next_decoration(exp_dec_iter, dec)) == 1) {
		if (scf_decoration_get_layer(dec, &lval))
			scfdie();

		if (exp_dec_layer_map == SVCCFG_CURRENT ||
		    (exp_dec_layer_map & scf_layer_to_flag(lval)))
			return (0);
	}
	if (r == -1)
		scfdie();

	return (-1);
}

static int
dec_check_type(scf_decoration_t *dec, scf_type_t ty)
{
	scf_type_t pty;

	if (scf_decoration_type(dec, &pty) != SCF_SUCCESS)
		scfdie();

	if (ty == pty)
		return (0);

	return (-1);
}

static int
dec_get_val(scf_decoration_t *dec, scf_value_t *val)
{
	scf_error_t err;

	if (scf_decoration_get_value(dec, val) == SCF_SUCCESS)
		return (0);

	err = scf_error();

	if (err != SCF_ERROR_NOT_FOUND &&
	    err != SCF_ERROR_CONSTRAINT_VIOLATED &&
	    err != SCF_ERROR_PERMISSION_DENIED)
		scfdie();

	return (-1);
}

/*
 * These wrappers around lowlevel functions provide consistent error checking
 * and warnings.
 */
static int
pg_get_prop(scf_propertygroup_t *pg, const char *propname, scf_property_t *prop)
{
	if (scf_pg_get_property(pg, propname, prop) == SCF_SUCCESS)
		return (0);

	if (scf_error() != SCF_ERROR_NOT_FOUND)
		scfdie();

	if (g_verbose) {
		ssize_t len;
		char *fmri;

		len = scf_pg_to_fmri(pg, NULL, 0);
		if (len < 0)
			scfdie();

		fmri = safe_malloc(len + 1);

		if (scf_pg_to_fmri(pg, fmri, len + 1) < 0)
			scfdie();

		warn(gettext("Expected property %s of property group %s is "
		    "missing.\n"), propname, fmri);

		free(fmri);
	}

	return (-1);
}

static int
prop_check_type(scf_property_t *prop, scf_type_t ty)
{
	scf_type_t pty;

	if (scf_property_type(prop, &pty) != SCF_SUCCESS)
		scfdie();

	if (ty == pty)
		return (0);

	if (g_verbose) {
		ssize_t len;
		char *fmri;
		const char *tystr;

		len = scf_property_to_fmri(prop, NULL, 0);
		if (len < 0)
			scfdie();

		fmri = safe_malloc(len + 1);

		if (scf_property_to_fmri(prop, fmri, len + 1) < 0)
			scfdie();

		tystr = scf_type_to_string(ty);
		if (tystr == NULL)
			tystr = "?";

		warn(gettext("Property %s is not of expected type %s.\n"),
		    fmri, tystr);

		free(fmri);
	}

	return (-1);
}

static int
prop_get_val(scf_property_t *prop, scf_value_t *val)
{
	scf_error_t err;

	if (scf_property_get_value(prop, val) == SCF_SUCCESS)
		return (0);

	err = scf_error();

	if (err != SCF_ERROR_NOT_FOUND &&
	    err != SCF_ERROR_CONSTRAINT_VIOLATED &&
	    err != SCF_ERROR_PERMISSION_DENIED)
		scfdie();

	if (g_verbose) {
		ssize_t len;
		char *fmri, *emsg;

		len = scf_property_to_fmri(prop, NULL, 0);
		if (len < 0)
			scfdie();

		fmri = safe_malloc(len + 1);

		if (scf_property_to_fmri(prop, fmri, len + 1) < 0)
			scfdie();

		if (err == SCF_ERROR_NOT_FOUND)
			emsg = gettext("Property %s has no values; expected "
			    "one.\n");
		else if (err == SCF_ERROR_CONSTRAINT_VIOLATED)
			emsg = gettext("Property %s has multiple values; "
			    "expected one.\n");
		else
			emsg = gettext("No permission to read property %s.\n");

		warn(emsg, fmri);

		free(fmri);
	}

	return (-1);
}


static boolean_t
snaplevel_is_instance(const scf_snaplevel_t *level)
{
	if (scf_snaplevel_get_instance_name(level, NULL, 0) < 0) {
		if (scf_error() != SCF_ERROR_CONSTRAINT_VIOLATED)
			scfdie();
		return (0);
	} else {
		return (1);
	}
}

/*
 * Decode FMRI into a service or instance, and put the result in *ep.  If
 * memory cannot be allocated, return SCF_ERROR_NO_MEMORY.  If the FMRI is
 * invalid, return SCF_ERROR_INVALID_ARGUMENT.  If the FMRI does not specify
 * an entity, return SCF_ERROR_CONSTRAINT_VIOLATED.  If the entity cannot be
 * found, return SCF_ERROR_NOT_FOUND.  Otherwise return SCF_ERROR_NONE, point
 * *ep to a valid scf_service_t or scf_instance_t, and set *isservice to
 * whether *ep is a service.
 */
static scf_error_t
fmri_to_entity(scf_handle_t *h, const char *fmri, void **ep, int *isservice)
{
	char *fmri_copy;
	const char *sstr, *istr, *pgstr;
	scf_service_t *svc;
	scf_instance_t *inst;

	fmri_copy = strdup(fmri);
	if (fmri_copy == NULL)
		return (SCF_ERROR_NO_MEMORY);

	if (scf_parse_svc_fmri(fmri_copy, NULL, &sstr, &istr, &pgstr, NULL) !=
	    SCF_SUCCESS) {
		free(fmri_copy);
		return (SCF_ERROR_INVALID_ARGUMENT);
	}

	free(fmri_copy);

	if (sstr == NULL || pgstr != NULL)
		return (SCF_ERROR_CONSTRAINT_VIOLATED);

	if (istr == NULL) {
		svc = scf_service_create(h);
		if (svc == NULL)
			return (SCF_ERROR_NO_MEMORY);

		if (scf_handle_decode_fmri(h, fmri, NULL, svc, NULL, NULL, NULL,
		    SCF_DECODE_FMRI_EXACT) != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();

			return (SCF_ERROR_NOT_FOUND);
		}

		*ep = svc;
		*isservice = 1;
	} else {
		inst = scf_instance_create(h);
		if (inst == NULL)
			return (SCF_ERROR_NO_MEMORY);

		if (scf_handle_decode_fmri(h, fmri, NULL, NULL, inst, NULL,
		    NULL, SCF_DECODE_FMRI_EXACT) != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();

			return (SCF_ERROR_NOT_FOUND);
		}

		*ep = inst;
		*isservice = 0;
	}

	return (SCF_ERROR_NONE);
}

/*
 * Create the entity named by fmri.  Place a pointer to its libscf handle in
 * *ep, and set or clear *isservicep if it is a service or an instance.
 * Returns
 *   SCF_ERROR_NONE - success
 *   SCF_ERROR_NO_MEMORY - scf_*_create() failed
 *   SCF_ERROR_INVALID_ARGUMENT - fmri is invalid
 *   SCF_ERROR_CONSTRAINT_VIOLATED - fmri is not a service or instance
 *   SCF_ERROR_NOT_FOUND - no such scope
 *   SCF_ERROR_PERMISSION_DENIED
 *   SCF_ERROR_BACKEND_READONLY
 *   SCF_ERROR_BACKEND_ACCESS
 */
static scf_error_t
create_entity(scf_handle_t *h, const char *fmri, void **ep, int *isservicep)
{
	char *fmri_copy;
	const char *scstr, *sstr, *istr, *pgstr;
	scf_scope_t *scope = NULL;
	scf_service_t *svc = NULL;
	scf_instance_t *inst = NULL;
	scf_error_t scfe;

	fmri_copy = safe_strdup(fmri);

	if (scf_parse_svc_fmri(fmri_copy, &scstr, &sstr, &istr, &pgstr, NULL) !=
	    0) {
		free(fmri_copy);
		return (SCF_ERROR_INVALID_ARGUMENT);
	}

	if (scstr == NULL || sstr == NULL || pgstr != NULL) {
		free(fmri_copy);
		return (SCF_ERROR_CONSTRAINT_VIOLATED);
	}

	*ep = NULL;

	if ((scope = scf_scope_create(h)) == NULL ||
	    (svc = scf_service_create(h)) == NULL ||
	    (inst = scf_instance_create(h)) == NULL) {
		scfe = SCF_ERROR_NO_MEMORY;
		goto out;
	}

get_scope:
	if (scf_handle_get_scope(h, scstr, scope) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			scfdie();
			/* NOTREACHED */

		case SCF_ERROR_NOT_FOUND:
			scfe = SCF_ERROR_NOT_FOUND;
			goto out;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_INVALID_ARGUMENT:
		default:
			bad_error("scf_handle_get_scope", scf_error());
		}
	}

get_svc:
	if (scf_scope_get_service(scope, sstr, svc) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			scfdie();
			/* NOTREACHED */

		case SCF_ERROR_DELETED:
			goto get_scope;

		case SCF_ERROR_NOT_FOUND:
			break;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_scope_get_service", scf_error());
		}

		if (scf_scope_add_service(scope, sstr, svc) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
				scfdie();
				/* NOTREACHED */

			case SCF_ERROR_DELETED:
				goto get_scope;

			case SCF_ERROR_PERMISSION_DENIED:
			case SCF_ERROR_BACKEND_READONLY:
			case SCF_ERROR_BACKEND_ACCESS:
				scfe = scf_error();
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_scope_get_service", scf_error());
			}
		}
	}

	if (istr == NULL) {
		scfe = SCF_ERROR_NONE;
		*ep = svc;
		*isservicep = 1;
		goto out;
	}

get_inst:
	if (scf_service_get_instance(svc, istr, inst) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			scfdie();
			/* NOTREACHED */

		case SCF_ERROR_DELETED:
			goto get_svc;

		case SCF_ERROR_NOT_FOUND:
			break;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_service_get_instance", scf_error());
		}

		if (scf_service_add_instance(svc, istr, inst) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
				scfdie();
				/* NOTREACHED */

			case SCF_ERROR_DELETED:
				goto get_svc;

			case SCF_ERROR_PERMISSION_DENIED:
			case SCF_ERROR_BACKEND_READONLY:
			case SCF_ERROR_BACKEND_ACCESS:
				scfe = scf_error();
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_service_add_instance",
				    scf_error());
			}
		}
	}

	scfe = SCF_ERROR_NONE;
	*ep = inst;
	*isservicep = 0;

out:
	if (*ep != inst)
		scf_instance_destroy(inst);
	if (*ep != svc)
		scf_service_destroy(svc);
	scf_scope_destroy(scope);
	free(fmri_copy);
	return (scfe);
}

/*
 * Create or update a snapshot of inst.  snap is a required scratch object.
 *
 * Returns
 *   0 - success
 *   ECONNABORTED - repository connection broken
 *   EPERM - permission denied
 *   ENOSPC - configd is out of resources
 *   ECANCELED - inst was deleted
 *   -1 - unknown libscf error (message printed)
 */
static int
take_snap(scf_instance_t *inst, const char *name, scf_snapshot_t *snap)
{
again:
	if (scf_instance_get_snapshot(inst, name, snap) == 0) {
		if (_scf_snapshot_take_attach(inst, snap) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
			case SCF_ERROR_PERMISSION_DENIED:
			case SCF_ERROR_NO_RESOURCES:
				return (scferror2errno(scf_error()));

			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_INVALID_ARGUMENT:
			default:
				bad_error("_scf_snapshot_take_attach",
				    scf_error());
			}
		}
	} else {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			break;

		case SCF_ERROR_DELETED:
		case SCF_ERROR_CONNECTION_BROKEN:
			return (scferror2errno(scf_error()));

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_instance_get_snapshot", scf_error());
		}

		if (_scf_snapshot_take_new(inst, name, snap) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_EXISTS:
				goto again;

			case SCF_ERROR_CONNECTION_BROKEN:
			case SCF_ERROR_NO_RESOURCES:
			case SCF_ERROR_PERMISSION_DENIED:
				return (scferror2errno(scf_error()));

			default:
				scfwarn();
				return (-1);

			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_INTERNAL:
			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_HANDLE_MISMATCH:
				bad_error("_scf_snapshot_take_new",
				    scf_error());
			}
		}
	}

	return (0);
}

static int
refresh_running_snapshot(void *entity)
{
	scf_snapshot_t *snap;
	int r;

	if ((snap = scf_snapshot_create(g_hndl)) == NULL)
		scfdie();
	r = take_snap(entity, snap_running, snap);
	scf_snapshot_destroy(snap);

	return (r);
}

/*
 * Refresh entity.  If isservice is zero, take entity to be an scf_instance_t *.
 * Otherwise take entity to be an scf_service_t * and refresh all of its child
 * instances.  fmri is used for messages.  inst, iter, and name_buf are used
 * for scratch space.  Returns
 *   0 - success
 *   ECONNABORTED - repository connection broken
 *   ECANCELED - entity was deleted
 *   EACCES - backend denied access
 *   EPERM - permission denied
 *   ENOSPC - repository server out of resources
 *   -1 - _smf_refresh_instance_i() failed.  scf_error() should be set.
 */
static int
refresh_entity(int isservice, void *entity, const char *fmri,
    scf_instance_t *inst, scf_iter_t *iter, char *name_buf)
{
	scf_error_t scfe;
	int r;

	if (!isservice) {
		/*
		 * Let restarter handles refreshing and making new running
		 * snapshot only if operating on a live repository and not
		 * running in early import.
		 */
		if (est->sc_repo_filename == NULL &&
		    est->sc_repo_doorname == NULL &&
		    est->sc_in_emi == 0) {
			if (_smf_refresh_instance_i(entity) == 0) {
				if (g_verbose)
					warn(gettext("Refreshed %s.\n"), fmri);
				return (0);
			}

			switch (scf_error()) {
			case SCF_ERROR_BACKEND_ACCESS:
				return (EACCES);

			case SCF_ERROR_PERMISSION_DENIED:
				return (EPERM);

			default:
				return (-1);
			}
		} else {
			r = refresh_running_snapshot(entity);
			switch (r) {
			case 0:
				break;

			case ECONNABORTED:
			case ECANCELED:
			case EPERM:
			case ENOSPC:
				break;

			default:
				bad_error("refresh_running_snapshot",
				    scf_error());
			}

			return (r);
		}
	}

	if (scf_iter_service_instances(iter, entity) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			return (ECONNABORTED);

		case SCF_ERROR_DELETED:
			return (ECANCELED);

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_iter_service_instances", scf_error());
		}
	}

	for (;;) {
		r = scf_iter_next_instance(iter, inst);
		if (r == 0)
			break;
		if (r != 1) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
				return (ECONNABORTED);

			case SCF_ERROR_DELETED:
				return (ECANCELED);

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_INVALID_ARGUMENT:
			default:
				bad_error("scf_iter_next_instance",
				    scf_error());
			}
		}

		/*
		 * Similarly, just take a new running snapshot if operating on
		 * a non-live repository or running during early import.
		 */
		if (est->sc_repo_filename != NULL ||
		    est->sc_repo_doorname != NULL ||
		    est->sc_in_emi == 1) {
			r = refresh_running_snapshot(inst);
			switch (r) {
			case 0:
				continue;

			case ECONNABORTED:
			case ECANCELED:
			case EPERM:
			case ENOSPC:
				break;
			default:
				bad_error("refresh_running_snapshot",
				    scf_error());
			}

			return (r);

		}

		if (_smf_refresh_instance_i(inst) == 0) {
			if (g_verbose) {
				if (scf_instance_get_name(inst, name_buf,
				    max_scf_name_len + 1) < 0)
					(void) strcpy(name_buf, "?");

				warn(gettext("Refreshed %s:%s.\n"),
				    fmri, name_buf);
			}
		} else {
			if (scf_error() != SCF_ERROR_BACKEND_ACCESS ||
			    g_verbose) {
				scfe = scf_error();

				if (scf_instance_to_fmri(inst, name_buf,
				    max_scf_name_len + 1) < 0)
					(void) strcpy(name_buf, "?");

				warn(gettext(
				    "Refresh of %s:%s failed: %s.\n"), fmri,
				    name_buf, scf_strerror(scfe));
			}
		}
	}

	return (0);
}

static void
private_refresh(void)
{
	scf_instance_t *pinst = NULL;
	scf_iter_t *piter = NULL;
	ssize_t fmrilen;
	size_t bufsz;
	char *fmribuf;
	void *ent;
	int issvc;
	int r;

	if (est->sc_repo_filename == NULL && est->sc_repo_doorname == NULL)
		return;

	assert(cur_svc != NULL);

	bufsz = max_scf_fmri_len + 1;
	fmribuf = safe_malloc(bufsz);
	if (cur_inst) {
		issvc = 0;
		ent = cur_inst;
		fmrilen = scf_instance_to_fmri(ent, fmribuf, bufsz);
	} else {
		issvc = 1;
		ent = cur_svc;
		fmrilen = scf_service_to_fmri(ent, fmribuf, bufsz);
		if ((pinst = scf_instance_create(g_hndl)) == NULL)
			scfdie();

		if ((piter = scf_iter_create(g_hndl)) == NULL)
			scfdie();
	}
	if (fmrilen < 0) {
		free(fmribuf);
		if (scf_error() != SCF_ERROR_DELETED)
			scfdie();

		warn(emsg_deleted);
		return;
	}
	assert(fmrilen < bufsz);

	r = refresh_entity(issvc, ent, fmribuf, pinst, piter, NULL);
	switch (r) {
	case 0:
		break;

	case ECONNABORTED:
		warn(gettext("Could not refresh %s "
		    "(repository connection broken).\n"), fmribuf);
		break;

	case ECANCELED:
		warn(emsg_deleted);
		break;

	case EPERM:
		warn(gettext("Could not refresh %s "
		    "(permission denied).\n"), fmribuf);
		break;

	case ENOSPC:
		warn(gettext("Could not refresh %s "
		    "(repository server out of resources).\n"),
		    fmribuf);
		break;

	case EACCES:
	default:
		bad_error("refresh_entity", scf_error());
	}

	if (issvc) {
		scf_instance_destroy(pinst);
		scf_iter_destroy(piter);
	}

	free(fmribuf);
}


static int
stash_scferror_err(scf_callback_t *cbp, scf_error_t err)
{
	cbp->sc_err = scferror2errno(err);
	return (UU_WALK_ERROR);
}

static int
stash_scferror(scf_callback_t *cbp)
{
	return (stash_scferror_err(cbp, scf_error()));
}

static int select_inst(const char *);
static int select_svc(const char *);

/*
 * Take a property that does not have a type and check to see if a type
 * exists or can be gleened from the current data.  Set the type.
 *
 * Check the current level (instance) and then check the higher level
 * (service).  This could be the case for adding a new property to
 * the instance that's going to "override" a service level property.
 *
 * For a property :
 * 1. Take the type from an existing property
 * 2. Take the type from a template entry
 *
 * If the type can not be found, then leave the type as is, and let the import
 * report the problem of the missing type.
 */
static int
find_current_prop_type(void *p, void *g)
{
	property_t *prop = p;
	scf_callback_t *lcb = g;
	pgroup_t *pg = NULL;

	const char *fmri = NULL;
	char *lfmri = NULL;
	char *cur_selection = NULL;

	scf_propertygroup_t *sc_pg = NULL;
	scf_property_t *sc_prop = NULL;
	scf_pg_tmpl_t *t_pg = NULL;
	scf_prop_tmpl_t *t_prop = NULL;
	scf_type_t prop_type;

	value_t *vp;
	int issvc = lcb->sc_service;
	int r = UU_WALK_ERROR;

	if (prop->sc_value_type != SCF_TYPE_INVALID)
		return (UU_WALK_NEXT);

	t_prop = scf_tmpl_prop_create(g_hndl);
	sc_prop = scf_property_create(g_hndl);
	if (sc_prop == NULL || t_prop == NULL) {
		warn(gettext("Unable to create the property to attempt and "
		    "find a missing type.\n"));

		scf_property_destroy(sc_prop);
		scf_tmpl_prop_destroy(t_prop);

		return (UU_WALK_ERROR);
	}

	if (lcb->sc_flags == 1) {
		pg = lcb->sc_parent;
		issvc = (pg->sc_parent->sc_etype == SVCCFG_SERVICE_OBJECT);
		fmri = pg->sc_parent->sc_fmri;
retry_pg:
		if (cur_svc && cur_selection == NULL) {
			cur_selection =
			    safe_malloc(max_scf_selection_str_len + 1);
			lscf_get_selection_str(cur_selection,
			    max_scf_selection_str_len + 1);

			if (strcmp(cur_selection, fmri) != 0) {
				lscf_select(fmri);
			} else {
				free(cur_selection);
				cur_selection = NULL;
			}
		} else {
			lscf_select(fmri);
		}

		if (sc_pg == NULL && (sc_pg = scf_pg_create(g_hndl)) == NULL) {
			warn(gettext("Unable to create property group to "
			    "find a missing property type.\n"));

			goto out;
		}

		if (get_pg(pg->sc_pgroup_name, sc_pg) != SCF_SUCCESS) {
			/*
			 * If this is the sc_pg from the parent
			 * let the caller clean up the sc_pg,
			 * and just throw it away in this case.
			 */
			if (sc_pg != lcb->sc_parent)
				scf_pg_destroy(sc_pg);

			sc_pg = NULL;
			if ((t_pg = scf_tmpl_pg_create(g_hndl)) == NULL) {
				warn(gettext("Unable to create template "
				    "property group to find a property "
				    "type.\n"));

				goto out;
			}

			if (scf_tmpl_get_by_pg_name(fmri, NULL,
			    pg->sc_pgroup_name, NULL, t_pg,
			    SCF_PG_TMPL_FLAG_EXACT) != SCF_SUCCESS) {
				/*
				 * if instance get service and jump back
				 */
				scf_tmpl_pg_destroy(t_pg);
				t_pg = NULL;
				if (issvc == 0) {
					entity_t *e = pg->sc_parent->sc_parent;

					fmri = e->sc_fmri;
					issvc = 1;
					goto retry_pg;
				} else {
					goto out;
				}
			}
		}
	} else {
		sc_pg = lcb->sc_parent;
	}

	/*
	 * Attempt to get the type from an existing property.  If the property
	 * cannot be found then attempt to get the type from a template entry
	 * for the property.
	 *
	 * Finally, if at the instance level look at the service level.
	 */
	if (sc_pg != NULL &&
	    pg_get_prop(sc_pg, prop->sc_property_name,
	    sc_prop) == SCF_SUCCESS &&
	    scf_property_type(sc_prop, &prop_type) == SCF_SUCCESS) {
		prop->sc_value_type = prop_type;

		/*
		 * Found a type, update the value types and validate
		 * the actual value against this type.
		 */
		for (vp = uu_list_first(prop->sc_property_values);
		    vp != NULL;
		    vp = uu_list_next(prop->sc_property_values, vp)) {
			vp->sc_type = prop->sc_value_type;
			lxml_store_value(vp, 0, NULL);
		}

		r = UU_WALK_NEXT;
		goto out;
	}

	/*
	 * If we get here with t_pg set to NULL then we had to have
	 * gotten an sc_pg but that sc_pg did not have the property
	 * we are looking for.   So if the t_pg is not null look up
	 * the template entry for the property.
	 *
	 * If the t_pg is null then need to attempt to get a matching
	 * template entry for the sc_pg, and see if there is a property
	 * entry for that template entry.
	 */
do_tmpl :
	if (t_pg != NULL &&
	    scf_tmpl_get_by_prop(t_pg, prop->sc_property_name,
	    t_prop, 0) == SCF_SUCCESS) {
		if (scf_tmpl_prop_type(t_prop, &prop_type) == SCF_SUCCESS) {
			prop->sc_value_type = prop_type;

			/*
			 * Found a type, update the value types and validate
			 * the actual value against this type.
			 */
			for (vp = uu_list_first(prop->sc_property_values);
			    vp != NULL;
			    vp = uu_list_next(prop->sc_property_values, vp)) {
				vp->sc_type = prop->sc_value_type;
				lxml_store_value(vp, 0, NULL);
			}

			r = UU_WALK_NEXT;
			goto out;
		}
	} else {
		if (t_pg == NULL && sc_pg) {
			if ((t_pg = scf_tmpl_pg_create(g_hndl)) == NULL) {
				warn(gettext("Unable to create template "
				    "property group to find a property "
				    "type.\n"));

				goto out;
			}

			if (scf_tmpl_get_by_pg(sc_pg, t_pg, 0) != SCF_SUCCESS) {
				scf_tmpl_pg_destroy(t_pg);
				t_pg = NULL;
			} else {
				goto do_tmpl;
			}
		}
	}

	if (issvc == 0) {
		scf_instance_t *i;
		scf_service_t *s;

		issvc = 1;
		if (lcb->sc_flags == 1) {
			entity_t *e = pg->sc_parent->sc_parent;

			fmri = e->sc_fmri;
			goto retry_pg;
		}

		/*
		 * because lcb->sc_flags was not set then this means
		 * the pg was not used and can be used here.
		 */
		if ((pg = internal_pgroup_new()) == NULL) {
			warn(gettext("Could not create internal property group "
			    "to find a missing type."));

			goto out;
		}

		pg->sc_pgroup_name = safe_malloc(max_scf_name_len + 1);
		if (scf_pg_get_name(sc_pg, (char *)pg->sc_pgroup_name,
		    max_scf_name_len + 1) < 0)
				goto out;

		i = scf_instance_create(g_hndl);
		s = scf_service_create(g_hndl);
		if (i == NULL || s == NULL ||
		    scf_pg_get_parent_instance(sc_pg, i) != SCF_SUCCESS) {
			warn(gettext("Could not get a service for the instance "
			    "to find a missing type."));

			goto out;
		}

		/*
		 * Check to see truly at the instance level.
		 */
		lfmri = safe_malloc(max_scf_fmri_len + 1);
		if (scf_instance_get_parent(i, s) == SCF_SUCCESS &&
		    scf_service_to_fmri(s, lfmri, max_scf_fmri_len + 1) < 0)
			goto out;
		else
			fmri = (const char *)lfmri;

		goto retry_pg;
	}

out :
	if (sc_pg != lcb->sc_parent) {
		scf_pg_destroy(sc_pg);
	}

	/*
	 * If this is true then the pg was allocated
	 * here, and the name was set so need to free
	 * the name and the pg.
	 */
	if (pg != NULL && pg != lcb->sc_parent) {
		free((char *)pg->sc_pgroup_name);
		internal_pgroup_free(pg);
	}

	if (cur_selection) {
		lscf_select(cur_selection);
		free(cur_selection);
	}

	scf_tmpl_pg_destroy(t_pg);
	scf_tmpl_prop_destroy(t_prop);
	scf_property_destroy(sc_prop);

	if (r != UU_WALK_NEXT)
		warn(gettext("Could not find property type for \"%s\" "
		    "from \"%s\"\n"), prop->sc_property_name,
		    fmri != NULL ? fmri : lcb->sc_source_fmri);

	free(lfmri);

	return (r);
}

/*
 * Take a property group that does not have a type and check to see if a type
 * exists or can be gleened from the current data.  Set the type.
 *
 * Check the current level (instance) and then check the higher level
 * (service).  This could be the case for adding a new property to
 * the instance that's going to "override" a service level property.
 *
 * For a property group
 * 1. Take the type from an existing property group
 * 2. Take the type from a template entry
 *
 * If the type can not be found, then leave the type as is, and let the import
 * report the problem of the missing type.
 */
static int
find_current_pg_type(void *p, void *sori)
{
	entity_t *si = sori;
	pgroup_t *pg = p;

	const char *ofmri, *fmri;
	char *cur_selection = NULL;
	char *pg_type = NULL;

	scf_propertygroup_t *sc_pg = NULL;
	scf_pg_tmpl_t *t_pg = NULL;

	int issvc = (si->sc_etype == SVCCFG_SERVICE_OBJECT);
	int r = UU_WALK_ERROR;
	int use_sc_pg = 0;

	ofmri = fmri = si->sc_fmri;
	if (pg->sc_pgroup_type != NULL) {
		r = UU_WALK_NEXT;

		goto out;
	}

	sc_pg = scf_pg_create(g_hndl);
	if (sc_pg == NULL) {
		warn(gettext("Unable to create property group to attempt "
		    "and find a missing type.\n"));

		return (UU_WALK_ERROR);
	}

	/*
	 * Using get_pg() requires that the cur_svc/cur_inst be
	 * via lscf_select.  Need to preserve the current selection
	 * if going to use lscf_select() to set up the cur_svc/cur_inst
	 */
	if (cur_svc) {
		cur_selection = safe_malloc(max_scf_selection_str_len + 1);
		lscf_get_selection_str(cur_selection,
		    max_scf_selection_str_len + 1);
	}

	/*
	 * If the property group exists get the type, and set
	 * the pgroup_t type of that type.
	 *
	 * If not the check for a template pg_pattern entry
	 * and take the type from that.
	 */
retry_svc:
	lscf_select(fmri);

	if (get_pg(pg->sc_pgroup_name, sc_pg) == SCF_SUCCESS) {
		pg_type = safe_malloc(max_scf_pg_type_len + 1);
		if (pg_type != NULL && scf_pg_get_type(sc_pg, pg_type,
		    max_scf_pg_type_len + 1) != -1) {
			pg->sc_pgroup_type = pg_type;

			r = UU_WALK_NEXT;
			use_sc_pg = 1;
			goto out;
		} else {
			free(pg_type);
		}
	} else {
		if ((t_pg == NULL) &&
		    (t_pg = scf_tmpl_pg_create(g_hndl)) == NULL)
			goto out;

		if (scf_tmpl_get_by_pg_name(fmri, NULL, pg->sc_pgroup_name,
		    NULL, t_pg, SCF_PG_TMPL_FLAG_EXACT) == SCF_SUCCESS &&
		    scf_tmpl_pg_type(t_pg, &pg_type) != -1) {
			pg->sc_pgroup_type = pg_type;

			r = UU_WALK_NEXT;
			goto out;
		}
	}

	/*
	 * If type is not found at the instance level then attempt to
	 * find the type at the service level.
	 */
	if (!issvc) {
		si = si->sc_parent;
		fmri = si->sc_fmri;
		issvc = (si->sc_etype == SVCCFG_SERVICE_OBJECT);
		goto retry_svc;
	}

out :
	if (cur_selection) {
		lscf_select(cur_selection);
		free(cur_selection);
	}

	/*
	 * Now walk the properties of the property group to make sure that
	 * all properties have the correct type and values are valid for
	 * those types.
	 */
	if (r == UU_WALK_NEXT) {
		scf_callback_t cb;

		cb.sc_service = issvc;
		cb.sc_source_fmri = ofmri;

		/* Set sc_parent to the sc_pg if it's found earlier */
		if (use_sc_pg == 1) {
			cb.sc_parent = sc_pg;
			cb.sc_flags = 0;
		} else {
			cb.sc_parent = pg;
			cb.sc_flags = 1;
		}

		if (uu_list_walk(pg->sc_pgroup_props, find_current_prop_type,
		    &cb, UU_DEFAULT) != 0) {
			if (uu_error() != UU_ERROR_CALLBACK_FAILED)
				bad_error("uu_list_walk", uu_error());

			r = UU_WALK_ERROR;
		}
	} else {
		warn(gettext("Could not find property group type for "
		    "\"%s\" from \"%s\"\n"), pg->sc_pgroup_name, fmri);
	}

	scf_tmpl_pg_destroy(t_pg);
	scf_pg_destroy(sc_pg);

	return (r);
}

/*
 * Import.  These functions import a bundle into the repository.
 */

/*
 * Add a transaction entry to lcbdata->sc_trans for this property_t.  Uses
 * sc_handle, sc_trans, and sc_flags (SCI_NOENABLED) in lcbdata.  On success,
 * returns UU_WALK_NEXT.  On error returns UU_WALK_ERROR and sets
 * lcbdata->sc_err to
 *   ENOMEM - out of memory
 *   ECONNABORTED - repository connection broken
 *   ECANCELED - sc_trans's property group was deleted
 *   EINVAL - p's name is invalid (error printed)
 *	    - p has an invalid value (error printed)
 */
static int
lscf_property_import(void *v, void *pvt)
{
	property_t *p = v;
	scf_callback_t *lcbdata = pvt;
	value_t *vp;
	scf_transaction_t *trans = lcbdata->sc_trans;
	scf_transaction_entry_t *entr;
	scf_value_t *val;
	scf_type_t tp;

	if ((lcbdata->sc_flags & SCI_NOENABLED ||
	    lcbdata->sc_flags & SCI_DELAYENABLE) &&
	    strcmp(p->sc_property_name, SCF_PROPERTY_ENABLED) == 0) {
		lcbdata->sc_enable = p;
		return (UU_WALK_NEXT);
	}

	entr = scf_entry_create(lcbdata->sc_handle);
	if (entr == NULL) {
		switch (scf_error()) {
		case SCF_ERROR_NO_MEMORY:
			return (stash_scferror(lcbdata));

		case SCF_ERROR_INVALID_ARGUMENT:
		default:
			bad_error("scf_entry_create", scf_error());
		}
	}

	tp = p->sc_value_type;

	if (scf_transaction_property_new(trans, entr,
	    p->sc_property_name, tp) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_INVALID_ARGUMENT:
			semerr(emsg_invalid_prop_name, p->sc_property_name);
			scf_entry_destroy(entr);
			return (stash_scferror(lcbdata));

		case SCF_ERROR_EXISTS:
			break;

		case SCF_ERROR_DELETED:
		case SCF_ERROR_CONNECTION_BROKEN:
			scf_entry_destroy(entr);
			return (stash_scferror(lcbdata));

		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_transaction_property_new", scf_error());
		}

		if (scf_transaction_property_change_type(trans, entr,
		    p->sc_property_name, tp) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				scf_entry_destroy(entr);
				return (stash_scferror(lcbdata));

			case SCF_ERROR_INVALID_ARGUMENT:
				semerr(emsg_invalid_prop_name,
				    p->sc_property_name);
				scf_entry_destroy(entr);
				return (stash_scferror(lcbdata));

			case SCF_ERROR_NOT_FOUND:
			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error(
				    "scf_transaction_property_change_type",
				    scf_error());
			}
		}
	}

	for (vp = uu_list_first(p->sc_property_values);
	    vp != NULL;
	    vp = uu_list_next(p->sc_property_values, vp)) {
		val = scf_value_create(g_hndl);
		if (val == NULL) {
			switch (scf_error()) {
			case SCF_ERROR_NO_MEMORY:
				return (stash_scferror(lcbdata));

			case SCF_ERROR_INVALID_ARGUMENT:
			default:
				bad_error("scf_value_create", scf_error());
			}
		}

		switch (tp) {
		case SCF_TYPE_BOOLEAN:
			scf_value_set_boolean(val, vp->sc_u.sc_count);
			break;
		case SCF_TYPE_COUNT:
			scf_value_set_count(val, vp->sc_u.sc_count);
			break;
		case SCF_TYPE_INTEGER:
			scf_value_set_integer(val, vp->sc_u.sc_integer);
			break;
		default:
			assert(vp->sc_u.sc_string != NULL);
			if (scf_value_set_from_string(val, tp,
			    vp->sc_u.sc_string) != 0) {
				if (scf_error() != SCF_ERROR_INVALID_ARGUMENT)
					bad_error("scf_value_set_from_string",
					    scf_error());

				warn(gettext("Value \"%s\" is not a valid "
				    "%s.\n"), vp->sc_u.sc_string,
				    scf_type_to_string(tp));
				scf_value_destroy(val);
				return (stash_scferror(lcbdata));
			}
			break;
		}

		if (scf_entry_add_value(entr, val) != 0)
			bad_error("scf_entry_add_value", scf_error());
	}

	return (UU_WALK_NEXT);
}

/*
 * Import a pgroup_t into the repository.  Uses sc_handle, sc_parent,
 * sc_service, sc_flags (SCI_GENERALLAST, SCI_FORCE, & SCI_KEEP),
 * sc_source_fmri, and sc_target_fmri in lcbdata, and uses imp_pg and imp_tx.
 * On success, returns UU_WALK_NEXT.  On error returns UU_WALK_ERROR and sets
 * lcbdata->sc_err to
 *
 *   ECONNABORTED - repository connection broken
 *   ENOMEM - out of memory
 *   ENOSPC - svc.configd is out of resources
 *   ECANCELED - sc_parent was deleted
 *   EPERM - could not create property group (permission denied) (error printed)
 *	   - could not modify property group (permission denied) (error printed)
 *	   - could not delete property group (permission denied) (error	printed)
 *   EROFS - could not create property group (repository is read-only)
 *	   - could not delete property group (repository is read-only)
 *   EACCES - could not create property group (backend access denied)
 *	    - could not delete property group (backend access denied)
 *   EEXIST - could not create property group (already exists)
 *   EINVAL - invalid property group name (error printed)
 *	    - invalid property name (error printed)
 *	    - invalid value (error printed)
 *   EBUSY - new property group deleted (error printed)
 *	   - new property group changed (error printed)
 *	   - property group added (error printed)
 *	   - property group deleted (error printed)
 *
 * The SCI_FORCE flag is used to support the override attribute.  This works
 * because it will delete the pg that is associated with the pg that is being
 * overridden and re-create a new pg, to attach the properties to.
 *
 */
static int
entity_pgroup_import(void *v, void *pvt)
{
	pgroup_t *p = v;
	scf_callback_t cbdata;
	scf_callback_t *lcbdata = pvt;
	void *ent = lcbdata->sc_parent;
	int issvc = lcbdata->sc_service;
	int pg_exists = 0;
	int r;

	const char * const pg_changed = gettext("%s changed unexpectedly "
	    "(new property group \"%s\" changed).\n");

	/* Never import deleted property groups. */
	if (p->sc_pgroup_delete) {
		if (entity_get_pg(ent, issvc, p->sc_pgroup_name, imp_pg) == 0) {
			goto delete_pg;
		}
		return (UU_WALK_NEXT);
	}

	if (!issvc && (lcbdata->sc_flags & SCI_GENERALLAST) &&
	    strcmp(p->sc_pgroup_name, SCF_PG_GENERAL) == 0) {
		lcbdata->sc_general = p;
		return (UU_WALK_NEXT);
	}

add_pg:
	if (issvc)
		r = scf_service_add_pg(ent, p->sc_pgroup_name,
		    p->sc_pgroup_type, p->sc_pgroup_flags, imp_pg);
	else
		r = scf_instance_add_pg(ent, p->sc_pgroup_name,
		    p->sc_pgroup_type, p->sc_pgroup_flags, imp_pg);
	if (r != 0) {
		switch (scf_error()) {
		case SCF_ERROR_DELETED:
		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_BACKEND_READONLY:
		case SCF_ERROR_BACKEND_ACCESS:
		case SCF_ERROR_NO_RESOURCES:
			return (stash_scferror(lcbdata));

		case SCF_ERROR_EXISTS:
			pg_exists = lcbdata->sc_flags &
			    (SCI_OP_APPLY|SCI_OP_IMPORT);

			if (pg_exists)
				break;

			if (lcbdata->sc_flags & SCI_FORCE)
				break;

			return (stash_scferror(lcbdata));

		case SCF_ERROR_INVALID_ARGUMENT:
			warn(emsg_fmri_invalid_pg_name_type,
			    lcbdata->sc_source_fmri,
			    p->sc_pgroup_name, p->sc_pgroup_type);
			return (stash_scferror(lcbdata));

		case SCF_ERROR_PERMISSION_DENIED:
			warn(emsg_pg_add_perm, p->sc_pgroup_name,
			    lcbdata->sc_target_fmri);
			return (stash_scferror(lcbdata));

		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_service_add_pg", scf_error());
		}

		if (entity_get_pg(ent, issvc, p->sc_pgroup_name, imp_pg) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
			case SCF_ERROR_DELETED:
				return (stash_scferror(lcbdata));

			case SCF_ERROR_INVALID_ARGUMENT:
				warn(emsg_fmri_invalid_pg_name,
				    lcbdata->sc_source_fmri,
				    p->sc_pgroup_name);
				return (stash_scferror(lcbdata));

			case SCF_ERROR_NOT_FOUND:
				warn(emsg_pg_deleted, lcbdata->sc_target_fmri,
				    p->sc_pgroup_name);
				lcbdata->sc_err = EBUSY;
				return (UU_WALK_ERROR);

			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("entity_get_pg", scf_error());
			}
		}

		if (pg_exists || lcbdata->sc_flags & SCI_KEEP)
			goto props;

delete_pg:
		if (scf_pg_delete(imp_pg) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
				warn(emsg_pg_deleted, lcbdata->sc_target_fmri,
				    p->sc_pgroup_name);
				lcbdata->sc_err = EBUSY;
				return (UU_WALK_ERROR);

			case SCF_ERROR_PERMISSION_DENIED:
				warn(emsg_pg_del_perm, p->sc_pgroup_name,
				    lcbdata->sc_target_fmri);
				return (stash_scferror(lcbdata));

			case SCF_ERROR_BACKEND_READONLY:
			case SCF_ERROR_BACKEND_ACCESS:
			case SCF_ERROR_CONNECTION_BROKEN:
				return (stash_scferror(lcbdata));

			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_pg_delete", scf_error());
			}
		}

		if (p->sc_pgroup_delete)
			return (UU_WALK_NEXT);

		goto add_pg;
	}

props:

	/*
	 * Add properties to property group, if any.
	 */
	cbdata.sc_handle = lcbdata->sc_handle;
	cbdata.sc_parent = imp_pg;
	cbdata.sc_flags = lcbdata->sc_flags;
	cbdata.sc_trans = imp_tx;
	cbdata.sc_enable = NULL;

	if (scf_transaction_start(imp_tx, imp_pg) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_BACKEND_ACCESS:
		case SCF_ERROR_BACKEND_READONLY:
		case SCF_ERROR_CONNECTION_BROKEN:
			return (stash_scferror(lcbdata));

		case SCF_ERROR_DELETED:
			warn(pg_changed, lcbdata->sc_target_fmri,
			    p->sc_pgroup_name);
			lcbdata->sc_err = EBUSY;
			return (UU_WALK_ERROR);

		case SCF_ERROR_PERMISSION_DENIED:
			warn(emsg_pg_mod_perm, p->sc_pgroup_name,
			    lcbdata->sc_target_fmri);
			return (stash_scferror(lcbdata));

		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		case SCF_ERROR_IN_USE:
		case SCF_ERROR_HANDLE_MISMATCH:
		default:
			bad_error("scf_transaction_start", scf_error());
		}
	}

	/*
	 * If the property group exists and has a different type or
	 * flags then push a transaction to update those attributes
	 * of the property group.
	 *
	 * If the pg is masked, then we may not know if it exists
	 * because the add is going to not return failure.
	 */
	if (pg_exists || scf_pg_is_masked(imp_pg) == 1) {
		pgroup_t *p1 = NULL;

		if ((r = load_pg_attrs(imp_pg, &p1)) == 0 &&
		    pg_attrs_equal(p, p1, NULL, 0) == 0) {
			r = scf_transaction_pg_modify(imp_tx, p->sc_pgroup_type,
			    p->sc_pgroup_flags);

			if (r != 0) {
				scf_transaction_reset(imp_tx);
				return (stash_scferror(lcbdata));
			}
		}

		if (p1)
			internal_pgroup_free(p1);

		switch (r) {
		case 0:
			break;

		case ECANCELED:
		case ECONNABORTED:
		case ENOMEM:
			lcbdata->sc_err = r;
			return (UU_WALK_ERROR);

		default:
			bad_error("load_pg_attrs", r);
		}
	}

	if (uu_list_walk(p->sc_pgroup_props, lscf_property_import, &cbdata,
	    UU_DEFAULT) != 0) {
		if (uu_error() != UU_ERROR_CALLBACK_FAILED)
			bad_error("uu_list_walk", uu_error());
		scf_transaction_reset(imp_tx);

		lcbdata->sc_err = cbdata.sc_err;
		if (cbdata.sc_err == ECANCELED) {
			warn(pg_changed, lcbdata->sc_target_fmri,
			    p->sc_pgroup_name);
			lcbdata->sc_err = EBUSY;
		}
		return (UU_WALK_ERROR);
	}

	if ((lcbdata->sc_flags & SCI_DELAYENABLE) && cbdata.sc_enable) {
		cbdata.sc_flags = cbdata.sc_flags & (~SCI_DELAYENABLE);

		/*
		 * take the snapshot running snapshot then
		 * import the stored general/enable property
		 */
		r = take_snap(ent, snap_running, imp_rsnap);
		switch (r) {
		case 0:
			break;

		case ECONNABORTED:
			warn(gettext("Could not take %s snapshot on import "
			    "(repository connection broken).\n"),
			    snap_running);
			lcbdata->sc_err = r;
			return (UU_WALK_ERROR);
		case ECANCELED:
			warn(emsg_deleted);
			lcbdata->sc_err = r;
			return (UU_WALK_ERROR);

		case EPERM:
			warn(gettext("Could not take %s snapshot "
			    "(permission denied).\n"), snap_running);
			lcbdata->sc_err = r;
			return (UU_WALK_ERROR);

		case ENOSPC:
			warn(gettext("Could not take %s snapshot"
			    "(repository server out of resources).\n"),
			    snap_running);
			lcbdata->sc_err = r;
			return (UU_WALK_ERROR);

		default:
			bad_error("take_snap", r);
		}

		r = lscf_property_import(cbdata.sc_enable, &cbdata);
		if (r != UU_WALK_NEXT) {
			if (r != UU_WALK_ERROR)
				bad_error("lscf_property_import", r);
			return (EINVAL);
		}
	}

	r = scf_transaction_commit(imp_tx);
	switch (r) {
	case 1:
		r = UU_WALK_NEXT;
		break;

	case 0:
		warn(pg_changed, lcbdata->sc_target_fmri, p->sc_pgroup_name);
		lcbdata->sc_err = EBUSY;
		r = UU_WALK_ERROR;
		break;

	case -1:
		switch (scf_error()) {
		case SCF_ERROR_BACKEND_READONLY:
		case SCF_ERROR_BACKEND_ACCESS:
		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_NO_RESOURCES:
			r = stash_scferror(lcbdata);
			break;

		case SCF_ERROR_DELETED:
			warn(emsg_pg_deleted, lcbdata->sc_target_fmri,
			    p->sc_pgroup_name);
			lcbdata->sc_err = EBUSY;
			r = UU_WALK_ERROR;
			break;

		case SCF_ERROR_PERMISSION_DENIED:
			warn(emsg_pg_mod_perm, p->sc_pgroup_name,
			    lcbdata->sc_target_fmri);
			r = stash_scferror(lcbdata);
			break;

		case SCF_ERROR_CONFLICT:
			warn("%s: property group \"%s\" has a conflict.\n",
			    p->sc_parent->sc_fmri, p->sc_pgroup_name);

			/* Update the pg so that we can see the conflicts. */
			(void) scf_pg_update(imp_pg);

			/*
			 * An error in printing the conflict shouldn't
			 * cause the processing to stop, so we ignore
			 * errors.
			 */
			(void) smf_pg_print_conflicts(imp_pg,
			    p->sc_parent->sc_fmri);

			r = UU_WALK_NEXT;
			break;

		case SCF_ERROR_NOT_SET:
		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_BOUND:
		default:
			bad_error("scf_transaction_commit", scf_error());
		}
		break;

	default:
		bad_error("scf_transaction_commit", r);
	}

	scf_transaction_destroy_children(imp_tx);

	return (r);
}

/*
 * Returns
 *   0 - success
 *   ECONNABORTED - repository connection broken
 *   ENOMEM - out of memory
 *   ENOSPC - svc.configd is out of resources
 *   ECANCELED - inst was deleted
 *   EPERM - could not create property group (permission denied) (error printed)
 *	   - could not modify property group (permission denied) (error printed)
 *   EROFS - could not create property group (repository is read-only)
 *   EACCES - could not create property group (backend access denied)
 *   EEXIST - could not create property group (already exists)
 *   EINVAL - invalid property group name (error printed)
 *	    - invalid property name (error printed)
 *	    - invalid value (error printed)
 *   EBUSY - new property group changed (error printed)
 */
static int
lscf_import_service_pgs(scf_service_t *svc, const char *target_fmri,
    const entity_t *isvc, int flags)
{
	scf_callback_t cbdata;

	cbdata.sc_handle = scf_service_handle(svc);
	cbdata.sc_parent = svc;
	cbdata.sc_service = 1;
	cbdata.sc_general = 0;
	cbdata.sc_enable = 0;
	cbdata.sc_flags = flags;
	cbdata.sc_source_fmri = isvc->sc_fmri;
	cbdata.sc_target_fmri = target_fmri;

	/*
	 * If the op is set, then add the flag to the callback
	 * flags for later use.
	 */
	if (isvc->sc_op != SVCCFG_OP_NONE) {
		switch (isvc->sc_op) {
		case SVCCFG_OP_IMPORT :
			cbdata.sc_flags |= SCI_OP_IMPORT;
			break;
		case SVCCFG_OP_APPLY :
			cbdata.sc_flags |= SCI_OP_APPLY;
			break;
		default :
			uu_die(gettext("lscf_import_service_pgs : "
			    "Unknown op stored in the service entity\n"));

		}
	}

	if (uu_list_walk(isvc->sc_pgroups, entity_pgroup_import, &cbdata,
	    UU_DEFAULT) != 0) {
		if (uu_error() != UU_ERROR_CALLBACK_FAILED)
			bad_error("uu_list_walk", uu_error());

		return (cbdata.sc_err);
	}

	return (0);
}

/*
 * Returns
 *   0 - success
 *   ECONNABORTED - repository connection broken
 *   ENOMEM - out of memory
 *   ENOSPC - svc.configd is out of resources
 *   ECANCELED - inst was deleted
 *   EPERM - could not create property group (permission denied) (error printed)
 *	   - could not modify property group (permission denied) (error printed)
 *   EROFS - could not create property group (repository is read-only)
 *   EACCES - could not create property group (backend access denied)
 *   EEXIST - could not create property group (already exists)
 *   EINVAL - invalid property group name (error printed)
 *	    - invalid property name (error printed)
 *	    - invalid value (error printed)
 *   EBUSY - new property group changed (error printed)
 */
static int
lscf_import_instance_pgs(scf_instance_t *inst, const char *target_fmri,
    const entity_t *iinst, int flags)
{
	scf_callback_t cbdata;

	cbdata.sc_handle = scf_instance_handle(inst);
	cbdata.sc_parent = inst;
	cbdata.sc_service = 0;
	cbdata.sc_general = NULL;
	cbdata.sc_enable = NULL;
	cbdata.sc_source_fmri = iinst->sc_fmri;
	cbdata.sc_target_fmri = target_fmri;

	/*
	 * If the op is set, then add the flag to the callback
	 * flags for later use.
	 */
	if (iinst->sc_op != SVCCFG_OP_NONE) {
		switch (iinst->sc_op) {
		case SVCCFG_OP_IMPORT :
			cbdata.sc_flags = flags | SCI_OP_IMPORT;
			break;
		case SVCCFG_OP_APPLY :
			cbdata.sc_flags = flags | SCI_OP_APPLY;
			break;
		default :
			uu_die(gettext("lscf_import_instance_pgs : "
			    "Unknown op stored in the instance entity\n"));
		}
	} else {
		/*
		 * This should never happen, but just in case
		 * just set the cbdata.sc_flags.
		 */
		cbdata.sc_flags = flags;
	}

	if (uu_list_walk(iinst->sc_pgroups, entity_pgroup_import, &cbdata,
	    UU_DEFAULT) != 0) {
		if (uu_error() != UU_ERROR_CALLBACK_FAILED)
			bad_error("uu_list_walk", uu_error());

		return (cbdata.sc_err);
	}

	if ((flags & SCI_GENERALLAST) && cbdata.sc_general) {
		cbdata.sc_flags = cbdata.sc_flags & (~SCI_GENERALLAST);
		/*
		 * If importing with the SCI_NOENABLED flag then
		 * skip the delay, but if not then add the delay
		 * of the enable property.
		 */
		if (!(cbdata.sc_flags & SCI_NOENABLED)) {
			cbdata.sc_flags |= SCI_DELAYENABLE;
		}

		if (entity_pgroup_import(cbdata.sc_general, &cbdata)
		    != UU_WALK_NEXT)
			return (cbdata.sc_err);
	}

	return (0);
}

/*
 * Compares two entity FMRIs.  Returns
 *
 *   1 - equal
 *   0 - not equal
 *   -1 - f1 is invalid or not an entity
 *   -2 - f2 is invalid or not an entity
 */
static int
fmri_equal(const char *f1, const char *f2)
{
	int r;
	const char *s1, *i1, *pg1;
	const char *s2, *i2, *pg2;

	if (strlcpy(imp_fe1, f1, max_scf_fmri_len + 1) >= max_scf_fmri_len + 1)
		return (-1);
	if (scf_parse_svc_fmri(imp_fe1, NULL, &s1, &i1, &pg1, NULL) != 0)
		return (-1);

	if (s1 == NULL || pg1 != NULL)
		return (-1);

	if (strlcpy(imp_fe2, f2, max_scf_fmri_len + 1) >= max_scf_fmri_len + 1)
		return (-2);
	if (scf_parse_svc_fmri(imp_fe2, NULL, &s2, &i2, &pg2, NULL) != 0)
		return (-2);

	if (s2 == NULL || pg2 != NULL)
		return (-2);

	r = strcmp(s1, s2);
	if (r != 0)
		return (0);

	if (i1 == NULL && i2 == NULL)
		return (1);

	if (i1 == NULL || i2 == NULL)
		return (0);

	return (strcmp(i1, i2) == 0);
}

/*
 * Creates a pgroup_t manifestfiles type framework and populates one property
 * with the bundle name in the argument.
 */
static pgroup_t *
internal_manifestfiles_create_entry(const char *fname)
{
	pgroup_t *pg = internal_pgroup_new();
	property_t *p;
	char *buf;

	buf = mhash_filename_to_propname(fname, B_TRUE);

	pg->sc_pgroup_name = safe_strdup(SCF_PG_MANIFESTFILES);
	pg->sc_pgroup_type = safe_strdup(SCF_GROUP_FRAMEWORK);

	p = internal_property_create(buf, SCF_TYPE_ASTRING, 1,
	    fname);

	if (internal_attach_property(pg, p) != 0)
		uu_die("%s/%s already exists\n", SCF_PG_MANIFESTFILES, buf);

	return (pg);
}

/*
 * lscf_manifestfiles_entry will create a manifestfile entry in the service
 * we just created a dependency property group generated by a dependent
 * element in a bundle. In order to work correctly, it should be called
 * from lscf_dependent_import. This way, we don't have to duplicate the error
 * handling for the fmri_to_entity() call here.
 */
static int
lscf_manifestfiles_entry(void *v, void *pvt)
{
	pgroup_t *pg = v;
	pgroup_t *mnfst_pg = NULL;
	scf_callback_t *lcb = pvt;
	scf_callback_t mnfst_cbdata;
	char *p;

	int r = 0;
	scf_error_t scf_err;
	char *fmri = safe_strdup(pg->sc_pgroup_fmri);

	/*
	 * trim out instance
	 * p is our trimming point candidate.
	 */
	p = strrchr(fmri, ':');
	if (p != NULL) {
		/*
		 * if we find a second colon, we can assume it is from svc:/
		 * and trim the fmri at p
		 */
		*p = '\0';
		if (strrchr(fmri, ':') == NULL) {
			/*
			 * oops! we don't have an instance, so we need the
			 * colon back!
			 */
			*p = ':';
		}
	}
	scf_err = fmri_to_entity(lcb->sc_handle, fmri, &mnfst_cbdata.sc_parent,
	    &mnfst_cbdata.sc_service);

	if (scf_err != SCF_ERROR_NONE) {
		free(fmri);
		return (stash_scferror_err(&mnfst_cbdata, scf_error()));
	}

	mnfst_pg = internal_manifestfiles_create_entry(pg->sc_pgroup_bundle);

	mnfst_pg->sc_pgroup_fmri = fmri;

	mnfst_cbdata.sc_handle = lcb->sc_handle;
	mnfst_cbdata.sc_flags = (lcb->sc_flags &
	    (SCI_OP_APPLY|SCI_OP_IMPORT|SCI_FORCE));
	mnfst_cbdata.sc_source_fmri = pg->sc_pgroup_fmri;
	mnfst_cbdata.sc_target_fmri = fmri;

	r = entity_pgroup_import(mnfst_pg, &mnfst_cbdata);
	entity_destroy(mnfst_cbdata.sc_parent, mnfst_cbdata.sc_service);
	free(fmri);

	if (r == UU_WALK_NEXT)
		return (r);

	if (r != UU_WALK_ERROR)
		bad_error("entity_pgroup_import", r);

	switch (mnfst_cbdata.sc_err) {
	case ECANCELED:
		warn(gettext("%s deleted unexpectedly.\n"), fmri);
		lcb->sc_err = EBUSY;
		break;

	case EEXIST:
		warn(gettext("Could not create \"%s\" property_group in %s "
		    "(already exists).\n"), pg->sc_pgroup_name, fmri);
		/* FALLTHROUGH */

	default:
		lcb->sc_err = mnfst_cbdata.sc_err;
	}

	return (UU_WALK_ERROR);
}

/*
 * Import a dependent by creating a dependency property group in the dependent
 * entity.  If lcbdata->sc_trans is set, assume it's been started on the
 * dependents pg, and add an entry to create a new property for this
 * dependent.  Uses sc_handle, sc_trans, and sc_fmri in lcbdata.
 *
 * On success, returns UU_WALK_NEXT.  On error, returns UU_WALK_ERROR and sets
 * lcbdata->sc_err to
 *   ECONNABORTED - repository connection broken
 *   ENOMEM - out of memory
 *   ENOSPC - configd is out of resources
 *   EINVAL - target is invalid (error printed)
 *	    - target is not an entity (error printed)
 *	    - dependent has invalid name (error printed)
 *	    - invalid property name (error printed)
 *	    - invalid value (error printed)
 *	    - scope of target does not exist (error printed)
 *   EPERM - couldn't create target (permission denied) (error printed)
 *	   - couldn't create dependency pg (permission denied) (error printed)
 *	   - couldn't modify dependency pg (permission denied) (error printed)
 *   EROFS - couldn't create target (repository read-only)
 *	   - couldn't create dependency pg (repository read-only)
 *   EACCES - couldn't create target (backend access denied)
 *	    - couldn't create dependency pg (backend access denied)
 *   ECANCELED - sc_trans's pg was deleted
 *   EALREADY - property for dependent already exists in sc_trans's pg
 *   EEXIST - dependency pg already exists in target (error printed)
 *   EBUSY - target deleted (error printed)
 *         - property group changed during import (error printed)
 */
static int
lscf_dependent_import(void *a1, void *pvt)
{
	pgroup_t *pgrp = a1;
	scf_callback_t *lcbdata = pvt;

	int isservice;
	int ret;
	scf_transaction_entry_t *e;
	scf_value_t *val;
	scf_callback_t dependent_cbdata;
	scf_error_t scfe;

	/*
	 * Decode the FMRI into dependent_cbdata->sc_parent.  Do it here so if
	 * it's invalid, we fail before modifying the repository.
	 */
	scfe = fmri_to_entity(lcbdata->sc_handle, pgrp->sc_pgroup_fmri,
	    &dependent_cbdata.sc_parent, &isservice);
	switch (scfe) {
	case SCF_ERROR_NONE:
		break;

	case SCF_ERROR_NO_MEMORY:
		return (stash_scferror_err(lcbdata, scfe));

	case SCF_ERROR_INVALID_ARGUMENT:
		semerr(gettext("The FMRI for the \"%s\" dependent is "
		    "invalid.\n"), pgrp->sc_pgroup_name);
		return (stash_scferror_err(lcbdata, scfe));

	case SCF_ERROR_CONSTRAINT_VIOLATED:
		semerr(gettext("The FMRI \"%s\" for the \"%s\" dependent "
		    "specifies neither a service nor an instance.\n"),
		    pgrp->sc_pgroup_fmri, pgrp->sc_pgroup_name);
		return (stash_scferror_err(lcbdata, scfe));

	case SCF_ERROR_NOT_FOUND:
		scfe = create_entity(lcbdata->sc_handle, pgrp->sc_pgroup_fmri,
		    &dependent_cbdata.sc_parent, &isservice);
		switch (scfe) {
		case SCF_ERROR_NONE:
			break;

		case SCF_ERROR_NO_MEMORY:
		case SCF_ERROR_BACKEND_READONLY:
		case SCF_ERROR_BACKEND_ACCESS:
			return (stash_scferror_err(lcbdata, scfe));

		case SCF_ERROR_NOT_FOUND:
			semerr(gettext("The scope in FMRI \"%s\" for the "
			    "\"%s\" dependent does not exist.\n"),
			    pgrp->sc_pgroup_fmri, pgrp->sc_pgroup_name);
			lcbdata->sc_err = EINVAL;
			return (UU_WALK_ERROR);

		case SCF_ERROR_PERMISSION_DENIED:
			warn(gettext(
			    "Could not create %s (permission denied).\n"),
			    pgrp->sc_pgroup_fmri);
			return (stash_scferror_err(lcbdata, scfe));

		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_CONSTRAINT_VIOLATED:
		default:
			bad_error("create_entity", scfe);
		}
		break;

	default:
		bad_error("fmri_to_entity", scfe);
	}

	if (lcbdata->sc_trans != NULL) {
		e = scf_entry_create(lcbdata->sc_handle);
		if (e == NULL) {
			if (scf_error() != SCF_ERROR_NO_MEMORY)
				bad_error("scf_entry_create", scf_error());

			entity_destroy(dependent_cbdata.sc_parent, isservice);
			return (stash_scferror(lcbdata));
		}

		if (scf_transaction_property_new(lcbdata->sc_trans, e,
		    pgrp->sc_pgroup_name, SCF_TYPE_FMRI) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_INVALID_ARGUMENT:
				warn(gettext("Dependent of %s has invalid name "
				    "\"%s\".\n"), pgrp->sc_parent->sc_fmri,
				    pgrp->sc_pgroup_name);
				/* FALLTHROUGH */

			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				scf_entry_destroy(e);
				entity_destroy(dependent_cbdata.sc_parent,
				    isservice);
				return (stash_scferror(lcbdata));

			case SCF_ERROR_EXISTS:
				scf_entry_destroy(e);
				entity_destroy(dependent_cbdata.sc_parent,
				    isservice);
				lcbdata->sc_err = EALREADY;
				return (UU_WALK_ERROR);

			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_transaction_property_new",
				    scf_error());
			}
		}

		val = scf_value_create(lcbdata->sc_handle);
		if (val == NULL) {
			if (scf_error() != SCF_ERROR_NO_MEMORY)
				bad_error("scf_value_create", scf_error());

			entity_destroy(dependent_cbdata.sc_parent, isservice);
			return (stash_scferror(lcbdata));
		}

		if (scf_value_set_from_string(val, SCF_TYPE_FMRI,
		    pgrp->sc_pgroup_fmri) != 0)
			/* invalid should have been caught above */
			bad_error("scf_value_set_from_string", scf_error());

		if (scf_entry_add_value(e, val) != 0)
			bad_error("scf_entry_add_value", scf_error());
	}

	/* Add the property group to the target entity. */

	dependent_cbdata.sc_handle = lcbdata->sc_handle;
	dependent_cbdata.sc_flags = lcbdata->sc_flags;
	dependent_cbdata.sc_source_fmri = lcbdata->sc_source_fmri;
	dependent_cbdata.sc_target_fmri = pgrp->sc_pgroup_fmri;

	ret = entity_pgroup_import(pgrp, &dependent_cbdata);

	entity_destroy(dependent_cbdata.sc_parent, isservice);

	if (ret == UU_WALK_NEXT) {
		if (lscf_manifestfiles_entry(a1, pvt) == UU_WALK_NEXT)
			return (UU_WALK_NEXT);
		else
			return (UU_WALK_ERROR);
	}

	if (ret != UU_WALK_ERROR)
		bad_error("entity_pgroup_import", ret);

	switch (dependent_cbdata.sc_err) {
	case ECANCELED:
		warn(gettext("%s deleted unexpectedly.\n"),
		    pgrp->sc_pgroup_fmri);
		lcbdata->sc_err = EBUSY;
		break;

	case EEXIST:
		warn(gettext("Could not create \"%s\" dependency in %s "
		    "(already exists).\n"), pgrp->sc_pgroup_name,
		    pgrp->sc_pgroup_fmri);
		/* FALLTHROUGH */

	default:
		lcbdata->sc_err = dependent_cbdata.sc_err;
	}

	return (UU_WALK_ERROR);
}

/*
 * Used to add the manifests to the list of currently supported manifests.
 * We can modify the existing manifest list removing entries if the files
 * don't exist.
 *
 * Get the old list
 * If the new file name is in the list return
 * If not then add the file to the list
 * As we process the list check to see if the files in the old list exist
 * 	if not then remove the file from the list.
 *
 */
static int
upgrade_manifestfiles(pgroup_t *mfst_pgroup, scf_propertygroup_t *mfsts_pg)
{
	scf_property_t *ud_prop = NULL;
	scf_iter_t *ud_prop_iter;
	scf_value_t *fname_value;
	property_t *mfst_prop;
	property_t *old_prop;
	char *pname = malloc(MAXPATHLEN);
	char *fval = NULL;
	char *old_pname;
	char *old_fval;
	int mfst_seen;
	int r;

	/*
	 * This should always be the service base on the code
	 * path, and the fact that the manifests pg is a service
	 * level property group only.
	 */
	ud_prop = scf_property_create(g_hndl);
	ud_prop_iter = scf_iter_create(g_hndl);
	fname_value = scf_value_create(g_hndl);

	if ((r = scf_iter_pg_properties(ud_prop_iter, mfsts_pg)) !=
	    SCF_SUCCESS)
		return (-1);

	while ((r = scf_iter_next_property(ud_prop_iter, ud_prop)) == 1) {
		mfst_seen = 0;
		if (scf_property_get_name(ud_prop, pname, MAXPATHLEN) < 0)
			continue;

		for (mfst_prop = uu_list_first(mfst_pgroup->sc_pgroup_props);
		    mfst_prop != NULL;
		    mfst_prop = uu_list_next(mfst_pgroup->sc_pgroup_props,
		    mfst_prop)) {
			if (strcmp(mfst_prop->sc_property_name, pname) == 0) {
				mfst_seen = 1;
			}
		}

		/*
		 * If the manifest is not seen then add it to the new mfst
		 * property list to get proccessed into the repo.
		 */
		if (mfst_seen == 0) {
			if (fval == NULL)
				fval = malloc(MAXPATHLEN);

			/*
			 * If we cannot get the value then there is no
			 * reason to attempt to attach the value to
			 * the property group
			 */
			if (fval != NULL &&
			    prop_get_val(ud_prop, fname_value) == 0 &&
			    scf_value_get_astring(fname_value, fval,
			    MAXPATHLEN) != -1)  {
				old_pname = safe_strdup(pname);
				old_fval = safe_strdup(fval);
				old_prop = internal_property_create(old_pname,
				    SCF_TYPE_ASTRING, 1, old_fval);

				/*
				 * Already checked to see if the property exists
				 * in the group, and it does not.
				 */
				(void) internal_attach_property(mfst_pgroup,
				    old_prop);
			}
		}
	}
	free(fval);

	return (r);
}

/*
 * Do the following for each pg in repository:
 *
 * - If pg is delivered in entity (handled in normal pg import code path)
 *   - Send new properties to configd
 *
 * - If pg is not delivered in entity (handled in this function)
 *   - Skip external dependency
 *   - Iterate through properties
 *     - Delete property if there's no customizations (only file backed
 *       decorations) and property was delivered by the processing bundle.
 *   - If all properties in pg were delivered by the current processing bundle
 *     and there are no customizations, delete the pg.
 *
 * XXX - There are a couple of issues with this function in that
 * bundles need to be iterated over for support check and
 * the transaction needs to be handled by a different (new) library
 * function that is being re-factored post this current code base.
 *
 * This re-factor work will be done before integration and an
 * incremental webrev will be sent out for review for those
 * specific changes.
 *
 * property groups and property values can be mis-represented when properties
 * are removed from one bundle that has multiple bundles at the same layer
 * supporting them, depending on the ordering of the bundles being imported
 * and/or applied.
 */
static int
process_old_pgs(const entity_t *sient, void *si,
    scf_decoration_layer_t op_layer)
{
	scf_transaction_entry_t	*e = NULL;
	scf_transaction_t	*tx = NULL;
	scf_decoration_layer_t	layer;
	scf_decoration_t	*dec;
	scf_error_t		serr;
	scf_iter_t	*pgit, *propit, *decit;
	pgroup_t	*mpg, pgrp;
	property_t	prop;
	int issvc = (sient->sc_etype == SVCCFG_SERVICE_OBJECT);
	char		*buf, *bbuf;
	ssize_t		len;
	int		pg_delete;
	int r;

	len = scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	buf = safe_malloc(len);

	bbuf = safe_malloc(MAXPATHLEN);

	pgit = scf_iter_create(g_hndl);
	if (pgit == NULL)
		scfdie();

	propit = scf_iter_create(g_hndl);
	if (propit == NULL)
		scfdie();

	decit = scf_iter_create(g_hndl);
	if (decit == NULL)
		scfdie();

	dec = scf_decoration_create(g_hndl);
	if (dec == NULL)
		scfdie();

	if (issvc) {
		if (scf_iter_service_pgs(pgit, si) != SCF_SUCCESS)
			scfdie();
	} else {
		if (scf_iter_instance_pgs(pgit, si) != SCF_SUCCESS)
			scfdie();
	}

	while (scf_iter_next_pg(pgit, imp_pg) == 1) {
		/* Skip dependent property groups. */
		if (scf_pg_get_type(imp_pg, imp_str, imp_str_sz) < 0) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
				free(buf);
				free(bbuf);
				return (ENODEV);

			case SCF_ERROR_CONNECTION_BROKEN:
				free(buf);
				free(bbuf);
				return (ECONNABORTED);
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error("scf_pg_get_type", scf_error());
			}
		}

		if (strcmp(imp_str, SCF_GROUP_DEPENDENCY) == 0) {
			if (scf_pg_get_property(imp_pg, "external", NULL) == 0)
				continue;

			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
				break;

			case SCF_ERROR_CONNECTION_BROKEN:
				free(buf);
				free(bbuf);
				return (ECONNABORTED);

			case SCF_ERROR_DELETED:
				free(buf);
				free(bbuf);
				return (ENODEV);

			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_pg_get_property", scf_error());
			}
		} /* end - Skip dependent property groups */

		/* lookup pg in new properties */
		if (scf_pg_get_name(imp_pg, imp_str, imp_str_sz) < 0) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
				free(buf);
				free(bbuf);
				return (ENODEV);

			case SCF_ERROR_CONNECTION_BROKEN:
				free(buf);
				free(bbuf);
				return (ECONNABORTED);

			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error("scf_pg_get_name", scf_error());
			}
		}

		pg_delete = 1;
		pgrp.sc_pgroup_name = imp_str;
		mpg = uu_list_find(sient->sc_pgroups, &pgrp, NULL, NULL);

		/* If property group is found then process the properties */
		if (mpg != NULL) {
			/* Skip manifestfiles property groups */
			if (strcmp(imp_str, SCF_PG_MANIFESTFILES) == 0) {
				r = upgrade_manifestfiles(mpg, imp_pg);

				switch (r) {
				case 0:
					break;

				case ECONNABORTED:
				case ENOMEM:
				case ENOSPC:
				case ECANCELED:
				case ENODEV:
				case EBADF:
				case EBUSY:
				case EINVAL:
				case EPERM:
				case EROFS:
				case EACCES:
				case EEXIST:
					free(buf);
					free(bbuf);
					return (r);

				default:
					bad_error("upgrade_manifestfiles", r);
				}

				continue;
			}

			pg_delete = 0;
		}

		/*
		 * Check to see if the property group was supported by this
		 * bundle.  If it was not then just continue as there is no
		 * need to process the property group as part of this bundle
		 */
		r = 0;
		if (scf_iter_pg_decorations(decit, imp_pg, 0) != SCF_SUCCESS)
			scfdie();

		while (scf_iter_next_decoration(decit, dec) == 1) {
			if (scf_decoration_get_layer(dec, &layer)
			    != SCF_SUCCESS)
				scfdie();

			/*
			 * Admin layer decoration is not considered for bundle
			 * support.
			 */
			if (layer == SCF_DECORATION_ADMIN)
				continue;

			/*
			 * Don't delete pg if it is supported by another bundle.
			 */
			if (layer != op_layer) {
				pg_delete = 0;
				continue;
			}

			if (scf_decoration_get_bundle(dec, bbuf,
			    MAXPATHLEN) < 0)
				scfdie();

			if (strcmp(imp_bundle, bbuf) == 0) {
				r = 1;
				continue;
			}

			/*
			 * Some other bundle supports this property group, make
			 * sure that we don't remove it.
			 */
			pg_delete = 0;
		}

		/*
		 * There is no bundle support for this pg
		 */
		if (r == 0)
			continue;

		if (mpg == NULL) {
			tx = scf_transaction_create(g_hndl);
			if (scf_pg_update(imp_pg) == -1)
				scfdie();

			if (scf_transaction_start(tx, imp_pg) != SCF_SUCCESS)
				scfdie();

			if (scf_transaction_pg_bundle_remove(tx) !=
			    SCF_SUCCESS)
				scfdie();
		}

retry:
		/*
		 * Now process each property in the property group with the
		 * following checks :
		 *
		 * 1. check to see if the property has higher layer support.
		 *    If so make sure the pg is not destroyed.
		 * 2. is the property not supported by the incoming bundle.
		 * 3. was the property supported by this bundle.
		 * 4. add the property to a transaction for bundle support
		 *    removal.
		 *
		 */

		r = 1;
		if (scf_iter_pg_properties(propit, imp_pg) != SCF_SUCCESS)
			scfdie();

		while (scf_iter_next_property(propit, imp_prop) == 1) {
			if (scf_property_get_name(imp_prop, imp_str,
			    imp_str_sz) < 0) {
				switch (scf_error()) {
				case SCF_ERROR_DELETED:
					free(buf);
					free(bbuf);
					return (ENODEV);

				case SCF_ERROR_CONNECTION_BROKEN:
					free(buf);
					free(bbuf);
					return (ECONNABORTED);

				case SCF_ERROR_NOT_SET:
				case SCF_ERROR_NOT_BOUND:
				default:
					bad_error("scf_property_get_name",
					    scf_error());
				}
			}

			if (scf_property_get_decoration(imp_prop,
			    SCF_DECORATION_TOP, dec) != SCF_SUCCESS) {
				switch (scf_error()) {
				case SCF_ERROR_CONSTRAINT_VIOLATED:
					continue;
				case SCF_ERROR_NOT_FOUND:
					/*
					 * Set the high_property because
					 * this property does not have
					 * a decoration and therefore
					 * could be part of the non-persistent
					 * repo which we don't want to remove.
					 */
					pg_delete = 0;
					continue;
				default:
					scfdie();
				}
			}

			if (scf_decoration_get_layer(dec, &layer)
			    != SCF_SUCCESS)
				scfdie();

			/*
			 * A property outside of the manifest layer is part
			 * of this pg, make sure that the pg is not destroyed.
			 */
			if (layer != op_layer &&
			    layer != SCF_DECORATION_ADMIN &&
			    IS_LAYER_BACKED_BY_FILE(layer)) {
				pg_delete = 0;
			}

			if (mpg) {
				prop.sc_property_name = imp_str;
				if (uu_list_find(mpg->sc_pgroup_props, &prop,
				    NULL, NULL) != NULL)
					continue;
			}

			/*
			 * Check to see if the property was supported by this
			 * bundle.
			 */
			r = 0;
			if (scf_iter_property_decorations(decit, imp_prop, 0) !=
			    SCF_SUCCESS)
				scfdie();

			while (scf_iter_next_decoration(decit, dec) == 1) {
				if (scf_decoration_get_layer(dec, &layer)
				    != SCF_SUCCESS)
					scfdie();

				if (layer != op_layer)
					continue;

				if (scf_decoration_get_bundle(dec, bbuf,
				    MAXPATHLEN) < 0)
					scfdie();

				if (strcmp(imp_bundle, bbuf) == 0) {
					r = 1;
					break;
				}
			}

			/*
			 * Property wasn't supported by the incoming bundle.
			 */
			if (r == 0)
				continue;

			/*
			 * Add the property to the transaction so that it can
			 * be processed as part of the bundle support removal.
			 */
			if (tx == NULL) {
				tx = scf_transaction_create(g_hndl);
				if (scf_pg_update(imp_pg) == -1)
					scfdie();

				if (scf_transaction_start(tx, imp_pg) !=
				    SCF_SUCCESS)
					scfdie();
			}

			if (scf_property_get_name(imp_prop, imp_str,
			    imp_str_sz) < 0)
				scfdie();

			e = scf_entry_create(g_hndl);
			if (scf_transaction_property_delete(tx, e,
			    imp_str) != SCF_SUCCESS) {
				if (scf_error() == SCF_ERROR_NOT_FOUND) {
					semerr(gettext("No such "
					    "property %s\n"), imp_str);
					scf_entry_destroy(e);
					e = NULL;
				} else {
					scfdie();
				}
			}
		}

		if (pg_delete) {
			if (scf_pg_delete(imp_pg) != 0) {
				serr = scf_error();
				if (serr == SCF_ERROR_NOT_FOUND ||
				    serr == SCF_ERROR_DELETED) {
					break;
				} else {
					bad_error("scf_pg_delete", scf_error());
				}
			}

			if (tx) {
				scf_transaction_destroy_children(tx);

				scf_transaction_destroy(tx);

				tx = NULL;
			}
		} else if (tx) {
			r = scf_prop_bundle_remove(tx, imp_bundle);

			scf_transaction_destroy_children(tx);
			switch (r) {
			case 1:
				break;
			case 0:
				warn(gettext("pg changed for property : %s\n"),
				    imp_str);
				scf_transaction_destroy(tx);
				tx = NULL;
				goto retry;
			case -1:
			default:
				bad_error("scf_transaction_commit", r);
			}

			scf_transaction_destroy(tx);
			tx = NULL;
		}
	}

	free(buf);
	free(bbuf);
	scf_iter_destroy(pgit);
	scf_iter_destroy(propit);
	scf_iter_destroy(decit);
	scf_decoration_destroy(dec);

	return (0);
}

/*
 * _lscf_import_err() summarize the error handling returned by
 * lscf_import_{instance | service}_pgs
 * Return values are:
 * IMPORT_NEXT
 * IMPORT_OUT
 * IMPORT_BAD
 */

#define	IMPORT_BAD	-1
#define	IMPORT_NEXT	0
#define	IMPORT_OUT	1

static int
_lscf_import_err(int err, const char *fmri)
{
	switch (err) {
	case 0:
		if (g_verbose)
			warn(gettext("%s updated.\n"), fmri);
		return (IMPORT_NEXT);

	case ECONNABORTED:
		warn(gettext("Could not update %s "
		    "(repository connection broken).\n"), fmri);
		return (IMPORT_OUT);

	case ENOMEM:
		warn(gettext("Could not update %s (out of memory).\n"), fmri);
		return (IMPORT_OUT);

	case ENOSPC:
		warn(gettext("Could not update %s "
		    "(repository server out of resources).\n"), fmri);
		return (IMPORT_OUT);

	case ECANCELED:
		warn(gettext(
		    "Could not update %s (deleted).\n"), fmri);
		return (IMPORT_NEXT);

	case EPERM:
	case EINVAL:
	case EBUSY:
		return (IMPORT_NEXT);

	case EROFS:
		warn(gettext("Could not update %s (repository read-only).\n"),
		    fmri);
		return (IMPORT_OUT);

	case EACCES:
		warn(gettext("Could not update %s "
		    "(backend access denied).\n"), fmri);
		return (IMPORT_NEXT);

	case EEXIST:
	default:
		return (IMPORT_BAD);
	}

	/*NOTREACHED*/
}


/*
 * Import an instance. Update properties or create instance if it
 * doesn't exist. If there's a conflict, don't override, like now?
 *
 * On success, returns UU_WALK_NEXT.  On error returns UU_WALK_ERROR and sets
 * lcbdata->sc_err to
 *   ECONNABORTED - repository connection broken
 *   ENOMEM - out of memory
 *   ENOSPC - svc.configd is out of resources
 *   EEXIST - dependency collision in dependent service (error printed)
 *   EPERM - couldn't create temporary instance (permission denied)
 *	   - couldn't import into temporary instance (permission denied)
 *	   - couldn't take snapshot (permission denied)
 *	   - couldn't upgrade properties (permission denied)
 *	   - couldn't import properties (permission denied)
 *	   - couldn't import dependents (permission denied)
 *   EROFS - couldn't create temporary instance (repository read-only)
 *	   - couldn't import into temporary instance (repository read-only)
 *	   - couldn't upgrade properties (repository read-only)
 *	   - couldn't import properties (repository read-only)
 *	   - couldn't import dependents (repository read-only)
 *   EACCES - couldn't create temporary instance (backend access denied)
 *	    - couldn't import into temporary instance (backend access denied)
 *	    - couldn't upgrade properties (backend access denied)
 *	    - couldn't import properties (backend access denied)
 *	    - couldn't import dependents (backend access denied)
 *   EINVAL - invalid instance name (error printed)
 *	    - invalid pgroup_t's (error printed)
 *	    - invalid dependents (error printed)
 *   EBUSY - temporary service deleted (error printed)
 *	   - temporary instance deleted (error printed)
 *	   - temporary instance changed (error printed)
 *	   - temporary instance already exists (error printed)
 *	   - instance deleted (error printed)
 *   EBADF - instance is corrupt (error printed)
 *	   - dependent has corrupt pg (error printed)
 *	   - dependent target has a corrupt snapshot (error printed)
 *   -1 - unknown libscf error (error printed)
 */
static int
lscf_instance_import(void *v, void *pvt)
{
	entity_t *inst = v;
	scf_callback_t ctx;
	scf_callback_t *lcbdata = pvt;
	scf_service_t *rsvc = lcbdata->sc_parent;
	int r;
	int flags = lcbdata->sc_flags;

	if (lcbdata->sc_flags & SCI_FRESH)
		goto fresh;

	if (scf_service_get_instance(rsvc, inst->sc_name, imp_inst) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			goto connaborted;

		case SCF_ERROR_NOT_FOUND:
			break;

		case SCF_ERROR_INVALID_ARGUMENT:	/* caught above */
		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_service_get_instance", scf_error());
		}

fresh:
		/* create instance */
		if (scf_service_add_instance(rsvc, inst->sc_name,
		    imp_inst) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
				goto connaborted;

			case SCF_ERROR_NO_RESOURCES:
			case SCF_ERROR_BACKEND_READONLY:
			case SCF_ERROR_BACKEND_ACCESS:
				r = stash_scferror(lcbdata);
				goto out;

			case SCF_ERROR_EXISTS:
				warn(gettext("%s changed unexpectedly "
				    "(instance \"%s\" added).\n"),
				    inst->sc_parent->sc_fmri, inst->sc_name);
				lcbdata->sc_err = EBUSY;
				r = UU_WALK_ERROR;
				goto out;

			case SCF_ERROR_PERMISSION_DENIED:
				warn(gettext("Could not create \"%s\" instance "
				    "in %s (permission denied).\n"),
				    inst->sc_name, inst->sc_parent->sc_fmri);
				r = stash_scferror(lcbdata);
				goto out;

			case SCF_ERROR_INVALID_ARGUMENT:  /* caught above */
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_service_add_instance",
				    scf_error());
			}
		}
	}

	inst->sc_import_state = IMPORT_PROP_BEGUN;

	if (inst->sc_seen)
		if (process_old_pgs(inst, imp_inst,
		    SCF_DECORATION_MANIFEST) != 0)
			goto out;

	r = lscf_import_instance_pgs(imp_inst, inst->sc_fmri, inst, flags);
	switch (r) {
	case 0:
			break;

	case ECONNABORTED:
		goto connaborted;

	case ECANCELED:
		warn(gettext("%s changed unexpectedly instance \"%s\" "
		    "deleted).\n"), inst->sc_parent->sc_fmri, inst->sc_name);
		lcbdata->sc_err = EBUSY;
		r = UU_WALK_ERROR;
		goto out;

	case EEXIST:
		warn(gettext("%s changed unexpectedly (property group "
		    "added).\n"), inst->sc_fmri);
		lcbdata->sc_err = EBUSY;
		r = UU_WALK_ERROR;
		goto out;

	default:
		lcbdata->sc_err = r;
		r = UU_WALK_ERROR;
		goto out;

	case EINVAL:	/* caught above */
		bad_error("lscf_import_instance_pgs", r);
	}

	ctx.sc_handle = lcbdata->sc_handle;
	ctx.sc_parent = imp_inst;
	ctx.sc_service = 0;
	ctx.sc_trans = NULL;
	ctx.sc_flags = 0;
	if (inst->sc_op != SVCCFG_OP_NONE) {
		switch (inst->sc_op) {
		case SVCCFG_OP_IMPORT :
			ctx.sc_flags |= SCI_OP_IMPORT;
			break;
		case SVCCFG_OP_APPLY :
			ctx.sc_flags |= SCI_OP_APPLY;
			break;
		default :
			break;
		}
	}
	if (uu_list_walk(inst->sc_dependents, lscf_dependent_import, &ctx,
	    UU_DEFAULT) != 0) {
		if (uu_error() != UU_ERROR_CALLBACK_FAILED)
			bad_error("uu_list_walk", uu_error());

		if (ctx.sc_err == ECONNABORTED)
			goto connaborted;
		lcbdata->sc_err = ctx.sc_err;
		r = UU_WALK_ERROR;
		goto out;
	}

	inst->sc_import_state = IMPORT_PROP_DONE;

	if (lcbdata->sc_flags & SCI_FRESH) {
		if (g_verbose)
			warn(gettext("Taking \"%s\" snapshot for %s.\n"),
			    snap_initial, inst->sc_fmri);
		r = take_snap(imp_inst, snap_initial, imp_snap);
		switch (r) {
		case 0:
			break;

		case ECONNABORTED:
			goto connaborted;

		case ENOSPC:
		case -1:
			lcbdata->sc_err = r;
			r = UU_WALK_ERROR;
			goto out;

		case ECANCELED:
			warn(gettext("%s changed unexpectedly "
			    "(instance %s deleted).\n"),
			    inst->sc_parent->sc_fmri, inst->sc_name);
			lcbdata->sc_err = r;
			r = UU_WALK_ERROR;
		goto out;

		case EPERM:
			warn(emsg_snap_perm, snap_initial, inst->sc_fmri);
			lcbdata->sc_err = r;
			r = UU_WALK_ERROR;
			goto out;

		default:
			bad_error("take_snap", r);
		}
	}

	inst->sc_import_state = IMPORT_COMPLETE;

	r = UU_WALK_NEXT;

out:
	return (r);

connaborted:
	warn(gettext("Could not delete svc:/%s:%s "
	    "(repository connection broken).\n"), imp_tsname, inst->sc_name);
	lcbdata->sc_err = ECONNABORTED;
	return (UU_WALK_ERROR);
}

/*
 * If the service is missing, create it, import its properties, and import the
 * instances.  Since the service is brand new, it should be empty, and if we
 * run into any existing entities (SCF_ERROR_EXISTS), abort.
 *
 * If the service exists, we want to upgrade its properties and import the
 * instances.  Before we change anything, though, we want to take the previous
 * snapshots.
 *
 * On success, returns UU_WALK_NEXT.  On failure, returns UU_WALK_ERROR and
 * sets lcbdata->sc_err to
 *   ECONNABORTED - repository connection broken
 *   ENOMEM - out of memory
 *   ENOSPC - svc.configd is out of resources
 *   EPERM - couldn't create temporary service (error printed)
 *	   - couldn't import into temp service (error printed)
 *	   - couldn't create service (error printed)
 *	   - couldn't import dependent (error printed)
 *	   - couldn't take snapshot (error printed)
 *	   - couldn't create instance (error printed)
 *	   - couldn't create, modify, or delete pg (error printed)
 *	   - couldn't create, modify, or delete dependent (error printed)
 *	   - couldn't import instance (error printed)
 *   EROFS - couldn't create temporary service (repository read-only)
 *	   - couldn't import into temporary service (repository read-only)
 *	   - couldn't create service (repository read-only)
 *	   - couldn't import dependent (repository read-only)
 *	   - couldn't create instance (repository read-only)
 *	   - couldn't create, modify, or delete pg or dependent
 *	   - couldn't import instance (repository read-only)
 *   EACCES - couldn't create temporary service (backend access denied)
 *	    - couldn't import into temporary service (backend access denied)
 *	    - couldn't create service (backend access denied)
 *	    - couldn't import dependent (backend access denied)
 *	    - couldn't create instance (backend access denied)
 *	    - couldn't create, modify, or delete pg or dependent
 *	    - couldn't import instance (backend access denied)
 *   EINVAL - service name is invalid (error printed)
 *	    - service name is too long (error printed)
 *	    - s has invalid pgroup (error printed)
 *	    - s has invalid dependent (error printed)
 *	    - instance name is invalid (error printed)
 *	    - instance entity_t is invalid (error printed)
 *   EEXIST - couldn't create temporary service (already exists) (error printed)
 *	    - couldn't import dependent (dependency pg already exists) (printed)
 *	    - dependency collision in dependent service (error printed)
 *   EBUSY - temporary service deleted (error printed)
 *	   - property group added to temporary service (error printed)
 *	   - new property group changed or was deleted (error printed)
 *	   - service was added unexpectedly (error printed)
 *	   - service was deleted unexpectedly (error printed)
 *	   - property group added to new service (error printed)
 *	   - instance added unexpectedly (error printed)
 *	   - instance deleted unexpectedly (error printed)
 *	   - dependent service deleted unexpectedly (error printed)
 *	   - pg was added, changed, or deleted (error printed)
 *	   - dependent pg changed (error printed)
 *	   - temporary instance added, changed, or deleted (error printed)
 *   EBADF - the service is corrupt (error printed)
 *	   - a dependent is corrupt (error printed)
 *	   - an instance is corrupt (error printed)
 *	   - dependent target has a corrupt snapshot (error printed)
 *   -1 - unknown libscf error (error printed)
 */
static int
lscf_service_import(void *v, void *pvt)
{
	entity_t *s = v;
	scf_callback_t cbdata;
	scf_callback_t *lcbdata = pvt;
	scf_scope_t *scope = lcbdata->sc_parent;
	entity_t *inst, linst;
	int r;
	int fresh = 0;
	int masked_svc = 0;

	const char * const s_deleted =
	    gettext("%s was deleted unexpectedly.\n");
	const char * const i_deleted =
	    gettext("%s changed unexpectedly (instance \"%s\" deleted).\n");

	/* Validate the service name */
	if (scf_scope_get_service(scope, s->sc_name, imp_svc) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			return (stash_scferror(lcbdata));

		case SCF_ERROR_INVALID_ARGUMENT:
			warn(gettext("\"%s\" is an invalid service name.  "
			    "Cannot import.\n"), s->sc_name);
			return (stash_scferror(lcbdata));

		case SCF_ERROR_NOT_FOUND:
			break;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_scope_get_service", scf_error());
		}
	}

	/*
	 * If service is found then take the previous snapshots, then import
	 * the properties.
	 *
	 * If not add the service then import the properties.
	 *
	 * Or if the service exists, but is masked then add the service as if
	 * it were new.
	 */
	if (scf_scope_get_service(scope, s->sc_name, imp_svc) != 0 ||
	    (masked_svc = scf_service_is_masked(imp_svc)) == 1) {
		if (masked_svc == 0) {
			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
				break;

			case SCF_ERROR_CONNECTION_BROKEN:
				goto connaborted;

			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_scope_get_service", scf_error());
			}
		}

		if (scf_scope_add_service(scope, s->sc_name, imp_svc) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
				goto connaborted;

			case SCF_ERROR_NO_RESOURCES:
			case SCF_ERROR_BACKEND_READONLY:
			case SCF_ERROR_BACKEND_ACCESS:
				r = stash_scferror(lcbdata);
				goto out;

			case SCF_ERROR_EXISTS:
				warn(gettext("Scope \"%s\" changed unexpectedly"
				    " (service \"%s\" added).\n"),
				    SCF_SCOPE_LOCAL, s->sc_name);
				lcbdata->sc_err = EBUSY;
				goto out;

			case SCF_ERROR_PERMISSION_DENIED:
				warn(gettext("Could not create service \"%s\" "
				    "(permission denied).\n"), s->sc_name);
				goto out;

			case SCF_ERROR_INVALID_ARGUMENT:
			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_scope_add_service", scf_error());
			}
		}

		/*
		 * This is a new service, so we can't take previous snapshots
		 * or upgrade service properties.
		 */
		s->sc_seen = 0;
		fresh = 1;

		cbdata.sc_flags = lcbdata->sc_flags;
	} else {
		/* Clear sc_seen for the instances. */
		if (uu_list_walk(s->sc_u.sc_service.sc_service_instances,
		    clear_int, (void *)offsetof(entity_t, sc_seen),
		    UU_DEFAULT) != 0)
			bad_error("uu_list_walk", uu_error());

		/*
		 * Take previous snapshots for all instances.  Even for ones not
		 * mentioned in the bundle, since we might change their service
		 * properties.
		 */
		s->sc_seen = 1;
		if (scf_iter_service_instances(imp_iter, imp_svc) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_CONNECTION_BROKEN:
				goto connaborted;

			case SCF_ERROR_DELETED:
				warn(s_deleted, s->sc_fmri);
				lcbdata->sc_err = EBUSY;
				r = UU_WALK_ERROR;
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_iter_service_instances",
				    scf_error());
			}
		}

		for (;;) {
			r = scf_iter_next_instance(imp_iter, imp_inst);
			if (r == 0)
				break;
			if (r != 1) {
				switch (scf_error()) {
				case SCF_ERROR_DELETED:
					warn(s_deleted, s->sc_fmri);
					lcbdata->sc_err = EBUSY;
					r = UU_WALK_ERROR;
					goto out;

				case SCF_ERROR_CONNECTION_BROKEN:
					goto connaborted;

				case SCF_ERROR_NOT_BOUND:
				case SCF_ERROR_HANDLE_MISMATCH:
				case SCF_ERROR_INVALID_ARGUMENT:
				case SCF_ERROR_NOT_SET:
				default:
					bad_error("scf_iter_next_instance",
					    scf_error());
				}
			}

			if (scf_instance_get_name(imp_inst, imp_str,
			    imp_str_sz) < 0) {
				switch (scf_error()) {
				case SCF_ERROR_DELETED:
					continue;

				case SCF_ERROR_CONNECTION_BROKEN:
					goto connaborted;

				case SCF_ERROR_NOT_SET:
				case SCF_ERROR_NOT_BOUND:
				default:
					bad_error("scf_instance_get_name",
					    scf_error());
				}
			}

			if (g_verbose)
				warn(gettext(
				    "Taking \"%s\" snapshot for svc:/%s:%s.\n"),
				    snap_previous, s->sc_name, imp_str);

			r = take_snap(imp_inst, snap_previous, imp_snap);
			switch (r) {
			case 0:
				break;

			case ECANCELED:
				continue;

			case ECONNABORTED:
				goto connaborted;

			case EPERM:
				warn(gettext("Could not take \"%s\" snapshot "
				    "of svc:/%s:%s (permission denied).\n"),
				    snap_previous, s->sc_name, imp_str);
				lcbdata->sc_err = r;
				return (UU_WALK_ERROR);

			case ENOSPC:
			case -1:
				lcbdata->sc_err = r;
				r = UU_WALK_ERROR;
				goto out;

			default:
				bad_error("take_snap", r);
			}

			linst.sc_name = imp_str;
			inst = uu_list_find(
			    s->sc_u.sc_service.sc_service_instances, &linst,
			    NULL, NULL);
			if (inst != NULL) {
				inst->sc_import_state = IMPORT_PREVIOUS;
				inst->sc_seen = 1;
			}
		}

		/*
		 * Create the new instances and take previous snapshots of
		 * them.  This is not necessary, but it maximizes data
		 * preservation.
		 */
		for (inst = uu_list_first(
		    s->sc_u.sc_service.sc_service_instances);
		    inst != NULL;
		    inst = uu_list_next(s->sc_u.sc_service.sc_service_instances,
		    inst)) {
			if (inst->sc_seen)
				continue;

			if (scf_service_add_instance(imp_svc, inst->sc_name,
			    imp_inst) != 0) {
				switch (scf_error()) {
				case SCF_ERROR_CONNECTION_BROKEN:
					goto connaborted;

				case SCF_ERROR_BACKEND_READONLY:
				case SCF_ERROR_BACKEND_ACCESS:
				case SCF_ERROR_NO_RESOURCES:
					r = stash_scferror(lcbdata);
					goto out;

				case SCF_ERROR_EXISTS:
					warn(gettext("%s changed unexpectedly "
					    "(instance \"%s\" added).\n"),
					    s->sc_fmri, inst->sc_name);
					lcbdata->sc_err = EBUSY;
					r = UU_WALK_ERROR;
					goto out;

				case SCF_ERROR_INVALID_ARGUMENT:
					warn(gettext("Service \"%s\" has "
					    "instance with invalid name "
					    "\"%s\".\n"), s->sc_name,
					    inst->sc_name);
					r = stash_scferror(lcbdata);
					goto out;

				case SCF_ERROR_PERMISSION_DENIED:
					warn(gettext("Could not create "
					    "instance \"%s\" in %s (permission "
					    "denied).\n"),
					    inst->sc_name, s->sc_fmri);
					r = stash_scferror(lcbdata);
					goto out;

				case SCF_ERROR_HANDLE_MISMATCH:
				case SCF_ERROR_NOT_BOUND:
				case SCF_ERROR_NOT_SET:
				default:
					bad_error("scf_service_add_instance",
					    scf_error());
				}
			}

			if (g_verbose)
				warn(gettext("Taking \"%s\" snapshot for "
				    "new service %s.\n"), snap_previous,
				    inst->sc_fmri);
			r = take_snap(imp_inst, snap_previous, imp_snap);
			switch (r) {
			case 0:
				break;

			case ECANCELED:
				warn(i_deleted, s->sc_fmri, inst->sc_name);
				lcbdata->sc_err = EBUSY;
				r = UU_WALK_ERROR;
				goto out;

			case ECONNABORTED:
				goto connaborted;

			case EPERM:
				warn(emsg_snap_perm, snap_previous,
				    inst->sc_fmri);
				lcbdata->sc_err = r;
				r = UU_WALK_ERROR;
				goto out;

			case ENOSPC:
			case -1:
				r = UU_WALK_ERROR;
				goto out;

			default:
				bad_error("take_snap", r);
			}
		}

		s->sc_import_state = IMPORT_PREVIOUS;
	}

	/*
	 * If the service was seen then we need to determine if the
	 * property groups in that service are in the manifest.
	 */
	if (s->sc_seen == 1)
		if (process_old_pgs(s, imp_svc, SCF_DECORATION_MANIFEST) != 0)
			goto out;

	s->sc_import_state = IMPORT_PROP_BEGUN;

	/* import service properties */

	r = lscf_import_service_pgs(imp_svc, s->sc_fmri, s, lcbdata->sc_flags);
	switch (_lscf_import_err(r, s->sc_fmri)) {
	case IMPORT_NEXT:
		break;
	case IMPORT_OUT:
		goto out;
	case IMPORT_BAD:
	default:
		bad_error("lscf_import_service_pgs", r);
	}

	cbdata.sc_handle = lcbdata->sc_handle;
	cbdata.sc_parent = imp_svc;
	cbdata.sc_service = 1;
	cbdata.sc_trans = NULL;
	cbdata.sc_flags = 0;
	if (s->sc_op != SVCCFG_OP_NONE) {
		switch (s->sc_op) {
		case SVCCFG_OP_IMPORT :
			cbdata.sc_flags |= SCI_OP_IMPORT;
			break;
		case SVCCFG_OP_APPLY :
			cbdata.sc_flags |= SCI_OP_APPLY;
			break;
		default :
			break;
		}
	}
	if (uu_list_walk(s->sc_dependents, lscf_dependent_import, &cbdata,
	    UU_DEFAULT) != 0) {
		if (uu_error() != UU_ERROR_CALLBACK_FAILED)
			bad_error("uu_list_walk", uu_error());

		if (cbdata.sc_err == ECONNABORTED)
			goto connaborted;

		lcbdata->sc_err = cbdata.sc_err;
		r = UU_WALK_ERROR;

		goto out;
	}

	s->sc_import_state = IMPORT_PROP_DONE;

instances:
	/* import instances */
	cbdata.sc_handle = lcbdata->sc_handle;
	cbdata.sc_parent = imp_svc;
	cbdata.sc_service = 1;
	cbdata.sc_flags = lcbdata->sc_flags | (fresh ? SCI_FRESH : 0);
	cbdata.sc_general = NULL;

	if (uu_list_walk(s->sc_u.sc_service.sc_service_instances,
	    lscf_instance_import, &cbdata, UU_DEFAULT) != 0) {
		if (uu_error() != UU_ERROR_CALLBACK_FAILED)
			bad_error("uu_list_walk", uu_error());

		lcbdata->sc_err = cbdata.sc_err;
		if (cbdata.sc_err == ECONNABORTED)
			goto connaborted;
		r = UU_WALK_ERROR;
		goto out;
	}

	s->sc_import_state = IMPORT_COMPLETE;
	r = UU_WALK_NEXT;

out:
	return (r);

connaborted:
	warn(gettext("Could not delete svc:/%s "
	    "(repository connection broken).\n"), imp_tsname);
	lcbdata->sc_err = ECONNABORTED;
	return (UU_WALK_ERROR);
}

static const char *
import_progress(int st)
{
	switch (st) {
	case 0:
		return (gettext("not reached."));

	case IMPORT_PREVIOUS:
		return (gettext("previous snapshot taken."));

	case IMPORT_PROP_BEGUN:
		return (gettext("some properties imported."));

	case IMPORT_PROP_DONE:
		return (gettext("properties imported."));

	case IMPORT_COMPLETE:
		return (gettext("imported."));

	case IMPORT_REFRESHED:
		return (gettext("refresh requested."));

	default:
#ifndef NDEBUG
		(void) fprintf(stderr, "%s:%d: Unknown entity state %d.\n",
		    __FILE__, __LINE__, st);
#endif
		abort();
		/* NOTREACHED */
	}
}

/*
 * Refresh if the given instance is a hand crafted instance (i.e. instance
 * created via libscf or svccfg rather than manifest delivered instance).
 *
 * Returns
 *   0 - success
 *     - entity was deleted (error printed)
 *     - backend denied access (error printed)
 *   ECONNABORTED - repository connection broken (error printed)
 *   EPERM - permission denied (error printed)
 *   ENOSPC - repository out of resource (error printed)
 *   -1 - unknown libscf error (error printed)
 */

/* ARGSUSED0 */
static int
imp_refresh_handcrafted_cb(void *data, scf_walkinfo_t *wip)
{
	scf_decoration_t *dec;
	int r;

	assert(wip->inst != NULL);

	if ((dec = scf_decoration_create(g_hndl)) == NULL)
		scfdie();

	/* Do nothing if instance is not administratively created */
	if (scf_instance_get_decoration(wip->inst, SCF_DECORATION_ADMIN, dec) &&
	    scf_error() != SCF_ERROR_CONSTRAINT_VIOLATED) {
		scf_decoration_destroy(dec);
		return (0);
	}
	scf_decoration_destroy(dec);

	r = refresh_entity(0, (void *) wip->inst, wip->fmri, imp_inst,
	    imp_iter, imp_str);
	switch (r) {
	case 0:
		break;

	case ECONNABORTED:
		return (r);

	case ECANCELED:
		warn(gettext("Could not refresh %s (deleted).\n"), wip->fmri);
		return (0);

	case EACCES:
		if (g_verbose)
			warn(gettext("Could not refresh %s "
			    "(backend access denied).\n"), wip->fmri);
		return (0);

	case EPERM:
		warn(gettext("Could not refresh %s "
		    "(permission denied).\n"), wip->fmri);
		return (r);

	case ENOSPC:
		warn(gettext("Could not refresh %s "
		    "(repository server out of resources).\n"), wip->fmri);
		return (r);

	case -1:
		scfwarn();
		return (r);

	default:
		bad_error("refresh_entity", r);
	}

	return (0);
}

/*
 * Returns
 *   0 - success
 *     - fmri wasn't found (error printed)
 *     - entity was deleted (error printed)
 *     - backend denied access (error printed)
 *   ENOMEM - out of memory (error printed)
 *   ECONNABORTED - repository connection broken (error printed)
 *   EPERM - permission denied (error printed)
 *   -1 - unknown libscf error (error printed)
 */
static int
imp_refresh_fmri(const char *fmri, const char *name, const char *d_fmri)
{
	scf_error_t serr;
	void *ent;
	int issvc;
	int r;

	const char *deleted = gettext("Could not refresh %s (deleted).\n");
	const char *dpt_deleted = gettext("Could not refresh %s "
	    "(dependent \"%s\" of %s) (deleted).\n");

	serr = fmri_to_entity(g_hndl, fmri, &ent, &issvc);
	switch (serr) {
	case SCF_ERROR_NONE:
		break;

	case SCF_ERROR_NO_MEMORY:
		if (name == NULL)
			warn(gettext("Could not refresh %s (out of memory).\n"),
			    fmri);
		else
			warn(gettext("Could not refresh %s "
			    "(dependent \"%s\" of %s) (out of memory).\n"),
			    fmri, name, d_fmri);
		return (ENOMEM);

	case SCF_ERROR_NOT_FOUND:
		if (name == NULL)
			warn(deleted, fmri);
		else
			warn(dpt_deleted, fmri, name, d_fmri);
		return (0);

	case SCF_ERROR_INVALID_ARGUMENT:
	case SCF_ERROR_CONSTRAINT_VIOLATED:
	default:
		bad_error("fmri_to_entity", serr);
	}

	r = refresh_entity(issvc, ent, fmri, imp_inst, imp_iter, imp_str);
	switch (r) {
	case 0:
		break;

	case ECONNABORTED:
		if (name != NULL)
			warn(gettext("Could not refresh %s "
			    "(dependent \"%s\" of %s) "
			    "(repository connection broken).\n"), fmri, name,
			    d_fmri);
		return (r);

	case ECANCELED:
		if (name == NULL)
			warn(deleted, fmri);
		else
			warn(dpt_deleted, fmri, name, d_fmri);
		return (0);

	case EACCES:
		if (!g_verbose)
			return (0);
		if (name == NULL)
			warn(gettext("Could not refresh %s "
			    "(backend access denied).\n"), fmri);
		else
			warn(gettext("Could not refresh %s "
			    "(dependent \"%s\" of %s) "
			    "(backend access denied).\n"), fmri, name, d_fmri);
		return (0);

	case EPERM:
		if (name == NULL)
			warn(gettext("Could not refresh %s "
			    "(permission denied).\n"), fmri);
		else
			warn(gettext("Could not refresh %s "
			    "(dependent \"%s\" of %s) "
			    "(permission denied).\n"), fmri, name, d_fmri);
		return (r);

	case ENOSPC:
		if (name == NULL)
			warn(gettext("Could not refresh %s "
			    "(repository server out of resources).\n"),
			    fmri);
		else
			warn(gettext("Could not refresh %s "
			    "(dependent \"%s\" of %s) "
			    "(repository server out of resources).\n"),
			    fmri, name, d_fmri);
		return (r);

	case -1:
		scfwarn();
		return (r);

	default:
		bad_error("refresh_entity", r);
	}

	if (issvc)
		scf_service_destroy(ent);
	else
		scf_instance_destroy(ent);

	return (0);
}

static int
alloc_imp_globals()
{
	int r;

	const char * const emsg_nomem = gettext("Out of memory.\n");
	const char * const emsg_nores =
	    gettext("svc.configd is out of resources.\n");

	imp_str_sz = ((max_scf_name_len > max_scf_fmri_len) ?
	    max_scf_name_len : max_scf_fmri_len) + 1;

	if ((imp_scope = scf_scope_create(g_hndl)) == NULL ||
	    (imp_svc = scf_service_create(g_hndl)) == NULL ||
	    (imp_tsvc = scf_service_create(g_hndl)) == NULL ||
	    (imp_inst = scf_instance_create(g_hndl)) == NULL ||
	    (imp_tinst = scf_instance_create(g_hndl)) == NULL ||
	    (imp_snap = scf_snapshot_create(g_hndl)) == NULL ||
	    (imp_lisnap = scf_snapshot_create(g_hndl)) == NULL ||
	    (imp_tlisnap = scf_snapshot_create(g_hndl)) == NULL ||
	    (imp_rsnap = scf_snapshot_create(g_hndl)) == NULL ||
	    (imp_snpl = scf_snaplevel_create(g_hndl)) == NULL ||
	    (imp_rsnpl = scf_snaplevel_create(g_hndl)) == NULL ||
	    (imp_pg = scf_pg_create(g_hndl)) == NULL ||
	    (imp_pg2 = scf_pg_create(g_hndl)) == NULL ||
	    (imp_prop = scf_property_create(g_hndl)) == NULL ||
	    (imp_iter = scf_iter_create(g_hndl)) == NULL ||
	    (imp_rpg_iter = scf_iter_create(g_hndl)) == NULL ||
	    (imp_up_iter = scf_iter_create(g_hndl)) == NULL ||
	    (imp_tx = scf_transaction_create(g_hndl)) == NULL ||
	    (imp_str = malloc(imp_str_sz)) == NULL ||
	    (imp_tsname = malloc(max_scf_name_len + 1)) == NULL ||
	    (imp_fe1 = malloc(max_scf_fmri_len + 1)) == NULL ||
	    (imp_fe2 = malloc(max_scf_fmri_len + 1)) == NULL ||
	    (imp_deleted_dpts = uu_list_create(string_pool, NULL, 0)) == NULL ||
	    (imp_bundle = malloc(max_scf_value_len + 1)) == NULL ||
	    (ud_inst = scf_instance_create(g_hndl)) == NULL ||
	    (ud_snpl = scf_snaplevel_create(g_hndl)) == NULL ||
	    (ud_pg = scf_pg_create(g_hndl)) == NULL ||
	    (ud_cur_depts_pg = scf_pg_create(g_hndl)) == NULL ||
	    (ud_run_dpts_pg = scf_pg_create(g_hndl)) == NULL ||
	    (ud_prop = scf_property_create(g_hndl)) == NULL ||
	    (ud_dpt_prop = scf_property_create(g_hndl)) == NULL ||
	    (ud_val = scf_value_create(g_hndl)) == NULL ||
	    (ud_iter = scf_iter_create(g_hndl)) == NULL ||
	    (ud_iter2 = scf_iter_create(g_hndl)) == NULL ||
	    (ud_tx = scf_transaction_create(g_hndl)) == NULL ||
	    (ud_ctarg = malloc(max_scf_value_len + 1)) == NULL ||
	    (ud_oldtarg = malloc(max_scf_value_len + 1)) == NULL ||
	    (ud_name = malloc(max_scf_name_len + 1)) == NULL) {
		if (scf_error() == SCF_ERROR_NO_RESOURCES)
			warn(emsg_nores);
		else
			warn(emsg_nomem);

		return (-1);
	}

	r = load_init();
	switch (r) {
	case 0:
		break;

	case ENOMEM:
		warn(emsg_nomem);
		return (-1);

	default:
		bad_error("load_init", r);
	}

	return (0);
}

static void
free_imp_globals()
{
	pgroup_t *old_dpt;
	void *cookie;

	load_fini();

	free(ud_ctarg);
	free(ud_oldtarg);
	free(ud_name);
	ud_ctarg = ud_oldtarg = ud_name = NULL;

	scf_transaction_destroy(ud_tx);
	ud_tx = NULL;
	scf_iter_destroy(ud_iter);
	scf_iter_destroy(ud_iter2);
	ud_iter = ud_iter2 = NULL;
	scf_value_destroy(ud_val);
	ud_val = NULL;
	scf_property_destroy(ud_prop);
	scf_property_destroy(ud_dpt_prop);
	ud_prop = ud_dpt_prop = NULL;
	scf_pg_destroy(ud_pg);
	scf_pg_destroy(ud_cur_depts_pg);
	scf_pg_destroy(ud_run_dpts_pg);
	ud_pg = ud_cur_depts_pg = ud_run_dpts_pg = NULL;
	scf_snaplevel_destroy(ud_snpl);
	ud_snpl = NULL;
	scf_instance_destroy(ud_inst);
	ud_inst = NULL;

	free(imp_str);
	free(imp_tsname);
	free(imp_fe1);
	free(imp_fe2);
	free(imp_bundle);
	imp_str = imp_tsname = imp_fe1 = imp_fe2 = imp_bundle = NULL;

	cookie = NULL;
	while ((old_dpt = uu_list_teardown(imp_deleted_dpts, &cookie)) !=
	    NULL) {
		free((char *)old_dpt->sc_pgroup_name);
		free((char *)old_dpt->sc_pgroup_fmri);
		internal_pgroup_free(old_dpt);
	}
	uu_list_destroy(imp_deleted_dpts);

	scf_transaction_destroy(imp_tx);
	imp_tx = NULL;
	scf_iter_destroy(imp_iter);
	scf_iter_destroy(imp_rpg_iter);
	scf_iter_destroy(imp_up_iter);
	imp_iter = imp_rpg_iter = imp_up_iter = NULL;
	scf_property_destroy(imp_prop);
	imp_prop = NULL;
	scf_pg_destroy(imp_pg);
	scf_pg_destroy(imp_pg2);
	imp_pg = imp_pg2 = NULL;
	scf_snaplevel_destroy(imp_snpl);
	scf_snaplevel_destroy(imp_rsnpl);
	imp_snpl = imp_rsnpl = NULL;
	scf_snapshot_destroy(imp_snap);
	scf_snapshot_destroy(imp_lisnap);
	scf_snapshot_destroy(imp_tlisnap);
	scf_snapshot_destroy(imp_rsnap);
	imp_snap = imp_lisnap = imp_tlisnap = imp_rsnap = NULL;
	scf_instance_destroy(imp_inst);
	scf_instance_destroy(imp_tinst);
	imp_inst = imp_tinst = NULL;
	scf_service_destroy(imp_svc);
	scf_service_destroy(imp_tsvc);
	imp_svc = imp_tsvc = NULL;
	scf_scope_destroy(imp_scope);
	imp_scope = NULL;

	load_fini();
}

int
lscf_bundle_import(bundle_t *bndl, const char *filename, uint_t flags)
{
	scf_callback_t cbdata;
	int result = 0;
	entity_t *svc, *inst;
	uu_list_t *insts;
	int r = 0;
	pgroup_t *old_dpt;
	int annotation_set = 0;
	scf_decoration_layer_t layer;

	char *bname;
	size_t	sz = max_scf_value_len + 1;

	const char * const emsg_nomem = gettext("Out of memory.\n");
	const char * const emsg_nores =
	    gettext("svc.configd is out of resources.\n");

	bname = get_bundle_layer(filename, SVCCFG_MANIFEST, &layer);

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (alloc_imp_globals())
		goto out;

	if (strlcpy(imp_bundle, bname, sz) >= sz)
		uu_die(gettext("Unable to copy bundle file %s\n"), bname);

	uu_free(bname);

	if (scf_handle_get_scope(g_hndl, SCF_SCOPE_LOCAL, imp_scope) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			warn(gettext("Repository connection broken.\n"));
			repository_teardown();
			result = -1;
			goto out;

		case SCF_ERROR_NOT_FOUND:
		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_HANDLE_MISMATCH:
		default:
			bad_error("scf_handle_get_scope", scf_error());
		}
	}

#ifndef NATIVE_BUILD
	/*
	 * Check the contract information to make sure that import
	 * is running from within startd.  If not then message
	 * and restart manifest-import to complete the import
	 */
	if (est->sc_repo_filename == NULL &&
	    layer == SCF_DECORATION_MANIFEST &&
	    (r = contract_pr_check_decorations(SCF_INSTANCE_EMI, -1,
	    CT_CREATOR_STARTD, NULL)) ==  1 &&
	    (r = contract_pr_check_decorations(SCF_INSTANCE_MI, -1,
	    CT_CREATOR_STARTD, NULL)) == 1 &&
	    (r = contract_pr_check_decorations(ORCL_TESTING, -1,
	    CT_CREATOR_CTRUN, NULL)) == 1)  {
		/*
		 * The ORCL_TESTING test is a workaround for the case
		 * where running manifest-import from the command line
		 * until the synchronous restart can be implemented to
		 * help out testing.
		 */
		result = 1;
		if (g_restart_mi)
			goto out;

		warn(gettext("Restarting svc:/system/manifest-import\n "
		    "The manifest being imported is from a standard "
		    "location and should be imported with the "
		    " command : svcadm restart "
		    "svc:/system/manifest-import\n"));

		if (smf_restart_instance(SCF_INSTANCE_MI) != 0) {
			uu_die(gettext("Cannot restart %s\n"),
			    SCF_INSTANCE_MI);
		}

		g_restart_mi = 1;
		goto out;
	}
	if (r == -1) {
		warn(gettext("Failed to get process contract terms: %s\n"),
		    strerror(errno));
		result = -1;
		goto out;
	}
#endif

	if (_scf_set_tx_decoration(g_hndl, imp_bundle, layer) != 0)
		bad_error("_scf_set_tx_decoration", scf_error());

	/* Set up the auditing annotation. */
	if (_scf_set_annotation(g_hndl) == 0) {
		annotation_set = 1;
	} else {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			warn(gettext("Repository connection broken.\n"));
			repository_teardown();
			result = -1;
			goto out;

		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NO_RESOURCES:
		case SCF_ERROR_INTERNAL:
			bad_error("_scf_set_annotation", scf_error());
			/* NOTREACHED */

		default:
			/*
			 * Do not terminate import because of inability to
			 * generate annotation audit event.
			 */
			warn(gettext("_scf_set_annotation() unexpectedly "
			    "failed with return code of %d\n"), scf_error());
			break;
		}
	}

	/*
	 * Clear the sc_import_state's of all services & instances so we can
	 * report how far we got if we fail.
	 */
	for (svc = uu_list_first(bndl->sc_bundle_services);
	    svc != NULL;
	    svc = uu_list_next(bndl->sc_bundle_services, svc)) {
		svc->sc_import_state = 0;

		if (uu_list_walk(svc->sc_u.sc_service.sc_service_instances,
		    clear_int, (void *)offsetof(entity_t, sc_import_state),
		    UU_DEFAULT) != 0)
			bad_error("uu_list_walk", uu_error());
	}

	cbdata.sc_handle = g_hndl;
	cbdata.sc_parent = imp_scope;
	cbdata.sc_flags = flags;
	cbdata.sc_general = NULL;

	if (uu_list_walk(bndl->sc_bundle_services, lscf_service_import,
	    &cbdata, UU_DEFAULT) == 0) {
		/* Success.  Refresh everything. */

		if (flags & SCI_NOREFRESH || no_refresh) {
			no_refresh = 0;
			result = 0;
			goto out;
		}

		/*
		 * For a given service, refresh instances specified in the
		 * bundle and hand crafted instances rather than simply iterate
		 * the service and refresh all instances. The reason is we don't
		 * want to refresh instance(s) that are going to be deleted
		 * because the updated manifest no longer support those
		 * instances.
		 *
		 * Dependent services specified in the bundle are also
		 * refreshed.
		 */
		for (svc = uu_list_first(bndl->sc_bundle_services);
		    svc != NULL;
		    svc = uu_list_next(bndl->sc_bundle_services, svc)) {
			pgroup_t *dpt;

			/* Refresh hand crafted services */
			if ((r = scf_walk_fmri(g_hndl, 1,
			    (char **)&svc->sc_fmri, SCF_WALK_MULTIPLE |
			    SCF_WALK_EMPTY, imp_refresh_handcrafted_cb, NULL,
			    NULL, semerr)) != 0) {
				bad_error("scf_walk_fmri", r);
			}

			/* Refresh instances specified in manifest */
			insts = svc->sc_u.sc_service.sc_service_instances;

			for (inst = uu_list_first(insts);
			    inst != NULL;
			    inst = uu_list_next(insts, inst)) {
				r = imp_refresh_fmri(inst->sc_fmri, NULL, NULL);
				switch (r) {
				case 0:
					break;

				case ENOMEM:
				case ECONNABORTED:
				case EPERM:
				case -1:
					goto progress;

				default:
					bad_error("imp_refresh_fmri", r);
				}

				inst->sc_import_state = IMPORT_REFRESHED;

				for (dpt = uu_list_first(inst->sc_dependents);
				    dpt != NULL;
				    dpt = uu_list_next(inst->sc_dependents,
				    dpt))
					if (imp_refresh_fmri(
					    dpt->sc_pgroup_fmri,
					    dpt->sc_pgroup_name,
					    inst->sc_fmri) != 0)
						goto progress;
			}

			for (dpt = uu_list_first(svc->sc_dependents);
			    dpt != NULL;
			    dpt = uu_list_next(svc->sc_dependents, dpt))
				if (imp_refresh_fmri(dpt->sc_pgroup_fmri,
				    dpt->sc_pgroup_name, svc->sc_fmri) != 0)
					goto progress;
		}

		for (old_dpt = uu_list_first(imp_deleted_dpts);
		    old_dpt != NULL;
		    old_dpt = uu_list_next(imp_deleted_dpts, old_dpt))
			if (imp_refresh_fmri(old_dpt->sc_pgroup_fmri,
			    old_dpt->sc_pgroup_name,
			    old_dpt->sc_parent->sc_fmri) != 0)
				goto progress;

		result = 0;
		goto out;
	}

	if (uu_error() != UU_ERROR_CALLBACK_FAILED)
		bad_error("uu_list_walk", uu_error());

printerr:
	/* If the error hasn't been printed yet, do so here. */
	switch (cbdata.sc_err) {
	case ECONNABORTED:
		warn(gettext("Repository connection broken.\n"));
		break;

	case ENOMEM:
		warn(emsg_nomem);
		break;

	case ENOSPC:
		warn(emsg_nores);
		break;

	case EROFS:
		warn(gettext("Repository is read-only.\n"));
		break;

	case EACCES:
		warn(gettext("Repository backend denied access.\n"));
		break;

	case EPERM:
	case EINVAL:
	case EEXIST:
	case EBUSY:
	case EBADF:
	case -1:
		break;

	default:
		bad_error("lscf_service_import", cbdata.sc_err);
	}

progress:
	warn(gettext("Import of %s failed.  Progress:\n"), filename);

	for (svc = uu_list_first(bndl->sc_bundle_services);
	    svc != NULL;
	    svc = uu_list_next(bndl->sc_bundle_services, svc)) {
		insts = svc->sc_u.sc_service.sc_service_instances;

		warn(gettext("  Service \"%s\": %s\n"), svc->sc_name,
		    import_progress(svc->sc_import_state));

		for (inst = uu_list_first(insts);
		    inst != NULL;
		    inst = uu_list_next(insts, inst))
			warn(gettext("    Instance \"%s\": %s\n"),
			    inst->sc_name,
			    import_progress(inst->sc_import_state));
	}

	if (cbdata.sc_err == ECONNABORTED)
		repository_teardown();


	result = -1;

out:
	if (annotation_set != 0) {
		/* Turn off annotation.  It is no longer needed. */
		(void) _scf_unset_annotation(g_hndl);
	}

	if (_scf_unset_tx_decoration(g_hndl) != 0) {
		bad_error("_scf_unset_tx_decoration", scf_error());
	}

	free_imp_globals();

	return (result);
}

/*
 * The global imp_svc and imp_inst should be set by the caller in the
 * check to make sure the service and instance exist that the apply is
 * working on.
 */
static int
lscf_dependent_apply(void *dpg, void *e)
{
	scf_callback_t cb;
	pgroup_t *dpt_pgroup = dpg;
	pgroup_t *deldpt;
	entity_t *ent = e;
	int tissvc;
	void *sc_ent, *tent;
	scf_error_t serr;
	int r;

	const char * const dependents = "dependents";
	const int issvc = (ent->sc_etype == SVCCFG_SERVICE_OBJECT);

	if (issvc)
		sc_ent = imp_svc;
	else
		sc_ent = imp_inst;

	if (entity_get_running_pg(sc_ent, issvc, dependents, imp_pg,
	    imp_iter, imp_tinst, imp_snap, imp_snpl) != 0 ||
	    scf_pg_get_property(imp_pg, dpt_pgroup->sc_pgroup_name,
	    imp_prop) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
		case SCF_ERROR_DELETED:
			break;

		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_NOT_SET:
		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		default:
			bad_error("entity_get_pg", scf_error());
		}
	} else {
		/*
		 * Found the dependents/<wip dep> so check to
		 * see if the service is different.  If so
		 * store the service for later refresh, and
		 * delete the wip dependency from the service
		 */
		if (scf_property_get_value(imp_prop, ud_val) != 0) {
			switch (scf_error()) {
				case SCF_ERROR_DELETED:
					break;

				case SCF_ERROR_CONNECTION_BROKEN:
				case SCF_ERROR_NOT_SET:
				case SCF_ERROR_INVALID_ARGUMENT:
				case SCF_ERROR_HANDLE_MISMATCH:
				case SCF_ERROR_NOT_BOUND:
				default:
					bad_error("scf_property_get_value",
					    scf_error());
			}
		}

		if (scf_value_get_as_string(ud_val, ud_oldtarg,
		    max_scf_value_len + 1) < 0)
			bad_error("scf_value_get_as_string", scf_error());

		r = fmri_equal(dpt_pgroup->sc_pgroup_fmri, ud_oldtarg);
		switch (r) {
		case 1:
			break;
		case 0:
			if ((serr = fmri_to_entity(g_hndl, ud_oldtarg, &tent,
			    &tissvc)) != SCF_ERROR_NONE) {
				if (serr == SCF_ERROR_NOT_FOUND) {
					break;
				} else {
					bad_error("fmri_to_entity", serr);
				}
			}

			if (entity_get_pg(tent, tissvc,
			    dpt_pgroup->sc_pgroup_name, imp_pg) != 0) {
				serr = scf_error();
				if (serr == SCF_ERROR_NOT_FOUND ||
				    serr == SCF_ERROR_DELETED) {
					break;
				} else {
					bad_error("entity_get_pg", scf_error());
				}
			}

			if (scf_pg_delete(imp_pg) != 0) {
				serr = scf_error();
				if (serr == SCF_ERROR_NOT_FOUND ||
				    serr == SCF_ERROR_DELETED) {
					break;
				} else {
					bad_error("scf_pg_delete", scf_error());
				}
			}

			deldpt = internal_pgroup_new();
			if (deldpt == NULL)
				return (ENOMEM);
			deldpt->sc_pgroup_name =
			    strdup(dpt_pgroup->sc_pgroup_name);
			deldpt->sc_pgroup_fmri = strdup(ud_oldtarg);
			if (deldpt->sc_pgroup_name == NULL ||
			    deldpt->sc_pgroup_fmri == NULL)
				return (ENOMEM);
			deldpt->sc_parent = (entity_t *)ent;
			if (uu_list_insert_after(imp_deleted_dpts, NULL,
			    deldpt) != 0)
				uu_die(gettext("libuutil error: %s\n"),
				    uu_strerror(uu_error()));

			break;
		default:
			bad_error("fmri_equal", r);
		}
	}

	cb.sc_handle = g_hndl;
	cb.sc_parent = ent;
	cb.sc_service = ent->sc_etype == SVCCFG_SERVICE_OBJECT;
	cb.sc_source_fmri = ent->sc_fmri;
	cb.sc_target_fmri = ent->sc_fmri;
	cb.sc_trans = NULL;
	cb.sc_flags = SCI_FORCE;

	if (lscf_dependent_import(dpt_pgroup, &cb) != UU_WALK_NEXT)
		return (UU_WALK_ERROR);

	r = imp_refresh_fmri(dpt_pgroup->sc_pgroup_fmri, NULL, NULL);
	switch (r) {
	case 0:
		break;

	case ENOMEM:
	case ECONNABORTED:
	case EPERM:
	case -1:
		warn(gettext("Unable to refresh \"%s\"\n"),
		    dpt_pgroup->sc_pgroup_fmri);
		return (UU_WALK_ERROR);

	default:
		bad_error("imp_refresh_fmri", r);
	}

	return (UU_WALK_NEXT);
}

/*
 * Set layer decoration for this application:
 * - site profiles go to SCF_DECORATION_SITE_PROFILE
 * - system profiles go to SCF_DECORATION_SYSTEM_PROFILE
 * - other profiles go to SCF_DECORATION_ADMIN
 *
 * Return the full realpath to the file for use by the calling
 * function.  It is the calling functions responsibility to free
 * the memory from this allocation.
 *
 * Note that we're using 'file' to strcmp against system profiles.
 * There are two issues:
 *
 * 1) We can't guarantee 'file' is a full path so the manifestfile
 *    property value can be wrong.
 * 2) How do we determine whether to strcmp 'file' or 'bname'?
 */
char *
get_bundle_layer(const char *file, bundle_type_t btype,
    scf_decoration_layer_t *layer)
{
	const char *base;
	char *bname, *fname, *bfile, *locfile, *rpath;
	char *lnkname = NULL;
	size_t	base_sz = 0;
	size_t  file_base_sz = 0;
	struct stat st;

	/*
	 * If this is a symbolic link then we want to use the link's
	 * name instead of the file linked to.
	 */
	if (lstat(file, &st) == -1)
		uu_die(gettext("Unable to stat bundle file %s\n"), file);

	if ((st.st_mode & S_IFMT) == S_IFLNK) {
		locfile = safe_strdup(file);

		lnkname = basename(locfile);
		rpath = dirname(locfile);
	} else {
		rpath = (char *)file;
	}

	bname = uu_zalloc(PATH_MAX + 1);
	if (bname == NULL || realpath(rpath, bname) == NULL) {
		uu_die(gettext("Unable to create the real path of the "
		    "manifest file \"%s\" : %d\n"), locfile, errno);
	}

	if (lnkname != NULL) {
		(void) strcat(bname, "/");
		(void) strcat(bname, lnkname);
		free(locfile);
	}

	if (btype != SVCCFG_MANIFEST && btype != SVCCFG_PROFILE) {
		uu_die(gettext("Attempting to get bundle layer for a "
		    "bundle that is not a profile or manifest \"%s\"\n"),
		    file);
	}

	/*
	 * Must remove the PKG_INSTALL_ROOT, point to the correct
	 * directory after install
	 *
	 * This could either be SVCCFG_MANIFEST_PREFIX or the repository
	 * subcommand -p <prefix> or finally PKG_INSTALL_ROOT
	 */
	base = est->sc_bundle_prefix;

	if (base != NULL) {
		if (strncmp(bname, base, strlen(base)) == 0)
			base_sz = strlen(base);

		if (strncmp(file, base, strlen(base)) == 0)
			file_base_sz = strlen(base);
	}

	fname = safe_strdup(bname + base_sz);
	bfile = safe_strdup(file + file_base_sz);

	uu_free(bname);

	if (btype == SVCCFG_MANIFEST) {
		if (strncmp(fname, LIBSVC_DIR, strlen(LIBSVC_DIR)) == 0 ||
		    strncmp(fname, VARSVC_DIR, strlen(VARSVC_DIR)) == 0) {
			*layer = SCF_DECORATION_MANIFEST;
		} else {
			*layer = SCF_DECORATION_ADMIN;
		}

		goto out;
	}

	if (strcmp(fname, ETCSITEPROFILE_FILE) == 0 ||
	    strcmp(fname, VARSITEPROFILE_FILE) == 0 ||
	    strncmp(fname, ETCSITEPROFILE_DIR,
	    strlen(ETCSITEPROFILE_DIR)) == 0) {
		*layer = SCF_DECORATION_SITE_PROFILE;
	} else if (strcmp(bfile, ETCSYSPROFILE_GENERIC) == 0 ||
	    strcmp(bfile, ETCSYSPROFILE_PLATFORM) == 0) {
		*layer = SCF_DECORATION_SYSTEM_PROFILE;
	} else {
		*layer = SCF_DECORATION_ADMIN;
	}

out:
	free(bfile);

	return (fname);
}

/*
 * Returns
 *   0 - success
 *   -1 - lscf_import_instance_pgs() failed.
 */
int
lscf_bundle_apply(bundle_t *bndl, const char *file)
{
	pgroup_t *old_dpt;
	entity_t *svc, *inst;
	int annotation_set = 0;
	int ret = 0;
	int r = 0;

	scf_decoration_layer_t layer;
	char *bname;
	size_t	sz = max_scf_value_len + 1;

	bname = get_bundle_layer(file, SVCCFG_PROFILE, &layer);

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if ((ret = alloc_imp_globals()))
		goto out;

	if (strlcpy(imp_bundle, bname, sz) >= sz)
		uu_die(gettext("Unable to copy bundle file %s\n"), bname);

	if (_scf_set_tx_decoration(g_hndl, imp_bundle, layer) != 0) {
		bad_error("_scf_set_tx_decoration", scf_error());
	}

	if (scf_handle_get_scope(g_hndl, SCF_SCOPE_LOCAL, imp_scope) != 0)
		scfdie();

	uu_free(bname);

	/*
	 * Set the strings to be used for the security audit annotation
	 * event.
	 */
	if (_scf_set_annotation(g_hndl) == 0) {
		annotation_set = 1;
	} else {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
			warn(gettext("Repository connection broken.\n"));
			goto out;

		case SCF_ERROR_INVALID_ARGUMENT:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NO_RESOURCES:
		case SCF_ERROR_INTERNAL:
			bad_error("_scf_set_annotation", scf_error());
			/* NOTREACHED */

		default:
			/*
			 * Do not abort apply operation because of
			 * inability to create annotation audit event.
			 */
			warn(gettext("_scf_set_annotation() unexpectedly "
			    "failed with return code of %d\n"), scf_error());
			break;
		}
	}

	for (svc = uu_list_first(bndl->sc_bundle_services);
	    svc != NULL;
	    svc = uu_list_next(bndl->sc_bundle_services, svc)) {
		int refresh = 0;

		if (scf_scope_get_service(imp_scope, svc->sc_name,
		    imp_svc) != 0) {
			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
				/*
				 * Attempt to add the service if unable
				 * to report and move on to the next
				 * service.
				 */
				if (scf_scope_add_service(imp_scope,
				    svc->sc_name, imp_svc) != 0) {
					warn(gettext("Unable to add service "
					    "%s  %s\n"), svc->sc_name,
					    scf_strerror(scf_error()));

					continue;
				}

				break;
			default:
				scfdie();
			}
		} else if (IS_LAYER_BACKED_BY_FILE(layer)) {
			if (process_old_pgs(svc, imp_svc, layer) != 0) {
				warn(gettext(
				    "Unable to process pgs for %s: %s\n"),
				    svc->sc_name, scf_strerror(scf_error()));
				continue;
			}
		}

		/*
		 * If there were missing types in the profile, then need to
		 * attempt to find the types.
		 */
		if (svc->sc_miss_type) {
			if (uu_list_numnodes(svc->sc_pgroups) &&
			    uu_list_walk(svc->sc_pgroups, find_current_pg_type,
			    svc, UU_DEFAULT) != 0) {
				if (uu_error() != UU_ERROR_CALLBACK_FAILED)
					bad_error("uu_list_walk", uu_error());

				ret = -1;
				continue;
			}

			for (inst = uu_list_first(
			    svc->sc_u.sc_service.sc_service_instances);
			    inst != NULL;
			    inst = uu_list_next(
			    svc->sc_u.sc_service.sc_service_instances, inst)) {
				/*
				 * If the instance doesn't exist just
				 * skip to the next instance and let the
				 * import note the missing instance.
				 */
				if (scf_service_get_instance(imp_svc,
				    inst->sc_name, imp_inst) != 0)
					continue;

				if (uu_list_walk(inst->sc_pgroups,
				    find_current_pg_type, inst,
				    UU_DEFAULT) != 0) {
					if (uu_error() !=
					    UU_ERROR_CALLBACK_FAILED)
						bad_error("uu_list_walk",
						    uu_error());

					ret = -1;
					inst->sc_miss_type = B_TRUE;
				}
			}
		}

		/*
		 * if we have pgs in the profile, we need to refresh ALL
		 * instances of the service
		 */
		if (uu_list_numnodes(svc->sc_pgroups) != 0) {
			refresh = 1;
			r = lscf_import_service_pgs(imp_svc, svc->sc_fmri, svc,
			    SCI_FORCE | SCI_KEEP);
			switch (_lscf_import_err(r, svc->sc_fmri)) {
			case IMPORT_NEXT:
				break;

			case IMPORT_OUT:
				goto out;

			case IMPORT_BAD:
			default:
				bad_error("lscf_import_service_pgs", r);
			}
		}

		if (uu_list_numnodes(svc->sc_dependents) != 0) {
			uu_list_walk(svc->sc_dependents,
			    lscf_dependent_apply, svc, UU_DEFAULT);
		}

		for (inst = uu_list_first(
		    svc->sc_u.sc_service.sc_service_instances);
		    inst != NULL;
		    inst = uu_list_next(
		    svc->sc_u.sc_service.sc_service_instances, inst)) {
			/*
			 * This instance still has missing types
			 * so skip it.
			 */
			if (inst->sc_miss_type) {
				if (g_verbose)
					warn(gettext("Ignoring instance "
					    "%s:%s with missing types\n"),
					    inst->sc_parent->sc_name,
					    inst->sc_name);

				continue;
			}

			if (scf_service_get_instance(imp_svc, inst->sc_name,
			    imp_inst) != 0) {
				/*
				 * Attempt to add the instance if unable
				 * to report and move on to the next
				 * instance.
				 */
				if (scf_service_add_instance(imp_svc,
				    inst->sc_name, imp_inst) != 0) {
					warn(gettext("Unable to add instance "
					    "%s:%s  %s\n"),
					    inst->sc_parent->sc_name,
					    inst->sc_name,
					    scf_strerror(scf_error()));

					continue;
				}
			} else if (IS_LAYER_BACKED_BY_FILE(layer)) {
				if (process_old_pgs(inst, imp_inst, layer) !=
				    0) {
					warn(gettext("Unable to process pgs "
					    "for %s: %s\n"), svc->sc_name,
					    scf_strerror(scf_error()));
					continue;
				}
			}

			r = lscf_import_instance_pgs(imp_inst, inst->sc_fmri,
			    inst, SCI_FORCE | SCI_KEEP);
			switch (_lscf_import_err(r, inst->sc_fmri)) {
			case IMPORT_NEXT:
				break;

			case IMPORT_OUT:
				goto out;

			case IMPORT_BAD:
			default:
				bad_error("lscf_import_instance_pgs", r);
			}

			if (uu_list_numnodes(inst->sc_dependents) != 0) {
				uu_list_walk(inst->sc_dependents,
				    lscf_dependent_apply, inst, UU_DEFAULT);
			}

			/* refresh only if there is no pgs in the service */
			if (refresh == 0)
				(void) refresh_entity(0, imp_inst,
				    inst->sc_fmri, NULL, NULL, NULL);
		}

		if (refresh == 1) {
			char *name_buf = safe_malloc(max_scf_name_len + 1);

			(void) refresh_entity(1, imp_svc, svc->sc_name,
			    imp_inst, imp_iter, name_buf);
			free(name_buf);
		}

		for (old_dpt = uu_list_first(imp_deleted_dpts);
		    old_dpt != NULL;
		    old_dpt = uu_list_next(imp_deleted_dpts, old_dpt)) {
			if (imp_refresh_fmri(old_dpt->sc_pgroup_fmri,
			    old_dpt->sc_pgroup_name,
			    old_dpt->sc_parent->sc_fmri) != 0) {
				warn(gettext("Unable to refresh \"%s\"\n"),
				    old_dpt->sc_pgroup_fmri);
			}
		}
	}

out:
	if (annotation_set) {
		/* Remove security audit annotation strings. */
		(void) _scf_unset_annotation(g_hndl);
	}

	if (_scf_unset_tx_decoration(g_hndl) != 0) {
		bad_error("_scf_unset_tx_decoration", scf_error());
	}

	free_imp_globals();
	return (ret);
}


/*
 * Export.  These functions create and output an XML tree of a service
 * description from the repository.  This is largely the inverse of
 * lxml_get_bundle() in svccfg_xml.c, but with some kickers:
 *
 * - We must include any properties which are not represented specifically by
 *   a service manifest, e.g., properties created by an admin post-import.  To
 *   do so we'll iterate through all properties and deal with each
 *   apropriately.
 *
 * - Children of services and instances must must be in the order set by the
 *   DTD, but we iterate over the properties in undefined order.  The elements
 *   are not easily (or efficiently) sortable by name.  Since there's a fixed
 *   number of classes of them, however, we'll keep the classes separate and
 *   assemble them in order.
 */

/*
 * Convenience function to handle xmlSetProp errors (and type casting).
 */
static void
safe_setprop(xmlNodePtr n, const char *name, const char *val)
{
	if (xmlSetProp(n, (const xmlChar *)name, (const xmlChar *)val) == NULL)
		uu_die(gettext("Could not set XML property.\n"));
}

/*
 * Convenience function to set an XML attribute to the single value of an
 * astring property.  If the value happens to be the default, don't set the
 * attribute.  "dval" should be the default value supplied by the DTD, or
 * NULL for no default.
 */
static int
set_attr_from_prop_default(scf_property_t *prop, xmlNodePtr n,
    const char *name, const char *dval)
{
	scf_value_t *val;
	scf_decoration_t *dec;
	ssize_t len;
	char *str = NULL;
	int ret = -1;

	if ((val = scf_value_create(g_hndl)) == NULL ||
	    (dec = scf_decoration_create(g_hndl)) == NULL)
		scfdie();

	/* Don't proceed if this property is not in the requested layers */
	if (prop_get_dec(prop, dec) != 0 || dec_get_val(dec, val) != 0)
		goto out;

	len = scf_value_get_as_string(val, NULL, 0);
	if (len < 0)
		scfdie();

	str = safe_malloc(len + 1);

	if (scf_value_get_as_string(val, str, len + 1) < 0)
		scfdie();

	if (dval == NULL || strcmp(str, dval) != 0)
		safe_setprop(n, name, str);

	ret = 0;
out:

	free(str);
	scf_value_destroy(val);
	scf_decoration_destroy(dec);

	return (ret);
}

/*
 * As above, but the attribute is always set.
 */
static int
set_attr_from_prop(scf_property_t *prop, xmlNodePtr n, const char *name)
{
	return (set_attr_from_prop_default(prop, n, name, NULL));
}

/*
 * Dump the given document onto f, with "'s replaced by ''s.
 */
static int
write_service_bundle(xmlDocPtr doc, FILE *f)
{
	xmlChar *mem;
	int sz, i;

	mem = NULL;
	xmlDocDumpFormatMemory(doc, &mem, &sz, 1);

	if (mem == NULL) {
		semerr(gettext("Could not dump XML tree.\n"));
		return (-1);
	}

	/*
	 * Fortunately libxml produces &quot; instead of ", so we can blindly
	 * replace all " with '.  Cursed libxml2!  Why must you #ifdef out the
	 * &apos; code?!
	 */
	for (i = 0; i < sz; ++i) {
		char c = (char)mem[i];

		if (c == '"')
			(void) fputc('\'', f);
		else if (c == '\'')
			(void) fwrite("&apos;", sizeof ("&apos;") - 1, 1, f);
		else
			(void) fputc(c, f);
	}

	return (0);
}

/*
 * Create the DOM elements in elts necessary to (generically) represent prop
 * (i.e., a property or propval element).  If the name of the property is
 * known, it should be passed as name_arg.  Otherwise, pass NULL.
 */
static void
export_property(scf_property_t *prop, const char *name_arg,
    struct pg_elts *elts, int flags)
{
	const char *type;
	scf_error_t err = 0;
	xmlNodePtr pnode, lnode;
	char *lnname;
	int ret;

	/* name */
	if (name_arg != NULL) {
		(void) strcpy(exp_str, name_arg);
	} else {
		if (scf_property_get_name(prop, exp_str, exp_str_sz) < 0)
			scfdie();
	}

	/* Skip if property is not in the requested layers */
	if (prop_get_dec(prop, exp_dec) == -1)
		return;

	/* type */
	type = prop_to_typestr(prop);
	if (type == NULL)
		uu_die(gettext("Can't export property %s: unknown type.\n"),
		    exp_str);

	/* If we're exporting values, and there's just one, export it here. */
	if (!(flags & SCE_ALL_VALUES))
		goto empty;

	if (scf_decoration_get_value(exp_dec, exp_val) == SCF_SUCCESS) {
		xmlNodePtr n;

		/* Single value, so use propval */
		n = xmlNewNode(NULL, (xmlChar *)"propval");
		if (n == NULL)
			uu_die(emsg_create_xml);

		safe_setprop(n, name_attr, exp_str);
		safe_setprop(n, type_attr, type);

		if (scf_value_get_as_string(exp_val, exp_str, exp_str_sz) < 0)
			scfdie();
		safe_setprop(n, value_attr, exp_str);

		if (elts->propvals == NULL)
			elts->propvals = n;
		else
			(void) xmlAddSibling(elts->propvals, n);

		return;
	}

	err = scf_error();

	if (err == SCF_ERROR_PERMISSION_DENIED) {
		semerr(emsg_permission_denied);
		return;
	}

	if (err != SCF_ERROR_CONSTRAINT_VIOLATED &&
	    err != SCF_ERROR_NOT_FOUND &&
	    err != SCF_ERROR_PERMISSION_DENIED)
		scfdie();

empty:
	/* Multiple (or no) values, so use property */
	pnode = xmlNewNode(NULL, (xmlChar *)"property");
	if (pnode == NULL)
		uu_die(emsg_create_xml);

	safe_setprop(pnode, name_attr, exp_str);
	safe_setprop(pnode, type_attr, type);

	if (err == SCF_ERROR_CONSTRAINT_VIOLATED) {
		lnname = uu_msprintf("%s_list", type);
		if (lnname == NULL)
			uu_die(gettext("Could not create string"));

		lnode = xmlNewChild(pnode, NULL, (xmlChar *)lnname, NULL);
		if (lnode == NULL)
			uu_die(emsg_create_xml);

		uu_free(lnname);

		if (scf_iter_decoration_values(exp_val_iter, exp_dec) !=
		    SCF_SUCCESS)
			scfdie();

		while ((ret = scf_iter_next_value(exp_val_iter, exp_val)) ==
		    1) {
			xmlNodePtr vn;

			vn = xmlNewChild(lnode, NULL, (xmlChar *)"value_node",
			    NULL);
			if (vn == NULL)
				uu_die(emsg_create_xml);

			if (scf_value_get_as_string(exp_val, exp_str,
			    exp_str_sz) < 0)
				scfdie();
			safe_setprop(vn, value_attr, exp_str);
		}
		if (ret != 0)
			scfdie();
	}

	if (elts->properties == NULL)
		elts->properties = pnode;
	else
		(void) xmlAddSibling(elts->properties, pnode);
}

/*
 * Add a property_group element for this property group to elts.
 */
static void
export_pg(scf_propertygroup_t *pg, struct entity_elts *eelts, int flags)
{
	xmlNodePtr n;
	struct pg_elts elts;
	int ret;
	boolean_t read_protected;

	n = xmlNewNode(NULL, (xmlChar *)"property_group");

	/* name */
	if (scf_pg_get_name(pg, exp_str, max_scf_name_len + 1) < 0)
		scfdie();
	safe_setprop(n, name_attr, exp_str);

	/* type */
	if (scf_pg_get_type(pg, exp_str, exp_str_sz) < 0)
		scfdie();
	safe_setprop(n, type_attr, exp_str);

	/* properties */
	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	(void) memset(&elts, 0, sizeof (elts));

	/*
	 * If this property group is not read protected, we always want to
	 * output all the values.  Otherwise, we only output the values if the
	 * caller set SCE_ALL_VALUES (i.e., the user gave us export).
	 */
	if (_scf_pg_is_read_protected(pg, &read_protected) != SCF_SUCCESS)
		scfdie();

	if (!read_protected)
		flags |= SCE_ALL_VALUES;

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_PROPERTY_STABILITY) == 0) {
			xmlNodePtr m;

			m = xmlNewNode(NULL, (xmlChar *)"stability");
			if (m == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, m, value_attr) == 0) {
				elts.stability = m;
				continue;
			}

			xmlFreeNode(m);
		}

		export_property(exp_prop, NULL, &elts, flags);
	}
	if (ret == -1)
		scfdie();

	/* Don't add this pg to output if it doesn't have any properties */
	if (elts.properties == NULL && elts.propvals == NULL) {
		return;
	}

	(void) xmlAddChild(n, elts.stability);
	(void) xmlAddChildList(n, elts.propvals);
	(void) xmlAddChildList(n, elts.properties);

	if (eelts->property_groups == NULL)
		eelts->property_groups = n;
	else
		(void) xmlAddSibling(eelts->property_groups, n);
}

/*
 * Create an XML node representing the dependency described by the given
 * property group and put it in eelts.  Unless the dependency is not valid, in
 * which case create a generic property_group element which represents it and
 * put it in eelts.
 */
static void
export_dependency(scf_propertygroup_t *pg, struct entity_elts *eelts)
{
	xmlNodePtr n;
	int err = 0, ret;
	struct pg_elts elts;

	n = xmlNewNode(NULL, (xmlChar *)"dependency");
	if (n == NULL)
		uu_die(emsg_create_xml);

	/*
	 * If the external flag is present, skip this dependency because it
	 * should have been created by another manifest.
	 */
	if (scf_pg_get_property(pg, scf_property_external, exp_prop) == 0) {
		if (prop_check_type(exp_prop, SCF_TYPE_BOOLEAN) == 0 &&
		    prop_get_val(exp_prop, exp_val) == 0) {
			uint8_t b;

			if (scf_value_get_boolean(exp_val, &b) != SCF_SUCCESS)
				scfdie();

			if (b)
				return;
		}
	} else if (scf_error() != SCF_ERROR_NOT_FOUND)
		scfdie();

	/* Get the required attributes. */

	/* name */
	if (scf_pg_get_name(pg, exp_str, max_scf_name_len + 1) < 0)
		scfdie();
	safe_setprop(n, name_attr, exp_str);

	/* grouping */
	if (pg_get_prop(pg, SCF_PROPERTY_GROUPING, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, "grouping") != 0)
		err = 1;

	/* restart_on */
	if (pg_get_prop(pg, SCF_PROPERTY_RESTART_ON, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, "restart_on") != 0)
		err = 1;

	/* type */
	if (pg_get_prop(pg, SCF_PROPERTY_TYPE, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, type_attr) != 0)
		err = 1;

	/*
	 * entities: Not required, but if we create no children, it will be
	 * created as empty on import, so fail if it's missing.
	 */
	if (pg_get_prop(pg, SCF_PROPERTY_ENTITIES, exp_prop) == 0 &&
	    prop_check_type(exp_prop, SCF_TYPE_FMRI) == 0) {
		scf_iter_t *eiter;
		scf_decoration_t *edec;
		int ret2;

		if ((eiter = scf_iter_create(g_hndl)) == NULL ||
		    (edec = scf_decoration_create(g_hndl)) == NULL)
			scfdie();

		if (prop_get_dec(exp_prop, edec) != 0) {
			err = 1;
			scf_decoration_destroy(edec);
			scf_iter_destroy(eiter);
		} else {
			if (scf_iter_decoration_values(eiter, edec) !=
			    SCF_SUCCESS)
				scfdie();

			while ((ret2 = scf_iter_next_value(eiter,
			    exp_val)) == 1) {
				xmlNodePtr ch;

				if (scf_value_get_astring(exp_val, exp_str,
				    exp_str_sz) < 0)
					scfdie();

				/*
				 * service_fmri's must be first, so we can add
				 * them here.
				 */
				ch = xmlNewChild(n, NULL,
				    (xmlChar *)"service_fmri", NULL);
				if (ch == NULL)
					uu_die(emsg_create_xml);

				safe_setprop(ch, value_attr, exp_str);
			}
			if (ret2 == -1)
				scfdie();

			scf_decoration_destroy(edec);
			scf_iter_destroy(eiter);
		}
	} else
		err = 1;

	if (err) {
		xmlFreeNode(n);

		export_pg(pg, eelts, 0);

		return;
	}

	/* Iterate through the properties & handle each. */
	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	(void) memset(&elts, 0, sizeof (elts));

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_PROPERTY_GROUPING) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_RESTART_ON) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_TYPE) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_ENTITIES) == 0) {
			continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_STABILITY) == 0) {
			xmlNodePtr m;

			m = xmlNewNode(NULL, (xmlChar *)"stability");
			if (m == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, m, value_attr) == 0) {
				elts.stability = m;
				continue;
			}

			xmlFreeNode(m);
		}

		export_property(exp_prop, exp_str, &elts, 0);
	}
	if (ret == -1)
		scfdie();

	(void) xmlAddChild(n, elts.stability);
	(void) xmlAddChildList(n, elts.propvals);
	(void) xmlAddChildList(n, elts.properties);

	if (eelts->dependencies == NULL)
		eelts->dependencies = n;
	else
		(void) xmlAddSibling(eelts->dependencies, n);
}

static xmlNodePtr
export_method_environment(scf_propertygroup_t *pg)
{
	xmlNodePtr env;
	int ret;
	int children = 0;

	if (scf_pg_get_property(pg, SCF_PROPERTY_ENVIRONMENT, NULL) != 0)
		return (NULL);

	env = xmlNewNode(NULL, (xmlChar *)"method_environment");
	if (env == NULL)
		uu_die(emsg_create_xml);

	if (pg_get_prop(pg, SCF_PROPERTY_ENVIRONMENT, exp_prop) != 0)
		scfdie();

	if (prop_get_dec(exp_prop, exp_dec) != 0) {
		xmlFreeNode(env);
		return (NULL);
	}

	if (scf_iter_decoration_values(exp_val_iter, exp_dec) != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_value(exp_val_iter, exp_val)) == 1) {
		xmlNodePtr ev;
		char *cp;

		if (scf_value_get_as_string(exp_val, exp_str, exp_str_sz) < 0)
			scfdie();

		if ((cp = strchr(exp_str, '=')) == NULL || cp == exp_str) {
			warn(gettext("Invalid environment variable \"%s\".\n"),
			    exp_str);
			continue;
		} else if (strncmp(exp_str, "SMF_", 4) == 0) {
			warn(gettext("Invalid environment variable \"%s\"; "
			    "\"SMF_\" prefix is reserved.\n"), exp_str);
			continue;
		}

		*cp = '\0';
		cp++;

		ev = xmlNewChild(env, NULL, (xmlChar *)"envvar", NULL);
		if (ev == NULL)
			uu_die(emsg_create_xml);

		safe_setprop(ev, name_attr, exp_str);
		safe_setprop(ev, value_attr, cp);
		children++;
	}

	if (ret != 0)
		scfdie();

	if (children == 0) {
		xmlFreeNode(env);
		return (NULL);
	}

	return (env);
}

/*
 * As above, but for a method property group.
 */
static void
export_method(scf_propertygroup_t *pg, struct entity_elts *eelts)
{
	xmlNodePtr n, env;
	char *str;
	int err = 0, nonenv, ret;
	uint8_t use_profile;
	struct pg_elts elts;
	xmlNodePtr ctxt = NULL;

	n = xmlNewNode(NULL, (xmlChar *)"exec_method");

	/* Get the required attributes. */

	/* name */
	if (scf_pg_get_name(pg, exp_str, max_scf_name_len + 1) < 0)
		scfdie();
	safe_setprop(n, name_attr, exp_str);

	/* type */
	if (pg_get_prop(pg, SCF_PROPERTY_TYPE, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, type_attr) != 0)
		err = 1;

	/* exec */
	if (pg_get_prop(pg, SCF_PROPERTY_EXEC, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, "exec") != 0)
		err = 1;

	/* timeout */
	if (pg_get_prop(pg, SCF_PROPERTY_TIMEOUT, exp_prop) == 0 &&
	    prop_get_dec(exp_prop, exp_dec) == 0 &&
	    dec_check_type(exp_dec, SCF_TYPE_COUNT) == 0 &&
	    dec_get_val(exp_dec, exp_val) == 0) {
		uint64_t c;

		if (scf_value_get_count(exp_val, &c) != SCF_SUCCESS)
			scfdie();

		str = uu_msprintf("%llu", c);
		if (str == NULL)
			uu_die(gettext("Could not create string"));

		safe_setprop(n, "timeout_seconds", str);
		free(str);
	} else
		err = 1;

	if (err) {
		xmlFreeNode(n);

		export_pg(pg, eelts, 0);

		return;
	}


	/*
	 * If we're going to have a method_context child, we need to know
	 * before we iterate through the properties.  Since method_context's
	 * are optional, we don't want to complain about any properties
	 * missing if none of them are there.  Thus we can't use the
	 * convenience functions.
	 */
	nonenv =
	    scf_pg_get_property(pg, SCF_PROPERTY_WORKING_DIRECTORY, NULL) ==
	    SCF_SUCCESS ||
	    scf_pg_get_property(pg, SCF_PROPERTY_PROJECT, NULL) ==
	    SCF_SUCCESS ||
	    scf_pg_get_property(pg, SCF_PROPERTY_RESOURCE_POOL, NULL) ==
	    SCF_SUCCESS ||
	    scf_pg_get_property(pg, SCF_PROPERTY_USE_PROFILE, NULL) ==
	    SCF_SUCCESS;

	if (nonenv) {
		ctxt = xmlNewNode(NULL, (xmlChar *)"method_context");
		if (ctxt == NULL)
			uu_die(emsg_create_xml);

		if (pg_get_prop(pg, SCF_PROPERTY_WORKING_DIRECTORY, exp_prop) ==
		    0 &&
		    set_attr_from_prop_default(exp_prop, ctxt,
		    "working_directory", ":default") != 0)
			err = 1;

		if (pg_get_prop(pg, SCF_PROPERTY_PROJECT, exp_prop) == 0 &&
		    set_attr_from_prop_default(exp_prop, ctxt, "project",
		    ":default") != 0)
			err = 1;

		if (pg_get_prop(pg, SCF_PROPERTY_RESOURCE_POOL, exp_prop) ==
		    0 &&
		    set_attr_from_prop_default(exp_prop, ctxt,
		    "resource_pool", ":default") != 0)
			err = 1;
		/*
		 * We only want to complain about profile or credential
		 * properties if we will use them.  To determine that we must
		 * examine USE_PROFILE.
		 */
		if (pg_get_prop(pg, SCF_PROPERTY_USE_PROFILE, exp_prop) == 0 &&
		    prop_get_dec(exp_prop, exp_dec) == 0 &&
		    dec_check_type(exp_dec, SCF_TYPE_BOOLEAN) == 0 &&
		    dec_get_val(exp_dec, exp_val) == 0) {
			if (scf_value_get_boolean(exp_val, &use_profile) !=
			    SCF_SUCCESS) {
				scfdie();
			}

			if (use_profile) {
				xmlNodePtr prof;

				prof = xmlNewChild(ctxt, NULL,
				    (xmlChar *)"method_profile", NULL);
				if (prof == NULL)
					uu_die(emsg_create_xml);

				if (pg_get_prop(pg, SCF_PROPERTY_PROFILE,
				    exp_prop) != 0 ||
				    set_attr_from_prop(exp_prop, prof,
				    name_attr) != 0)
					err = 1;
			} else {
				xmlNodePtr cred;

				cred = xmlNewChild(ctxt, NULL,
				    (xmlChar *)"method_credential", NULL);
				if (cred == NULL)
					uu_die(emsg_create_xml);

				if (pg_get_prop(pg, SCF_PROPERTY_USER,
				    exp_prop) != 0 ||
				    set_attr_from_prop(exp_prop, cred,
				    "user") != 0) {
					err = 1;
				}

				if (pg_get_prop(pg, SCF_PROPERTY_GROUP,
				    exp_prop) == 0 &&
				    set_attr_from_prop_default(exp_prop, cred,
				    "group", ":default") != 0)
					err = 1;

				if (pg_get_prop(pg, SCF_PROPERTY_SUPP_GROUPS,
				    exp_prop) == 0 &&
				    set_attr_from_prop_default(exp_prop, cred,
				    "supp_groups", ":default") != 0)
					err = 1;

				if (pg_get_prop(pg, SCF_PROPERTY_PRIVILEGES,
				    exp_prop) == 0 &&
				    set_attr_from_prop_default(exp_prop, cred,
				    "privileges", ":default") != 0)
					err = 1;

				if (pg_get_prop(pg,
				    SCF_PROPERTY_LIMIT_PRIVILEGES,
				    exp_prop) == 0 &&
				    set_attr_from_prop_default(exp_prop, cred,
				    "limit_privileges", ":default") != 0)
					err = 1;
			}
		}
	}

	if ((env = export_method_environment(pg)) != NULL) {
		if (ctxt == NULL) {
			ctxt = xmlNewNode(NULL, (xmlChar *)"method_context");
			if (ctxt == NULL)
				uu_die(emsg_create_xml);
		}
		(void) xmlAddChild(ctxt, env);
	}

	if (env != NULL || (nonenv && err == 0))
		(void) xmlAddChild(n, ctxt);
	else
		xmlFreeNode(ctxt);

	nonenv = (err == 0);

	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	(void) memset(&elts, 0, sizeof (elts));

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_PROPERTY_TYPE) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_EXEC) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_TIMEOUT) == 0) {
			continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_STABILITY) == 0) {
			xmlNodePtr m;

			m = xmlNewNode(NULL, (xmlChar *)"stability");
			if (m == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, m, value_attr) == 0) {
				elts.stability = m;
				continue;
			}

			xmlFreeNode(m);
		} else if (strcmp(exp_str, SCF_PROPERTY_WORKING_DIRECTORY) ==
		    0 ||
		    strcmp(exp_str, SCF_PROPERTY_PROJECT) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_RESOURCE_POOL) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_USE_PROFILE) == 0) {
			if (nonenv)
				continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_USER) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_GROUP) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_SUPP_GROUPS) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_PRIVILEGES) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_LIMIT_PRIVILEGES) == 0) {
			if (nonenv && !use_profile)
				continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_PROFILE) == 0) {
			if (nonenv && use_profile)
				continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_ENVIRONMENT) == 0) {
			if (env != NULL)
				continue;
		}

		export_property(exp_prop, exp_str, &elts, 0);
	}
	if (ret == -1)
		scfdie();

	(void) xmlAddChild(n, elts.stability);
	(void) xmlAddChildList(n, elts.propvals);
	(void) xmlAddChildList(n, elts.properties);

	if (eelts->exec_methods == NULL)
		eelts->exec_methods = n;
	else
		(void) xmlAddSibling(eelts->exec_methods, n);
}

static void
export_pg_elts(struct pg_elts *elts, const char *name, const char *type,
    struct entity_elts *eelts)
{
	xmlNodePtr pgnode;

	pgnode = xmlNewNode(NULL, (xmlChar *)"property_group");
	if (pgnode == NULL)
		uu_die(emsg_create_xml);

	safe_setprop(pgnode, name_attr, name);
	safe_setprop(pgnode, type_attr, type);

	(void) xmlAddChildList(pgnode, elts->propvals);
	(void) xmlAddChildList(pgnode, elts->properties);

	if (eelts->property_groups == NULL)
		eelts->property_groups = pgnode;
	else
		(void) xmlAddSibling(eelts->property_groups, pgnode);
}

/*
 * Process the general property group for a service.  This is the one with the
 * goodies.
 */
static void
export_svc_general(scf_propertygroup_t *pg, struct entity_elts *selts)
{
	struct pg_elts elts;
	int ret;

	/*
	 * In case there are properties which don't correspond to child
	 * entities of the service entity, we'll set up a pg_elts structure to
	 * put them in.
	 */
	(void) memset(&elts, 0, sizeof (elts));

	/* Walk the properties, looking for special ones. */
	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_PROPERTY_SINGLE_INSTANCE) == 0) {
			if (prop_check_type(exp_prop, SCF_TYPE_BOOLEAN) == 0 &&
			    prop_get_val(exp_prop, exp_val) == 0) {
				uint8_t b;

				if (scf_value_get_boolean(exp_val, &b) !=
				    SCF_SUCCESS)
					scfdie();

				if (b) {
					selts->single_instance =
					    xmlNewNode(NULL,
					    (xmlChar *)"single_instance");
					if (selts->single_instance == NULL)
						uu_die(emsg_create_xml);
				}

				continue;
			}
		} else if (strcmp(exp_str, SCF_PROPERTY_RESTARTER) == 0) {
			xmlNodePtr rnode, sfnode;

			rnode = xmlNewNode(NULL, (xmlChar *)"restarter");
			if (rnode == NULL)
				uu_die(emsg_create_xml);

			sfnode = xmlNewChild(rnode, NULL,
			    (xmlChar *)"service_fmri", NULL);
			if (sfnode == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, sfnode,
			    value_attr) == 0) {
				selts->restarter = rnode;
				continue;
			}

			xmlFreeNode(rnode);
		} else if (strcmp(exp_str, SCF_PROPERTY_ENTITY_STABILITY) ==
		    0) {
			xmlNodePtr s;

			s = xmlNewNode(NULL, (xmlChar *)"stability");
			if (s == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, s, value_attr) == 0) {
				selts->stability = s;
				continue;
			}

			xmlFreeNode(s);
		}

		export_property(exp_prop, exp_str, &elts, 0);
	}
	if (ret == -1)
		scfdie();

	if (elts.propvals != NULL || elts.properties != NULL)
		export_pg_elts(&elts, scf_pg_general, scf_group_framework,
		    selts);
}

static void
export_method_context(scf_propertygroup_t *pg, struct entity_elts *elts)
{
	xmlNodePtr n, prof, cred, env;
	uint8_t use_profile;
	int ret, err = 0;

	n = xmlNewNode(NULL, (xmlChar *)"method_context");

	env = export_method_environment(pg);

	/* Need to know whether we'll use a profile or not. */
	if (pg_get_prop(pg, SCF_PROPERTY_USE_PROFILE, exp_prop) == 0 &&
	    prop_get_dec(exp_prop, exp_dec) == 0 &&
	    dec_check_type(exp_dec, SCF_TYPE_BOOLEAN) == 0 &&
	    dec_get_val(exp_dec, exp_val) == 0) {
		if (scf_value_get_boolean(exp_val, &use_profile) != SCF_SUCCESS)
			scfdie();

		if (use_profile)
			prof =
			    xmlNewChild(n, NULL, (xmlChar *)"method_profile",
			    NULL);
		else
			cred =
			    xmlNewChild(n, NULL, (xmlChar *)"method_credential",
			    NULL);
	}

	if (env != NULL)
		(void) xmlAddChild(n, env);

	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_PROPERTY_WORKING_DIRECTORY) == 0) {
			if (set_attr_from_prop(exp_prop, n,
			    "working_directory") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_PROJECT) == 0) {
			if (set_attr_from_prop(exp_prop, n, "project") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_RESOURCE_POOL) == 0) {
			if (set_attr_from_prop(exp_prop, n,
			    "resource_pool") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_USE_PROFILE) == 0) {
			/* EMPTY */
		} else if (strcmp(exp_str, SCF_PROPERTY_USER) == 0) {
			if (use_profile ||
			    set_attr_from_prop(exp_prop, cred, "user") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_GROUP) == 0) {
			if (use_profile ||
			    set_attr_from_prop(exp_prop, cred, "group") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_SUPP_GROUPS) == 0) {
			if (use_profile || set_attr_from_prop(exp_prop, cred,
			    "supp_groups") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_PRIVILEGES) == 0) {
			if (use_profile || set_attr_from_prop(exp_prop, cred,
			    "privileges") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_LIMIT_PRIVILEGES) ==
		    0) {
			if (use_profile || set_attr_from_prop(exp_prop, cred,
			    "limit_privileges") != 0)
				err = 1;
		} else if (strcmp(exp_str, SCF_PROPERTY_PROFILE) == 0) {
			if (!use_profile || set_attr_from_prop(exp_prop,
			    prof, name_attr) != 0)
				err = 1;
		} else {
			/* Can't have generic properties in method_context's */
			err = 1;
		}
	}
	if (ret == -1)
		scfdie();

	if (err && env == NULL) {
		xmlFreeNode(n);
		export_pg(pg, elts, 0);
		return;
	}

	elts->method_context = n;
}

/*
 * Given a dependency property group in the tfmri entity (target fmri), return
 * a dependent element which represents it.
 */
static xmlNodePtr
export_dependent(scf_propertygroup_t *pg, const char *name, const char *tfmri)
{
	uint8_t b;
	xmlNodePtr n, sf;
	int err = 0, ret;
	struct pg_elts pgelts;

	/*
	 * If external isn't set to true then exporting the service will
	 * export this as a normal dependency, so we should stop to avoid
	 * duplication.
	 */
	if (scf_pg_get_property(pg, scf_property_external, exp_prop) != 0 ||
	    scf_property_get_value(exp_prop, exp_val) != 0 ||
	    scf_value_get_boolean(exp_val, &b) != 0 || !b) {
		if (g_verbose) {
			warn(gettext("Dependent \"%s\" cannot be exported "
			    "properly because the \"%s\" property of the "
			    "\"%s\" dependency of %s is not set to true.\n"),
			    name, scf_property_external, name, tfmri);
		}

		return (NULL);
	}

	n = xmlNewNode(NULL, (xmlChar *)"dependent");
	if (n == NULL)
		uu_die(emsg_create_xml);

	safe_setprop(n, name_attr, name);

	/* Get the required attributes */
	if (pg_get_prop(pg, SCF_PROPERTY_RESTART_ON, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, "restart_on") != 0)
		err = 1;

	if (pg_get_prop(pg, SCF_PROPERTY_GROUPING, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, n, "grouping") != 0)
		err = 1;

	if (pg_get_prop(pg, SCF_PROPERTY_ENTITIES, exp_prop) == 0 &&
	    prop_get_dec(exp_prop, exp_dec) == 0 &&
	    dec_check_type(exp_dec, SCF_TYPE_FMRI) == 0 &&
	    dec_get_val(exp_dec, exp_val) == 0) {
		/* EMPTY */
	} else
		err = 1;

	if (err) {
		xmlFreeNode(n);
		return (NULL);
	}

	sf = xmlNewChild(n, NULL, (xmlChar *)"service_fmri", NULL);
	if (sf == NULL)
		uu_die(emsg_create_xml);

	safe_setprop(sf, value_attr, tfmri);

	/*
	 * Now add elements for the other properties.
	 */
	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	(void) memset(&pgelts, 0, sizeof (pgelts));

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, scf_property_external) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_RESTART_ON) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_GROUPING) == 0 ||
		    strcmp(exp_str, SCF_PROPERTY_ENTITIES) == 0) {
			continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_TYPE) == 0) {
			if (prop_get_dec(exp_prop, exp_dec) == 0 &&
			    dec_check_type(exp_dec, SCF_TYPE_ASTRING) == 0 &&
			    dec_get_val(exp_dec, exp_val) == 0) {
				char type[sizeof ("service") + 1];

				if (scf_value_get_astring(exp_val, type,
				    sizeof (type)) < 0)
					scfdie();

				if (strcmp(type, "service") == 0)
					continue;
			}
		} else if (strcmp(exp_str, SCF_PROPERTY_STABILITY) == 0) {
			xmlNodePtr s;

			s = xmlNewNode(NULL, (xmlChar *)"stability");
			if (s == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, s, value_attr) == 0) {
				pgelts.stability = s;
				continue;
			}

			xmlFreeNode(s);
		}

		export_property(exp_prop, exp_str, &pgelts, 0);
	}
	if (ret == -1)
		scfdie();

	(void) xmlAddChild(n, pgelts.stability);
	(void) xmlAddChildList(n, pgelts.propvals);
	(void) xmlAddChildList(n, pgelts.properties);

	return (n);
}

static void
export_dependents(scf_propertygroup_t *pg, struct entity_elts *eelts)
{
	scf_propertygroup_t *opg;
	scf_iter_t *iter;
	scf_decoration_t *dec;
	char *type, *fmri;
	int ret;
	struct pg_elts pgelts;
	xmlNodePtr n;
	scf_error_t serr;

	if ((opg = scf_pg_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL ||
	    (dec = scf_decoration_create(g_hndl)) == NULL)
		scfdie();

	/* Can't use exp_prop_iter due to export_dependent(). */
	if (scf_iter_pg_properties(iter, pg) != SCF_SUCCESS)
		scfdie();

	type = safe_malloc(max_scf_pg_type_len + 1);

	/* Get an extra byte so we can tell if values are too long. */
	fmri = safe_malloc(max_scf_fmri_len + 2);

	(void) memset(&pgelts, 0, sizeof (pgelts));

	while ((ret = scf_iter_next_property(iter, exp_prop)) == 1) {
		void *entity;
		int isservice;
		scf_type_t ty;

		/* Continue if we can't find requested decoration */
		if (prop_get_dec(exp_prop, dec) != 0)
			continue;

		if (scf_decoration_type(dec, &ty) != SCF_SUCCESS)
			scfdie();

		if ((ty != SCF_TYPE_ASTRING &&
		    dec_check_type(dec, SCF_TYPE_FMRI) != 0) ||
		    dec_get_val(dec, exp_val) != 0) {
			export_property(exp_prop, NULL, &pgelts, 0);
			continue;
		}

		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (scf_value_get_astring(exp_val, fmri,
		    max_scf_fmri_len + 2) < 0)
			scfdie();

		/* Look for a dependency group in the target fmri. */
		serr = fmri_to_entity(g_hndl, fmri, &entity, &isservice);
		switch (serr) {
		case SCF_ERROR_NONE:
			break;

		case SCF_ERROR_NO_MEMORY:
			uu_die(gettext("Out of memory.\n"));
			/* NOTREACHED */

		case SCF_ERROR_INVALID_ARGUMENT:
			if (g_verbose) {
				if (scf_property_to_fmri(exp_prop, fmri,
				    max_scf_fmri_len + 2) < 0)
					scfdie();

				warn(gettext("The value of %s is not a valid "
				    "FMRI.\n"), fmri);
			}

			export_property(exp_prop, exp_str, &pgelts, 0);
			continue;

		case SCF_ERROR_CONSTRAINT_VIOLATED:
			if (g_verbose) {
				if (scf_property_to_fmri(exp_prop, fmri,
				    max_scf_fmri_len + 2) < 0)
					scfdie();

				warn(gettext("The value of %s does not specify "
				    "a service or an instance.\n"), fmri);
			}

			export_property(exp_prop, exp_str, &pgelts, 0);
			continue;

		case SCF_ERROR_NOT_FOUND:
			if (g_verbose) {
				if (scf_property_to_fmri(exp_prop, fmri,
				    max_scf_fmri_len + 2) < 0)
					scfdie();

				warn(gettext("The entity specified by %s does "
				    "not exist.\n"), fmri);
			}

			export_property(exp_prop, exp_str, &pgelts, 0);
			continue;

		default:
#ifndef NDEBUG
			(void) fprintf(stderr, "%s:%d: %s() failed with "
			    "unexpected error %d.\n", __FILE__, __LINE__,
			    "fmri_to_entity", serr);
#endif
			abort();
		}

		if (entity_get_pg(entity, isservice, exp_str, opg) != 0) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();

			warn(gettext("Entity %s is missing dependency property "
			    "group %s.\n"), fmri, exp_str);

			export_property(exp_prop, NULL, &pgelts, 0);
			continue;
		}

		if (scf_pg_get_type(opg, type, max_scf_pg_type_len + 1) < 0)
			scfdie();

		if (strcmp(type, SCF_GROUP_DEPENDENCY) != 0) {
			if (scf_pg_to_fmri(opg, fmri, max_scf_fmri_len + 2) < 0)
				scfdie();

			warn(gettext("Property group %s is not of "
			    "expected type %s.\n"), fmri, SCF_GROUP_DEPENDENCY);

			export_property(exp_prop, NULL, &pgelts, 0);
			continue;
		}

		n = export_dependent(opg, exp_str, fmri);
		if (n == NULL)
			export_property(exp_prop, exp_str, &pgelts, 0);
		else {
			if (eelts->dependents == NULL)
				eelts->dependents = n;
			else
				(void) xmlAddSibling(eelts->dependents,
				    n);
		}
	}
	if (ret == -1)
		scfdie();

	free(fmri);
	free(type);

	scf_decoration_destroy(dec);
	scf_iter_destroy(iter);
	scf_pg_destroy(opg);

	if (pgelts.propvals != NULL || pgelts.properties != NULL)
		export_pg_elts(&pgelts, SCF_PG_DEPENDENTS, scf_group_framework,
		    eelts);
}

static void
make_node(xmlNodePtr *nodep, const char *name)
{
	if (*nodep == NULL) {
		*nodep = xmlNewNode(NULL, (xmlChar *)name);
		if (*nodep == NULL)
			uu_die(emsg_create_xml);
	}
}

static xmlNodePtr
export_tm_loctext(scf_propertygroup_t *pg, const char *parname)
{
	int ret;
	xmlNodePtr parent = NULL;
	xmlNodePtr loctext = NULL;

	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (prop_get_dec(exp_prop, exp_dec) != 0 ||
		    dec_check_type(exp_dec, SCF_TYPE_USTRING) != 0 ||
		    dec_get_val(exp_dec, exp_val) != 0)
			continue;

		if (scf_value_get_ustring(exp_val, exp_str, exp_str_sz) < 0)
			scfdie();

		make_node(&parent, parname);
		loctext = xmlNewTextChild(parent, NULL, (xmlChar *)"loctext",
		    (xmlChar *)exp_str);
		if (loctext == NULL)
			uu_die(emsg_create_xml);

		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		safe_setprop(loctext, "xml:lang", exp_str);
	}

	if (ret == -1)
		scfdie();

	return (parent);
}

static xmlNodePtr
export_tm_manpage(scf_propertygroup_t *pg)
{
	xmlNodePtr manpage = xmlNewNode(NULL, (xmlChar *)"manpage");
	if (manpage == NULL)
		uu_die(emsg_create_xml);

	if (pg_get_prop(pg, SCF_PROPERTY_TM_TITLE, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, manpage, "title") != 0 ||
	    pg_get_prop(pg, SCF_PROPERTY_TM_SECTION, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, manpage, "section") != 0) {
		xmlFreeNode(manpage);
		return (NULL);
	}

	if (pg_get_prop(pg, SCF_PROPERTY_TM_MANPATH, exp_prop) == 0)
		(void) set_attr_from_prop_default(exp_prop,
		    manpage, "manpath", ":default");

	return (manpage);
}

static xmlNodePtr
export_tm_doc_link(scf_propertygroup_t *pg)
{
	xmlNodePtr doc_link = xmlNewNode(NULL, (xmlChar *)"doc_link");
	if (doc_link == NULL)
		uu_die(emsg_create_xml);

	if (pg_get_prop(pg, SCF_PROPERTY_TM_NAME, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, doc_link, "name") != 0 ||
	    pg_get_prop(pg, SCF_PROPERTY_TM_URI, exp_prop) != 0 ||
	    set_attr_from_prop(exp_prop, doc_link, "uri") != 0) {
		xmlFreeNode(doc_link);
		return (NULL);
	}
	return (doc_link);
}

/*
 * Process template information for a service or instances.
 */
static void
export_template(scf_propertygroup_t *pg, struct entity_elts *elts,
    struct template_elts *telts)
{
	size_t mansz = strlen(SCF_PG_TM_MAN_PREFIX);
	size_t docsz = strlen(SCF_PG_TM_DOC_PREFIX);
	xmlNodePtr child = NULL;

	if (scf_pg_get_name(pg, exp_str, exp_str_sz) < 0)
		scfdie();

	if (strcmp(exp_str, SCF_PG_TM_COMMON_NAME) == 0) {
		telts->common_name = export_tm_loctext(pg, "common_name");
		if (telts->common_name == NULL)
			export_pg(pg, elts, 0);
		return;
	} else if (strcmp(exp_str, SCF_PG_TM_DESCRIPTION) == 0) {
		telts->description = export_tm_loctext(pg, "description");
		if (telts->description == NULL)
			export_pg(pg, elts, 0);
		return;
	}

	if (strncmp(exp_str, SCF_PG_TM_MAN_PREFIX, mansz) == 0) {
		child = export_tm_manpage(pg);
	} else if (strncmp(exp_str, SCF_PG_TM_DOC_PREFIX, docsz) == 0) {
		child = export_tm_doc_link(pg);
	}

	if (child != NULL) {
		make_node(&telts->documentation, "documentation");
		(void) xmlAddChild(telts->documentation, child);
	} else {
		export_pg(pg, elts, 0);
	}
}

/*
 * Process parameter and paramval elements
 */
static void
export_parameter(scf_property_t *prop, const char *name,
    struct params_elts *elts)
{
	xmlNodePtr param;
	scf_error_t err = 0;
	int ret;

	if (prop_get_dec(prop, exp_dec) != 0)
		return;

	if (scf_decoration_get_value(exp_dec, exp_val) == SCF_SUCCESS) {
		if ((param = xmlNewNode(NULL, (xmlChar *)"paramval")) == NULL)
			uu_die(emsg_create_xml);

		safe_setprop(param, name_attr, name);

		if (scf_value_get_as_string(exp_val, exp_str, exp_str_sz) < 0)
			scfdie();
		safe_setprop(param, value_attr, exp_str);

		if (elts->paramval == NULL)
			elts->paramval = param;
		else
			(void) xmlAddSibling(elts->paramval, param);

		return;
	}

	err = scf_error();

	if (err != SCF_ERROR_CONSTRAINT_VIOLATED &&
	    err != SCF_ERROR_NOT_FOUND)
		scfdie();

	if ((param = xmlNewNode(NULL, (xmlChar *)"parameter")) == NULL)
		uu_die(emsg_create_xml);

	safe_setprop(param, name_attr, name);

	if (err == SCF_ERROR_CONSTRAINT_VIOLATED) {
		if (scf_iter_decoration_values(exp_val_iter, exp_dec) !=
		    SCF_SUCCESS)
			scfdie();

		while ((ret = scf_iter_next_value(exp_val_iter,
		    exp_val)) == 1) {
			xmlNodePtr vn;

			if ((vn = xmlNewChild(param, NULL,
			    (xmlChar *)"value_node", NULL)) == NULL)
				uu_die(emsg_create_xml);

			if (scf_value_get_as_string(exp_val, exp_str,
			    exp_str_sz) < 0)
				scfdie();

			safe_setprop(vn, value_attr, exp_str);
		}
		if (ret != 0)
			scfdie();
	}

	if (elts->parameter == NULL)
		elts->parameter = param;
	else
		(void) xmlAddSibling(elts->parameter, param);
}

/*
 * Process notification parameters for a service or instance
 */
static void
export_notify_params(scf_propertygroup_t *pg, struct entity_elts *elts)
{
	xmlNodePtr n, event, *type;
	struct params_elts *eelts;
	int ret, err, i;

	n = xmlNewNode(NULL, (xmlChar *)"notification_parameters");
	event = xmlNewNode(NULL, (xmlChar *)"event");
	if (n == NULL || event == NULL)
		uu_die(emsg_create_xml);

	/* event value */
	if (scf_pg_get_name(pg, exp_str, max_scf_name_len + 1) < 0)
		scfdie();
	safe_setprop(event, value_attr, exp_str);

	(void) xmlAddChild(n, event);

	if ((type = calloc(URI_SCHEME_NUM, sizeof (xmlNodePtr))) == NULL ||
	    (eelts = calloc(URI_SCHEME_NUM,
	    sizeof (struct params_elts))) == NULL)
		uu_die(gettext("Out of memory.\n"));

	err = 0;

	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		char *t, *p;

		/* Don't proceed if property is not in the requested layers */
		if (prop_get_dec(exp_prop, exp_dec) != 0) {
			err = 1;
			continue;
		}

		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if ((t = strtok_r(exp_str, ",", &p)) == NULL || p == NULL) {
			/*
			 * this is not a well formed notification parameters
			 * element, we should export as regular pg
			 */
			err = 1;
			break;
		}

		if ((i = check_uri_protocol(t)) < 0) {
			err = 1;
			break;
		}

		if (type[i] == NULL) {
			if ((type[i] = xmlNewNode(NULL, (xmlChar *)"type")) ==
			    NULL)
				uu_die(emsg_create_xml);

			safe_setprop(type[i], name_attr, t);
		}
		if (strcmp(p, active_attr) == 0) {
			if (set_attr_from_prop(exp_prop, type[i],
			    active_attr) != 0) {
				err = 1;
				break;
			}
			continue;
		}
		/*
		 * We export the parameter
		 */
		export_parameter(exp_prop, p, &eelts[i]);
	}

	if (ret == -1)
		scfdie();

	if (err == 1) {
		for (i = 0; i < URI_SCHEME_NUM; ++i)
			xmlFree(type[i]);
		free(type);
		free(eelts);

		export_pg(pg, elts, 0);

		return;
	} else {
		for (i = 0; i < URI_SCHEME_NUM; ++i)
			if (type[i] != NULL) {
				(void) xmlAddChildList(type[i],
				    eelts[i].paramval);
				(void) xmlAddChildList(type[i],
				    eelts[i].parameter);
				(void) xmlAddSibling(event, type[i]);
			}
	}
	free(type);
	free(eelts);

	if (elts->notify_params == NULL)
		elts->notify_params = n;
	else
		(void) xmlAddSibling(elts->notify_params, n);
}

/*
 * Process the general property group for an instance.
 *
 * At the moment, 'enabled' is a required attribute in the DTD thus
 * every instance definition needs to specify an enabled_attr even if
 * that enabled property isn't from the requested layer. This is problematic
 * for two reasons:
 *
 * 1) An exported enabled_attr may not be correct since the requested layer(s)
 *    may not have general/enabled decoration. Currently, the code sets
 *    enable_attr to the current enabled value (similar to previous behavior).
 *
 * 2) Applying the exported profile may incorrectly set instance's enabled state
 *
 * Enabled attribute should prob be optional to make profiles more useful.
 */
static void
export_inst_general(scf_propertygroup_t *pg, xmlNodePtr inode,
    struct entity_elts *elts)
{
	uint8_t enabled;
	struct pg_elts pgelts;
	int ret;

	/*
	 * enabled
	 * Getting the current value rather than value in requested layer.
	 */
	if (pg_get_prop(pg, scf_property_enabled, exp_prop) == 0 &&
	    prop_check_type(exp_prop, SCF_TYPE_BOOLEAN) == 0 &&
	    prop_get_val(exp_prop, exp_val) == 0) {
		if (scf_value_get_boolean(exp_val, &enabled) != SCF_SUCCESS)
			scfdie();
	} else {
		enabled = 0;
	}

	safe_setprop(inode, enabled_attr, enabled ? true : false);

	if (pg_get_prop(pg, scf_property_complete, exp_prop) == 0 &&
	    prop_get_dec(exp_prop, exp_dec) == 0 &&
	    dec_check_type(exp_dec, SCF_TYPE_ASTRING) == 0)
		safe_setprop(inode, complete_attr, true);

	if (scf_iter_pg_properties(exp_prop_iter, pg) != SCF_SUCCESS)
		scfdie();

	(void) memset(&pgelts, 0, sizeof (pgelts));

	while ((ret = scf_iter_next_property(exp_prop_iter, exp_prop)) == 1) {
		if (scf_property_get_name(exp_prop, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, scf_property_enabled) == 0 ||
		    strcmp(exp_str, scf_property_complete) == 0) {
			continue;
		} else if (strcmp(exp_str, SCF_PROPERTY_RESTARTER) == 0) {
			xmlNodePtr rnode, sfnode;

			rnode = xmlNewNode(NULL, (xmlChar *)"restarter");
			if (rnode == NULL)
				uu_die(emsg_create_xml);

			sfnode = xmlNewChild(rnode, NULL,
			    (xmlChar *)"service_fmri", NULL);
			if (sfnode == NULL)
				uu_die(emsg_create_xml);

			if (set_attr_from_prop(exp_prop, sfnode,
			    value_attr) == 0) {
				elts->restarter = rnode;
				continue;
			}

			xmlFreeNode(rnode);
		}

		export_property(exp_prop, exp_str, &pgelts, 0);
	}
	if (ret == -1)
		scfdie();

	if (pgelts.propvals != NULL || pgelts.properties != NULL)
		export_pg_elts(&pgelts, scf_pg_general, scf_group_framework,
		    elts);
}

/*
 * Put an instance element for the given instance into selts.
 */
static void
export_instance(scf_instance_t *inst, struct entity_elts *selts, int flags)
{
	xmlNodePtr n;
	boolean_t isdefault;
	struct entity_elts elts;
	struct template_elts template_elts;
	int ret;

	n = xmlNewNode(NULL, (xmlChar *)"instance");
	if (n == NULL)
		uu_die(emsg_create_xml);

	/* name */
	if (scf_instance_get_name(inst, exp_str, exp_str_sz) < 0)
		scfdie();
	safe_setprop(n, name_attr, exp_str);
	isdefault = strcmp(exp_str, "default") == 0;

	/* check existance of general pg (since general/enabled is required) */
	if (scf_instance_get_pg(inst, scf_pg_general, exp_pg) != SCF_SUCCESS) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();

		if (g_verbose) {
			if (scf_instance_to_fmri(inst, exp_str, exp_str_sz) < 0)
				scfdie();

			warn(gettext("Instance %s has no general property "
			    "group; it will be marked disabled.\n"), exp_str);
		}

		safe_setprop(n, enabled_attr, false);
	} else if (scf_pg_get_type(exp_pg, exp_str, exp_str_sz) < 0 ||
	    strcmp(exp_str, scf_group_framework) != 0) {
		if (g_verbose) {
			if (scf_pg_to_fmri(exp_pg, exp_str, exp_str_sz) < 0)
				scfdie();

			warn(gettext("Property group %s is not of type "
			    "framework; the instance will be marked "
			    "disabled.\n"), exp_str);
		}

		safe_setprop(n, enabled_attr, false);
	}

	/* property groups */
	if (scf_iter_instance_pgs(exp_pg_iter, inst) < 0)
		scfdie();

	(void) memset(&elts, 0, sizeof (elts));
	(void) memset(&template_elts, 0, sizeof (template_elts));

	while ((ret = scf_iter_next_pg(exp_pg_iter, exp_pg)) == 1) {
		uint32_t pgflags;

		if (scf_pg_get_flags(exp_pg, &pgflags) != 0)
			scfdie();

		if (pgflags & SCF_PG_FLAG_NONPERSISTENT)
			continue;

		if (scf_pg_get_type(exp_pg, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_GROUP_DEPENDENCY) == 0) {
			export_dependency(exp_pg, &elts);
			continue;
		} else if (strcmp(exp_str, SCF_GROUP_METHOD) == 0) {
			export_method(exp_pg, &elts);
			continue;
		} else if (strcmp(exp_str, scf_group_framework) == 0) {
			if (scf_pg_get_name(exp_pg, exp_str,
			    max_scf_name_len + 1) < 0)
				scfdie();

			if (strcmp(exp_str, scf_pg_general) == 0) {
				export_inst_general(exp_pg, n, &elts);
				continue;
			} else if (strcmp(exp_str, SCF_PG_METHOD_CONTEXT) ==
			    0) {
				export_method_context(exp_pg, &elts);
				continue;
			} else if (strcmp(exp_str, SCF_PG_DEPENDENTS) == 0) {
				export_dependents(exp_pg, &elts);
				continue;
			}
		} else if (strcmp(exp_str, SCF_GROUP_TEMPLATE) == 0) {
			export_template(exp_pg, &elts, &template_elts);
			continue;
		} else if (strcmp(exp_str, SCF_NOTIFY_PARAMS_PG_TYPE) == 0) {
			export_notify_params(exp_pg, &elts);
			continue;
		}

		/* Ordinary pg. */
		export_pg(exp_pg, &elts, flags);
	}
	if (ret == -1)
		scfdie();

	if (template_elts.common_name != NULL) {
		elts.template = xmlNewNode(NULL, (xmlChar *)"template");
		(void) xmlAddChild(elts.template, template_elts.common_name);
		(void) xmlAddChild(elts.template, template_elts.description);
		(void) xmlAddChild(elts.template, template_elts.documentation);
	} else {
		xmlFreeNode(template_elts.description);
		xmlFreeNode(template_elts.documentation);
	}

	if (isdefault && elts.restarter == NULL &&
	    elts.dependencies == NULL && elts.method_context == NULL &&
	    elts.exec_methods == NULL && elts.notify_params == NULL &&
	    elts.property_groups == NULL && elts.template == NULL) {
		xmlChar *eval;

		/* This is a default instance */
		eval = xmlGetProp(n, (xmlChar *)enabled_attr);

		xmlFreeNode(n);

		n = xmlNewNode(NULL, (xmlChar *)"create_default_instance");
		if (n == NULL)
			uu_die(emsg_create_xml);

		safe_setprop(n, enabled_attr, (char *)eval);
		xmlFree(eval);

		selts->create_default_instance = n;
	} else {
		/* Assemble the children in order. */
		(void) xmlAddChild(n, elts.restarter);
		(void) xmlAddChildList(n, elts.dependencies);
		(void) xmlAddChildList(n, elts.dependents);
		(void) xmlAddChild(n, elts.method_context);
		(void) xmlAddChildList(n, elts.exec_methods);
		(void) xmlAddChildList(n, elts.notify_params);
		(void) xmlAddChildList(n, elts.property_groups);
		(void) xmlAddChild(n, elts.template);

		if (selts->instances == NULL)
			selts->instances = n;
		else
			(void) xmlAddSibling(selts->instances, n);
	}
}

/*
 * Return a service element for the given service.
 */
static xmlNodePtr
export_service(scf_service_t *svc, int flags)
{
	xmlNodePtr snode;
	struct entity_elts elts;
	struct template_elts template_elts;
	int ret;

	snode = xmlNewNode(NULL, (xmlChar *)"service");
	if (snode == NULL)
		uu_die(emsg_create_xml);

	/* Get & set name attribute */
	if (scf_service_get_name(svc, exp_str, max_scf_name_len + 1) < 0)
		scfdie();
	safe_setprop(snode, name_attr, exp_str);

	safe_setprop(snode, type_attr, "service");
	safe_setprop(snode, "version", "0");

	/* Acquire child elements. */
	if (scf_iter_service_pgs(exp_pg_iter, svc) != SCF_SUCCESS)
		scfdie();

	(void) memset(&elts, 0, sizeof (elts));
	(void) memset(&template_elts, 0, sizeof (template_elts));

	while ((ret = scf_iter_next_pg(exp_pg_iter, exp_pg)) == 1) {
		uint32_t pgflags;

		if (scf_pg_get_flags(exp_pg, &pgflags) != 0)
			scfdie();

		if (pgflags & SCF_PG_FLAG_NONPERSISTENT)
			continue;

		if (scf_pg_get_type(exp_pg, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, SCF_GROUP_DEPENDENCY) == 0) {
			export_dependency(exp_pg, &elts);
			continue;
		} else if (strcmp(exp_str, SCF_GROUP_METHOD) == 0) {
			export_method(exp_pg, &elts);
			continue;
		} else if (strcmp(exp_str, scf_group_framework) == 0) {
			if (scf_pg_get_name(exp_pg, exp_str,
			    max_scf_name_len + 1) < 0)
				scfdie();

			if (strcmp(exp_str, scf_pg_general) == 0) {
				export_svc_general(exp_pg, &elts);
				continue;
			} else if (strcmp(exp_str, SCF_PG_METHOD_CONTEXT) ==
			    0) {
				export_method_context(exp_pg, &elts);
				continue;
			} else if (strcmp(exp_str, SCF_PG_DEPENDENTS) == 0) {
				export_dependents(exp_pg, &elts);
				continue;
			} else if (strcmp(exp_str, SCF_PG_MANIFESTFILES) == 0) {
				continue;
			}
		} else if (strcmp(exp_str, SCF_GROUP_TEMPLATE) == 0) {
			export_template(exp_pg, &elts, &template_elts);
			continue;
		} else if (strcmp(exp_str, SCF_NOTIFY_PARAMS_PG_TYPE) == 0) {
			export_notify_params(exp_pg, &elts);
			continue;
		}

		export_pg(exp_pg, &elts, flags);
	}
	if (ret == -1)
		scfdie();

	if (template_elts.common_name != NULL) {
		elts.template = xmlNewNode(NULL, (xmlChar *)"template");
		(void) xmlAddChild(elts.template, template_elts.common_name);
		(void) xmlAddChild(elts.template, template_elts.description);
		(void) xmlAddChild(elts.template, template_elts.documentation);
	} else {
		xmlFreeNode(template_elts.description);
		xmlFreeNode(template_elts.documentation);
	}

	/* Iterate instances */
	if (scf_iter_service_instances(exp_inst_iter, svc) != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_instance(exp_inst_iter, exp_inst)) == 1)
		export_instance(exp_inst, &elts, flags);
	if (ret == -1)
		scfdie();

	/* Now add all of the accumulated elements in order. */
	(void) xmlAddChild(snode, elts.create_default_instance);
	(void) xmlAddChild(snode, elts.single_instance);
	(void) xmlAddChild(snode, elts.restarter);
	(void) xmlAddChildList(snode, elts.dependencies);
	(void) xmlAddChildList(snode, elts.dependents);
	(void) xmlAddChild(snode, elts.method_context);
	(void) xmlAddChildList(snode, elts.exec_methods);
	(void) xmlAddChildList(snode, elts.notify_params);
	(void) xmlAddChildList(snode, elts.property_groups);
	(void) xmlAddChildList(snode, elts.instances);
	(void) xmlAddChild(snode, elts.stability);
	(void) xmlAddChild(snode, elts.template);

	return (snode);
}

static int
export_callback(void *data, scf_walkinfo_t *wip)
{
	FILE *f;
	xmlDocPtr doc;
	xmlNodePtr sb;
	int result;
	struct export_args *argsp = (struct export_args *)data;

	if ((exp_inst = scf_instance_create(g_hndl)) == NULL ||
	    (exp_pg = scf_pg_create(g_hndl)) == NULL ||
	    (exp_prop = scf_property_create(g_hndl)) == NULL ||
	    (exp_dec = scf_decoration_create(g_hndl)) == NULL ||
	    (exp_val = scf_value_create(g_hndl)) == NULL ||
	    (exp_inst_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_pg_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_prop_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_dec_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_val_iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	exp_dec_layer_map = SVCCFG_CURRENT;
	exp_str_sz = max_scf_len + 1;
	exp_str = safe_malloc(exp_str_sz);

	if (argsp->filename != NULL) {
		errno = 0;
		f = fopen(argsp->filename, "wb");
		if (f == NULL) {
			if (errno == 0)
				uu_die(gettext("Could not open \"%s\": no free "
				    "stdio streams.\n"), argsp->filename);
			else
				uu_die(gettext("Could not open \"%s\""),
				    argsp->filename);
		}
	} else
		f = stdout;

	doc = xmlNewDoc((xmlChar *)"1.0");
	if (doc == NULL)
		uu_die(gettext("Could not create XML document.\n"));

	if (xmlCreateIntSubset(doc, (xmlChar *)"service_bundle", NULL,
	    (xmlChar *)MANIFEST_DTD_PATH) == NULL)
		uu_die(emsg_create_xml);

	sb = xmlNewNode(NULL, (xmlChar *)"service_bundle");
	if (sb == NULL)
		uu_die(emsg_create_xml);
	safe_setprop(sb, type_attr, "manifest");
	safe_setprop(sb, name_attr, "export");
	(void) xmlAddSibling(doc->children, sb);

	(void) xmlAddChild(sb, export_service(wip->svc, argsp->flags));

	result = write_service_bundle(doc, f);

	free(exp_str);
	scf_iter_destroy(exp_val_iter);
	scf_iter_destroy(exp_dec_iter);
	scf_iter_destroy(exp_prop_iter);
	scf_iter_destroy(exp_pg_iter);
	scf_iter_destroy(exp_inst_iter);
	scf_value_destroy(exp_val);
	scf_decoration_destroy(exp_dec);
	scf_property_destroy(exp_prop);
	scf_pg_destroy(exp_pg);
	scf_instance_destroy(exp_inst);

	xmlFreeDoc(doc);

	if (f != stdout)
		(void) fclose(f);

	return (result);
}

/*
 * Get the service named by fmri, build an XML tree which represents it, and
 * dump it into filename (or stdout if filename is NULL).
 */
int
lscf_service_export(char *fmri, const char *filename, int flags)
{
	struct export_args args;
	int ret, err;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return (-1);

	bzero(&args, sizeof (args));
	args.filename = filename;
	args.flags = flags;

	err = 0;
	if ((ret = scf_walk_fmri(g_hndl, 1, (char **)&fmri,
	    SCF_WALK_SERVICE | SCF_WALK_NOINSTANCE, export_callback,
	    &args, &err, semerr)) != 0) {
		if (ret != -1)
			semerr(gettext("Failed to walk instances: %s\n"),
			    scf_strerror(ret));
		return (-1);
	}

	/*
	 * Error message has already been printed.
	 */
	if (err != 0)
		return (-1);

	return (0);
}

static int
lscf_parse_input_layers(const char *layernames)
{
	scf_decoration_layer_t lval;
	char *layers_out;
	char *lasts;
	char *ln;
	int layers_value = SVCCFG_CURRENT;
	int l = 0;

	layers_out = safe_strdup(layernames);
	ln = strtok_r(layers_out, ",", &lasts);

	while (ln != NULL) {

		if (l == (sizeof (layers_all) /
		    sizeof (scf_decoration_layer_t)) - 2) {
			semerr(gettext("Too many layers: %s\n"), layernames);
			goto errout;
		}

		switch (lval = scf_decoration_layer_from_string(ln)) {

		case SCF_DECORATION_INVALID:
			semerr(gettext("Invalid layer: %s\n"), ln);
			goto errout;

		case SCF_DECORATION_TOP:
		case SCF_DECORATION_ALL:
			semerr(gettext(
			    "May not specify %s with other layers.\n"), ln);
			goto errout;

		default:
			layers_value |= scf_layer_to_flag(lval);
			break;
		}

		ln = strtok_r(NULL, ",", &lasts);
	}

errout:
	free(layers_out);
	return (layers_value);
}

/*
 * "Extract" a profile.
 */
int
lscf_profile_extract(const char *filename, const char *layernames, int flags)
{
	FILE *f;
	xmlDocPtr doc;
	xmlNodePtr sb;
	scf_scope_t *scope;
	scf_service_t *svc;
	scf_iter_t *siter;
	int r;
	int result;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return (-1);

	if (filename != NULL) {
		errno = 0;
		f = fopen(filename, "wb");
		if (f == NULL) {
			if (errno == 0)
				uu_die(gettext("Could not open \"%s\": no "
				    "free stdio streams.\n"), filename);
			else
				uu_die(gettext("Could not open \"%s\""),
				    filename);
		}
	} else
		f = stdout;

	if (layernames)
		exp_dec_layer_map = lscf_parse_input_layers(layernames);
	else
		exp_dec_layer_map = SVCCFG_ADMIN | SVCCFG_SITE_PROFILE;

	doc = xmlNewDoc((xmlChar *)"1.0");
	if (doc == NULL)
		uu_die(gettext("Could not create XML document.\n"));

	if (xmlCreateIntSubset(doc, (xmlChar *)"service_bundle", NULL,
	    (xmlChar *)MANIFEST_DTD_PATH) == NULL)
		uu_die(emsg_create_xml);

	sb = xmlNewNode(NULL, (xmlChar *)"service_bundle");
	if (sb == NULL)
		uu_die(emsg_create_xml);
	safe_setprop(sb, type_attr, "profile");
	safe_setprop(sb, name_attr, "extract");
	(void) xmlAddSibling(doc->children, sb);

	if ((scope = scf_scope_create(g_hndl)) == NULL ||
	    (svc = scf_service_create(g_hndl)) == NULL ||
	    (siter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_inst = scf_instance_create(g_hndl)) == NULL ||
	    (exp_pg = scf_pg_create(g_hndl)) == NULL ||
	    (exp_prop = scf_property_create(g_hndl)) == NULL ||
	    (exp_dec = scf_decoration_create(g_hndl)) == NULL ||
	    (exp_val = scf_value_create(g_hndl)) == NULL ||
	    (exp_inst_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_pg_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_prop_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_dec_iter = scf_iter_create(g_hndl)) == NULL ||
	    (exp_val_iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (scf_handle_get_local_scope(g_hndl, scope) != SCF_SUCCESS)
		scfdie();

	if (scf_iter_scope_services(siter, scope) != SCF_SUCCESS)
		scfdie();

	exp_str_sz = max_scf_len + 1;
	exp_str = safe_malloc(exp_str_sz);

	while ((r = scf_iter_next_service(siter, svc)) == 1) {
		xmlNodePtr svc_node;

		/* Skip smf/manifest */
		if (scf_service_get_name(svc, exp_str, exp_str_sz) < 0)
			scfdie();

		if (strcmp(exp_str, HASH_SVC) == 0)
			continue;

		svc_node = export_service(svc, flags);
		/* Don't add empty service element to output */
		if (xmlGetLastChild(svc_node) != NULL)
			(void) xmlAddChild(sb, svc_node);
	}
	if (r < 0)
		scfdie();

	result = write_service_bundle(doc, f);

	exp_dec_layer_map = SVCCFG_CURRENT;

	free(exp_str);
	scf_iter_destroy(exp_val_iter);
	scf_iter_destroy(exp_dec_iter);
	scf_iter_destroy(exp_prop_iter);
	scf_iter_destroy(exp_pg_iter);
	scf_iter_destroy(exp_inst_iter);
	scf_value_destroy(exp_val);
	scf_property_destroy(exp_prop);
	scf_decoration_destroy(exp_dec);
	scf_pg_destroy(exp_pg);
	scf_instance_destroy(exp_inst);

	xmlFreeDoc(doc);

	if (f != stdout)
		(void) fclose(f);

	return (result);
}


/*
 * Entity manipulation commands
 */

/*
 * Entity selection.  If no entity is selected, then the current scope is in
 * cur_scope, and cur_svc and cur_inst are NULL.  When a service is selected,
 * only cur_inst is NULL, and when an instance is selected, none are NULL.
 * When the snaplevel of a snapshot is selected, cur_level, cur_snap, and
 * cur_inst will be non-NULL.
 */

/* Returns 1 if maybe absolute fmri, 0 on success (dies on failure) */
static int
select_inst(const char *name)
{
	scf_instance_t *inst;
	scf_error_t err;
	int inst_masked;

	assert(cur_svc != NULL);

	inst = scf_instance_create(g_hndl);
	if (inst == NULL)
		scfdie();

	if (scf_service_get_instance(cur_svc, name, inst) == SCF_SUCCESS) {
		cur_inst = inst;
		inst_masked = scf_instance_is_masked(inst);
		if (inst_masked == -1)
			scfdie();
		cur_selection_is_masked = (inst_masked == 1);
		return (0);
	}

	err = scf_error();
	if (err != SCF_ERROR_NOT_FOUND && err != SCF_ERROR_INVALID_ARGUMENT)
		scfdie();

	scf_instance_destroy(inst);
	return (1);
}

/* Returns as above. */
static int
select_svc(const char *name)
{
	scf_service_t *svc;
	scf_error_t err;
	int svc_masked;

	assert(cur_scope != NULL);

	svc = scf_service_create(g_hndl);
	if (svc == NULL)
		scfdie();

	if (scf_scope_get_service(cur_scope, name, svc) == SCF_SUCCESS) {
		cur_svc = svc;
		svc_masked = scf_service_is_masked(svc);
		if (svc_masked == -1)
			scfdie();
		cur_selection_is_masked = (svc_masked == 1);
		return (0);
	}

	err = scf_error();
	if (err != SCF_ERROR_NOT_FOUND && err != SCF_ERROR_INVALID_ARGUMENT)
		scfdie();

	scf_service_destroy(svc);
	return (1);
}

/* ARGSUSED */
static int
select_callback(void *unused, scf_walkinfo_t *wip)
{
	scf_instance_t *inst;
	scf_service_t *svc;
	scf_scope_t *scope;
	int is_masked = -2;

	if (wip->inst != NULL) {
		if ((scope = scf_scope_create(g_hndl)) == NULL ||
		    (svc = scf_service_create(g_hndl)) == NULL ||
		    (inst = scf_instance_create(g_hndl)) == NULL)
			scfdie();

		if (scf_handle_decode_fmri(g_hndl, wip->fmri, scope, svc,
		    inst, NULL, NULL, SCF_DECODE_FMRI_EXACT) != SCF_SUCCESS)
			scfdie();
		is_masked = scf_instance_is_masked(inst);
	} else {
		assert(wip->svc != NULL);

		if ((scope = scf_scope_create(g_hndl)) == NULL ||
		    (svc = scf_service_create(g_hndl)) == NULL)
			scfdie();

		if (scf_handle_decode_fmri(g_hndl, wip->fmri, scope, svc,
		    NULL, NULL, NULL, SCF_DECODE_FMRI_EXACT) != SCF_SUCCESS)
			scfdie();

		inst = NULL;
		is_masked = scf_service_is_masked(svc);
	}

	if (is_masked == -1)
		scfdie();

	if (is_masked == -2)
		uu_die(gettext("select_callback() failed\n"));

	/* Clear out the current selection */
	assert(cur_scope != NULL);
	scf_scope_destroy(cur_scope);
	scf_service_destroy(cur_svc);
	scf_instance_destroy(cur_inst);

	cur_scope = scope;
	cur_svc = svc;
	cur_inst = inst;

	cur_selection_is_masked = (is_masked == 1);

	return (0);
}

static int
validate_callback(void *fmri_p, scf_walkinfo_t *wip)
{
	char **fmri = fmri_p;

	switch (scf_instance_is_masked(wip->inst)) {
	case -1:
		scfdie();
		break;
	case 0:
		break;
	case 1:
		return (1);
	}

	*fmri = strdup(wip->fmri);
	if (*fmri == NULL)
		uu_die(gettext("Out of memory.\n"));

	return (0);
}

/*
 * validate [fmri]
 * Perform the validation of an FMRI instance.
 */
void
lscf_validate_fmri(const char *fmri)
{
	int ret = 0;
	size_t inst_sz;
	char *inst_fmri = NULL;
	scf_tmpl_errors_t *errs = NULL;
	char *snapbuf = NULL;
	int use_current = 1;

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (fmri == NULL) {
		inst_sz = max_scf_fmri_len + 1;
		inst_fmri = safe_malloc(inst_sz);

		if (cur_snap != NULL) {
			snapbuf = safe_malloc(max_scf_name_len + 1);
			if (scf_snapshot_get_name(cur_snap, snapbuf,
			    max_scf_name_len + 1) < 0)
				scfdie();
			use_current = 0;
		}
		if (cur_inst == NULL) {
			semerr(gettext("No instance selected\n"));
			goto cleanup;
		} else if (scf_instance_to_fmri(cur_inst, inst_fmri,
		    inst_sz) >= inst_sz) {
			/* sanity check. Should never get here */
			uu_die(gettext("Unexpected error! file %s, line %d\n"),
			    __FILE__, __LINE__);
		}
	} else {
		scf_error_t scf_err;
		int err = 0;

		if ((scf_err = scf_walk_fmri(g_hndl, 1, (char **)&fmri, 0,
		    validate_callback, &inst_fmri, &err, semerr)) != 0) {
			uu_warn("Failed to walk instances: %s\n",
			    scf_strerror(scf_err));
			goto cleanup;
		}
		if (err == 1) {
			semerr(scf_get_msg(SCF_MSG_PATTERN_NOINSTSVC), fmri);
		}
		if (err != 0) {
			/* error message displayed by scf_walk_fmri */
			goto cleanup;
		}
		use_current = 0;
	}

	ret = scf_tmpl_validate_fmri(g_hndl, inst_fmri, snapbuf, &errs,
	    use_current ? SCF_TMPL_VALIDATE_FLAG_CURRENT : 0);
	if (ret == -1) {
		if (scf_error() == SCF_ERROR_TEMPLATE_INVALID) {
			warn(gettext("Template data for %s is invalid. "
			    "Consider reverting to a previous snapshot or "
			    "restoring original configuration.\n"), inst_fmri);
		} else {
			uu_warn("%s: %s\n",
			    gettext("Error validating the instance"),
			    scf_strerror(scf_error()));
		}
	} else if (ret == 1 && errs != NULL) {
		scf_tmpl_error_t *err = NULL;
		char *msg;
		size_t len = 256;	/* initial error buffer size */
		int flag = (est->sc_cmd_flags & SC_CMD_IACTIVE) ?
		    SCF_TMPL_STRERROR_HUMAN : 0;

		msg = safe_malloc(len);

		while ((err = scf_tmpl_next_error(errs)) != NULL) {
			int ret;

			if ((ret = scf_tmpl_strerror(err, msg, len,
			    flag)) >= len) {
				len = ret + 1;
				msg = realloc(msg, len);
				if (msg == NULL)
					uu_die(gettext(
					    "Out of memory.\n"));
				(void) scf_tmpl_strerror(err, msg, len,
				    flag);
			}
			(void) fprintf(stderr, "%s\n", msg);
		}
		if (msg != NULL)
			free(msg);

		/* exit 1 if not in interactive mode */
		if ((est->sc_cmd_flags &
		    (SC_CMD_IACTIVE | SC_CMD_DONT_EXIT)) == 0)
			exit(1);

	}
	if (errs != NULL)
		scf_tmpl_errors_destroy(errs);

cleanup:
	free(inst_fmri);
	free(snapbuf);
}

static void
lscf_validate_file(const char *filename)
{
	tmpl_errors_t *errs;

	bundle_t *b = internal_bundle_new();
	if (lxml_get_bundle_file(b, filename, SVCCFG_OP_IMPORT) == 0) {
		if (tmpl_validate_bundle(b, &errs) != TVS_SUCCESS) {
			tmpl_errors_print(stderr, errs, "");
			semerr(gettext("Validation failed.\n"));
		}
		tmpl_errors_destroy(errs);
	}
	(void) internal_bundle_free(b);
}

/*
 * validate [fmri|file]
 */
void
lscf_validate(const char *arg)
{
	const char *str;

	if (strncmp(arg, SCF_FMRI_FILE_PREFIX,
	    sizeof (SCF_FMRI_FILE_PREFIX) - 1) == 0) {
		str = arg + sizeof (SCF_FMRI_FILE_PREFIX) - 1;
		lscf_validate_file(str);
	} else if (strncmp(arg, SCF_FMRI_SVC_PREFIX,
	    sizeof (SCF_FMRI_SVC_PREFIX) - 1) == 0) {
		str = arg + sizeof (SCF_FMRI_SVC_PREFIX) - 1;
		lscf_validate_fmri(str);
	} else if (access(arg, R_OK | F_OK) == 0) {
		lscf_validate_file(arg);
	} else {
		lscf_validate_fmri(arg);
	}
}

void
lscf_set_select_pattern(const char *p)
{
	if (cur_selection_pattern != NULL)
		free(cur_selection_pattern);
	cur_selection_pattern = safe_strdup(p);
}

void
lscf_select(const char *fmri)
{
	int ret, err;

	if (g_hndl == NULL)
		(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (cur_snap != NULL) {
		struct snaplevel *elt;
		char *buf;

		/* Error unless name is that of the next level. */
		elt = uu_list_next(cur_levels, cur_elt);
		if (elt == NULL) {
			semerr(gettext("No children.\n"));
			return;
		}

		buf = safe_malloc(max_scf_name_len + 1);

		if (scf_snaplevel_get_instance_name(elt->sl, buf,
		    max_scf_name_len + 1) < 0)
			scfdie();

		if (strcmp(buf, fmri) != 0) {
			semerr(gettext("No such child.\n"));
			free(buf);
			return;
		}

		free(buf);

		cur_elt = elt;
		cur_level = elt->sl;
		goto select_done;
	}

	/*
	 * Special case for 'svc:', which takes the user to the scope level.
	 */
	if (strcmp(fmri, "svc:") == 0) {
		scf_instance_destroy(cur_inst);
		scf_service_destroy(cur_svc);
		cur_inst = NULL;
		cur_svc = NULL;
		cur_selection_is_masked = B_FALSE;
		return;
	}

	/*
	 * Special case for ':properties'.  This appears as part of 'list' but
	 * can't be selected.  Give a more helpful error message in this case.
	 */
	if (strcmp(fmri, ":properties") == 0) {
		semerr(gettext(":properties is not an entity.  Try 'listprop' "
		    "to list properties.\n"));
		return;
	}

	/*
	 * First try the argument as relative to the current selection.
	 */
	if (cur_inst != NULL) {
		/* EMPTY */;
	} else if (cur_svc != NULL) {
		if (select_inst(fmri) != 1)
			goto select_done;
	} else {
		if (select_svc(fmri) != 1)
			goto select_done;
	}

	err = 0;
	if ((ret = scf_walk_fmri(g_hndl, 1, (char **)&fmri, SCF_WALK_SERVICE,
	    select_callback, NULL, &err, semerr)) != 0) {
		semerr(gettext("Failed to walk instances: %s\n"),
		    scf_strerror(ret));
	}
select_done:
	if (cur_selection_is_masked)
		semerr(NULL);

}

void
lscf_unselect(void)
{
	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (cur_snap != NULL) {
		struct snaplevel *elt;

		elt = uu_list_prev(cur_levels, cur_elt);
		if (elt == NULL) {
			semerr(gettext("No parent levels.\n"));
		} else {
			cur_elt = elt;
			cur_level = elt->sl;
		}
	} else if (cur_inst != NULL) {
		int is_masked = scf_service_is_masked(cur_svc);
		if (is_masked == -1)
			scfdie();
		scf_instance_destroy(cur_inst);
		cur_inst = NULL;
		cur_selection_is_masked = (is_masked == 1);
	} else if (cur_svc != NULL) {
		scf_service_destroy(cur_svc);
		cur_svc = NULL;
		cur_selection_is_masked = B_FALSE;
	} else {
		semerr(gettext("Cannot unselect at scope level.\n"));
	}
}

/*
 * Return the FMRI of the current selection, for the prompt.
 */
void
lscf_get_selection_str(char *buf, size_t bufsz)
{
	char *cp;
	ssize_t fmrilen, szret;
	boolean_t deleted = B_FALSE;

	if (g_hndl == NULL) {
		(void) strlcpy(buf, "svc:", bufsz);
		return;
	}

	if (cur_level != NULL) {
		assert(cur_snap != NULL);

		/* [ snapshot ] FMRI [: instance ] */
		assert(bufsz >= max_scf_selection_str_len + 1);

		buf[0] = '[';

		szret = scf_snapshot_get_name(cur_snap, buf + 1,
		    max_scf_name_len + 1);
		if (szret < 0) {
			if (scf_error() != SCF_ERROR_DELETED)
				scfdie();

			goto snap_deleted;
		}

		(void) strcat(buf, "]svc:/");

		cp = strchr(buf, '\0');

		szret = scf_snaplevel_get_service_name(cur_level, cp,
		    max_scf_name_len + 1);
		if (szret < 0) {
			if (scf_error() != SCF_ERROR_DELETED)
				scfdie();

			goto snap_deleted;
		}

		cp = strchr(cp, '\0');

		if (snaplevel_is_instance(cur_level)) {
			*cp++ = ':';

			if (scf_snaplevel_get_instance_name(cur_level, cp,
			    max_scf_name_len + 1) < 0) {
				if (scf_error() != SCF_ERROR_DELETED)
					scfdie();

				goto snap_deleted;
			}
		} else {
			*cp++ = '[';
			*cp++ = ':';

			if (scf_instance_get_name(cur_inst, cp,
			    max_scf_name_len + 1) < 0) {
				if (scf_error() != SCF_ERROR_DELETED)
					scfdie();

				goto snap_deleted;
			}

			(void) strcat(buf, "]");
		}

		return;

snap_deleted:
		deleted = B_TRUE;
		free(buf);
		unselect_cursnap();
	}

	assert(cur_snap == NULL);

	if (cur_inst != NULL) {
		assert(cur_svc != NULL);
		assert(cur_scope != NULL);

		fmrilen = scf_instance_to_fmri(cur_inst, buf, bufsz);
		if (fmrilen >= 0) {
			assert(fmrilen < bufsz);
			if (deleted)
				warn(emsg_deleted);
			return;
		}

		if (scf_error() != SCF_ERROR_DELETED)
			scfdie();

		deleted = B_TRUE;

		scf_instance_destroy(cur_inst);
		cur_inst = NULL;
	}

	if (cur_svc != NULL) {
		assert(cur_scope != NULL);

		szret = scf_service_to_fmri(cur_svc, buf, bufsz);
		if (szret >= 0) {
			assert(szret < bufsz);
			if (deleted)
				warn(emsg_deleted);
			return;
		}

		if (scf_error() != SCF_ERROR_DELETED)
			scfdie();

		deleted = B_TRUE;
		scf_service_destroy(cur_svc);
		cur_svc = NULL;
	}

	assert(cur_scope != NULL);
	fmrilen = scf_scope_to_fmri(cur_scope, buf, bufsz);

	if (fmrilen < 0)
		scfdie();

	assert(fmrilen < bufsz);
	if (deleted)
		warn(emsg_deleted);
}

/*
 * Entity listing.  Entities and colon namespaces (e.g., :properties and
 * :statistics) are listed for the current selection.
 */
void
lscf_list(const char *pattern)
{
	scf_iter_t *iter;
	char *buf;
	int ret;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_level != NULL) {
		struct snaplevel *elt;

		(void) fputs(COLON_NAMESPACES, stdout);

		elt = uu_list_next(cur_levels, cur_elt);
		if (elt == NULL)
			return;

		/*
		 * For now, we know that the next level is an instance.  But
		 * if we ever have multiple scopes, this could be complicated.
		 */
		buf = safe_malloc(max_scf_name_len + 1);
		if (scf_snaplevel_get_instance_name(elt->sl, buf,
		    max_scf_name_len + 1) >= 0) {
			(void) puts(buf);
		} else {
			if (scf_error() != SCF_ERROR_DELETED)
				scfdie();
		}

		free(buf);

		return;
	}

	if (cur_inst != NULL) {
		(void) fputs(COLON_NAMESPACES, stdout);
		return;
	}

	iter = scf_iter_create(g_hndl);
	if (iter == NULL)
		scfdie();

	buf = safe_malloc(max_scf_name_len + 1);

	if (cur_svc != NULL) {
		/* List the instances in this service. */
		scf_instance_t *inst;

		inst = scf_instance_create(g_hndl);
		if (inst == NULL)
			scfdie();

		if (scf_iter_service_instances(iter, cur_svc) == 0) {
			safe_printf(COLON_NAMESPACES);

			for (;;) {
				ret = scf_iter_next_instance(iter, inst);
				if (ret == 0)
					break;
				if (ret != 1) {
					if (scf_error() != SCF_ERROR_DELETED)
						scfdie();

					break;
				}

				if (scf_instance_get_name(inst, buf,
				    max_scf_name_len + 1) >= 0) {
					if (pattern == NULL ||
					    fnmatch(pattern, buf, 0) == 0)
						(void) puts(buf);
				} else {
					if (scf_error() != SCF_ERROR_DELETED)
						scfdie();
				}
			}
		} else {
			if (scf_error() != SCF_ERROR_DELETED)
				scfdie();
		}

		scf_instance_destroy(inst);
	} else {
		/* List the services in this scope. */
		scf_service_t *svc;

		assert(cur_scope != NULL);

		svc = scf_service_create(g_hndl);
		if (svc == NULL)
			scfdie();

		if (scf_iter_scope_services(iter, cur_scope) != SCF_SUCCESS)
			scfdie();

		for (;;) {
			ret = scf_iter_next_service(iter, svc);
			if (ret == 0)
				break;
			if (ret != 1)
				scfdie();

			if (scf_service_get_name(svc, buf,
			    max_scf_name_len + 1) >= 0) {
				if (pattern == NULL ||
				    fnmatch(pattern, buf, 0) == 0)
					safe_printf("%s\n", buf);
			} else {
				if (scf_error() != SCF_ERROR_DELETED)
					scfdie();
			}
		}

		scf_service_destroy(svc);
	}

	free(buf);
	scf_iter_destroy(iter);
}

/*
 * Entity addition.  Creates an empty entity in the current selection.
 */
void
lscf_add(const char *name)
{
	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
	} else if (cur_inst != NULL) {
		semerr(gettext("Cannot add entities to an instance.\n"));
	} else if (cur_svc != NULL) {
		if (scf_service_add_instance(cur_svc, name, NULL) !=
		    SCF_SUCCESS) {
			switch (scf_error()) {
			case SCF_ERROR_INVALID_ARGUMENT:
				semerr(gettext("Invalid name.\n"));
				break;

			case SCF_ERROR_EXISTS:
				semerr(gettext("Instance already exists.\n"));
				break;

			case SCF_ERROR_PERMISSION_DENIED:
				semerr(emsg_permission_denied);
				break;

			default:
				scfdie();
			}
		}
	} else {
		assert(cur_scope != NULL);

		if (scf_scope_add_service(cur_scope, name, NULL) !=
		    SCF_SUCCESS) {
			switch (scf_error()) {
			case SCF_ERROR_INVALID_ARGUMENT:
				semerr(gettext("Invalid name.\n"));
				break;

			case SCF_ERROR_EXISTS:
				semerr(gettext("Service already exists.\n"));
				break;

			case SCF_ERROR_PERMISSION_DENIED:
				semerr(emsg_permission_denied);
				break;

			case SCF_ERROR_BACKEND_READONLY:
				semerr(emsg_read_only);
				break;

			default:
				scfdie();
			}
		}
	}
}

/* return 1 if the entity has no persistent pgs, else return 0 */
static int
entity_has_no_pgs(void *ent, int isservice)
{
	scf_iter_t *iter = NULL;
	scf_propertygroup_t *pg = NULL;
	uint32_t flags;
	int err;
	int ret = 1;

	if ((iter = scf_iter_create(g_hndl)) == NULL ||
	    (pg = scf_pg_create(g_hndl)) == NULL)
		scfdie();

	if (isservice) {
		if (scf_iter_service_pgs(iter, (scf_service_t *)ent) < 0)
			scfdie();
	} else {
		if (scf_iter_instance_pgs(iter, (scf_instance_t *)ent) < 0)
			scfdie();
	}

	while ((err = scf_iter_next_pg(iter, pg)) == 1) {
		if (scf_pg_get_flags(pg, &flags) != 0)
			scfdie();

		/* skip nonpersistent pgs */
		if (flags & SCF_PG_FLAG_NONPERSISTENT)
			continue;

		ret = 0;
		break;
	}

	if (err == -1)
		scfdie();

	scf_pg_destroy(pg);
	scf_iter_destroy(iter);

	return (ret);
}

/* return 1 if the service has no instances, else return 0 */
static int
svc_has_no_insts(scf_service_t *svc)
{
	scf_instance_t *inst;
	scf_iter_t *iter;
	int r;
	int ret = 1;

	if ((inst = scf_instance_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_service_instances(iter, svc) != 0)
		scfdie();

	r = scf_iter_next_instance(iter, inst);
	if (r == 1) {
		ret = 0;
	} else if (r == 0) {
		ret = 1;
	} else if (r == -1) {
		scfdie();
	} else {
		bad_error("scf_iter_next_instance", r);
	}

	scf_iter_destroy(iter);
	scf_instance_destroy(inst);

	return (ret);
}

/*
 * Entity deletion.
 */

/*
 * Delete the property group <fmri>/:properties/<name>.  Returns
 * SCF_ERROR_NONE on success (or if the entity is not found),
 * SCF_ERROR_INVALID_ARGUMENT if the fmri is bad, SCF_ERROR_TYPE_MISMATCH if
 * the pg is the wrong type, or SCF_ERROR_PERMISSION_DENIED if permission was
 * denied.
 */
static scf_error_t
delete_dependency_pg(const char *fmri, const char *name, int remove)
{
	void *entity = NULL;
	int isservice;
	scf_propertygroup_t *pg = NULL;
	scf_error_t result;
	char *pgty;
	scf_service_t *svc = NULL;
	scf_instance_t *inst = NULL;
	scf_iter_t *iter = NULL;
	char *name_buf = NULL;
	int ret;

	result = fmri_to_entity(g_hndl, fmri, &entity, &isservice);
	switch (result) {
	case SCF_ERROR_NONE:
		break;

	case SCF_ERROR_NO_MEMORY:
		uu_die(gettext("Out of memory.\n"));
		/* NOTREACHED */

	case SCF_ERROR_INVALID_ARGUMENT:
	case SCF_ERROR_CONSTRAINT_VIOLATED:
		return (SCF_ERROR_INVALID_ARGUMENT);

	case SCF_ERROR_NOT_FOUND:
		result = SCF_ERROR_NONE;
		goto out;

	default:
		bad_error("fmri_to_entity", result);
	}

	pg = scf_pg_create(g_hndl);
	if (pg == NULL)
		scfdie();

	if (entity_get_pg(entity, isservice, name, pg) != 0) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();

		result = SCF_ERROR_NONE;
		goto out;
	}

	pgty = safe_malloc(max_scf_pg_type_len + 1);

	if (scf_pg_get_type(pg, pgty, max_scf_pg_type_len + 1) < 0)
		scfdie();

	if (strcmp(pgty, SCF_GROUP_DEPENDENCY) != 0) {
		result = SCF_ERROR_TYPE_MISMATCH;
		free(pgty);
		goto out;
	}

	free(pgty);

	if (remove)
		ret = scf_pg_remove(pg);
	else
		ret = scf_pg_delete(pg);

	if (ret != 0) {
		result = scf_error();
		if (result != SCF_ERROR_PERMISSION_DENIED)
			scfdie();
		goto out;
	}

	/*
	 * We have to handle the case where we've just deleted the last
	 * property group of a "dummy" entity (instance or service).
	 * A "dummy" entity is an entity only present to hold an
	 * external dependency.
	 * So, in the case we deleted the last property group then we
	 * can also delete the entity. If the entity is an instance then
	 * we must verify if this was the last instance for the service
	 * and if it is, we can also delete the service if it doesn't
	 * have any property group either.
	 */

	result = SCF_ERROR_NONE;

	if (isservice) {
		svc = (scf_service_t *)entity;

		if ((inst = scf_instance_create(g_hndl)) == NULL ||
		    (iter = scf_iter_create(g_hndl)) == NULL)
			scfdie();

		name_buf = safe_malloc(max_scf_name_len + 1);
	} else {
		inst = (scf_instance_t *)entity;
	}

	/*
	 * If the entity is an instance and we've just deleted its last
	 * property group then we should delete it.
	 */
	if (!isservice && entity_has_no_pgs(entity, isservice)) {
		/* find the service before deleting the inst. - needed later */
		if ((svc = scf_service_create(g_hndl)) == NULL)
			scfdie();

		if (scf_instance_get_parent(inst, svc) != 0)
			scfdie();

		/* delete the instance */
		if (scf_instance_delete(inst) != 0) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			result = SCF_ERROR_PERMISSION_DENIED;
			goto out;
		}
		/* no need to refresh the instance */
		inst = NULL;
	}

	/*
	 * If the service has no more instances and pgs or we just deleted the
	 * last instance and the service doesn't have anymore propery groups
	 * then the service should be deleted.
	 */
	if (svc != NULL &&
	    svc_has_no_insts(svc) &&
	    entity_has_no_pgs((void *)svc, 1)) {
		if (scf_service_delete(svc) == 0) {
			if (isservice) {
				/* no need to refresh the service */
				svc = NULL;
			}

			goto out;
		}

		if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
			scfdie();

		result = SCF_ERROR_PERMISSION_DENIED;
	}

	/* if the entity has not been deleted, refresh it */
	if ((isservice && svc != NULL) || (!isservice && inst != NULL)) {
		(void) refresh_entity(isservice, entity, fmri, inst, iter,
		    name_buf);
	}

out:
	if (isservice && (inst != NULL && iter != NULL)) {
		free(name_buf);
		scf_iter_destroy(iter);
		scf_instance_destroy(inst);
	}

	if (!isservice && svc != NULL) {
		scf_service_destroy(svc);
	}

	scf_pg_destroy(pg);
	if (entity != NULL)
		entity_destroy(entity, isservice);

	return (result);
}

static int
delete_dependents(scf_propertygroup_t *pg, int remove)
{
	char *pgty, *name, *fmri;
	scf_property_t *prop;
	scf_value_t *val;
	scf_iter_t *iter;
	int r;
	scf_error_t err;

	/* Verify that the pg has the correct type. */
	pgty = safe_malloc(max_scf_pg_type_len + 1);
	if (scf_pg_get_type(pg, pgty, max_scf_pg_type_len + 1) < 0)
		scfdie();

	if (strcmp(pgty, scf_group_framework) != 0) {
		if (g_verbose) {
			fmri = safe_malloc(max_scf_fmri_len + 1);
			if (scf_pg_to_fmri(pg, fmri, max_scf_fmri_len + 1) < 0)
				scfdie();

			warn(gettext("Property group %s is not of expected "
			    "type %s.\n"), fmri, scf_group_framework);

			free(fmri);
		}

		free(pgty);
		return (-1);
	}

	free(pgty);

	/* map delete_dependency_pg onto the properties. */
	if ((prop = scf_property_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_pg_properties(iter, pg) != SCF_SUCCESS)
		scfdie();

	name = safe_malloc(max_scf_name_len + 1);
	fmri = safe_malloc(max_scf_fmri_len + 2);

	while ((r = scf_iter_next_property(iter, prop)) == 1) {
		scf_type_t ty;

		if (scf_property_get_name(prop, name, max_scf_name_len + 1) < 0)
			scfdie();

		if (scf_property_type(prop, &ty) != SCF_SUCCESS)
			scfdie();

		if ((ty != SCF_TYPE_ASTRING &&
		    prop_check_type(prop, SCF_TYPE_FMRI) != 0) ||
		    prop_get_val(prop, val) != 0)
			continue;

		if (scf_value_get_astring(val, fmri, max_scf_fmri_len + 2) < 0)
			scfdie();

		err = delete_dependency_pg(fmri, name, remove);
		if (err == SCF_ERROR_INVALID_ARGUMENT && g_verbose) {
			if (scf_property_to_fmri(prop, fmri,
			    max_scf_fmri_len + 2) < 0)
				scfdie();

			warn(gettext("Value of %s is not a valid FMRI.\n"),
			    fmri);
		} else if (err == SCF_ERROR_TYPE_MISMATCH && g_verbose) {
			warn(gettext("Property group \"%s\" of entity \"%s\" "
			    "does not have dependency type.\n"), name, fmri);
		} else if (err == SCF_ERROR_PERMISSION_DENIED && g_verbose) {
			warn(gettext("Could not delete property group \"%s\" "
			    "of entity \"%s\" (permission denied).\n"), name,
			    fmri);
		}
	}
	if (r == -1)
		scfdie();

	scf_value_destroy(val);
	scf_property_destroy(prop);

	return (0);
}

/*
 * Returns 1 if the instance may be running, and 0 otherwise.
 */
static int
inst_is_running(scf_instance_t *inst)
{
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_value_t *val;
	char buf[MAX_SCF_STATE_STRING_SZ];
	int ret = 0;
	ssize_t szret;

	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	if (scf_instance_get_pg(inst, SCF_PG_RESTARTER, pg) != SCF_SUCCESS) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();
		goto out;
	}

	if (pg_get_prop(pg, SCF_PROPERTY_STATE, prop) != 0 ||
	    prop_check_type(prop, SCF_TYPE_ASTRING) != 0 ||
	    prop_get_val(prop, val) != 0)
		goto out;

	szret = scf_value_get_astring(val, buf, sizeof (buf));
	assert(szret >= 0);

	ret = (strcmp(buf, SCF_STATE_STRING_ONLINE) == 0 ||
	    strcmp(buf, SCF_STATE_STRING_DEGRADED) == 0) ? 1 : 0;

out:
	scf_value_destroy(val);
	scf_property_destroy(prop);
	scf_pg_destroy(pg);
	return (ret);
}

static uint8_t
pg_is_external_dependency(scf_propertygroup_t *pg)
{
	char *type;
	scf_value_t *val;
	scf_property_t *prop;
	uint8_t b = B_FALSE;

	type = safe_malloc(max_scf_pg_type_len + 1);

	if (scf_pg_get_type(pg, type, max_scf_pg_type_len + 1) < 0)
		scfdie();

	if ((prop = scf_property_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	if (strcmp(type, SCF_GROUP_DEPENDENCY) == 0) {
		if (pg_get_prop(pg, scf_property_external, prop) == 0) {
			if (scf_property_get_value(prop, val) != 0)
				scfdie();
			if (scf_value_get_boolean(val, &b) != 0)
				scfdie();
		}
	}

	free(type);
	(void) scf_value_destroy(val);
	(void) scf_property_destroy(prop);

	return (b);
}

#define	DELETE_FAILURE			-1
#define	DELETE_SUCCESS_NOEXTDEPS	0
#define	DELETE_SUCCESS_EXTDEPS		1

/*
 * lscf_instance_delete() deletes an instance.  Before calling
 * scf_instance_delete(), though, we make sure the instance isn't
 * running and delete dependencies in other entities which the instance
 * declared as "dependents".  If there are dependencies which were
 * created for other entities, then instead of deleting the instance we
 * make it "empty" by deleting all other property groups and all
 * snapshots.
 *
 * lscf_instance_delete() verifies that there is no external dependency pgs
 * before suppressing the instance. If there is, then we must not remove them
 * now in case the instance is re-created otherwise the dependencies would be
 * lost. The external dependency pgs will be removed if the dependencies are
 * removed.
 *
 * Returns:
 *  DELETE_FAILURE		on failure
 *  DELETE_SUCCESS_NOEXTDEPS	on success - no external dependencies
 *  DELETE_SUCCESS_EXTDEPS	on success - external dependencies
 */
static int
lscf_instance_delete(scf_instance_t *inst, int force, int remove)
{
	scf_propertygroup_t *pg;
	scf_snapshot_t *snap;
	scf_iter_t *iter;
	int err;
	int external = 0;

	/* If we're not forcing and the instance is running, refuse. */
	if (!force && inst_is_running(inst)) {
		char *fmri;

		fmri = safe_malloc(max_scf_fmri_len + 1);

		if (scf_instance_to_fmri(inst, fmri, max_scf_fmri_len + 1) < 0)
			scfdie();

		semerr(gettext("Instance %s may be running.  "
		    "Use delete -f if it is not.\n"), fmri);

		free(fmri);
		return (DELETE_FAILURE);
	}

	pg = scf_pg_create(g_hndl);
	if (pg == NULL)
		scfdie();

	if (scf_instance_get_pg(inst, SCF_PG_DEPENDENTS, pg) == 0)
		(void) delete_dependents(pg, remove);
	else if (scf_error() != SCF_ERROR_NOT_FOUND)
		scfdie();

	scf_pg_destroy(pg);

	/*
	 * If the instance has some external dependencies then we must
	 * keep them in case the instance is reimported otherwise the
	 * dependencies would be lost on reimport.
	 */
	if ((iter = scf_iter_create(g_hndl)) == NULL ||
	    (pg = scf_pg_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_instance_pgs(iter, inst) < 0)
		scfdie();

	while ((err = scf_iter_next_pg(iter, pg)) == 1) {
		if (pg_is_external_dependency(pg)) {
			external = 1;
			continue;
		}

		if (remove) {
			if (scf_pg_remove(pg) != 0) {
				switch (scf_error()) {
				case SCF_ERROR_PERMISSION_DENIED:
					semerr(emsg_permission_denied);
					(void) scf_iter_destroy(iter);
					(void) scf_pg_destroy(pg);
					return (DELETE_FAILURE);
				case SCF_ERROR_DELETED:
					break;
				default:
					scfdie();
				}
			}
		} else {
			if (scf_pg_delete(pg) != 0) {
				switch (scf_error()) {
				case SCF_ERROR_PERMISSION_DENIED:
					semerr(emsg_permission_denied);
					(void) scf_iter_destroy(iter);
					(void) scf_pg_destroy(pg);
					return (DELETE_FAILURE);
				case SCF_ERROR_DELETED:
					break;
				default:
					scfdie();
				}
			}
		}
	}

	if (err == -1)
		scfdie();

	(void) scf_iter_destroy(iter);
	(void) scf_pg_destroy(pg);

	if (external) {
		/*
		 * All the pgs have been deleted for the instance except
		 * the ones holding the external dependencies.
		 * For the job to be complete, we must also delete the
		 * snapshots associated with the instance.
		 */
		if ((snap = scf_snapshot_create((scf_handle_t *)g_hndl)) ==
		    NULL)
			scfdie();
		if ((iter = scf_iter_create((scf_handle_t *)g_hndl)) == NULL)
			scfdie();

		if (scf_iter_instance_snapshots(iter, inst) == -1)
			scfdie();

		while ((err = scf_iter_next_snapshot(iter, snap)) == 1) {
			if (_scf_snapshot_delete(snap) != 0) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();

				semerr(emsg_permission_denied);

				(void) scf_iter_destroy(iter);
				(void) scf_snapshot_destroy(snap);
				return (DELETE_FAILURE);
			}
		}

		if (err == -1)
			scfdie();

		(void) scf_iter_destroy(iter);
		(void) scf_snapshot_destroy(snap);
		return (DELETE_SUCCESS_EXTDEPS);
	}

	if (remove) {
		if (scf_instance_remove(inst) != 0) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			semerr(emsg_permission_denied);

			return (DELETE_FAILURE);
		}
	} else {
		if (scf_instance_delete(inst) != 0) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			semerr(emsg_permission_denied);

			return (DELETE_FAILURE);
		}
	}

	return (DELETE_SUCCESS_NOEXTDEPS);
}

/*
 * lscf_service_delete() deletes a service.  Before calling
 * scf_service_delete(), though, we call lscf_instance_delete() for
 * each of the instances and delete dependencies in other entities
 * which were created as "dependents" of this service.  If there are
 * dependencies which were created for other entities, then we delete
 * all other property groups in the service and leave it as "empty".
 *
 * lscf_service_delete() verifies that there is no external dependency
 * pgs at the instance & service level before suppressing the service.
 * If there is, then we must not remove them now in case the service
 * is re-imported otherwise the dependencies would be lost. The external
 * dependency pgs will be removed if the dependencies are removed.
 *
 * Returns:
 *   DELETE_FAILURE		on failure
 *   DELETE_SUCCESS_NOEXTDEPS	on success - no external dependencies
 *   DELETE_SUCCESS_EXTDEPS	on success - external dependencies
 *
 * We are changing the NORMAL order of operations here, and will delete
 * the property groups at the service level before deleting the
 * instances.  This will allow for the delete of the service to
 * actually remove all the properties associated with the property
 * group as the snapshots are removed.  This is because in most
 * cases when deleting a property group we want to protect the
 * last layer of each property.  But in the case of a full entity
 * deletion then we want to drop the entire set.  In the backend
 * we do this protection but must short circuit that by removing
 * the higher entity (pg) before the property.  This will allow
 * that to be done correctly in the case of a snap shot row attempting
 * to remove the highest layer.
 *
 * Note : both svccfg delete and svccfg cleanup use this function
 * and must set the operation, so that the backend knows to really
 * remove the rows from the repository (in the case of cleanup) or
 * mute the rows in the case of delete which is an administrative
 * customization.
 */
static int
lscf_service_delete(scf_service_t *svc, int force, int remove)
{
	int r;
	scf_instance_t *inst;
	scf_propertygroup_t *pg;
	scf_iter_t *iter;
	int ret;
	int external = 0;

	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	/* Delete dependency property groups in dependent services. */
	if (scf_service_get_pg(svc, SCF_PG_DEPENDENTS, pg) == 0)
		(void) delete_dependents(pg, remove);
	else if (scf_error() != SCF_ERROR_NOT_FOUND)
		scfdie();

	scf_iter_destroy(iter);
	scf_pg_destroy(pg);

	/*
	 * If the service has some external dependencies then we don't
	 * want to remove them in case the service is re-imported.
	 */
	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_service_pgs(iter, svc) < 0)
		scfdie();

	while ((r = scf_iter_next_pg(iter, pg)) == 1) {
		if (pg_is_external_dependency(pg)) {
			external |= 2;
			continue;
		}

		if (remove) {
			if (scf_pg_remove(pg) != 0) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();
				else {
					semerr(emsg_permission_denied);

					(void) scf_iter_destroy(iter);
					(void) scf_pg_destroy(pg);
					return (DELETE_FAILURE);
				}
			}
		} else {
			if (scf_pg_delete(pg) != 0) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();
				else {
					semerr(emsg_permission_denied);

					(void) scf_iter_destroy(iter);
					(void) scf_pg_destroy(pg);
					return (DELETE_FAILURE);
				}
			}
		}
	}

	if (r == -1)
		scfdie();

	(void) scf_iter_destroy(iter);
	(void) scf_pg_destroy(pg);

	if ((inst = scf_instance_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_service_instances(iter, svc) != 0)
		scfdie();

	for (r = scf_iter_next_instance(iter, inst);
	    r == 1;
	    r = scf_iter_next_instance(iter, inst)) {

		ret = lscf_instance_delete(inst, force, remove);
		if (ret == DELETE_FAILURE) {
			scf_iter_destroy(iter);
			scf_instance_destroy(inst);
			return (DELETE_FAILURE);
		}

		/*
		 * Record the fact that there is some external dependencies
		 * at the instance level.
		 */
		if (ret == DELETE_SUCCESS_EXTDEPS)
			external |= 1;
	}

	scf_iter_destroy(iter);
	scf_instance_destroy(inst);

	if (r != 0)
		scfdie();

	if (external != 0)
		return (DELETE_SUCCESS_EXTDEPS);

	if (remove) {
		if (scf_service_remove(svc) == 0)
			return (DELETE_SUCCESS_NOEXTDEPS);
	} else {
		if (scf_service_delete(svc) == 0)
			return (DELETE_SUCCESS_NOEXTDEPS);
	}

	if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
		scfdie();

	semerr(emsg_permission_denied);
	return (DELETE_FAILURE);
}

static int
delete_callback(void *data, scf_walkinfo_t *wip)
{
	int force = (int)data;

	if (wip->inst != NULL)
		(void) lscf_instance_delete(wip->inst, force, 0);
	else
		(void) lscf_service_delete(wip->svc, force, 0);

	return (0);
}

void
lscf_delete(const char *fmri, int force)
{
	scf_service_t *svc;
	scf_instance_t *inst;
	int ret;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_snap != NULL) {
		if (!snaplevel_is_instance(cur_level)) {
			char *buf;

			buf = safe_malloc(max_scf_name_len + 1);
			if (scf_instance_get_name(cur_inst, buf,
			    max_scf_name_len + 1) >= 0) {
				if (strcmp(buf, fmri) == 0) {
					semerr(emsg_cant_modify_snapshots);
					free(buf);
					return;
				}
			} else if (scf_error() != SCF_ERROR_DELETED) {
				scfdie();
			}
			free(buf);
		}
	} else if (cur_inst != NULL) {
		/* EMPTY */;
	} else if (cur_svc != NULL) {

		/*
		 * Precondition: cur_svc != NULL && cur_inst == NULL
		 *
		 * Handle the case where the fmri argument exactly matches
		 * an instance of the currently selected servcie.
		 */

		inst = scf_instance_create(g_hndl);
		if (inst == NULL)
			scfdie();

		if (scf_service_get_instance(cur_svc, fmri, inst) ==
		    SCF_SUCCESS) {
			(void) lscf_instance_delete(inst, force, 0);
			scf_instance_destroy(inst);
			return;
		}

		if (scf_error() != SCF_ERROR_NOT_FOUND &&
		    scf_error() != SCF_ERROR_INVALID_ARGUMENT)
			scfdie();

		scf_instance_destroy(inst);
	} else {
		assert(cur_scope != NULL);

		/*
		 * Precondition: cur_svc == NULL && cur_inst == NULL
		 *
		 * Handle the case where the fmri argument exactly matches
		 * a service.
		 */

		svc = scf_service_create(g_hndl);
		if (svc == NULL)
			scfdie();

		if (scf_scope_get_service(cur_scope, fmri, svc) ==
		    SCF_SUCCESS) {
			(void) lscf_service_delete(svc, force, 0);
			scf_service_destroy(svc);
			return;
		}

		if (scf_error() != SCF_ERROR_NOT_FOUND &&
		    scf_error() != SCF_ERROR_INVALID_ARGUMENT)
			scfdie();

		scf_service_destroy(svc);
	}

	/*
	 * Match FMRI to entity.
	 */
	if ((ret = scf_walk_fmri(g_hndl, 1, (char **)&fmri, SCF_WALK_SERVICE,
	    delete_callback, (void *)force, NULL, semerr)) != 0) {
		semerr(gettext("Failed to walk instances: %s\n"),
		    scf_strerror(ret));
	}
}



/*
 * :properties commands.  These all end with "pg" or "prop" and generally
 * operate on the currently selected entity.
 */

/*
 * Used to point to fmri for service or instance, so that it can be
 * printed out only once on an as-needed basis.
 */
static const char *listprop_prefix = NULL;
static int listprop_indent = 0;

/*
 * Property listing.  List the property groups, properties, their types and
 * their values for the currently selected entity.
 */
static void
list_pg_info(const scf_propertygroup_t *pg, const char *name, size_t namewidth,
    const scf_decoration_layer_t *layers_out)
{
	char *buf;
	uint32_t flags;
	scf_decoration_t *dec = NULL;
	scf_decoration_layer_t layer;
	scf_decoration_layer_t skip_layer = SCF_DECORATION_INVALID;
	boolean_t skip_masked = B_FALSE;

	buf = safe_malloc(max_scf_pg_type_len + 1);

	if (scf_pg_get_type(pg, buf, max_scf_pg_type_len + 1) < 0)
		scfdie();

	if (scf_pg_get_flags(pg, &flags) != SCF_SUCCESS)
		scfdie();

	if ((dec = scf_decoration_create(g_hndl)) == NULL)
		scfdie();

	/*
	 * If non-NULL, layers_out will point to either
	 * SCF_DECORATION_TOP, or to a list of one or
	 * more real decoration layers, terminated by
	 * SCF_DECORATION_INVALID.
	 *
	 * If NULL, this corresponds to SCF_DECORATION_TOP with
	 * do_print_layer == 0.
	 */

	if (layers_out == NULL) {
		layers_out = layers_top;
		skip_masked = B_TRUE;
	}

	assert(*layers_out != SCF_DECORATION_INVALID);

	while ((layer = *layers_out++) != SCF_DECORATION_INVALID) {
		int pgim;

		if (skip_layer == layer)
			continue;

		if (scf_pg_get_decoration(pg, layer, dec) != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_NOT_FOUND &&
			    scf_error() != SCF_ERROR_CONSTRAINT_VIOLATED)
				scfdie();
			if (layer != SCF_DECORATION_TOP)
				continue;
		}

		if ((pgim = scf_pg_is_masked(pg)) == -1)
			scfdie();

		if (skip_masked && pgim)
			continue;

		if (listprop_prefix != NULL) {
			safe_printf("%s\n", listprop_prefix);
			listprop_prefix = NULL;
		}

		safe_printf("%*s%-*s  %-11s", listprop_indent, "",
		    namewidth, name, buf);

		if (do_print_layer) {
			if (flags & SCF_PG_FLAG_NONPERSISTENT) {
				safe_printf(" %-14s ", "<none>");
			} else {
				if (layer == SCF_DECORATION_TOP) {
					if (scf_decoration_get_layer(dec,
					    &layer) != SCF_SUCCESS)
						scfdie();
					skip_layer = layer;
				}
				safe_printf(" %-14s ",
				    scf_decoration_layer_to_string(layer));
			}
		}

		safe_printf(" %6s ", pgim ? "MASKED" : "");

		if (flags & SCF_PG_FLAG_NONPERSISTENT)
			safe_printf("\tNONPERSISTENT");

		safe_printf("\n");
	}

	scf_decoration_destroy(dec);
	free(buf);
}

static boolean_t
prop_has_multiple_values(const scf_property_t *prop, scf_value_t *val)
{
	if (scf_property_get_value(prop, val) == 0) {
		return (B_FALSE);
	} else {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			return (B_FALSE);
		case SCF_ERROR_PERMISSION_DENIED:
		case SCF_ERROR_CONSTRAINT_VIOLATED:
			return (B_TRUE);
		default:
			scfdie();
			/*NOTREACHED*/
		}
	}
}

static boolean_t
dec_has_multiple_values(const scf_decoration_t *dec, scf_value_t *val)
{
	if (scf_decoration_get_value(dec, val) == 0) {
		return (B_FALSE);
	} else {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			return (B_FALSE);
		case SCF_ERROR_PERMISSION_DENIED:
		case SCF_ERROR_CONSTRAINT_VIOLATED:
			return (B_TRUE);
		default:
			scfdie();
			/*NOTREACHED*/
		}
	}
}

static void
print_value(const scf_property_t *prop, const scf_decoration_t *dec)
{
	scf_value_t *val;
	scf_iter_t *iter;
	int ret;
	int multiple_strings = 0;

	if ((iter = scf_iter_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	if (dec != NULL) {
		if (dec_has_multiple_values(dec, val) &&
		    (scf_value_type(val) == SCF_TYPE_ASTRING ||
		    scf_value_type(val) == SCF_TYPE_USTRING))
			multiple_strings = 1;

		if (scf_iter_decoration_values(iter, dec) != SCF_SUCCESS)
			scfdie();
	} else {
		if (prop_has_multiple_values(prop, val) &&
		    (scf_value_type(val) == SCF_TYPE_ASTRING ||
		    scf_value_type(val) == SCF_TYPE_USTRING))
			multiple_strings = 1;

		if (scf_iter_property_values(iter, prop) != SCF_SUCCESS)
			scfdie();
	}

	while ((ret = scf_iter_next_value(iter, val)) == 1) {
		char *buf;
		ssize_t vlen, szret;

		vlen = scf_value_get_as_string(val, NULL, 0);
		if (vlen < 0)
			scfdie();

		buf = safe_malloc(vlen + 1);

		szret = scf_value_get_as_string(val, buf, vlen + 1);
		if (szret < 0)
			scfdie();
		assert(szret <= vlen);

		/*
		 * Need a space before every value.  Callers shouldn't
		 * output a leading space.
		 */
		safe_printf(" ");

		/* This is to be human-readable, so don't use CHARS_TO_QUOTE */
		if (multiple_strings || strpbrk(buf, " \t\n\"()") != NULL) {
			safe_printf("\"");
			(void) quote_and_print(buf, stdout, 0);
			(void) putchar('"');
			if (ferror(stdout)) {
				(void) putchar('\n');
				uu_die(gettext("Error writing to stdout.\n"));
			}
		} else {
			safe_printf("%s", buf);
		}

		free(buf);
	}
	if (ret != 0 && scf_error() != SCF_ERROR_PERMISSION_DENIED)
		scfdie();

	scf_value_destroy(val);
	scf_iter_destroy(iter);
}

static void
print_propname(const char *name, size_t len, int withpg)
{
	char *prop;

	if (withpg == 1) {
		safe_printf("%*s%-*s", listprop_indent, "", len, name);

	} else {
		prop = strchr(name, '/');
		assert(prop != NULL);

		prop++;
		safe_printf("%*s%-*s", listprop_indent, "", len, prop);
	}
}

static void
print_pgname(const char *name)
{
	char *pg, *slash;

	pg = safe_strdup(name);

	slash = strchr(pg, '/');
	assert(slash != NULL);
	*slash = '\0';

	safe_printf("%-*s", max_scf_name_len, pg);

	free(pg);
}

static void
print_instname(const char *name)
{
	safe_printf("%s", name);
}

static void
print_servicename(const char *name)
{
	safe_printf("%s", name);
}

static void
print_layer(const scf_decoration_t *dec)
{
	scf_decoration_layer_t layer;

	if (dec != NULL) {
		if (scf_decoration_get_layer(dec, &layer) != SCF_SUCCESS)
			scfdie();
		safe_printf("%-14s", scf_decoration_layer_to_string(layer));
	} else {
		safe_printf(" %-14s", "<none>");
	}
}

static void
print_proptype(const scf_property_t *prop, const scf_decoration_t *dec)
{
	const char *type;

	if (dec != NULL) {
		type = dec_to_typestr(dec);
		assert(type != NULL);
	} else {
		type = prop_to_typestr(prop);
		assert(type != NULL);
	}

	safe_printf("%-11s", type);
}

static void
print_file(const scf_decoration_t *dec)
{
	char *bundle_name;

	if (dec == NULL) {
		safe_printf("-");
		return;
	}

	bundle_name = safe_malloc(max_scf_value_len + 1);

	if (scf_decoration_get_bundle(dec, bundle_name,
	    max_scf_value_len + 1) == -1)
		scfdie();

	if (strcmp(bundle_name, "") == 0)
		safe_printf("-");
	else
		safe_printf("%s", bundle_name);

	free(bundle_name);
}

static void
print_masked(const scf_property_t *prop, int always_print)
{
	int pim = scf_property_is_masked(prop);

	if (pim == -1)
		scfdie();

	if (always_print == 0)
		safe_printf("%6s", pim ? "MASKED" : "");
	else
		safe_printf("%6s", pim ? "MASKED" : "-");
}

/*
 * For now, print "-", but should plumb this through to the
 * time.
 */
static void
/*ARGSUSED*/
print_time(const scf_property_t *prop, const scf_decoration_t *dec)
{
	safe_printf("-");
}

static fname_t
map_fname(const char *name)
{
	fname_t f;

	if (strcmp(name, "propname") == 0)
		f = F_PROPNAME;
	else if (strcmp(name, "pgname") == 0)
		f = F_PGNAME;
	else if (strcmp(name, "instname") == 0)
		f = F_INSTNAME;
	else if (strcmp(name, "servicename") == 0)
		f = F_SVCNAME;
	else if (strcmp(name, "layer") == 0)
		f = F_LAYER;
	else if (strcmp(name, "proptype") == 0)
		f = F_PROPTYPE;
	else if (strcmp(name, "value") == 0)
		f = F_VALUE;
	else if (strcmp(name, "file") == 0)
		f = F_FILE;
	else if (strcmp(name, "masked") == 0)
		f = F_MASKED;
	else if (strcmp(name, "time") == 0)
		f = F_TIME;
	else
		f = F_INVALID;

	return (f);
}

static int
map_field(const char *fieldnames, fnames_t *fields)
{
	int c;
	char *name, *arg, *f_dup;
	fname_t f;
	int format_errors = 0;

	fields->f_size = 0;

	/* no mapping to do.  no problem. */
	if (fieldnames == NULL)
		return (0);

	f_dup = safe_strdup(fieldnames);
	arg = f_dup;

	while (arg != NULL) {
		while ((c = *arg) != '\0' && (c == ','))
			arg++;
		if (c == '\0')
			break;

		name = arg;
		arg = strchr(arg, ',');
		if (arg != NULL) {
			*arg++ = '\0';
		}

		f = map_fname(name);
		if (f == F_INVALID) {
			uu_warn("unknown output format: -o %s\n", name);
			format_errors++;
			continue;
		}

		if (fields->f_size + 1 > MAX_FARRAY_SIZE) {
			uu_warn("too many output fields\n");
			format_errors++;
			continue;
		}

		fields->f_array[fields->f_size++] = f;
	}

	free(f_dup);
	if (format_errors != 0 || fields->f_size == 0)
		return (-2);

	return (0);
}

static void
print_field(fname_t f, const scf_property_t *prop, const scf_decoration_t *dec,
    const char *name, size_t len)
{
	if (f == F_PROPNAME)
		print_propname(name, len, 0);
	else if (f == F_PGNAME)
		print_pgname(name);
	else if (f == F_INSTNAME)
		print_instname(name);
	else if (f == F_SVCNAME)
		print_servicename(name);
	else if (f == F_LAYER)
		print_layer(dec);
	else if (f == F_PROPTYPE)
		print_proptype(prop, dec);
	else if (f == F_VALUE)
		print_value(prop, dec);
	else if (f == F_FILE)
		print_file(dec);
	else if (f == F_MASKED)
		print_masked(prop, 1);
	else if (f == F_TIME)
		print_time(prop, dec);
	else
		abort();
}

/*
 * Lists property value(s) in the requested decoration/property.
 */
static void
list_prop_print(const scf_property_t *prop, const scf_decoration_t *dec,
    const char *name, size_t len, fnames_t *fields)
{
	if (fields == NULL || fields->f_size == 0) {
		if (listprop_prefix != NULL) {
			safe_printf("%s\n", listprop_prefix);
			listprop_prefix = NULL;
		}

		print_propname(name, len, 1);
		safe_printf(" ");
		print_proptype(prop, dec);

		/* Output layer as requested */
		if (do_print_layer) {
			safe_printf(" ");
			print_layer(dec);
			safe_printf(" ");

			/*
			 * Only bother to print masked if we're also printing
			 * the layer.  Otherwise, it's just extra space.
			 */
			print_masked(prop, 0);
		}

		print_value(prop, dec);
	} else {
		int i;

		for (i = 0; i < (fields->f_size - 1); i++) {
			print_field(fields->f_array[i], prop, dec, name, len);
			safe_printf(" ");
		}

		print_field(fields->f_array[i], prop, dec, name, len);
	}

	if (putchar('\n') != '\n')
		uu_die(gettext("Could not output newline"));

}

/*
 * Lists property value(s) in the requested decoration layer. For properties in
 * non-persistent repository which don't have decorations, get values directly
 * from property rather than through decoration.
 */
static void
list_prop_info(const scf_property_t *prop, const char *name, size_t len,
    fnames_t *fields, const scf_decoration_layer_t *layers_out)
{
	scf_decoration_t	*dec;
	scf_decoration_layer_t	 layer;
	scf_decoration_layer_t	 skip_layer = SCF_DECORATION_INVALID;
	boolean_t		 skip_masked = B_FALSE;

	if ((dec = scf_decoration_create(g_hndl)) == NULL)
		scfdie();

	/*
	 * If non-NULL, layers_out will point to either
	 * SCF_DECORATION_TOP, or to a list of one or
	 * more real decoration layers, terminated by
	 * SCF_DECORATION_INVALID.
	 *
	 * If NULL, this corresponds to SCF_DECORATION_TOP with
	 * do_print_layer == 0.
	 */

	if (layers_out == NULL) {
		layers_out = layers_top;
		skip_masked = B_TRUE;
	}

	assert(*layers_out != SCF_DECORATION_INVALID);

	while ((layer = *layers_out++) != SCF_DECORATION_INVALID) {
		scf_decoration_layer_t l;
		int pim;

		if (skip_layer == layer)
			continue;

		if ((pim = scf_property_is_masked(prop)) == -1)
			scfdie();

		if (skip_masked && pim == 1)
			continue;

		if (scf_property_get_decoration(prop, layer, dec) !=
		    SCF_SUCCESS &&
		    scf_error() != SCF_ERROR_CONSTRAINT_VIOLATED) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();
			if (layer == SCF_DECORATION_TOP)
				list_prop_print(prop, NULL, name, len, fields);
			continue;
		}

		if (scf_decoration_get_layer(dec, &l) != SCF_SUCCESS)
			scfdie();

		list_prop_print(prop, dec, name, len, fields);

		if (layer == SCF_DECORATION_TOP)
			skip_layer = l;
	}

	scf_decoration_destroy(dec);
}

/*
 * Outputs template property group info for the describe subcommand.
 * If 'templates' == 2, verbose output is printed in the format expected
 * for describe -v, which includes all templates fields.  If pg is
 * not NULL, we're describing the template data, not an existing property
 * group, and formatting should be appropriate for describe -t.
 */
static void
list_pg_tmpl(scf_pg_tmpl_t *pgt, scf_propertygroup_t *pg, int templates)
{
	char *buf;
	uint8_t required;
	scf_property_t *stability_prop;
	scf_value_t *stability_val;

	if (templates == 0)
		return;

	if ((stability_prop = scf_property_create(g_hndl)) == NULL ||
	    (stability_val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	if (templates == 2 && pg != NULL) {
		if (scf_pg_get_property(pg, SCF_PROPERTY_STABILITY,
		    stability_prop) == 0) {
			if (prop_check_type(stability_prop,
			    SCF_TYPE_ASTRING) == 0 &&
			    prop_get_val(stability_prop, stability_val) == 0) {
				char *stability;

				stability = safe_malloc(max_scf_value_len + 1);

				if (scf_value_get_astring(stability_val,
				    stability, max_scf_value_len + 1) == -1 &&
				    scf_error() != SCF_ERROR_NOT_FOUND)
					scfdie();

				safe_printf("%s%s: %s\n", TMPL_INDENT,
				    gettext("stability"), stability);

				free(stability);
			}
		} else if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();
	}

	scf_property_destroy(stability_prop);
	scf_value_destroy(stability_val);

	if (pgt == NULL)
		return;

	if (pg == NULL || templates == 2) {
		/* print type info only if scf_tmpl_pg_name succeeds */
		if (scf_tmpl_pg_name(pgt, &buf) != -1) {
			if (pg != NULL)
				safe_printf("%s", TMPL_INDENT);
			safe_printf("%s: ", gettext("name"));
			safe_printf("%s\n", buf);
			free(buf);
		}

		/* print type info only if scf_tmpl_pg_type succeeds */
		if (scf_tmpl_pg_type(pgt, &buf) != -1) {
			if (pg != NULL)
				safe_printf("%s", TMPL_INDENT);
			safe_printf("%s: ", gettext("type"));
			safe_printf("%s\n", buf);
			free(buf);
		}
	}

	if (templates == 2 && scf_tmpl_pg_required(pgt, &required) == 0)
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("required"),
		    required ? "true" : "false");

	if (templates == 2 && scf_tmpl_pg_target(pgt, &buf) > 0) {
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("target"),
		    buf);
		free(buf);
	}

	if (templates == 2 && scf_tmpl_pg_common_name(pgt, NULL, &buf) > 0) {
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("common name"),
		    buf);
		free(buf);
	}

	if (scf_tmpl_pg_description(pgt, NULL, &buf) > 0) {
		if (templates == 2)
			safe_printf("%s%s: %s\n", TMPL_INDENT,
			    gettext("description"), buf);
		else
			safe_printf("%s%s\n", TMPL_INDENT, buf);
		free(buf);
	}

}

/*
 * With as_value set to true, indent as appropriate for the value level.
 * If false, indent to appropriate level for inclusion in constraint
 * or choice printout.
 */
static void
print_template_value_details(scf_prop_tmpl_t *prt, const char *val_buf,
    int as_value)
{
	char *buf;

	if (scf_tmpl_value_common_name(prt, NULL, val_buf, &buf) > 0) {
		if (as_value == 0)
			safe_printf("%s", TMPL_CHOICE_INDENT);
		else
			safe_printf("%s", TMPL_INDENT);
		safe_printf("%s: %s\n", gettext("value common name"), buf);
		free(buf);
	}

	if (scf_tmpl_value_description(prt, NULL, val_buf, &buf) > 0) {
		if (as_value == 0)
			safe_printf("%s", TMPL_CHOICE_INDENT);
		else
			safe_printf("%s", TMPL_INDENT);
		safe_printf("%s: %s\n", gettext("value description"), buf);
		free(buf);
	}
}

static void
print_template_value(scf_prop_tmpl_t *prt, const char *val_buf)
{
	safe_printf("%s%s: ", TMPL_VALUE_INDENT, gettext("value"));
	/* This is to be human-readable, so don't use CHARS_TO_QUOTE */
	safe_printf("%s\n", val_buf);

	print_template_value_details(prt, val_buf, 1);
}

static void
print_template_constraints(scf_prop_tmpl_t *prt, int verbose)
{
	int i, printed = 0;
	scf_values_t values;
	scf_count_ranges_t c_ranges;
	scf_int_ranges_t i_ranges;

	printed = 0;
	i = 0;
	if (scf_tmpl_value_name_constraints(prt, &values) == 0) {
		safe_printf("%s%s:\n", TMPL_VALUE_INDENT,
		    gettext("value constraints"));
		printed++;
		for (i = 0; i < values.value_count; ++i) {
			safe_printf("%s%s: %s\n", TMPL_INDENT,
			    gettext("value name"), values.values_as_strings[i]);
			if (verbose == 1)
				print_template_value_details(prt,
				    values.values_as_strings[i], 0);
		}

		scf_values_destroy(&values);
	}

	if (scf_tmpl_value_count_range_constraints(prt, &c_ranges) == 0) {
		if (printed++ == 0)
			safe_printf("%s%s:\n", TMPL_VALUE_INDENT,
			    gettext("value constraints"));
		for (i = 0; i < c_ranges.scr_num_ranges; ++i) {
			safe_printf("%s%s: %llu to %llu\n", TMPL_INDENT,
			    gettext("range"), c_ranges.scr_min[i],
			    c_ranges.scr_max[i]);
		}
		scf_count_ranges_destroy(&c_ranges);
	} else if (scf_error() == SCF_ERROR_CONSTRAINT_VIOLATED &&
	    scf_tmpl_value_int_range_constraints(prt, &i_ranges) == 0) {
		if (printed++ == 0)
			safe_printf("%s%s:\n", TMPL_VALUE_INDENT,
			    gettext("value constraints"));
		for (i = 0; i < i_ranges.sir_num_ranges; ++i) {
			safe_printf("%s%s: %lld to %lld\n", TMPL_INDENT,
			    gettext("range"), i_ranges.sir_min[i],
			    i_ranges.sir_max[i]);
		}
		scf_int_ranges_destroy(&i_ranges);
	}
}

static void
print_template_choices(scf_prop_tmpl_t *prt, int verbose)
{
	int i = 0, printed = 0;
	scf_values_t values;
	scf_count_ranges_t c_ranges;
	scf_int_ranges_t i_ranges;

	printed = 0;
	if (scf_tmpl_value_name_choices(prt, &values) == 0) {
		safe_printf("%s%s:\n", TMPL_VALUE_INDENT,
		    gettext("value constraints"));
		printed++;
		for (i = 0; i < values.value_count; i++) {
			safe_printf("%s%s: %s\n", TMPL_INDENT,
			    gettext("value name"), values.values_as_strings[i]);
			if (verbose == 1)
				print_template_value_details(prt,
				    values.values_as_strings[i], 0);
		}

		scf_values_destroy(&values);
	}

	if (scf_tmpl_value_count_range_choices(prt, &c_ranges) == 0) {
		for (i = 0; i < c_ranges.scr_num_ranges; ++i) {
			if (printed++ == 0)
				safe_printf("%s%s:\n", TMPL_VALUE_INDENT,
				    gettext("value choices"));
			safe_printf("%s%s: %llu to %llu\n", TMPL_INDENT,
			    gettext("range"), c_ranges.scr_min[i],
			    c_ranges.scr_max[i]);
		}
		scf_count_ranges_destroy(&c_ranges);
	} else if (scf_error() == SCF_ERROR_CONSTRAINT_VIOLATED &&
	    scf_tmpl_value_int_range_choices(prt, &i_ranges) == 0) {
		for (i = 0; i < i_ranges.sir_num_ranges; ++i) {
			if (printed++ == 0)
				safe_printf("%s%s:\n", TMPL_VALUE_INDENT,
				    gettext("value choices"));
			safe_printf("%s%s: %lld to %lld\n", TMPL_INDENT,
			    gettext("range"), i_ranges.sir_min[i],
			    i_ranges.sir_max[i]);
		}
		scf_int_ranges_destroy(&i_ranges);
	}
}

static void
list_values_by_template(scf_prop_tmpl_t *prt)
{
	print_template_constraints(prt, 1);
	print_template_choices(prt, 1);
}

static void
list_values_tmpl(scf_prop_tmpl_t *prt, scf_property_t *prop)
{
	char *val_buf;
	scf_iter_t *iter;
	scf_value_t *val;
	int ret;

	if ((iter = scf_iter_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_property_values(iter, prop) != SCF_SUCCESS)
		scfdie();

	val_buf = safe_malloc(max_scf_value_len + 1);

	while ((ret = scf_iter_next_value(iter, val)) == 1) {
		if (scf_value_get_as_string(val, val_buf,
		    max_scf_value_len + 1) < 0)
			scfdie();

		print_template_value(prt, val_buf);
	}
	if (ret != 0 && scf_error() != SCF_ERROR_PERMISSION_DENIED)
		scfdie();
	free(val_buf);

	print_template_constraints(prt, 0);
	print_template_choices(prt, 0);

}

/*
 * Outputs property info for the describe subcommand
 * Verbose output if templates == 2, -v option of svccfg describe
 * Displays template data if prop is not NULL, -t option of svccfg describe
 */
static void
list_prop_tmpl(scf_prop_tmpl_t *prt, scf_property_t *prop, int templates)
{
	char *buf;
	uint8_t u_buf;
	int i;
	uint64_t min, max;
	scf_values_t values;

	if (prt == NULL || templates == 0)
		return;

	if (prop == NULL) {
		safe_printf("%s%s: ", TMPL_VALUE_INDENT, gettext("name"));
		if (scf_tmpl_prop_name(prt, &buf) > 0) {
			safe_printf("%s\n", buf);
			free(buf);
		} else
			safe_printf("(%s)\n", gettext("any"));
	}

	if (prop == NULL || templates == 2) {
		if (prop != NULL)
			safe_printf("%s", TMPL_INDENT);
		else
			safe_printf("%s", TMPL_VALUE_INDENT);
		safe_printf("%s: ", gettext("type"));
		if ((buf = _scf_read_tmpl_prop_type_as_string(prt)) != NULL) {
			safe_printf("%s\n", buf);
			free(buf);
		} else
			safe_printf("(%s)\n", gettext("any"));
	}

	if (templates == 2 && scf_tmpl_prop_required(prt, &u_buf) == 0)
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("required"),
		    u_buf ? "true" : "false");

	if (templates == 2 && scf_tmpl_prop_common_name(prt, NULL, &buf) > 0) {
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("common name"),
		    buf);
		free(buf);
	}

	if (templates == 2 && scf_tmpl_prop_units(prt, NULL, &buf) > 0) {
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("units"),
		    buf);
		free(buf);
	}

	if (scf_tmpl_prop_description(prt, NULL, &buf) > 0) {
		safe_printf("%s%s\n", TMPL_INDENT, buf);
		free(buf);
	}

	if (templates == 2 && scf_tmpl_prop_visibility(prt, &u_buf) == 0)
		safe_printf("%s%s: %s\n", TMPL_INDENT, gettext("visibility"),
		    scf_tmpl_visibility_to_string(u_buf));

	if (templates == 2 && scf_tmpl_prop_cardinality(prt, &min, &max) == 0) {
		safe_printf("%s%s: %" PRIu64 "\n", TMPL_INDENT,
		    gettext("minimum number of values"), min);
		if (max == ULLONG_MAX) {
			safe_printf("%s%s: %s\n", TMPL_INDENT,
			    gettext("maximum number of values"),
			    gettext("unlimited"));
		} else {
			safe_printf("%s%s: %" PRIu64 "\n", TMPL_INDENT,
			    gettext("maximum number of values"), max);
		}
	}

	if (templates == 2 && scf_tmpl_prop_internal_seps(prt, &values) == 0) {
		for (i = 0; i < values.value_count; i++) {
			if (i == 0) {
				safe_printf("%s%s:", TMPL_INDENT,
				    gettext("internal separators"));
			}
			safe_printf(" \"%s\"", values.values_as_strings[i]);
		}
		safe_printf("\n");
	}

	if (templates != 2)
		return;

	if (prop != NULL)
		list_values_tmpl(prt, prop);
	else
		list_values_by_template(prt);
}

static char *
read_astring(scf_propertygroup_t *pg, const char *prop_name)
{
	char *rv;

	rv = _scf_read_single_astring_from_pg(pg, prop_name);
	if (rv == NULL) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			break;
		default:
			scfdie();
		}
	}
	return (rv);
}

static void
display_documentation(scf_iter_t *iter, scf_propertygroup_t *pg)
{
	size_t doc_len;
	size_t man_len;
	char *pg_name;
	char *text = NULL;
	int rv;

	doc_len = strlen(SCF_PG_TM_DOC_PREFIX);
	man_len = strlen(SCF_PG_TM_MAN_PREFIX);
	pg_name = safe_malloc(max_scf_name_len + 1);
	while ((rv = scf_iter_next_pg(iter, pg)) == 1) {
		if (scf_pg_get_name(pg, pg_name, max_scf_name_len + 1) == -1) {
			scfdie();
		}
		if (strncmp(pg_name, SCF_PG_TM_DOC_PREFIX, doc_len) == 0) {
			/* Display doc_link and and uri */
			safe_printf("%s%s:\n", TMPL_INDENT,
			    gettext("doc_link"));
			text = read_astring(pg, SCF_PROPERTY_TM_NAME);
			if (text != NULL) {
				safe_printf("%s%s%s: %s\n", TMPL_INDENT,
				    TMPL_INDENT, gettext("name"), text);
				uu_free(text);
			}
			text = read_astring(pg, SCF_PROPERTY_TM_URI);
			if (text != NULL) {
				safe_printf("%s%s: %s\n", TMPL_INDENT_2X,
				    gettext("uri"), text);
				uu_free(text);
			}
		} else if (strncmp(pg_name, SCF_PG_TM_MAN_PREFIX,
		    man_len) == 0) {
			/* Display manpage title, section and path */
			safe_printf("%s%s:\n", TMPL_INDENT,
			    gettext("manpage"));
			text = read_astring(pg, SCF_PROPERTY_TM_TITLE);
			if (text != NULL) {
				safe_printf("%s%s%s: %s\n", TMPL_INDENT,
				    TMPL_INDENT, gettext("title"), text);
				uu_free(text);
			}
			text = read_astring(pg, SCF_PROPERTY_TM_SECTION);
			if (text != NULL) {
				safe_printf("%s%s%s: %s\n", TMPL_INDENT,
				    TMPL_INDENT, gettext("section"), text);
				uu_free(text);
			}
			text = read_astring(pg, SCF_PROPERTY_TM_MANPATH);
			if (text != NULL) {
				safe_printf("%s%s%s: %s\n", TMPL_INDENT,
				    TMPL_INDENT, gettext("manpath"), text);
				uu_free(text);
			}
		}
	}
	if (rv == -1)
		scfdie();

done:
	free(pg_name);
}

static void
list_entity_tmpl(int templates)
{
	char *common_name = NULL;
	char *description = NULL;
	char *locale = NULL;
	scf_iter_t *iter;
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	int r;
	scf_value_t *val;

	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	locale = setlocale(LC_MESSAGES, NULL);

	if (get_pg(SCF_PG_TM_COMMON_NAME, pg) == 0) {
		common_name = safe_malloc(max_scf_value_len + 1);

		/* Try both the current locale and the "C" locale. */
		if (scf_pg_get_property(pg, locale, prop) == 0 ||
		    (scf_error() == SCF_ERROR_NOT_FOUND &&
		    scf_pg_get_property(pg, "C", prop) == 0)) {
			if (prop_get_val(prop, val) == 0 &&
			    scf_value_get_ustring(val, common_name,
			    max_scf_value_len + 1) != -1) {
				safe_printf("%s%s: %s\n", TMPL_INDENT,
				    gettext("common name"), common_name);
			}
		}
	}

	/*
	 * Do description, manpages, and doc links if templates == 2.
	 */
	if (templates == 2) {
		/* Get the description. */
		if (get_pg(SCF_PG_TM_DESCRIPTION, pg) == 0) {
			description = safe_malloc(max_scf_value_len + 1);

			/* Try both the current locale and the "C" locale. */
			if (scf_pg_get_property(pg, locale, prop) == 0 ||
			    (scf_error() == SCF_ERROR_NOT_FOUND &&
			    scf_pg_get_property(pg, "C", prop) == 0)) {
				if (prop_get_val(prop, val) == 0 &&
				    scf_value_get_ustring(val, description,
				    max_scf_value_len + 1) != -1) {
					safe_printf("%s%s: %s\n", TMPL_INDENT,
					    gettext("description"),
					    description);
				}
			}
		}

		/* Process doc_link & manpage elements. */
		if (cur_level != NULL) {
			r = scf_iter_snaplevel_pgs_typed(iter, cur_level,
			    SCF_GROUP_TEMPLATE);
		} else if (cur_inst != NULL) {
			r = scf_iter_instance_pgs_typed(iter, cur_inst,
			    SCF_GROUP_TEMPLATE);
		} else {
			r = scf_iter_service_pgs_typed(iter, cur_svc,
			    SCF_GROUP_TEMPLATE);
		}
		if (r == 0) {
			display_documentation(iter, pg);
		}
	}

	free(common_name);
	free(description);
	scf_pg_destroy(pg);
	scf_property_destroy(prop);
	scf_value_destroy(val);
	scf_iter_destroy(iter);
}

static void
listtmpl(const char *pattern, int templates)
{
	scf_pg_tmpl_t *pgt;
	scf_prop_tmpl_t *prt;
	char *snapbuf = NULL;
	char *fmribuf;
	char *pg_name = NULL, *prop_name = NULL;
	ssize_t prop_name_size;
	char *qual_prop_name;
	char *search_name;
	int listed = 0;

	if ((pgt = scf_tmpl_pg_create(g_hndl)) == NULL ||
	    (prt = scf_tmpl_prop_create(g_hndl)) == NULL)
		scfdie();

	fmribuf = safe_malloc(max_scf_name_len + 1);
	qual_prop_name = safe_malloc(max_scf_name_len + 1);

	if (cur_snap != NULL) {
		snapbuf = safe_malloc(max_scf_name_len + 1);
		if (scf_snapshot_get_name(cur_snap, snapbuf,
		    max_scf_name_len + 1) < 0)
			scfdie();
	}

	if (cur_inst != NULL) {
		if (scf_instance_to_fmri(cur_inst, fmribuf,
		    max_scf_name_len + 1) < 0)
			scfdie();
	} else if (cur_svc != NULL) {
		if (scf_service_to_fmri(cur_svc, fmribuf,
		    max_scf_name_len + 1) < 0)
			scfdie();
	} else
		abort();

	/* If pattern is specified, we want to list only those items. */
	while (scf_tmpl_iter_pgs(pgt, fmribuf, snapbuf, NULL, NULL) == 1) {
		listed = 0;
		if (pattern == NULL || (scf_tmpl_pg_name(pgt, &pg_name) > 0 &&
		    fnmatch(pattern, pg_name, 0) == 0)) {
			list_pg_tmpl(pgt, NULL, templates);
			listed++;
		}

		scf_tmpl_prop_reset(prt);

		while (scf_tmpl_iter_props(pgt, prt, NULL) == 0) {
			search_name = NULL;
			prop_name_size = scf_tmpl_prop_name(prt, &prop_name);
			if ((prop_name_size > 0) && (pg_name != NULL)) {
				if (snprintf(qual_prop_name,
				    max_scf_name_len + 1, "%s/%s",
				    pg_name, prop_name) >=
				    max_scf_name_len + 1) {
					prop_name_size = -1;
				} else {
					search_name = qual_prop_name;
				}
			}
			if (listed > 0 || pattern == NULL ||
			    (prop_name_size > 0 &&
			    fnmatch(pattern, search_name,
			    FNM_PATHNAME) == 0))
				list_prop_tmpl(prt, NULL, templates);
			if (prop_name != NULL) {
				free(prop_name);
				prop_name = NULL;
			}
		}
		if (pg_name != NULL) {
			free(pg_name);
			pg_name = NULL;
		}
	}

	scf_tmpl_prop_destroy(prt);
	scf_tmpl_pg_destroy(pgt);
	free(snapbuf);
	free(fmribuf);
	free(qual_prop_name);
}

static void
listprop(const char *pattern, int only_pgs, int templates, int only_masked,
    fnames_t *fields, const scf_decoration_layer_t *layers)
{
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_iter_t *iter, *piter;
	char *pgnbuf, *prnbuf, *ppnbuf;
	scf_pg_tmpl_t *pgt, *pgtp;
	scf_prop_tmpl_t *prt;

	void **objects;
	char **names;
	void **tmpls;
	int allocd, i;

	int ret;
	ssize_t pgnlen, prnlen, szret;
	size_t max_len = 0;

	/*
	 * fields and layers are used by listprop and listcust
	 * subcommands, and templates by describe.
	 */
	if (templates)
		assert(fields == NULL && layers == NULL);

	if (cur_svc == NULL && cur_inst == NULL) {
		semerr(emsg_entity_not_selected);
		return;
	}

	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL ||
	    (piter = scf_iter_create(g_hndl)) == NULL ||
	    (prt = scf_tmpl_prop_create(g_hndl)) == NULL ||
	    (pgt = scf_tmpl_pg_create(g_hndl)) == NULL)
		scfdie();

	prnbuf = safe_malloc(max_scf_name_len + 1);

	if (cur_level != NULL)
		ret = scf_iter_snaplevel_pgs(iter, cur_level);
	else if (cur_inst != NULL)
		ret = scf_iter_instance_pgs(iter, cur_inst);
	else
		ret = scf_iter_service_pgs(iter, cur_svc);
	if (ret != 0) {
		return;
	}

	/*
	 * We want to only list items which match pattern, and we want the
	 * second column to line up, so during the first pass we'll save
	 * matching items, their names, and their templates in objects,
	 * names, and tmpls, computing the maximum name length as we go,
	 * and then we'll print them out.
	 *
	 * Note: We always keep an extra slot available so the array can be
	 * NULL-terminated.
	 */
	i = 0;
	allocd = 1;
	objects = safe_malloc(sizeof (*objects));
	names = safe_malloc(sizeof (*names));
	tmpls = safe_malloc(sizeof (*tmpls));

	while ((ret = scf_iter_next_pg(iter, pg)) == 1) {
		int new_pg = 0;
		int print_props = 0;
		pgtp = NULL;

		pgnlen = scf_pg_get_name(pg, NULL, 0);
		if (pgnlen < 0)
			scfdie();

		pgnbuf = safe_malloc(pgnlen + 1);

		szret = scf_pg_get_name(pg, pgnbuf, pgnlen + 1);
		if (szret < 0)
			scfdie();
		assert(szret <= pgnlen);

		if (scf_tmpl_get_by_pg(pg, pgt, NULL) == -1) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();
			pgtp = NULL;
		} else {
			pgtp = pgt;
		}

		if (pattern == NULL ||
		    fnmatch(pattern, pgnbuf, 0) == 0) {
			int ent_masked = 0;

			/*
			 * If only_masked is set, we don't add visible
			 * pg's to the objects array, but we still
			 * continue on through to look for masked
			 * properties.
			 */

			if ((ent_masked = scf_pg_is_masked(pg)) == -1)
				scfdie();

			if (!only_masked || (ent_masked == 1)) {

				if (i+1 >= allocd) {
					allocd *= 2;
					objects = realloc(objects,
					    sizeof (*objects) * allocd);
					names = realloc(names,
					    sizeof (*names) * allocd);
					tmpls = realloc(tmpls,
					    sizeof (*tmpls) * allocd);
					if (objects == NULL || names == NULL ||
					    tmpls == NULL)
						uu_die(gettext(
						    "Out of memory"));
				}

				objects[i] = pg;
				names[i] = pgnbuf;

				if (pgtp == NULL)
					tmpls[i] = NULL;
				else
					tmpls[i] = pgt;

				++i;

				if (pgnlen > max_len)
					max_len = pgnlen;

				new_pg = 1;
			}

			print_props = 1;
		}

		if (only_pgs) {
			if (new_pg) {
				pg = scf_pg_create(g_hndl);
				if (pg == NULL)
					scfdie();
				pgt = scf_tmpl_pg_create(g_hndl);
				if (pgt == NULL)
					scfdie();
			} else
				free(pgnbuf);

			continue;
		}

		if (scf_iter_pg_properties(piter, pg) != SCF_SUCCESS)
			scfdie();

		while ((ret = scf_iter_next_property(piter, prop)) == 1) {
			int ent_masked = 0;
			prnlen = scf_property_get_name(prop, prnbuf,
			    max_scf_name_len + 1);
			if (prnlen < 0)
				scfdie();

			/* Will prepend the property group name and a slash. */
			prnlen += pgnlen + 1;

			ppnbuf = safe_malloc(prnlen + 1);

			if (snprintf(ppnbuf, prnlen + 1, "%s/%s", pgnbuf,
			    prnbuf) < 0)
				uu_die("snprintf");

			if (!(pattern == NULL || print_props == 1 ||
			    fnmatch(pattern, ppnbuf, 0) == 0)) {
				free(ppnbuf);
				continue;
			}

			if ((ent_masked = scf_property_is_masked(prop)) == -1)
				scfdie();

			if (!only_masked || (ent_masked == 1)) {

				if (i+1 >= allocd) {
					allocd *= 2;
					objects = realloc(objects,
					    sizeof (*objects) * allocd);
					names = realloc(names,
					    sizeof (*names) * allocd);
					tmpls = realloc(tmpls,
					    sizeof (*tmpls) * allocd);
					if (objects == NULL || names == NULL ||
					    tmpls == NULL)
						uu_die(gettext(
						    "Out of memory"));
				}

				objects[i] = prop;
				names[i] = ppnbuf;

				if (pgtp != NULL) {
					if (scf_tmpl_get_by_prop(pgt,
					    prnbuf, prt, NULL) < 0) {
						if (scf_error() !=
						    SCF_ERROR_NOT_FOUND)
							scfdie();
						tmpls[i] = NULL;
					} else {
						tmpls[i] = prt;
					}
				} else {
					tmpls[i] = NULL;
				}

				++i;

				if (prnlen > max_len)
					max_len = prnlen;

				prop = scf_property_create(g_hndl);
				prt = scf_tmpl_prop_create(g_hndl);
			}
		}

		if (new_pg) {
			pg = scf_pg_create(g_hndl);
			if (pg == NULL)
				scfdie();
			pgt = scf_tmpl_pg_create(g_hndl);
			if (pgt == NULL)
				scfdie();
		} else
			free(pgnbuf);
	}
	if (ret != 0)
		scfdie();

	objects[i] = NULL;

	scf_pg_destroy(pg);
	scf_tmpl_pg_destroy(pgt);
	scf_property_destroy(prop);
	scf_tmpl_prop_destroy(prt);

	/*
	 * At this point, we have filtered our list based on pattern
	 * and only_masked, since those elements toggle output on a
	 * pg/prop basis.  The fields and layers are handled in the
	 * respective print routines.
	 */

	for (i = 0; objects[i] != NULL; ++i) {
		if (strchr(names[i], '/') == NULL) {
			/*
			 * Only print property group info if we're
			 * not doing field selection.
			 */
			if (fields != NULL && fields->f_size > 0)
				continue;
			pg = (scf_propertygroup_t *)objects[i];
			pgt = (scf_pg_tmpl_t *)tmpls[i];
			list_pg_info(pg, names[i], max_len, layers);
			list_pg_tmpl(pgt, pg, templates);
			free(names[i]);
			scf_pg_destroy(pg);
			if (pgt != NULL)
				scf_tmpl_pg_destroy(pgt);
		} else {
			/* property */
			prop = (scf_property_t *)objects[i];
			prt = (scf_prop_tmpl_t *)tmpls[i];
			list_prop_info(prop, names[i], max_len, fields, layers);
			list_prop_tmpl(prt, prop, templates);
			free(names[i]);
			scf_property_destroy(prop);
			if (prt != NULL)
				scf_tmpl_prop_destroy(prt);
		}
	}

	free(names);
	free(objects);
	free(tmpls);
}

void
lscf_listpg(const char *pattern)
{
	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	listprop(pattern, 1, 0, 0, NULL, NULL);
}

/*
 * Property group and property creation, setting, and deletion.  setprop (and
 * its alias, addprop) can either create a property group of a given type, or
 * it can create or set a property to a given type and list of values.
 */
void
lscf_addpg(const char *name, const char *type, const char *flags)
{
	scf_propertygroup_t *pg;
	int ret;
	uint32_t flgs = 0;
	const char *cp;

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		return;
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		return;
	}

	if (flags != NULL) {
		for (cp = flags; *cp != '\0'; ++cp) {
			switch (*cp) {
			case 'P':
				flgs |= SCF_PG_FLAG_NONPERSISTENT;
				break;

			case 'p':
				flgs &= ~SCF_PG_FLAG_NONPERSISTENT;
				break;

			default:
				semerr(gettext("Invalid property group flag "
				    "%c."), *cp);
				return;
			}
		}
	}

	pg = scf_pg_create(g_hndl);
	if (pg == NULL)
		scfdie();

	if (cur_inst != NULL)
		ret = scf_instance_add_pg(cur_inst, name, type, flgs, pg);
	else
		ret = scf_service_add_pg(cur_svc, name, type, flgs, pg);

	if (ret != SCF_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_INVALID_ARGUMENT:
			semerr(gettext("Name, type, or flags are invalid.\n"));
			break;

		case SCF_ERROR_EXISTS:
			semerr(gettext("Property group already exists.\n"));
			break;

		case SCF_ERROR_PERMISSION_DENIED:
			semerr(emsg_permission_denied);
			break;

		case SCF_ERROR_BACKEND_ACCESS:
			semerr(gettext("Backend refused access.\n"));
			break;

		default:
			scfdie();
		}
	}

	scf_pg_destroy(pg);

	private_refresh();
}

void
lscf_delpg(char *name, int remove)
{
	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		return;
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		return;
	}

	if (strchr(name, '/') != NULL) {
		semerr(emsg_invalid_pg_name, name);
		return;
	}

	lscf_delprop(name, remove);
}

/*
 * scf_delhash() is used to remove the property group related to the
 * hash entry for a specific manifest in the repository. pgname will be
 * constructed from the location of the manifest file. If deathrow isn't 0,
 * manifest file doesn't need to exist (manifest string will be used as
 * an absolute path).
 */
void
lscf_delhash(char *manifest, int deathrow)
{
	char *pgname;
	uint_t curflags = est->sc_cmd_flags;

	if (cur_snap != NULL ||
	    cur_inst != NULL || cur_svc != NULL) {
		warn(gettext("error, an entity is selected\n"));
		return;
	}

	if (est->sc_cmd_filename != NULL)
		est->sc_cmd_flags |= SC_CMD_DONT_EXIT;

	/* select smf/manifest */
	lscf_select(HASH_SVC);
	/*
	 * Translate the manifest file name to property name. In the deathrow
	 * case, the manifest file does not need to exist.
	 */
	pgname = mhash_filename_to_propname(manifest,
	    deathrow ? B_TRUE : B_FALSE);
	if (pgname == NULL) {
		warn(gettext("cannot resolve pathname for %s\n"), manifest);
		est->sc_cmd_flags = curflags;
		return;
	}
	/* delete the hash property name */
	lscf_delpg(pgname, 1);

	est->sc_cmd_flags = curflags;
}

int
lscf_listprop(const char *fieldnames, const char *layernames,
    const char *pattern)
{
	scf_decoration_layer_t lval;
	scf_decoration_layer_t layers[sizeof (layers_all) /
	    sizeof (scf_decoration_layer_t) - 1];
	int l = 0;
	char *layers_out = NULL;
	char *lasts;
	char *ln;
	int ret = 0;
	fnames_t fields;

	if (layernames != NULL)
		layers_out = safe_strdup(layernames);

	if (cur_snap && (layernames != NULL || fieldnames != NULL)) {
		semerr(gettext("Listing decoration information "
		    "is not supported when a snapshot is selected. "));
		goto listprop_out;
	}

	if (map_field(fieldnames, &fields) != 0)
		goto usage;

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (layernames == NULL) {
		do_print_layer = 0;

		listprop(pattern, 0, 0, 0, &fields, NULL);
		goto listprop_out;
	} else {
		do_print_layer = 1;
	}

	/*
	 * Short cut logic: if there was only one layer
	 * specified, and it was a synthetic layer, simply use it.
	 * This makes detection of mixed real and synthetic layers
	 * in the strtok loop much simpler.
	 */
	lval = scf_decoration_layer_from_string(layernames);
	switch (lval) {
	case SCF_DECORATION_ALL:
		listprop(pattern, 0, 0, 0, &fields, layers_all);
		goto listprop_out;
	case SCF_DECORATION_TOP:
		listprop(pattern, 0, 0, 0, &fields, layers_top);
		goto listprop_out;
	default:
		break;
	}

	ln = strtok_r(layers_out, ",", &lasts);

	while (ln != NULL) {

		if (l ==
		    (sizeof (layers) / sizeof (scf_decoration_layer_t)) - 1) {
			semerr(gettext("Too many layers: %s\n"), layernames);
			goto listprop_out;
		}

		switch (lval = scf_decoration_layer_from_string(ln)) {

		case SCF_DECORATION_INVALID:
			semerr(gettext("Invalid layer: %s\n"), ln);
			goto listprop_out;

		case SCF_DECORATION_TOP:
		case SCF_DECORATION_ALL:
			semerr(gettext(
			    "May not specify %s with other layers.\n"), ln);
			goto listprop_out;

		default:
			layers[l++] = lval;
			break;
		}

		ln = strtok_r(NULL, ",", &lasts);
	}

	layers[l] = SCF_DECORATION_INVALID;
	listprop(pattern, 0, 0, 0, &fields, &layers[0]);

listprop_out:
	do_print_layer = 0;
	free(layers_out);
	return (ret);

usage:
	ret = -2;
	goto listprop_out;
}

int
lscf_setprop(const char *pgname, const char *type, const char *value,
    const uu_list_t *values)
{
	scf_type_t ty, current_ty;
	scf_service_t *svc;
	scf_propertygroup_t *pg, *parent_pg;
	scf_property_t *prop, *parent_prop;
	scf_pg_tmpl_t *pgt;
	scf_prop_tmpl_t *prt;
	int ret, result = 0;
	scf_transaction_t *tx;
	scf_transaction_entry_t *e;
	scf_value_t *v;
	uu_list_walk_t *walk;
	string_list_t *sp;
	char *propname;
	int req_quotes = 0;

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if ((e = scf_entry_create(g_hndl)) == NULL ||
	    (svc = scf_service_create(g_hndl)) == NULL ||
	    (parent_pg = scf_pg_create(g_hndl)) == NULL ||
	    (pg = scf_pg_create(g_hndl)) == NULL ||
	    (parent_prop = scf_property_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (pgt = scf_tmpl_pg_create(g_hndl)) == NULL ||
	    (prt = scf_tmpl_prop_create(g_hndl)) == NULL ||
	    (tx = scf_transaction_create(g_hndl)) == NULL)
		scfdie();

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		goto fail;
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		goto fail;
	}

	propname = strchr(pgname, '/');
	if (propname == NULL) {
		semerr(gettext("Property names must contain a `/'.\n"));
		goto fail;
	}

	*propname = '\0';
	++propname;

	if (type != NULL) {
		ty = string_to_type(type);
		if (ty == SCF_TYPE_INVALID) {
			semerr(gettext("Unknown type \"%s\".\n"), type);
			goto fail;
		}
	}

	if (cur_inst != NULL)
		ret = scf_instance_get_pg(cur_inst, pgname, pg);
	else
		ret = scf_service_get_pg(cur_svc, pgname, pg);
	if (ret != SCF_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			semerr(emsg_no_such_pg, pgname);
			goto fail;

		case SCF_ERROR_INVALID_ARGUMENT:
			semerr(emsg_invalid_pg_name, pgname);
			goto fail;

		default:
			scfdie();
			break;
		}
	}

	do {
		if (scf_pg_update(pg) == -1)
			scfdie();
		if (scf_transaction_start(tx, pg) != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			semerr(emsg_permission_denied);
			goto fail;
		}

		ret = scf_pg_get_property(pg, propname, prop);
		if (ret == SCF_SUCCESS) {
			if (scf_property_type(prop, &current_ty) != SCF_SUCCESS)
				scfdie();

			if (type == NULL)
				ty = current_ty;
			if (scf_transaction_property_change_type(tx, e,
			    propname, ty) == -1)
				scfdie();

		} else if (scf_error() == SCF_ERROR_NOT_FOUND) {
			/* Infer the type, if possible. */
			if (type == NULL) {
				/*
				 * First check if we're an instance and the
				 * property is set on the service.
				 */
				if (cur_inst != NULL &&
				    scf_instance_get_parent(cur_inst,
				    svc) == 0 &&
				    scf_service_get_pg(cur_svc, pgname,
				    parent_pg) == 0 &&
				    scf_pg_get_property(parent_pg, propname,
				    parent_prop) == 0 &&
				    scf_property_type(parent_prop,
				    &current_ty) == 0) {
					ty = current_ty;

				/* Then check for a type set in a template. */
				} else if (scf_tmpl_get_by_pg(pg, pgt,
				    NULL) == 0 &&
				    scf_tmpl_get_by_prop(pgt, propname, prt,
				    NULL) == 0 &&
				    scf_tmpl_prop_type(prt, &current_ty) == 0) {
					ty = current_ty;

				/* If type can't be inferred, fail. */
				} else {
					semerr(gettext("Type required for new "
					    "properties.\n"));
					goto fail;
				}
			}
			if (scf_transaction_property_new(tx, e, propname,
			    ty) == -1)
				scfdie();
		} else if (scf_error() == SCF_ERROR_INVALID_ARGUMENT) {
			semerr(emsg_invalid_prop_name, propname);
			goto fail;
		} else {
			scfdie();
		}

		if (ty == SCF_TYPE_ASTRING || ty == SCF_TYPE_USTRING)
			req_quotes = 1;

		if (value != NULL) {
			v = string_to_value(value, ty, 0);

			if (v == NULL)
				goto fail;

			ret = scf_entry_add_value(e, v);
			assert(ret == SCF_SUCCESS);
		} else {
			assert(values != NULL);

			walk = uu_list_walk_start((uu_list_t *)values,
			    UU_DEFAULT);
			if (walk == NULL)
				uu_die(gettext("Could not walk list"));

			for (sp = uu_list_walk_next(walk); sp != NULL;
			    sp = uu_list_walk_next(walk)) {
				v = string_to_value(sp->str, ty, req_quotes);

				if (v == NULL) {
					scf_entry_destroy_children(e);
					goto fail;
				}

				ret = scf_entry_add_value(e, v);
				assert(ret == SCF_SUCCESS);
			}
			uu_list_walk_end(walk);
		}
		result = scf_transaction_commit(tx);

		scf_transaction_reset(tx);
		scf_entry_destroy_children(e);
	} while (result == 0);

	if (result < 0) {
		if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
			scfdie();

		semerr(emsg_permission_denied);
		goto fail;
	}

	ret = 0;

	private_refresh();

	goto cleanup;

fail:
	ret = -1;

cleanup:
	scf_transaction_destroy(tx);
	scf_entry_destroy(e);
	scf_service_destroy(svc);
	scf_pg_destroy(parent_pg);
	scf_pg_destroy(pg);
	scf_property_destroy(parent_prop);
	scf_property_destroy(prop);
	scf_tmpl_pg_destroy(pgt);
	scf_tmpl_prop_destroy(prt);

	return (ret);
}

void
lscf_delprop(char *pgn, int remove)
{
	char *slash, *pn;
	scf_propertygroup_t *pg;
	scf_transaction_t *tx;
	scf_transaction_entry_t *e;
	int ret;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		return;
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		return;
	}

	pg = scf_pg_create(g_hndl);
	if (pg == NULL)
		scfdie();

	slash = strchr(pgn, '/');
	if (slash == NULL) {
		pn = NULL;
	} else {
		*slash = '\0';
		pn = slash + 1;
	}

	if (cur_inst != NULL)
		ret = scf_instance_get_pg(cur_inst, pgn, pg);
	else
		ret = scf_service_get_pg(cur_svc, pgn, pg);
	if (ret != SCF_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			semerr(emsg_no_such_pg, pgn);
			break;

		case SCF_ERROR_INVALID_ARGUMENT:
			semerr(emsg_invalid_pg_name, pgn);
			break;

		default:
			scfdie();
		}

		scf_pg_destroy(pg);

		return;
	}

	if (pn == NULL) {
		/* Try to delete the property group. */
		if (remove)
			ret = scf_pg_remove(pg);
		else
			ret = scf_pg_delete(pg);

		if (ret != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			semerr(emsg_permission_denied);
		} else {
			private_refresh();
		}

		scf_pg_destroy(pg);
		return;
	}

	e = scf_entry_create(g_hndl);
	tx = scf_transaction_create(g_hndl);

	do {
		if (scf_pg_update(pg) == -1)
			scfdie();
		if (scf_transaction_start(tx, pg) != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			semerr(emsg_permission_denied);
			break;
		}

		if (remove) {
			if (scf_transaction_property_remove(tx, e, pn) !=
			    SCF_SUCCESS) {
				if (scf_error() == SCF_ERROR_NOT_FOUND) {
					semerr(gettext("No such property "
					    "%s/%s.\n"), pgn, pn);
					break;
				} else if (scf_error() ==
				    SCF_ERROR_INVALID_ARGUMENT) {
					semerr(emsg_invalid_prop_name, pn);
					break;
				} else {
					scfdie();
				}
			}
		} else {
			if (scf_transaction_property_delete(tx, e, pn) !=
			    SCF_SUCCESS) {
				if (scf_error() == SCF_ERROR_NOT_FOUND) {
					semerr(gettext("No such property "
					    "%s/%s.\n"), pgn, pn);
					break;
				} else if (scf_error() ==
				    SCF_ERROR_INVALID_ARGUMENT) {
					semerr(emsg_invalid_prop_name, pn);
					break;
				} else {
					scfdie();
				}
			}
		}

		ret = scf_transaction_commit(tx);

		if (ret == 0)
			scf_transaction_reset(tx);
	} while (ret == 0);

	if (ret < 0) {
		if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
			scfdie();

		semerr(emsg_permission_denied);
	} else {
		private_refresh();
	}

	scf_transaction_destroy(tx);
	scf_entry_destroy(e);
	scf_pg_destroy(pg);
}

/*
 * Property editing.
 */

static int
write_edit_script(FILE *strm)
{
	char *fmribuf;
	ssize_t fmrilen;

	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_value_t *val;
	scf_type_t ty;
	int ret, result = 0;
	scf_iter_t *iter, *piter, *viter;
	char *buf, *tybuf, *pname;
	const char *emsg_write_error;


	emsg_write_error = gettext("Error writing temoprary file: %s.\n");


	/* select fmri */
	if (cur_inst != NULL) {
		fmrilen = scf_instance_to_fmri(cur_inst, NULL, 0);
		if (fmrilen < 0)
			scfdie();
		fmribuf = safe_malloc(fmrilen + 1);
		if (scf_instance_to_fmri(cur_inst, fmribuf, fmrilen + 1) < 0)
			scfdie();
	} else {
		assert(cur_svc != NULL);
		fmrilen = scf_service_to_fmri(cur_svc, NULL, 0);
		if (fmrilen < 0)
			scfdie();
		fmribuf = safe_malloc(fmrilen + 1);
		if (scf_service_to_fmri(cur_svc, fmribuf, fmrilen + 1) < 0)
			scfdie();
	}

	if (fprintf(strm, "select %s\n\n", fmribuf) < 0) {
		warn(emsg_write_error, strerror(errno));
		free(fmribuf);
		return (-1);
	}

	free(fmribuf);


	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL ||
	    (piter = scf_iter_create(g_hndl)) == NULL ||
	    (viter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	buf = safe_malloc(max_scf_name_len + 1);
	tybuf = safe_malloc(max_scf_pg_type_len + 1);
	pname = safe_malloc(max_scf_name_len + 1);

	if (cur_inst != NULL)
		ret = scf_iter_instance_pgs(iter, cur_inst);
	else
		ret = scf_iter_service_pgs(iter, cur_svc);
	if (ret != SCF_SUCCESS)
		scfdie();

	while ((ret = scf_iter_next_pg(iter, pg)) == 1) {
		int ret2;

		/*
		 * # delprop pg
		 * # addpg pg type
		 */
		if (scf_pg_get_name(pg, buf, max_scf_name_len + 1) < 0)
			scfdie();

		if (scf_pg_get_type(pg, tybuf, max_scf_pg_type_len + 1) < 0)
			scfdie();

		if (fprintf(strm, "# Property group \"%s\"\n"
		    "# delprop %s\n"
		    "# addpg %s %s\n", buf, buf, buf, tybuf) < 0) {
			warn(emsg_write_error, strerror(errno));
			result = -1;
			goto out;
		}

		/* # setprop pg/prop = (values) */

		if (scf_iter_pg_properties(piter, pg) != SCF_SUCCESS)
			scfdie();

		while ((ret2 = scf_iter_next_property(piter, prop)) == 1) {
			int first = 1;
			int ret3;
			int multiple;
			int is_str;
			scf_type_t bty;

			if (scf_property_get_name(prop, pname,
			    max_scf_name_len + 1) < 0)
				scfdie();

			if (scf_property_type(prop, &ty) != 0)
				scfdie();

			multiple = prop_has_multiple_values(prop, val);

			if (fprintf(strm, "# setprop %s/%s = %s: %s", buf,
			    pname, scf_type_to_string(ty), multiple ? "(" : "")
			    < 0) {
				warn(emsg_write_error, strerror(errno));
				result = -1;
				goto out;
			}

			(void) scf_type_base_type(ty, &bty);
			is_str = (bty == SCF_TYPE_ASTRING);

			if (scf_iter_property_values(viter, prop) !=
			    SCF_SUCCESS)
				scfdie();

			while ((ret3 = scf_iter_next_value(viter, val)) == 1) {
				char *buf;
				ssize_t buflen;

				buflen = scf_value_get_as_string(val, NULL, 0);
				if (buflen < 0)
					scfdie();

				buf = safe_malloc(buflen + 1);

				if (scf_value_get_as_string(val, buf,
				    buflen + 1) < 0)
					scfdie();

				if (first)
					first = 0;
				else {
					if (putc(' ', strm) != ' ') {
						warn(emsg_write_error,
						    strerror(errno));
						result = -1;
						goto out;
					}
				}

				if ((is_str && multiple) ||
				    strpbrk(buf, CHARS_TO_QUOTE) != NULL) {
					(void) putc('"', strm);
					(void) quote_and_print(buf, strm, 1);
					(void) putc('"', strm);

					if (ferror(strm)) {
						warn(emsg_write_error,
						    strerror(errno));
						result = -1;
						goto out;
					}
				} else {
					if (fprintf(strm, "%s", buf) < 0) {
						warn(emsg_write_error,
						    strerror(errno));
						result = -1;
						goto out;
					}
				}

				free(buf);
			}
			if (ret3 < 0 &&
			    scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			/* Write closing paren if mult-value property */
			if ((multiple && putc(')', strm) == EOF) ||

			    /* Write final newline */
			    fputc('\n', strm) == EOF) {
				warn(emsg_write_error, strerror(errno));
				result = -1;
				goto out;
			}
		}
		if (ret2 < 0)
			scfdie();

		if (fputc('\n', strm) == EOF) {
			warn(emsg_write_error, strerror(errno));
			result = -1;
			goto out;
		}
	}
	if (ret < 0)
		scfdie();

out:
	free(pname);
	free(tybuf);
	free(buf);
	scf_iter_destroy(viter);
	scf_iter_destroy(piter);
	scf_iter_destroy(iter);
	scf_value_destroy(val);
	scf_property_destroy(prop);
	scf_pg_destroy(pg);

	if (result == 0) {
		if (fflush(strm) != 0) {
			warn(emsg_write_error, strerror(errno));
			return (-1);
		}
	}

	return (result);
}

int
lscf_editprop()
{
	char *buf, *editor;
	size_t bufsz;
	int tmpfd;
	char tempname[] = TEMP_FILE_PATTERN;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return (-1);

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		return (-1);
	}

	if (cur_svc == NULL && cur_inst == NULL) {
		semerr(emsg_entity_not_selected);
		return (-1);
	}

	tmpfd = mkstemp(tempname);
	if (tmpfd == -1) {
		semerr(gettext("Could not create temporary file.\n"));
		return (-1);
	}

	(void) strcpy(tempfilename, tempname);

	tempfile = fdopen(tmpfd, "r+");
	if (tempfile == NULL) {
		warn(gettext("Could not create temporary file.\n"));
		if (close(tmpfd) == -1)
			warn(gettext("Could not close temporary file: %s.\n"),
			    strerror(errno));

		remove_tempfile();

		return (-1);
	}

	if (write_edit_script(tempfile) == -1) {
		remove_tempfile();
		return (-1);
	}

	editor = getenv("EDITOR");
	if (editor == NULL)
		editor = "vi";

	bufsz = strlen(editor) + 1 + strlen(tempname) + 1;
	buf = safe_malloc(bufsz);

	if (snprintf(buf, bufsz, "%s %s", editor, tempname) < 0)
		uu_die(gettext("Error creating editor command"));

	if (system(buf) == -1) {
		semerr(gettext("Could not launch editor %s: %s\n"), editor,
		    strerror(errno));
		free(buf);
		remove_tempfile();
		return (-1);
	}

	free(buf);

	(void) engine_source(tempname, est->sc_cmd_flags & SC_CMD_IACTIVE);

	remove_tempfile();

	return (0);
}

static void
add_string(uu_list_t *strlist, const char *str)
{
	string_list_t *elem;
	elem = safe_malloc(sizeof (*elem));
	uu_list_node_init(elem, &elem->node, string_pool);
	elem->str = safe_strdup(str);
	if (uu_list_append(strlist, elem) != 0)
		uu_die(gettext("libuutil error: %s\n"),
		    uu_strerror(uu_error()));
}

static int
remove_string(uu_list_t *strlist, const char *str)
{
	uu_list_walk_t	*elems;
	string_list_t	*sp;

	/*
	 * Find the element that needs to be removed.
	 */
	elems = uu_list_walk_start(strlist, UU_DEFAULT);
	while ((sp = uu_list_walk_next(elems)) != NULL) {
		if (strcmp(sp->str, str) == 0)
			break;
	}
	uu_list_walk_end(elems);

	/*
	 * Returning 1 here as the value was not found, this
	 * might not be an error.  Leave it to the caller to
	 * decide.
	 */
	if (sp == NULL) {
		return (1);
	}

	uu_list_remove(strlist, sp);

	free(sp->str);
	free(sp);

	return (0);
}

/*
 * Get all property values that don't match the given glob pattern,
 * if a pattern is specified.
 */
static void
get_prop_values(scf_property_t *prop, uu_list_t *values,
    const char *pattern)
{
	scf_iter_t *iter;
	scf_value_t *val;
	int ret;

	if ((iter = scf_iter_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_property_values(iter, prop) != 0)
		scfdie();

	while ((ret = scf_iter_next_value(iter, val)) == 1) {
		char *buf;
		ssize_t vlen, szret;

		vlen = scf_value_get_as_string(val, NULL, 0);
		if (vlen < 0)
			scfdie();

		buf = safe_malloc(vlen + 1);

		szret = scf_value_get_as_string(val, buf, vlen + 1);
		if (szret < 0)
			scfdie();
		assert(szret <= vlen);

		if (pattern == NULL || fnmatch(pattern, buf, 0) != 0)
			add_string(values, buf);

		free(buf);
	}

	if (ret == -1)
		scfdie();

	scf_value_destroy(val);
	scf_iter_destroy(iter);
}

static int
lscf_setpropvalue(const char *pgname, const char *type,
    const char *arg, int isadd, int isnotfoundok)
{
	scf_type_t ty;
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	int ret;
	int result = 0;
	scf_transaction_t *tx;
	scf_transaction_entry_t *e;
	scf_value_t *v;
	string_list_t *sp;
	char *propname;
	uu_list_t *values;
	uu_list_walk_t *walk;
	void *cookie = NULL;
	char *pattern = NULL;

	if (isadd) {
		(void) lscf_prep_hndl(LSCF_HNDL_MASKED);
	} else {
		if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
			return (-1);
	}

	if ((values = uu_list_create(string_pool, NULL, 0)) == NULL)
		uu_die(gettext("Could not create property list: %s\n"),
		    uu_strerror(uu_error()));

	if (!isadd)
		pattern = safe_strdup(arg);

	if ((e = scf_entry_create(g_hndl)) == NULL ||
	    (pg = scf_pg_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (tx = scf_transaction_create(g_hndl)) == NULL)
		scfdie();

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		goto fail;
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		goto fail;
	}

	propname = strchr(pgname, '/');
	if (propname == NULL) {
		semerr(gettext("Property names must contain a `/'.\n"));
		goto fail;
	}

	*propname = '\0';
	++propname;

	if (type != NULL) {
		ty = string_to_type(type);
		if (ty == SCF_TYPE_INVALID) {
			semerr(gettext("Unknown type \"%s\".\n"), type);
			goto fail;
		}
	}

	if (cur_inst != NULL)
		ret = scf_instance_get_pg(cur_inst, pgname, pg);
	else
		ret = scf_service_get_pg(cur_svc, pgname, pg);
	if (ret != 0) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			if (isnotfoundok) {
				result = 0;
			} else {
				semerr(emsg_no_such_pg, pgname);
				result = -1;
			}
			goto out;

		case SCF_ERROR_INVALID_ARGUMENT:
			semerr(emsg_invalid_pg_name, pgname);
			goto fail;

		default:
			scfdie();
		}
	}

	do {
		if (scf_pg_update(pg) == -1)
			scfdie();
		if (scf_transaction_start(tx, pg) != 0) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();

			semerr(emsg_permission_denied);
			goto fail;
		}

		ret = scf_pg_get_property(pg, propname, prop);
		if (ret == 0) {
			scf_type_t ptype;
			char *pat = pattern;

			if (scf_property_type(prop, &ptype) != 0)
				scfdie();

			if (isadd) {
				if (type != NULL && ptype != ty) {
					semerr(gettext("Property \"%s\" is not "
					    "of type \"%s\".\n"), propname,
					    type);
					goto fail;
				}

				pat = NULL;
			} else {
				size_t len = strlen(pat);
				if (len > 0 && pat[len - 1] == '\"')
					pat[len - 1] = '\0';
				if (len > 0 && pat[0] == '\"')
					pat++;
			}

			ty = ptype;

			if (scf_property_is_masked(prop) == 0)
				get_prop_values(prop, values, pat);

			if (isadd)
				add_string(values, arg);

			if (scf_transaction_property_change(tx, e,
			    propname, ty) == -1)
				scfdie();
		} else if (scf_error() == SCF_ERROR_NOT_FOUND) {
			if (isadd) {
				if (type == NULL) {
					semerr(gettext("Type required "
					    "for new properties.\n"));
					goto fail;
				}

				add_string(values, arg);

				if (scf_transaction_property_new(tx, e,
				    propname, ty) == -1)
					scfdie();
			} else if (isnotfoundok) {
				result = 0;
				goto out;
			} else {
				semerr(gettext("No such property %s/%s.\n"),
				    pgname, propname);
				result = -1;
				goto out;
			}
		} else if (scf_error() == SCF_ERROR_INVALID_ARGUMENT) {
			semerr(emsg_invalid_prop_name, propname);
			goto fail;
		} else {
			scfdie();
		}

		walk = uu_list_walk_start(values, UU_DEFAULT);
		if (walk == NULL)
			uu_die(gettext("Could not walk property list.\n"));

		for (sp = uu_list_walk_next(walk); sp != NULL;
		    sp = uu_list_walk_next(walk)) {
			v = string_to_value(sp->str, ty, 0);

			if (v == NULL) {
				scf_entry_destroy_children(e);
				goto fail;
			}
			ret = scf_entry_add_value(e, v);
			assert(ret == 0);
		}
		uu_list_walk_end(walk);

		result = scf_transaction_commit(tx);

		scf_transaction_reset(tx);
		scf_entry_destroy_children(e);
	} while (result == 0);

	if (result < 0) {
		if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
			scfdie();

		semerr(emsg_permission_denied);
		goto fail;
	}

	result = 0;

	private_refresh();

out:
	scf_transaction_destroy(tx);
	scf_entry_destroy(e);
	scf_pg_destroy(pg);
	scf_property_destroy(prop);
	free(pattern);

	while ((sp = uu_list_teardown(values, &cookie)) != NULL) {
		free(sp->str);
		free(sp);
	}

	uu_list_destroy(values);

	return (result);

fail:
	result = -1;
	goto out;
}

int
lscf_addpropvalue(const char *pgname, const char *type, const char *value)
{
	return (lscf_setpropvalue(pgname, type, value, 1, 0));
}

int
lscf_delpropvalue(const char *pgname, const char *pattern, int isnotfoundok)
{
	return (lscf_setpropvalue(pgname, NULL, pattern, 0, isnotfoundok));
}

/*
 * Look for a standard start method, first in the instance (if any),
 * then the service.
 */
static const char *
start_method_name(int *in_instance)
{
	scf_propertygroup_t *pg;
	char **p;
	int ret;
	scf_instance_t *inst = cur_inst;

	if ((pg = scf_pg_create(g_hndl)) == NULL)
		scfdie();

again:
	for (p = start_method_names; *p != NULL; p++) {
		if (inst != NULL)
			ret = scf_instance_get_pg(inst, *p, pg);
		else
			ret = scf_service_get_pg(cur_svc, *p, pg);

		if (ret == 0) {
			size_t bufsz = strlen(SCF_GROUP_METHOD) + 1;
			char *buf = safe_malloc(bufsz);

			if ((ret = scf_pg_get_type(pg, buf, bufsz)) < 0) {
				free(buf);
				continue;
			}
			if (strcmp(buf, SCF_GROUP_METHOD) != 0) {
				free(buf);
				continue;
			}

			free(buf);
			*in_instance = (inst != NULL);
			scf_pg_destroy(pg);
			return (*p);
		}

		if (scf_error() == SCF_ERROR_NOT_FOUND)
			continue;

		scfdie();
	}

	if (inst != NULL) {
		inst = NULL;
		goto again;
	}

	scf_pg_destroy(pg);
	return (NULL);
}

static int
addpg(const char *name, const char *type)
{
	scf_propertygroup_t *pg;
	int ret;

	pg = scf_pg_create(g_hndl);
	if (pg == NULL)
		scfdie();

	if (cur_inst != NULL)
		ret = scf_instance_add_pg(cur_inst, name, type, 0, pg);
	else
		ret = scf_service_add_pg(cur_svc, name, type, 0, pg);

	if (ret != 0) {
		switch (scf_error()) {
		case SCF_ERROR_EXISTS:
			ret = 0;
			break;

		case SCF_ERROR_PERMISSION_DENIED:
			semerr(emsg_permission_denied);
			break;

		default:
			scfdie();
		}
	}

	scf_pg_destroy(pg);
	return (ret);
}

int
lscf_setenv(uu_list_t *args, int isunset)
{
	int ret = 0;
	size_t i;
	int argc;
	char **argv = NULL;
	string_list_t *slp;
	char *pattern;
	char *prop;
	int do_service = 0;
	int do_instance = 0;
	const char *method = NULL;
	const char *name = NULL;
	const char *value = NULL;
	scf_instance_t *saved_cur_inst = cur_inst;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return (-1);

	argc = uu_list_numnodes(args);
	if (argc < 1)
		goto usage;

	argv = calloc(argc + 1, sizeof (char *));
	if (argv == NULL)
		uu_die(gettext("Out of memory.\n"));

	for (slp = uu_list_first(args), i = 0;
	    slp != NULL;
	    slp = uu_list_next(args, slp), ++i)
		argv[i] = slp->str;

	argv[i] = NULL;

	opterr = 0;
	optind = 0;
	for (;;) {
		ret = getopt(argc, argv, "sim:");
		if (ret == -1)
			break;

		switch (ret) {
		case 's':
			do_service = 1;
			cur_inst = NULL;
			break;

		case 'i':
			do_instance = 1;
			break;

		case 'm':
			method = optarg;
			break;

		case '?':
			goto usage;

		default:
			bad_error("getopt", ret);
		}
	}

	argc -= optind;
	if ((do_service && do_instance) ||
	    (isunset && argc != 1) ||
	    (!isunset && argc != 2))
		goto usage;

	name = argv[optind];
	if (!isunset)
		value = argv[optind + 1];

	if (cur_snap != NULL) {
		semerr(emsg_cant_modify_snapshots);
		ret = -1;
		goto out;
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		ret = -1;
		goto out;
	}

	if (do_instance && cur_inst == NULL) {
		semerr(gettext("No instance is selected.\n"));
		ret = -1;
		goto out;
	}

	if (do_service && cur_svc == NULL) {
		semerr(gettext("No service is selected.\n"));
		ret = -1;
		goto out;
	}

	if (method == NULL) {
		if (do_instance || do_service) {
			method = "method_context";
			if (!isunset) {
				ret = addpg("method_context",
				    SCF_GROUP_FRAMEWORK);
				if (ret != 0)
					goto out;
			}
		} else {
			int in_instance;
			method = start_method_name(&in_instance);
			if (method == NULL) {
				semerr(gettext(
				    "Couldn't find start method; please "
				    "specify a method with '-m'.\n"));
				ret = -1;
				goto out;
			}
			if (!in_instance)
				cur_inst = NULL;
		}
	} else {
		scf_propertygroup_t *pg;
		size_t bufsz;
		char *buf;
		int ret;

		if ((pg = scf_pg_create(g_hndl)) == NULL)
			scfdie();

		if (cur_inst != NULL)
			ret = scf_instance_get_pg(cur_inst, method, pg);
		else
			ret = scf_service_get_pg(cur_svc, method, pg);

		if (ret != 0) {
			scf_pg_destroy(pg);
			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND:
				semerr(gettext("Couldn't find the method "
				    "\"%s\".\n"), method);
				goto out;

			case SCF_ERROR_INVALID_ARGUMENT:
				semerr(gettext("Invalid method name \"%s\".\n"),
				    method);
				goto out;

			default:
				scfdie();
			}
		}

		bufsz = strlen(SCF_GROUP_METHOD) + 1;
		buf = safe_malloc(bufsz);

		if (scf_pg_get_type(pg, buf, bufsz) < 0 ||
		    strcmp(buf, SCF_GROUP_METHOD) != 0) {
			semerr(gettext("Property group \"%s\" is not of type "
			    "\"method\".\n"), method);
			ret = -1;
			free(buf);
			scf_pg_destroy(pg);
			goto out;
		}

		free(buf);
		scf_pg_destroy(pg);
	}

	prop = uu_msprintf("%s/environment", method);
	pattern = uu_msprintf("%s=*", name);

	if (prop == NULL || pattern == NULL)
		uu_die(gettext("Out of memory.\n"));

	ret = lscf_delpropvalue(prop, pattern, !isunset);

	if (ret == 0 && !isunset) {
		uu_free(pattern);
		uu_free(prop);
		prop = uu_msprintf("%s/environment", method);
		pattern = uu_msprintf("%s=%s", name, value);
		if (prop == NULL || pattern == NULL)
			uu_die(gettext("Out of memory.\n"));
		ret = lscf_addpropvalue(prop, "astring:", pattern);
	}
	uu_free(pattern);
	uu_free(prop);

out:
	cur_inst = saved_cur_inst;

	free(argv);
	return (ret);
usage:
	ret = -2;
	goto out;
}

/*
 * Snapshot commands
 */

void
lscf_listsnap()
{
	scf_snapshot_t *snap;
	scf_iter_t *iter;
	char *nb;
	int r;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_inst == NULL) {
		semerr(gettext("Instance not selected.\n"));
		return;
	}

	if ((snap = scf_snapshot_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (scf_iter_instance_snapshots(iter, cur_inst) != SCF_SUCCESS)
		scfdie();

	nb = safe_malloc(max_scf_name_len + 1);

	while ((r = scf_iter_next_snapshot(iter, snap)) == 1) {
		if (scf_snapshot_get_name(snap, nb, max_scf_name_len + 1) < 0)
			scfdie();

		(void) puts(nb);
	}
	if (r < 0)
		scfdie();

	free(nb);
	scf_iter_destroy(iter);
	scf_snapshot_destroy(snap);
}

void
lscf_selectsnap(const char *name)
{
	scf_snapshot_t *snap;
	scf_snaplevel_t *level;

	if (g_hndl == NULL)
		(void) lscf_prep_hndl(LSCF_HNDL_NONE);

	if (cur_inst == NULL) {
		semerr(gettext("Instance not selected.\n"));
		return;
	}

	if (cur_snap != NULL) {
		if (name != NULL) {
			char *cur_snap_name;
			boolean_t nochange;

			cur_snap_name = safe_malloc(max_scf_name_len + 1);

			if (scf_snapshot_get_name(cur_snap, cur_snap_name,
			    max_scf_name_len + 1) < 0)
				scfdie();

			nochange = strcmp(name, cur_snap_name) == 0;

			free(cur_snap_name);

			if (nochange)
				return;
		}

		unselect_cursnap();
	}

	if (name == NULL)
		return;

	if ((snap = scf_snapshot_create(g_hndl)) == NULL ||
	    (level = scf_snaplevel_create(g_hndl)) == NULL)
		scfdie();

	if (scf_instance_get_snapshot(cur_inst, name, snap) !=
	    SCF_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_INVALID_ARGUMENT:
			semerr(gettext("Invalid name \"%s\".\n"), name);
			break;

		case SCF_ERROR_NOT_FOUND:
			semerr(gettext("No such snapshot \"%s\".\n"), name);
			break;

		default:
			scfdie();
		}

		scf_snaplevel_destroy(level);
		scf_snapshot_destroy(snap);
		return;
	}

	/* Load the snaplevels into our list. */
	cur_levels = uu_list_create(snaplevel_pool, NULL, 0);
	if (cur_levels == NULL)
		uu_die(gettext("Could not create list: %s\n"),
		    uu_strerror(uu_error()));

	if (scf_snapshot_get_base_snaplevel(snap, level) != SCF_SUCCESS) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();

		semerr(gettext("Snapshot has no snaplevels.\n"));

		scf_snaplevel_destroy(level);
		scf_snapshot_destroy(snap);
		return;
	}

	cur_snap = snap;

	for (;;) {
		cur_elt = safe_malloc(sizeof (*cur_elt));
		uu_list_node_init(cur_elt, &cur_elt->list_node,
		    snaplevel_pool);
		cur_elt->sl = level;
		if (uu_list_insert_after(cur_levels, NULL, cur_elt) != 0)
			uu_die(gettext("libuutil error: %s\n"),
			    uu_strerror(uu_error()));

		level = scf_snaplevel_create(g_hndl);
		if (level == NULL)
			scfdie();

		if (scf_snaplevel_get_next_snaplevel(cur_elt->sl,
		    level) != SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();

			scf_snaplevel_destroy(level);
			break;
		}
	}

	cur_elt = uu_list_last(cur_levels);
	cur_level = cur_elt->sl;
}

/*
 * Copies the properties & values in src to dst.  Assumes src won't change.
 * Returns -1 if permission is denied, -2 if another transaction interrupts,
 * and 0 on success.
 *
 * If enabled is 0 or 1, its value is used for the SCF_PROPERTY_ENABLED
 * property, if it is copied and has type boolean.  (See comment in
 * lscf_revert()).
 */
static int
pg_copy(const scf_propertygroup_t *src, scf_propertygroup_t *dst,
    uint8_t enabled)
{
	scf_transaction_t *tx;
	scf_iter_t *iter, *viter;
	scf_property_t *prop;
	scf_value_t *v;
	char *nbuf;
	int r;

	tx = scf_transaction_create(g_hndl);
	if (tx == NULL)
		scfdie();

	if (scf_transaction_start(tx, dst) != SCF_SUCCESS) {
		if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
			scfdie();

		scf_transaction_destroy(tx);

		return (-1);
	}

	if ((iter = scf_iter_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (viter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	nbuf = safe_malloc(max_scf_name_len + 1);

	if (scf_iter_pg_properties(iter, src) != SCF_SUCCESS)
		scfdie();

	for (;;) {
		scf_transaction_entry_t *e;
		scf_type_t ty;

		r = scf_iter_next_property(iter, prop);
		if (r == -1)
			scfdie();
		if (r == 0)
			break;

		e = scf_entry_create(g_hndl);
		if (e == NULL)
			scfdie();

		if (scf_property_type(prop, &ty) != SCF_SUCCESS)
			scfdie();

		if (scf_property_get_name(prop, nbuf, max_scf_name_len + 1) < 0)
			scfdie();

		if (scf_transaction_property_new(tx, e, nbuf,
		    ty) != SCF_SUCCESS)
			scfdie();

		if ((enabled == 0 || enabled == 1) &&
		    strcmp(nbuf, scf_property_enabled) == 0 &&
		    ty == SCF_TYPE_BOOLEAN) {
			v = scf_value_create(g_hndl);
			if (v == NULL)
				scfdie();

			scf_value_set_boolean(v, enabled);

			if (scf_entry_add_value(e, v) != 0)
				scfdie();
		} else {
			if (scf_iter_property_values(viter, prop) != 0)
				scfdie();

			for (;;) {
				v = scf_value_create(g_hndl);
				if (v == NULL)
					scfdie();

				r = scf_iter_next_value(viter, v);
				if (r == -1)
					scfdie();
				if (r == 0) {
					scf_value_destroy(v);
					break;
				}

				if (scf_entry_add_value(e, v) != SCF_SUCCESS)
					scfdie();
			}
		}
	}

	free(nbuf);
	scf_iter_destroy(viter);
	scf_property_destroy(prop);
	scf_iter_destroy(iter);

	r = scf_transaction_commit(tx);
	if (r == -1 && scf_error() != SCF_ERROR_PERMISSION_DENIED)
		scfdie();

	scf_transaction_destroy_children(tx);
	scf_transaction_destroy(tx);

	switch (r) {
	case 1:		return (0);
	case 0:		return (-2);
	case -1:	return (-1);

	default:
		abort();
	}

	/* NOTREACHED */
}

void
lscf_revert(const char *snapname)
{
	scf_snapshot_t *snap, *prev;
	scf_snaplevel_t *level, *nlevel;
	scf_iter_t *iter;
	scf_propertygroup_t *pg, *npg;
	scf_property_t *prop;
	scf_value_t *val;
	char *nbuf, *tbuf;
	uint8_t enabled;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_inst == NULL) {
		semerr(gettext("Instance not selected.\n"));
		return;
	}

	if (snapname != NULL) {
		snap = scf_snapshot_create(g_hndl);
		if (snap == NULL)
			scfdie();

		if (scf_instance_get_snapshot(cur_inst, snapname, snap) !=
		    SCF_SUCCESS) {
			switch (scf_error()) {
			case SCF_ERROR_INVALID_ARGUMENT:
				semerr(gettext("Invalid snapshot name "
				    "\"%s\".\n"), snapname);
				break;

			case SCF_ERROR_NOT_FOUND:
				semerr(gettext("No such snapshot.\n"));
				break;

			default:
				scfdie();
			}

			scf_snapshot_destroy(snap);
			return;
		}
	} else {
		if (cur_snap != NULL) {
			snap = cur_snap;
		} else {
			semerr(gettext("No snapshot selected.\n"));
			return;
		}
	}

	if ((prev = scf_snapshot_create(g_hndl)) == NULL ||
	    (level = scf_snaplevel_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL ||
	    (pg = scf_pg_create(g_hndl)) == NULL ||
	    (npg = scf_pg_create(g_hndl)) == NULL ||
	    (prop = scf_property_create(g_hndl)) == NULL ||
	    (val = scf_value_create(g_hndl)) == NULL)
		scfdie();

	nbuf = safe_malloc(max_scf_name_len + 1);
	tbuf = safe_malloc(max_scf_pg_type_len + 1);

	/* Take the "previous" snapshot before we blow away the properties. */
	if (scf_instance_get_snapshot(cur_inst, snap_previous, prev) == 0) {
		if (_scf_snapshot_take_attach(cur_inst, prev) != 0)
			scfdie();
	} else {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();

		if (_scf_snapshot_take_new(cur_inst, snap_previous, prev) != 0)
			scfdie();
	}

	/* Save general/enabled, since we're probably going to replace it. */
	enabled = 2;
	if (scf_instance_get_pg(cur_inst, scf_pg_general, pg) == 0 &&
	    scf_pg_get_property(pg, scf_property_enabled, prop) == 0 &&
	    scf_property_get_value(prop, val) == 0)
		(void) scf_value_get_boolean(val, &enabled);

	if (scf_snapshot_get_base_snaplevel(snap, level) != SCF_SUCCESS) {
		if (scf_error() != SCF_ERROR_NOT_FOUND)
			scfdie();

		goto out;
	}

	for (;;) {
		boolean_t isinst;
		uint32_t flags;
		int r;

		/* Clear the properties from the corresponding entity. */
		isinst = snaplevel_is_instance(level);

		if (!isinst)
			r = scf_iter_service_pgs(iter, cur_svc);
		else
			r = scf_iter_instance_pgs(iter, cur_inst);
		if (r != SCF_SUCCESS)
			scfdie();

		while ((r = scf_iter_next_pg(iter, pg)) == 1) {
			if (scf_pg_get_flags(pg, &flags) != SCF_SUCCESS)
				scfdie();

			/* Skip nonpersistent pgs. */
			if (flags & SCF_PG_FLAG_NONPERSISTENT)
				continue;

			if (scf_pg_delete(pg) != SCF_SUCCESS) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();

				semerr(emsg_permission_denied);
				goto out;
			}
		}
		if (r == -1)
			scfdie();

		/* Copy the properties to the corresponding entity. */
		if (scf_iter_snaplevel_pgs(iter, level) != SCF_SUCCESS)
			scfdie();

		while ((r = scf_iter_next_pg(iter, pg)) == 1) {
			if (scf_pg_get_name(pg, nbuf, max_scf_name_len + 1) < 0)
				scfdie();

			if (scf_pg_get_type(pg, tbuf, max_scf_pg_type_len + 1) <
			    0)
				scfdie();

			if (scf_pg_get_flags(pg, &flags) != SCF_SUCCESS)
				scfdie();

			if (!isinst)
				r = scf_service_add_pg(cur_svc, nbuf, tbuf,
				    flags, npg);
			else
				r = scf_instance_add_pg(cur_inst, nbuf, tbuf,
				    flags, npg);
			if (r != SCF_SUCCESS) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();

				semerr(emsg_permission_denied);
				goto out;
			}

			if ((enabled == 0 || enabled == 1) &&
			    strcmp(nbuf, scf_pg_general) == 0)
				r = pg_copy(pg, npg, enabled);
			else
				r = pg_copy(pg, npg, 2);

			switch (r) {
			case 0:
				break;

			case -1:
				semerr(emsg_permission_denied);
				goto out;

			case -2:
				semerr(gettext(
				    "Interrupted by another change.\n"));
				goto out;

			default:
				abort();
			}
		}
		if (r == -1)
			scfdie();

		/* Get next level. */
		nlevel = scf_snaplevel_create(g_hndl);
		if (nlevel == NULL)
			scfdie();

		if (scf_snaplevel_get_next_snaplevel(level, nlevel) !=
		    SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_NOT_FOUND)
				scfdie();

			scf_snaplevel_destroy(nlevel);
			break;
		}

		scf_snaplevel_destroy(level);
		level = nlevel;
	}

	if (snapname == NULL) {
		lscf_selectsnap(NULL);
		snap = NULL;		/* cur_snap has been destroyed */
	}

out:
	free(tbuf);
	free(nbuf);
	scf_value_destroy(val);
	scf_property_destroy(prop);
	scf_pg_destroy(npg);
	scf_pg_destroy(pg);
	scf_iter_destroy(iter);
	scf_snaplevel_destroy(level);
	scf_snapshot_destroy(prev);
	if (snap != cur_snap)
		scf_snapshot_destroy(snap);
}

void
lscf_refresh(void)
{
	ssize_t fmrilen;
	size_t bufsz;
	char *fmribuf;
	int r;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return;

	if (cur_inst == NULL) {
		semerr(gettext("Instance not selected.\n"));
		return;
	}

	bufsz = max_scf_fmri_len + 1;
	fmribuf = safe_malloc(bufsz);
	fmrilen = scf_instance_to_fmri(cur_inst, fmribuf, bufsz);
	if (fmrilen < 0) {
		free(fmribuf);
		if (scf_error() != SCF_ERROR_DELETED)
			scfdie();
		scf_instance_destroy(cur_inst);
		cur_inst = NULL;
		warn(emsg_deleted);
		return;
	}
	assert(fmrilen < bufsz);

	r = refresh_entity(0, cur_inst, fmribuf, NULL, NULL, NULL);
	switch (r) {
	case 0:
		break;

	case ECONNABORTED:
		warn(gettext("Could not refresh %s "
		    "(repository connection broken).\n"), fmribuf);
		break;

	case ECANCELED:
		warn(emsg_deleted);
		break;

	case EPERM:
		warn(gettext("Could not refresh %s "
		    "(permission denied).\n"), fmribuf);
		break;

	case ENOSPC:
		warn(gettext("Could not refresh %s "
		    "(repository server out of resources).\n"),
		    fmribuf);
		break;

	case EACCES:
	default:
		bad_error("refresh_entity", scf_error());
	}

	free(fmribuf);
}

/*
 * describe [-v] [-t] [pg/prop]
 */
int
lscf_describe(uu_list_t *args, int hasargs)
{
	int ret = 0;
	size_t i;
	int argc;
	char **argv = NULL;
	string_list_t *slp;
	int do_verbose = 0;
	int do_templates = 0;
	char *pattern = NULL;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return (-1);

	if (hasargs != 0)  {
		argc = uu_list_numnodes(args);
		if (argc < 1)
			goto usage;

		argv = calloc(argc + 1, sizeof (char *));
		if (argv == NULL)
			uu_die(gettext("Out of memory.\n"));

		for (slp = uu_list_first(args), i = 0;
		    slp != NULL;
		    slp = uu_list_next(args, slp), ++i)
			argv[i] = slp->str;

		argv[i] = NULL;

		/*
		 * We start optind = 0 because our list of arguments
		 * starts at argv[0]
		 */
		optind = 0;
		opterr = 0;
		for (;;) {
			ret = getopt(argc, argv, "vt");
			if (ret == -1)
				break;

			switch (ret) {
			case 'v':
				do_verbose = 1;
				break;

			case 't':
				do_templates = 1;
				break;

			case '?':
				goto usage;

			default:
				bad_error("getopt", ret);
			}
		}

		pattern = argv[optind];
	}

	if (cur_inst == NULL && cur_svc == NULL) {
		semerr(emsg_entity_not_selected);
		ret = -1;
		goto out;
	}

	/*
	 * list_entity_tmpl(), listprop() and listtmpl() produce verbose
	 * output if their last parameter is set to 2.  Less information is
	 * produced if the parameter is set to 1.
	 */
	if (pattern == NULL) {
		if (do_verbose == 1)
			list_entity_tmpl(2);
		else
			list_entity_tmpl(1);
	}

	if (do_templates == 0) {
		if (do_verbose == 1)
			listprop(pattern, 0, 2, 0, NULL, NULL);
		else
			listprop(pattern, 0, 1, 0, NULL, NULL);
	} else {
		if (do_verbose == 1)
			listtmpl(pattern, 2);
		else
			listtmpl(pattern, 1);
	}

	ret = 0;
out:
	if (argv != NULL)
		free(argv);
	return (ret);
usage:
	ret = -2;
	goto out;
}

#define	PARAM_ACTIVE	((const char *) "active")
#define	PARAM_INACTIVE	((const char *) "inactive")
#define	PARAM_SMTP_TO	((const char *) "to")

/*
 * tokenize()
 * Breaks down the string according to the tokens passed.
 * Caller is responsible for freeing array of pointers returned.
 * Returns NULL on failure
 */
char **
tokenize(char *str, const char *sep)
{
	char *token, *lasts;
	char **buf;
	int n = 0;	/* number of elements */
	int size = 8;	/* size of the array (initial) */

	buf = safe_malloc(size * sizeof (char *));

	for (token = strtok_r(str, sep, &lasts); token != NULL;
	    token = strtok_r(NULL, sep, &lasts), ++n) {
		if (n + 1 >= size) {
			size *= 2;
			if ((buf = realloc(buf, size * sizeof (char *))) ==
			    NULL) {
				uu_die(gettext("Out of memory"));
			}
		}
		buf[n] = token;
	}
	/* NULL terminate the pointer array */
	buf[n] = NULL;

	return (buf);
}

int32_t
check_tokens(char **p)
{
	int32_t smf = 0;
	int32_t fma = 0;

	while (*p) {
		int32_t t = string_to_tset(*p);

		if (t == 0) {
			if (is_fma_token(*p) == 0)
				return (INVALID_TOKENS);
			fma = 1; /* this token is an fma event */
		} else {
			smf |= t;
		}

		if (smf != 0 && fma == 1)
			return (MIXED_TOKENS);
		++p;
	}

	if (smf > 0)
		return (smf);
	else if (fma == 1)
		return (FMA_TOKENS);

	return (INVALID_TOKENS);
}

static int
get_selection_str(char *fmri, size_t sz)
{
	if (g_hndl == NULL) {
		semerr(emsg_entity_not_selected);
		return (-1);
	} else if (cur_level != NULL) {
		semerr(emsg_invalid_for_snapshot);
		return (-1);
	} else {
		lscf_get_selection_str(fmri, sz);
	}

	return (0);
}

void
lscf_delnotify(const char *set, int global)
{
	char *str = strdup(set);
	char **pgs;
	char **p;
	int32_t tset;
	char *fmri = NULL;

	if (str == NULL)
		uu_die(gettext("Out of memory.\n"));

	pgs = tokenize(str, ",");

	if ((tset = check_tokens(pgs)) > 0) {
		size_t sz = max_scf_fmri_len + 1;

		fmri = safe_malloc(sz);
		if (global) {
			(void) strlcpy(fmri, SCF_INSTANCE_GLOBAL, sz);
		} else if (get_selection_str(fmri, sz) != 0) {
			goto out;
		}

		if (smf_notify_del_params(SCF_SVC_TRANSITION_CLASS, fmri,
		    tset) != SCF_SUCCESS) {
			uu_warn(gettext("Failed smf_notify_del_params: %s\n"),
			    scf_strerror(scf_error()));
		}
	} else if (tset == FMA_TOKENS) {
		if (global) {
			semerr(gettext("Can't use option '-g' with FMA event "
			    "definitions\n"));
			goto out;
		}

		for (p = pgs; *p; ++p) {
			if (smf_notify_del_params(de_tag(*p), NULL, NULL) !=
			    SCF_SUCCESS) {
				uu_warn(gettext("Failed for \"%s\": %s\n"), *p,
				    scf_strerror(scf_error()));
				goto out;
			}
		}
	} else if (tset == MIXED_TOKENS) {
		semerr(gettext("Can't mix SMF and FMA event definitions\n"));
		goto out;
	} else {
		uu_die(gettext("Invalid input.\n"));
	}

out:
	free(fmri);
	free(pgs);
	free(str);
}

void
lscf_listnotify(const char *set, int global)
{
	char *str = safe_strdup(set);
	char **pgs;
	char **p;
	int32_t tset;
	nvlist_t *nvl;
	char *fmri = NULL;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		uu_die(gettext("Out of memory.\n"));

	pgs = tokenize(str, ",");

	if ((tset = check_tokens(pgs)) > 0) {
		size_t sz = max_scf_fmri_len + 1;

		fmri = safe_malloc(sz);
		if (global) {
			(void) strlcpy(fmri, SCF_INSTANCE_GLOBAL, sz);
		} else if (get_selection_str(fmri, sz) != 0) {
			goto out;
		}

		if (_scf_get_svc_notify_params(fmri, nvl, tset, 1, 1) !=
		    SCF_SUCCESS) {
			if (scf_error() != SCF_ERROR_NOT_FOUND &&
			    scf_error() != SCF_ERROR_DELETED)
				uu_warn(gettext(
				    "Failed listnotify: %s\n"),
				    scf_strerror(scf_error()));
			goto out;
		}

		listnotify_print(nvl, NULL);
	} else if (tset == FMA_TOKENS) {
		if (global) {
			semerr(gettext("Can't use option '-g' with FMA event "
			    "definitions\n"));
			goto out;
		}

		for (p = pgs; *p; ++p) {
			if (_scf_get_fma_notify_params(de_tag(*p), nvl, 1) !=
			    SCF_SUCCESS) {
				/*
				 * if the preferences have just been deleted
				 * or does not exist, just skip.
				 */
				if (scf_error() == SCF_ERROR_NOT_FOUND ||
				    scf_error() == SCF_ERROR_DELETED)
					continue;
				uu_warn(gettext(
				    "Failed listnotify: %s\n"),
				    scf_strerror(scf_error()));
				goto out;
			}
			listnotify_print(nvl, re_tag(*p));
		}
	} else if (tset == MIXED_TOKENS) {
		semerr(gettext("Can't mix SMF and FMA event definitions\n"));
		goto out;
	} else {
		semerr(gettext("Invalid input.\n"));
	}

out:
	nvlist_free(nvl);
	free(fmri);
	free(pgs);
	free(str);
}

typedef struct {
	int			 lccb_masked;
	scf_decoration_layer_t	*lccb_layers;
	const char		*lccb_pattern;
} listcust_cb_data_t;

static int
listcust_cb(void *data, scf_walkinfo_t *wip)
{
	listcust_cb_data_t	*d = (listcust_cb_data_t *)data;
	scf_service_t		*save_svc = cur_svc;
	scf_instance_t		*save_inst = cur_inst;
	int			 ent_masked = 0;
	scf_decoration_t	*dec;
	int			 getdec = 0;
	scf_decoration_layer_t	 l;

	assert(wip != NULL);

	dec = scf_decoration_create(g_hndl);
	if (dec == NULL)
		scfdie();

	if (wip->inst != NULL) {
		ent_masked = scf_instance_is_masked(wip->inst);
		getdec = scf_instance_get_decoration(wip->inst,
		    SCF_DECORATION_TOP, dec);
	} else {
		/* Don't process smf/manifest */
		if (strcmp(wip->fmri, SCF_FMRI_SVC_PREFIX
		    SCF_FMRI_SERVICE_PREFIX HASH_SVC) == 0) {
			scf_decoration_destroy(dec);
			return (0);
		}

		ent_masked = scf_service_is_masked(wip->svc);
		getdec = scf_service_get_decoration(wip->svc,
		    SCF_DECORATION_TOP, dec);
	}
	cur_svc = wip->svc;
	cur_inst = wip->inst;

	if (ent_masked == -1 ||
	    (getdec != SCF_SUCCESS &&
	    scf_error() != SCF_ERROR_CONSTRAINT_VIOLATED)) {
		scfdie();
	}

	if (scf_decoration_get_layer(dec, &l) != SCF_SUCCESS)
		scfdie();

	if (((l == SCF_DECORATION_SITE_PROFILE ||
	    l == SCF_DECORATION_ADMIN) && d->lccb_masked == 0) ||
	    ent_masked == 1) {
		safe_printf("%s %s%s\n", wip->fmri,
		    scf_decoration_layer_to_string(l),
		    (ent_masked) ? " MASKED" : "");
		listprop_prefix = NULL;
	} else {
		listprop_prefix = wip->fmri;
	}

	listprop(d->lccb_pattern, 0, 0, d->lccb_masked, NULL,
	    d->lccb_layers);

	/* Walk instances if we're processing a service */
	if (cur_inst == NULL) {
		int ret;
		char *sname;

		sname = safe_strdup(wip->fmri);
		if ((ret = scf_walk_fmri(g_hndl, 1, (char **)&sname,
		    SCF_WALK_MULTIPLE | SCF_WALK_EMPTY, listcust_cb,
		    data, NULL, semerr)) != 0) {
			bad_error("scf_walk_fmri", ret);
		}
		free(sname);
	}

out:
	cur_inst = save_inst;
	cur_svc = save_svc;

	scf_decoration_destroy(dec);

	return (0);
}

static int
delcust(const void *entity, const char *pattern, int only_masked, int issvc)
{
	scf_iter_t *iter, *piter;
	scf_propertygroup_t *pg = NULL;
	scf_property_t *prop = NULL;
	scf_transaction_t *tx;
	scf_transaction_entry_t *e;
	char *pgnbuf, *prnbuf, *ppnbuf;

	void **objects;
	char **names;
	int allocd, i, r;
	int ret = 0;

	ssize_t prnlen, szret;

	if ((pg = scf_pg_create(g_hndl)) == NULL ||
	    (iter = scf_iter_create(g_hndl)) == NULL ||
	    (piter = scf_iter_create(g_hndl)) == NULL)
		scfdie();

	if (issvc)
		ret = scf_iter_service_pgs(iter, entity);
	else
		ret = scf_iter_instance_pgs(iter, entity);

	if (ret != 0)
		scfdie();

	/*
	 * Process one pg at a time to find pgs or properties
	 * that match the pattern.
	 *
	 * - If a pg matches the pattern, call scf_pg_delcust on the pg
	 *   and continue to next pg.
	 * - If a pg doesn't match the pattern, try properties and create
	 *   a transaction for all matched properties.
	 */
	while ((ret = scf_iter_next_pg(iter, pg)) == 1) {
		int process_props = 0;

		i = 0;
		allocd = 1;
		objects = safe_malloc(sizeof (*objects));
		names = safe_malloc(sizeof (*names));

		if ((prop = scf_property_create(g_hndl)) == NULL)
			scfdie();

		pgnbuf = safe_malloc(max_scf_name_len + 1);
		szret = scf_pg_get_name(pg, pgnbuf, max_scf_name_len + 1);
		if (szret < 0)
			scfdie();
		assert(szret <= max_scf_name_len);

		if (pattern == NULL || fnmatch(pattern, pgnbuf, 0) == 0) {
			int ent_masked = 0;

			/*
			 * If pg is not included due to only_masked,
			 * we still continue on through to look for
			 * properties.
			 */

			if ((ent_masked = scf_pg_is_masked(pg)) == -1)
				scfdie();

			if (!only_masked || (ent_masked == 1)) {
				(void) printf(msg_delcust_pg, pgnbuf);
				if (scf_pg_delcust(pg) != 0) {
					switch (scf_error()) {
					case SCF_ERROR_NOT_FOUND:
						break;
					case SCF_ERROR_PERMISSION_DENIED:
						semerr(emsg_permission_denied);
						break;
					default:
						scfdie();
					}
				}
				free(pgnbuf);
				continue;
			}

			process_props = 1;
		}

		if (scf_iter_pg_properties(piter, pg) != SCF_SUCCESS)
			scfdie();

		prnbuf = safe_malloc(max_scf_name_len + 1);
		while ((ret = scf_iter_next_property(piter, prop)) == 1) {
			int ent_masked = 0;
			szret = scf_property_get_name(prop, prnbuf,
			    max_scf_name_len + 1);
			if (szret < 0)
				scfdie();

			/* Will prepend the property group name and a slash. */
			prnlen = 2 * max_scf_name_len + 1;

			ppnbuf = safe_malloc(prnlen + 1);

			if (snprintf(ppnbuf, prnlen + 1, "%s/%s", pgnbuf,
			    prnbuf) < 0)
				uu_die("snprintf");

			if (process_props == 0 && pattern != NULL &&
			    fnmatch(pattern, ppnbuf, 0) != 0) {
				free(ppnbuf);
				continue;
			}

			if ((ent_masked = scf_property_is_masked(prop)) == -1)
				scfdie();

			if (!only_masked || (ent_masked == 1)) {
				if (i+1 >= allocd) {
					allocd *= 2;
					objects = realloc(objects,
					    sizeof (*objects) * allocd);
					names = realloc(names,
					    sizeof (*names) * allocd);
					if (objects == NULL || names == NULL)
						uu_die(gettext(
						    "Out of memory"));
				}

				objects[i] = prop;
				names[i] = prnbuf;
				++i;

				prop = scf_property_create(g_hndl);
				prnbuf = safe_malloc(max_scf_name_len + 1);
			}
		}

		free(prnbuf);
		objects[i] = NULL;
		scf_property_destroy(prop);

		/* Nothing to do if no property matches the pattern */
		if (i == 0) {
			free(pgnbuf);
			free(names);
			free(objects);
			continue;
		}

		/*
		 * At this point, we have filtered our list based on pattern
		 * and only_masked. Delete customizations for properties that
		 * match the pattern parameter.
		 */

		if ((tx = scf_transaction_create(g_hndl)) == NULL)
			scfdie();

		do {
			if (scf_pg_update(pg) == -1)
				scfdie();

			if (scf_transaction_start(tx, pg) != 0) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();

				semerr(emsg_permission_denied);
			}

			for (i = 0; objects[i] != NULL; ++i) {
				(void) printf(msg_delcust_prop, pgnbuf,
				    names[i]);

				prop = (scf_property_t *)objects[i];

				e = scf_entry_create(g_hndl);
				if (scf_transaction_property_delcust(tx, e,
				    names[i]) != 0) {
					switch (scf_error()) {
					case SCF_ERROR_NOT_FOUND:
						semerr(gettext("No such "
						    "property %s/%s.\n"),
						    pgnbuf, names[i]);
						goto next_pg;
					case SCF_ERROR_INVALID_ARGUMENT:
						semerr(emsg_invalid_prop_name,
						    names[i]);
						goto next_pg;
					default:
						scfdie();
					}
				}
			}

			if ((r = scf_transaction_commit(tx)) == 0)
				scf_transaction_reset(tx);

		} while (r == 0);

		if (r < 0) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();
			semerr(emsg_permission_denied);
		}

next_pg:
		/* Cleanup once transaction completed */
		for (i = 0; objects[i] != NULL; ++i) {
			prop = (scf_property_t *)objects[i];
			free(names[i]);
			scf_property_destroy(prop);
		}
		free(pgnbuf);
		free(names);
		free(objects);
		scf_transaction_destroy_children(tx);
		scf_transaction_destroy(tx);
	}
	if (ret != 0)
		scfdie();

	scf_pg_destroy(pg);
	scf_iter_destroy(iter);
	scf_iter_destroy(piter);

	return (0);
}

/*
 * delcust callback for both service and instance. Since scf_walk_fmri doesn't
 * walk both services and instances at the same time, we actually walk only
 * services from lscf_delcust and walk the service's instances in this function.
 */
static int
delcust_cb(void *data, scf_walkinfo_t *wip)
{
	listcust_cb_data_t	*d = (listcust_cb_data_t *)data;
	scf_service_t		*save_svc = cur_svc;
	scf_instance_t		*save_inst = cur_inst;
	int ent_masked;

	assert(wip != NULL);

	cur_svc = wip->svc;
	cur_inst = wip->inst;


	if (cur_inst != NULL) {
		(void) printf("%s\n", wip->fmri);

		if (d->lccb_pattern) {
			(void) delcust(cur_inst, d->lccb_pattern,
			    d->lccb_masked, 0);
			goto out;
		}

		if ((ent_masked = scf_instance_is_masked(cur_inst)) == -1)
			scfdie();

		if (!(d->lccb_masked) || (ent_masked == 1)) {
			(void) printf(msg_delcust_inst, wip->fmri);
			if (scf_instance_delcust(cur_inst)) {
				if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
					scfdie();
				semerr(emsg_permission_denied);
			}
			goto out;
		}
	} else {
		int ret;
		char *sname;

		/* Don't process smf/manifest */
		if (strcmp(wip->fmri, SCF_FMRI_SVC_PREFIX
		    SCF_FMRI_SERVICE_PREFIX HASH_SVC) == 0)
			goto out;

		(void) printf(msg_delcust_process, wip->fmri);

		/*
		 * If a pattern is not specified, delcust service is
		 * sufficient as that would delcust all lower entities.
		 */
		if (d->lccb_pattern == NULL) {
			if ((ent_masked =
			    scf_service_is_masked(cur_svc)) == -1)
				scfdie();

			if (!(d->lccb_masked) || (ent_masked == 1)) {
				(void) printf(msg_delcust_svc, wip->fmri);
				if (scf_service_delcust(cur_svc)) {
					if (scf_error() !=
					    SCF_ERROR_PERMISSION_DENIED)
						scfdie();
					semerr(emsg_permission_denied);
				}
			}
			goto out;
		}

		(void) delcust(cur_svc, d->lccb_pattern, d->lccb_masked, 1);

		sname = safe_strdup(wip->fmri);
		if ((ret = scf_walk_fmri(g_hndl, 1, (char **)&sname,
		    SCF_WALK_MULTIPLE | SCF_WALK_EMPTY, delcust_cb,
		    data, NULL, semerr)) != 0) {
			bad_error("scf_walk_fmri", ret);
		}
		free(sname);
	}

out:
	cur_inst = save_inst;
	cur_svc = save_svc;

	return (0);
}


/*
 * svccfg [-s fmri] delcust [-M] [pattern]
 *
 * - Specified pattern is applied only to property groups and properties.
 *
 * - When a selection is made (-s fmri), -M option is applied to children of
 *   the selected fmri.
 *
 * - When no selection is made, -M option is applied to services and instances.
 */
int
lscf_delcust(uu_list_t *args)
{
	scf_service_t *svc;
	scf_iter_t *siter;
	string_list_t *slp;
	char **argv = NULL;
	char *buf;
	char *pattern = NULL;
	int only_masked = 0;
	int argc, i, r;

	if (args != NULL) {
		argc = uu_list_numnodes(args);
		if (argc < 1) {
			return (-2);
		}

		argv = calloc(argc + 1, sizeof (char *));
		if (argv == NULL)
			uu_die(gettext("Out of memory.\n"));

		for (slp = uu_list_first(args), i = 0;
		    slp != NULL;
		    slp = uu_list_next(args, slp), ++i)
				argv[i] = slp->str;

		argv[i] = NULL;

		/*
		 * We start optind = 0 because our list of arguments
		 * starts at argv[0]
		 */
		optind = 0;
		opterr = 0;
		for (;;) {
			r = getopt(argc, argv, "M");
			if (r == -1)
				break;

			switch (r) {
			case 'M':
				only_masked = 1;
				break;

			case '?':
				return (-2);

			default:
				bad_error("getopt", r);
			}
		}
		pattern = argv[optind];
	}

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	buf = safe_malloc(max_scf_name_len + 1);

	/*
	 * Handle the non selection cases: svccfg delcust -M pattern
	 */
	if (cur_inst == NULL && cur_svc == NULL) {
		int ent_masked = 0;

		if (pattern == NULL) {
			assert(cur_scope != NULL);

			if ((siter = scf_iter_create(g_hndl)) == NULL ||
			    (svc = scf_service_create(g_hndl)) == NULL)
				scfdie();

			if (scf_iter_scope_services(siter, cur_scope))
				scfdie();

			for (;;) {
				r = scf_iter_next_service(siter, svc);
				if (r == 0)
					break;
				if (r != 1)
					scfdie();

				if ((ent_masked =
				    scf_service_is_masked(svc)) == -1)
					scfdie();

				if (!only_masked || (ent_masked == 1)) {
					if (scf_service_get_name(svc, buf,
					    max_scf_name_len + 1) < 0)
						scfdie();

					/* Don't process smf/manifest */
					if (strcmp(buf, HASH_SVC) == 0)
						continue;

					(void) printf(msg_delcust_svc, buf);
					if (scf_service_delcust(svc)) {
						if (scf_error() !=
						    SCF_ERROR_PERMISSION_DENIED)
							scfdie();
						semerr(emsg_permission_denied);
					}
				}
			}
			scf_service_destroy(svc);
			scf_iter_destroy(siter);
		} else {
			int ret;
			listcust_cb_data_t cbdata;

			cbdata.lccb_masked = only_masked;
			cbdata.lccb_pattern = pattern;

			if ((ret = scf_walk_fmri(g_hndl, 0, NULL,
			    SCF_WALK_SERVICE,
			    delcust_cb, (void *)&cbdata, NULL, semerr)) != 0) {
				bad_error("scf_walk_fmri", ret);
			}
		}
		goto out;
	}

	if (cur_inst != NULL) {
		if (pattern || only_masked) {
			if (delcust(cur_inst, pattern, only_masked, 0))
				scfdie();
			goto out;
		}

		if (scf_instance_get_name(cur_inst, buf,
		    max_scf_name_len + 1) < 0)
			scfdie();

		(void) printf(msg_delcust_inst, buf);
		if (scf_instance_delcust(cur_inst)) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();
			semerr(emsg_permission_denied);
		}
		goto out;
	} else {
		if (scf_service_get_name(cur_svc, buf,
		    max_scf_name_len + 1) < 0)
			scfdie();

		if (strcmp(buf, HASH_SVC) == 0) {
			(void) printf(emsg_delcust_svc, buf);
			goto out;
		}

		if (pattern || only_masked) {
			if (delcust(cur_svc, pattern, only_masked, 1))
				scfdie();
			goto out;
		}

		(void) printf(msg_delcust_svc, buf);
		if (scf_service_delcust(cur_svc)) {
			if (scf_error() != SCF_ERROR_PERMISSION_DENIED)
				scfdie();
			semerr(emsg_permission_denied);
		}
		goto out;
	}

out:
	free(buf);
	return (0);
}

int
lscf_listcust(uu_list_t *args)
{
	int ret = 0;
	size_t i;
	int argc;
	char **argv = NULL;
	string_list_t *slp;
	int maskedonly = 0;
	scf_decoration_layer_t *l = layers_admin_customizations;
	char *pattern = NULL;

	if (cur_snap) {
		semerr(gettext("Listing decoration information "
		    "is not supported when a snapshot is selected. "));

		return (ret);
	}

	(void) lscf_prep_hndl(LSCF_HNDL_MASKED);

	if (args != NULL) {
		argc = uu_list_numnodes(args);
		if (argc < 1) {
			ret = -2;
			goto out;
		}

		argv = calloc(argc + 1, sizeof (char *));
		if (argv == NULL)
			uu_die(gettext("Out of memory.\n"));

		for (slp = uu_list_first(args), i = 0;
		    slp != NULL;
		    slp = uu_list_next(args, slp), ++i)
				argv[i] = slp->str;

		argv[i] = NULL;

		/*
		 * We start optind = 0 because our list of arguments
		 * starts at argv[0]
		 */
		optind = 0;
		opterr = 0;
		for (;;) {
			ret = getopt(argc, argv, "LM");
			if (ret == -1)
				break;

			switch (ret) {
			case 'L':
				l = layers_local_customizations;
				break;

			case 'M':
				maskedonly = 1;
				break;

			case '?':
				ret = -2;
				goto out;

			default:
				bad_error("getopt", ret);
			}
		}

		pattern = argv[optind];

	}

	do_print_layer = 1;

	/*
	 * With a current selection, list the customised pgs/props.
	 *
	 * Otherwise, walk the services in the current scope and print
	 * each customized service and instance.  With -M (maskedonly),
	 * leave out the handcrafted entities and only list masked.
	 */

	if (cur_svc != NULL || cur_inst != NULL) {
		listprop(pattern, 0, 0, maskedonly, NULL, l);
	} else {
		int ret;
		listcust_cb_data_t cbdata;

		cbdata.lccb_masked = maskedonly;
		cbdata.lccb_layers = l;
		cbdata.lccb_pattern = pattern;

		listprop_indent = 2;

		if ((ret = scf_walk_fmri(g_hndl, 0, NULL,
		    SCF_WALK_SERVICE,
		    listcust_cb, (void *)&cbdata, NULL, semerr)) != 0) {
			bad_error("scf_walk_fmri", ret);
		}

		listprop_indent = 0;
	}

out:
	/*
	 * Theoretically, everybody that cares about global state is
	 * already setting these on entry.  Still, it's nice to clean up
	 * after ourselves.
	 */
	do_print_layer = 0;
	return (ret);
}

static char *
strip_quotes_and_blanks(char *s)
{
	char *start = s;
	char *end = strrchr(s, '\"');

	if (s[0] == '\"' && end != NULL && *(end + 1) == '\0') {
		start = s + 1;
		while (isblank(*start))
			start++;
		while (isblank(*(end - 1)) && end > start) {
			end--;
		}
		*end = '\0';
	}

	return (start);
}

static int
set_active(nvlist_t *mech, const char *hier_part)
{
	boolean_t b;

	if (*hier_part == '\0' || strcmp(hier_part, PARAM_ACTIVE) == 0) {
		b = B_TRUE;
	} else if (strcmp(hier_part, PARAM_INACTIVE) == 0) {
		b = B_FALSE;
	} else {
		return (-1);
	}

	if (nvlist_add_boolean_value(mech, PARAM_ACTIVE, b) != 0)
		uu_die(gettext("Out of memory.\n"));

	return (0);
}

static int
add_snmp_params(nvlist_t *mech, char *hier_part)
{
	return (set_active(mech, hier_part));
}

static int
add_syslog_params(nvlist_t *mech, char *hier_part)
{
	return (set_active(mech, hier_part));
}

/*
 * add_mailto_paramas()
 * parse the hier_part of mailto URI
 * mailto:<addr>[?<header1>=<value1>[&<header2>=<value2>]]
 * or mailto:{[active]|inactive}
 */
static int
add_mailto_params(nvlist_t *mech, char *hier_part)
{
	const char *tok = "?&";
	char *p;
	char *lasts;
	char *param;
	char *val;

	/*
	 * If the notification parametes are in the form of
	 *
	 *   malito:{[active]|inactive}
	 *
	 * we set the property accordingly and return.
	 * Otherwise, we make the notification type active and
	 * process the hier_part.
	 */
	if (set_active(mech, hier_part) == 0)
		return (0);
	else if (set_active(mech, PARAM_ACTIVE) != 0)
		return (-1);

	if ((p = strtok_r(hier_part, tok, &lasts)) == NULL) {
		/*
		 * sanity check: we only get here if hier_part = "", but
		 * that's handled by set_active
		 */
		uu_die("strtok_r");
	}

	if (nvlist_add_string(mech, PARAM_SMTP_TO, p) != 0)
		uu_die(gettext("Out of memory.\n"));

	while ((p = strtok_r(NULL, tok, &lasts)) != NULL)
		if ((param = strtok_r(p, "=", &val)) != NULL)
			if (nvlist_add_string(mech, param, val) != 0)
				uu_die(gettext("Out of memory.\n"));

	return (0);
}

static int
uri_split(char *uri, char **scheme, char **hier_part)
{
	int r = -1;

	if ((*scheme = strtok_r(uri, ":", hier_part)) == NULL ||
	    *hier_part == NULL) {
		semerr(gettext("'%s' is not an URI\n"), uri);
		return (r);
	}

	if ((r = check_uri_scheme(*scheme)) < 0) {
		semerr(gettext("Unkown URI scheme: %s\n"), *scheme);
		return (r);
	}

	return (r);
}

static int
process_uri(nvlist_t *params, char *uri)
{
	char *scheme;
	char *hier_part;
	nvlist_t *mech;
	int index;
	int r;

	if ((index = uri_split(uri, &scheme, &hier_part)) < 0)
		return (-1);

	if (nvlist_alloc(&mech, NV_UNIQUE_NAME, 0) != 0)
		uu_die(gettext("Out of memory.\n"));

	switch (index) {
	case 0:
		/* error messages displayed by called function */
		r = add_mailto_params(mech, hier_part);
		break;

	case 1:
		if ((r = add_snmp_params(mech, hier_part)) != 0)
			semerr(gettext("Not valid parameters: '%s'\n"),
			    hier_part);
		break;

	case 2:
		if ((r = add_syslog_params(mech, hier_part)) != 0)
			semerr(gettext("Not valid parameters: '%s'\n"),
			    hier_part);
		break;

	default:
		r = -1;
	}

	if (r == 0 && nvlist_add_nvlist(params, uri_scheme[index].protocol,
	    mech) != 0)
		uu_die(gettext("Out of memory.\n"));

	nvlist_free(mech);
	return (r);
}

static int
set_params(nvlist_t *params, char **p)
{
	char *uri;

	if (p == NULL)
		/* sanity check */
		uu_die("set_params");

	while (*p) {
		uri = strip_quotes_and_blanks(*p);
		if (process_uri(params, uri) != 0)
			return (-1);

		++p;
	}

	return (0);
}

static int
setnotify(const char *e, char **p, int global)
{
	char *str = safe_strdup(e);
	char **events;
	int32_t tset;
	int r = -1;
	nvlist_t *nvl, *params;
	char *fmri = NULL;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_alloc(&params, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint32(nvl, SCF_NOTIFY_NAME_VERSION,
	    SCF_NOTIFY_PARAMS_VERSION) != 0)
		uu_die(gettext("Out of memory.\n"));

	events = tokenize(str, ",");

	if ((tset = check_tokens(events)) > 0) {
		/* SMF state transitions parameters */
		size_t sz = max_scf_fmri_len + 1;

		fmri = safe_malloc(sz);
		if (global) {
			(void) strlcpy(fmri, SCF_INSTANCE_GLOBAL, sz);
		} else if (get_selection_str(fmri, sz) != 0) {
			goto out;
		}

		if (nvlist_add_string(nvl, SCF_NOTIFY_NAME_FMRI, fmri) != 0 ||
		    nvlist_add_int32(nvl, SCF_NOTIFY_NAME_TSET, tset) != 0)
			uu_die(gettext("Out of memory.\n"));

		if ((r = set_params(params, p)) == 0) {
			if (nvlist_add_nvlist(nvl, SCF_NOTIFY_PARAMS,
			    params) != 0)
				uu_die(gettext("Out of memory.\n"));

			if (smf_notify_set_params(SCF_SVC_TRANSITION_CLASS,
			    nvl) != SCF_SUCCESS) {
				r = -1;
				uu_warn(gettext(
				    "Failed smf_notify_set_params(3SCF): %s\n"),
				    scf_strerror(scf_error()));
			}
		}
	} else if (tset == FMA_TOKENS) {
		/* FMA event parameters */
		if (global) {
			semerr(gettext("Can't use option '-g' with FMA event "
			    "definitions\n"));
			goto out;
		}

		if ((r = set_params(params, p)) != 0)
			goto out;

		if (nvlist_add_nvlist(nvl, SCF_NOTIFY_PARAMS, params) != 0)
			uu_die(gettext("Out of memory.\n"));

		while (*events) {
			if (smf_notify_set_params(de_tag(*events), nvl) !=
			    SCF_SUCCESS)
				uu_warn(gettext(
				    "Failed smf_notify_set_params(3SCF) for "
				    "event %s: %s\n"), *events,
				    scf_strerror(scf_error()));
			events++;
		}
	} else if (tset == MIXED_TOKENS) {
		semerr(gettext("Can't mix SMF and FMA event definitions\n"));
	} else {
		/* Sanity check */
		uu_die(gettext("Invalid input.\n"));
	}

out:
	nvlist_free(nvl);
	nvlist_free(params);
	free(fmri);
	free(str);

	return (r);
}

int
lscf_setnotify(uu_list_t *args)
{
	int argc;
	char **argv = NULL;
	string_list_t *slp;
	int global;
	char *events;
	char **p;
	int i;
	int ret;

	if ((argc = uu_list_numnodes(args)) < 2)
		goto usage;

	argv = calloc(argc + 1, sizeof (char *));
	if (argv == NULL)
		uu_die(gettext("Out of memory.\n"));

	for (slp = uu_list_first(args), i = 0;
	    slp != NULL;
	    slp = uu_list_next(args, slp), ++i)
		argv[i] = slp->str;

	argv[i] = NULL;

	if (strcmp(argv[0], "-g") == 0) {
		global = 1;
		events = argv[1];
		p = argv + 2;
	} else {
		global = 0;
		events = argv[0];
		p = argv + 1;
	}

	ret = setnotify(events, p, global);

out:
	free(argv);
	return (ret);

usage:
	ret = -2;
	goto out;
}

/*
 * Creates a list of instance name strings associated with a service. If
 * wohandcrafted flag is set, get only instances that have were delivered
 * by bundles, instances that were imported via svccfg.
 */
static uu_list_t *
create_instance_list(scf_service_t *svc, int wohandcrafted)
{
	scf_snapshot_t		*snap = NULL;
	scf_instance_t		*inst;
	scf_decoration_t	*dec;
	scf_iter_t		*inst_iter;
	uu_list_t		*instances;
	char			*instname;
	int			r;

	inst_iter = scf_iter_create(g_hndl);
	inst = scf_instance_create(g_hndl);
	dec = scf_decoration_create(g_hndl);
	if (inst_iter == NULL || inst == NULL || dec == NULL) {
		uu_warn(gettext("Could not create instance, iterator, or "
		    "decoration\n"));
		scfdie();
	}

	if ((instances = uu_list_create(string_pool, NULL, 0)) == NULL)
		return (instances);

	if (scf_iter_service_instances(inst_iter, svc) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_DELETED:
			uu_list_destroy(instances);
			instances = NULL;
			goto out;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("create_instance_list", scf_error());
		}
	}

	instname = safe_malloc(max_scf_name_len + 1);
	while ((r = scf_iter_next_instance(inst_iter, inst)) != 0) {
		if (r == -1) {
			(void) uu_warn(gettext("Unable to iterate through "
			    "instances to create instance list : %s\n"),
			    scf_strerror(scf_error()));

			uu_list_destroy(instances);
			instances = NULL;
			goto out;
		}

		/*
		 * If the instance's decoration is SCF_DEC_LAYER_ADMIN,
		 * then do not add it to the list as it is a hand-crafted
		 * instance that should not be managed.
		 */
		if (wohandcrafted) {
			if (scf_instance_get_decoration(inst,
			    SCF_DECORATION_ADMIN, dec) == SCF_SUCCESS)
				continue;

			switch (scf_error()) {
			case SCF_ERROR_CONSTRAINT_VIOLATED:
				continue;

			case SCF_ERROR_NOT_FOUND:
				break;

			case SCF_ERROR_CONNECTION_BROKEN:
				uu_list_destroy(instances);
				instances = NULL;
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_DELETED:
			default:
				bad_error("create_instance_list",
				    scf_error());
			}
		}

		if (scf_instance_get_name(inst, instname,
		    max_scf_name_len + 1) < 0) {
			switch (scf_error()) {
			case SCF_ERROR_NOT_FOUND :
				continue;

			case SCF_ERROR_CONNECTION_BROKEN:
			case SCF_ERROR_DELETED:
				uu_list_destroy(instances);
				instances = NULL;
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("create_instance_list",
				    scf_error());
			}
		}

		add_string(instances, instname);
	}

out:
	if (snap)
		scf_snapshot_destroy(snap);

	scf_instance_destroy(inst);
	scf_iter_destroy(inst_iter);
	scf_decoration_destroy(dec);
	free(instname);
	return (instances);
}

/*
 * disable an instance but wait for the instance to
 * move out of the running state.
 *
 * Returns 0 : if the instance did not disable
 * Returns non-zero : if the instance disabled.
 *
 */
static int
disable_instance(scf_instance_t *instance)
{
	char	*fmribuf;
	int	enabled = 10000;

	if (inst_is_running(instance)) {
		fmribuf = safe_malloc(max_scf_name_len + 1);
		if (scf_instance_to_fmri(instance, fmribuf,
		    max_scf_name_len + 1) < 0) {
			free(fmribuf);
			return (0);
		}

		/*
		 * If the instance cannot be disabled then return
		 * failure to disable and let the caller decide
		 * if that is of importance.
		 */
		if (smf_disable_instance(fmribuf, 0) != 0) {
			free(fmribuf);
			return (0);
		}

		while (enabled) {
			if (!inst_is_running(instance))
				break;

			(void) poll(NULL, 0, 5);
			enabled = enabled - 5;
		}

		free(fmribuf);
	}

	return (enabled);
}

/*
 * Function to compare two service_manifest structures.
 */
/* ARGSUSED2 */
static int
service_manifest_compare(const void *left, const void *right, void *unused)
{
	service_manifest_t *l = (service_manifest_t *)left;
	service_manifest_t *r = (service_manifest_t *)right;
	int rc;

	rc = strcmp(l->servicename, r->servicename);

	return (rc);
}

/*
 * Look for the provided service in the service to manifest
 * tree.  If the service exists, and a manifest was provided
 * then add the manifest to that service.  If the service
 * does not exist, then add the service and manifest to the
 * list.
 *
 * If the manifest is NULL, return the element if found.  If
 * the service is not found return NULL.
 */
service_manifest_t *
find_add_svc_mfst(const char *svnbuf, const char *mfst)
{
	service_manifest_t	elem;
	service_manifest_t	*fnelem;
	uu_avl_index_t		marker;

	elem.servicename = svnbuf;
	fnelem = uu_avl_find(service_manifest_tree, &elem, NULL, &marker);

	if (mfst) {
		if (fnelem) {
			add_string(fnelem->mfstlist, strdup(mfst));
		} else {
			fnelem = safe_malloc(sizeof (*fnelem));
			fnelem->servicename = safe_strdup(svnbuf);
			if ((fnelem->mfstlist =
			    uu_list_create(string_pool, NULL, 0)) == NULL)
				uu_die(gettext("Could not create property "
				    "list: %s\n"), uu_strerror(uu_error()));

			add_string(fnelem->mfstlist, safe_strdup(mfst));

			uu_avl_insert(service_manifest_tree, fnelem, marker);
		}
	}

	return (fnelem);
}

/*
 * Create the service to manifest avl tree.
 *
 * Walk each of the manifests currently installed in the supported
 * directories, /lib/svc/manifests and /var/svc/manifests.  For
 * each of the manifests, inventory the services and add them to
 * the tree.
 *
 * Code that calls this function should make sure fileystem/minimal is online,
 * /var is available, since this function walks the /var/svc/manifest directory.
 */
static void
create_manifest_tree(void)
{
	manifest_info_t **entry;
	manifest_info_t **manifests;
	uu_list_walk_t	*svcs;
	bundle_t	*b;
	entity_t	*mfsvc;
	char		*dirs[] = {LIBSVC_DIR, VARSVC_DIR, ETCSITEPROFILE_DIR,
				NULL};
	int		c, status;

	if (service_manifest_pool)
		return;

	/*
	 * Create the list pool for the service manifest list
	 */
	service_manifest_pool = uu_avl_pool_create("service_manifest",
	    sizeof (service_manifest_t),
	    offsetof(service_manifest_t, svcmfst_node),
	    service_manifest_compare, UU_DEFAULT);
	if (service_manifest_pool == NULL)
		uu_die(gettext("service_manifest pool creation failed: %s\n"),
		    uu_strerror(uu_error()));

	/*
	 * Create the list
	 */
	service_manifest_tree = uu_avl_create(service_manifest_pool, NULL,
	    UU_DEFAULT);
	if (service_manifest_tree == NULL)
		uu_die(gettext("service_manifest tree creation failed: %s\n"),
		    uu_strerror(uu_error()));

	/*
	 * Walk the manifests adding the service(s) from each manifest.
	 *
	 * If a service already exists add the manifest to the manifest
	 * list for that service.  This covers the case of a service that
	 * is supported by multiple manifest files.
	 */
	for (c = 0; dirs[c]; c++) {
		status = find_manifests(g_hndl, dirs[c], &manifests, CHECKEXT);
		if (status < 0) {
			uu_warn(gettext("file tree walk of %s encountered "
			    "error %s\n"), dirs[c], strerror(errno));

			uu_avl_destroy(service_manifest_tree);
			service_manifest_tree = NULL;
			return;
		}

		/*
		 * If a manifest that was in the list is not found
		 * then skip and go to the next manifest file.
		 */
		if (manifests != NULL) {
			for (entry = manifests; *entry != NULL; entry++) {
				b = internal_bundle_new();
				if (lxml_get_bundle_file(b, (*entry)->mi_path,
				    SVCCFG_OP_INVENTORY) != 0) {
					internal_bundle_free(b);
					continue;
				}

				svcs = uu_list_walk_start(b->sc_bundle_services,
				    0);
				if (svcs == NULL) {
					internal_bundle_free(b);
					continue;
				}

				while ((mfsvc = uu_list_walk_next(svcs)) !=
				    NULL) {
					/* Add manifest to service */
					(void) find_add_svc_mfst(mfsvc->sc_name,
					    (*entry)->mi_path);
				}

				uu_list_walk_end(svcs);
				internal_bundle_free(b);
			}

			free_manifest_array(manifests);
		}
	}
}

/*
 * Check the manifest history file to see
 * if the service was ever installed from
 * one of the supported directories.
 *
 * Return Values :
 * 	-1 - if there's error reading manifest history file
 *	 1 - if the service is not found
 *	 0 - if the service is found
 */
static int
check_mfst_history(const char *svcname)
{
	struct stat	st;
	caddr_t		mfsthist_start;
	char		*svnbuf;
	int		fd;
	int		r = 1;

	fd = open(MFSTHISTFILE, O_RDONLY);
	if (fd == -1) {
		uu_warn(gettext("Unable to open the history file\n"));
		return (-1);
	}

	if (fstat(fd, &st) == -1) {
		uu_warn(gettext("Unable to stat the history file\n"));
		return (-1);
	}

	mfsthist_start = mmap(0, st.st_size, PROT_READ,
	    MAP_PRIVATE, fd, 0);

	(void) close(fd);
	if (mfsthist_start == MAP_FAILED ||
	    *(mfsthist_start + st.st_size) != '\0') {
		(void) munmap(mfsthist_start, st.st_size);
		return (-1);
	}

	/*
	 * The manifest history file is a space delimited list
	 * of service and instance to manifest linkage.  Adding
	 * a space to the end of the service name so to get only
	 * the service that is being searched for.
	 */
	svnbuf = uu_msprintf("%s ", svcname);
	if (svnbuf == NULL)
		uu_die(gettext("Out of memory"));

	if (strstr(mfsthist_start, svnbuf) != NULL)
		r = 0;

	(void) munmap(mfsthist_start, st.st_size);
	uu_free(svnbuf);
	return (r);
}

/*
 * Take down each of the instances in the service
 * and remove them, then delete the service.
 */
static void
teardown_service(scf_service_t *svc, const char *svnbuf, int remove)
{
	scf_instance_t	 *instance;
	scf_iter_t	 *iter;
	int		  r;

	safe_printf(gettext("Delete service %s as there are no "
	    "supporting manifests\n"), svnbuf);

	instance = scf_instance_create(g_hndl);
	iter = scf_iter_create(g_hndl);
	if (iter == NULL || instance == NULL) {
		uu_warn(gettext("Unable to create supporting entities to "
		    "teardown the service\n"));
		uu_warn(gettext("scf error is : %s\n"),
		    scf_strerror(scf_error()));
		scfdie();
	}

	if (scf_iter_service_instances(iter, svc) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_DELETED:
			goto out;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_iter_service_instances",
			    scf_error());
		}
	}

	while ((r = scf_iter_next_instance(iter, instance)) != 0) {
		if (r == -1) {
			uu_warn(gettext("Error - %s\n"),
			    scf_strerror(scf_error()));
			goto out;
		}

		(void) disable_instance(instance);
	}

	/*
	 * Delete the service... forcing the deletion in case
	 * any of the instances did not disable.
	 */
	(void) lscf_service_delete(svc, 1, remove);
out:
	scf_instance_destroy(instance);
	scf_iter_destroy(iter);
}

/*
 * For the given entity, check to see if it has the fmri as dependent.
 *
 * Return 0 if entity doesn't have a dependent with the target fmri.
 *
 * Return 1 if entity has a dependent with the target fmri.
 */
static int
entity_has_dependent(entity_t *entity, const char *fmri)
{
	pgroup_t *dpt;
	int ret = 0;

	/*
	 * For each dependent pgroup, compare the fmris. The input fmri
	 * argument is a service fmri but the dependent's fmri can be a service
	 * an instance fmri. Thus, a match up to the length of the target
	 * service fmri is sufficient since that implies the dependent
	 * definition point to the target service or one of its instances.
	 */
	for (dpt = uu_list_first(entity->sc_dependents); dpt != NULL;
	    dpt = uu_list_next(entity->sc_dependents, dpt)) {
		if (strncmp(dpt->sc_pgroup_fmri, fmri, strlen(fmri)) == 0) {
			ret = 1;
			break;
		}
	}

	return (ret);
}

/*
 * remove bundle support if necessary.
 * returns 0 on success, 1 on error
 */
static int
remove_bundle_support(scf_instance_t *inst, const char *inst_name,
    const char *fmri, const char *bundle, uu_list_t *lst)
{
	uu_list_walk_t	*insts;
	entity_t	*e;
	int		r = 1;

	if ((insts = uu_list_walk_start(lst, 0)) == NULL) {
		uu_warn(gettext("Unable to walk instances in bundle %s for "
		    "%s: %s\n"), bundle, fmri, uu_strerror(uu_error()));
		return (r);
	}

	while ((e = uu_list_walk_next(insts)) != NULL) {
		if (strcmp(e->sc_name, inst_name) == 0)
			/* this instance is still supported by this bundle */
			goto out;
	}

	/*
	 * If we get here, it means this instance is no longer supported by
	 * this bundle.
	 */
	(void) safe_printf(gettext("Removing bundle support from %s for "
	    "%s:%s\n"), bundle, fmri, inst_name);
	if (scf_instance_bundle_remove(inst, (char *)bundle) != 0) {
		safe_printf(gettext("Failed to remove bundle support from %s "
		    "for %s:%s  %s\n"),
		    bundle, fmri, inst_name, scf_strerror(scf_error()));
		goto out;
	}
	r = 0;
out:
	uu_list_walk_end(insts);
	return (r);
}

static void
log_cleanup_error(const char *fmri)
{
	uu_warn(gettext("Exiting cleanup for %s: %s"),
	    fmri, scf_strerror(scf_error()));
}

static void
cleanup_instance_bundle_support(scf_service_t *svc, const char *bundle,
    const char *fmri, uu_list_t *lst)
{
	scf_instance_t *inst = scf_instance_create(g_hndl);
	scf_iter_t *i_iter = scf_iter_create(g_hndl);
	scf_iter_t *d_iter = scf_iter_create(g_hndl);
	scf_decoration_t *dec = scf_decoration_create(g_hndl);
	scf_decoration_layer_t layer;
	int sz = max_scf_name_len + 1;
	char *b = safe_malloc(MAXPATHLEN);
	char *i_name = safe_malloc(sz);
	int r1, r2;

	if (inst == NULL || i_iter == NULL || d_iter == NULL || dec == NULL) {
		uu_warn(gettext("Could not create instance, iterator, or "
		    "decoration: processing bundle %s for %s\n"), bundle, fmri);
		goto out;
	}

	if (scf_iter_service_instances(i_iter, svc) != 0) {
		(void) uu_warn(gettext("Unable to create iterator for "
		    "instances of %s : %s\n"), fmri, scf_strerror(scf_error()));
		goto out;
	}

	while ((r1 = scf_iter_next_instance(i_iter, inst)) == 1) {
		if (scf_instance_get_name(inst, i_name, sz) < 0) {
			log_cleanup_error(fmri);
			scfdie();
		}

		scf_iter_reset(d_iter);
		if (scf_iter_instance_decorations(d_iter, inst, 0) != 0) {
			(void) uu_warn(gettext("Unable to create iterator for "
			    "decorations of %s:%s : %s\n"), fmri, i_name,
			    scf_strerror(scf_error()));
			goto out;
		}

		while ((r2 = scf_iter_next_decoration(d_iter, dec)) == 1) {
			if (scf_decoration_get_layer(dec, &layer) != 0) {
				(void) uu_warn(gettext("Unable to get "
				    "decoration layer for %s:%s : %s\n"), fmri,
				    i_name, scf_strerror(scf_error()));
				goto out;
			}

			if (!IS_LAYER_BACKED_BY_FILE(layer))
				continue;

			if (scf_decoration_get_bundle(dec, b, MAXPATHLEN) < 0) {
				(void) uu_warn(gettext("Unable to get bundle "
				    "for %s:%s : %s\n"), fmri, i_name,
				    scf_strerror(scf_error()));
				goto out;
			}

			if (strcmp(bundle, b) != 0)
				continue;

			if (remove_bundle_support(inst, i_name, fmri, b,
			    lst) != 0) {
					goto out;
			}
		}
		if (r2 == -1)
			(void) uu_warn(gettext("Unable to iterate through "
			    "decorations of %s:%s : %s\n"), fmri, i_name,
			    scf_strerror(scf_error()));
	}
	if (r1 == -1)
		(void) uu_warn(gettext("Unable to iterate through "
		    "instances of %s : %s\n"), fmri,
		    scf_strerror(scf_error()));

out:
	free(b);
	free(i_name);
	scf_decoration_destroy(dec);
	scf_iter_destroy(i_iter);
	scf_iter_destroy(d_iter);
	scf_instance_destroy(inst);
}

/*
 * Get the list of instances supported by the manifest
 * file.
 *
 * Return 0 if there are no instances.
 *
 * Return -1 if there are errors attempting to collect instances.
 *
 * Return the count of instances found if there are no errors.
 *
 */
static int
check_instance_support(scf_service_t *svc, char *mfstfile,
    const char *svcname, uu_list_t *instances)
{
	uu_list_walk_t	*svcs, *insts;
	uu_list_t	*ilist;
	bundle_t	*b;
	entity_t	*mfsvc, *mfinst;
	const char	*svcn;
	int		rminstcnt = 0;

	b = internal_bundle_new();

	if (lxml_get_bundle_file(b, mfstfile, SVCCFG_OP_INVENTORY) != 0) {
		/*
		 * Unable to process the manifest file for
		 * instance support, so just return as
		 * don't want to remove instances that could
		 * not be accounted for that might exist here.
		 */
		internal_bundle_free(b);
		return (0);
	}

	svcs = uu_list_walk_start(b->sc_bundle_services, 0);
	if (svcs == NULL) {
		internal_bundle_free(b);
		return (0);
	}

	svcn = svcname + (sizeof (SCF_FMRI_SVC_PREFIX) - 1) +
	    (sizeof (SCF_FMRI_SERVICE_PREFIX) - 1);

	while ((mfsvc = uu_list_walk_next(svcs)) != NULL) {
		if (strcmp(mfsvc->sc_name, svcn) == 0)
			break;
	}
	uu_list_walk_end(svcs);

	if (mfsvc == NULL) {
		/*
		 * Bundle doesn't deliver service, check to see if service is
		 * declared as a dependent and bail out on the first dependent
		 * definition.
		 */
		svcs = uu_list_walk_start(b->sc_bundle_services, 0);
		while ((mfsvc = uu_list_walk_next(svcs)) != NULL) {
			/* Check service's property groups */
			if (entity_has_dependent(mfsvc, svcname)) {
				uu_list_walk_end(svcs);
				internal_bundle_free(b);
				return (0);
			}

			ilist = mfsvc->sc_u.sc_service.sc_service_instances;
			if ((insts = uu_list_walk_start(ilist, 0)) == NULL)
				continue;

			while ((mfinst = uu_list_walk_next(insts)) != NULL) {
				if (entity_has_dependent(mfinst, svcname)) {
					uu_list_walk_end(insts);
					internal_bundle_free(b);
					return (0);
				}
			}
			uu_list_walk_end(insts);
		}

		uu_list_walk_end(svcs);
		internal_bundle_free(b);
		return (-1);
	}

	ilist = mfsvc->sc_u.sc_service.sc_service_instances;
	if ((insts = uu_list_walk_start(ilist, 0)) == NULL) {
		internal_bundle_free(b);
		return (0);
	}

	while ((mfinst = uu_list_walk_next(insts)) != NULL) {
		/*
		 * Remove the instance from the instances list.
		 * The unaccounted for instances will be removed
		 * from the service once all manifests are
		 * processed.
		 */
		(void) remove_string(instances,
		    mfinst->sc_name);
		rminstcnt++;
	}

	uu_list_walk_end(insts);
	cleanup_instance_bundle_support(svc, mfstfile, svcname, ilist);

	internal_bundle_free(b);

	return (rminstcnt);
}

/*
 * For the given service, set its SCF_PG_MANIFESTFILES/SUPPORT property to
 * 'false' to indicate there's no manifest file(s) found for the service.
 */
static void
svc_add_no_support(scf_service_t *svc)
{
	char	*pname;

	/* Add no support */
	cur_svc = svc;
	if (addpg(SCF_PG_MANIFESTFILES, SCF_GROUP_FRAMEWORK))
		return;

	pname = uu_msprintf("%s/%s", SCF_PG_MANIFESTFILES, SUPPORTPROP);
	if (pname == NULL)
		uu_die(gettext("Out of memory.\n"));

	(void) lscf_addpropvalue(pname, "boolean:", "0");

	uu_free(pname);
	cur_svc = NULL;
}

/*
 * This function handles all upgrade scenarios for a service that doesn't have
 * SCF_PG_MANIFESTFILES pg. The function creates and populates
 * SCF_PG_MANIFESTFILES pg for the given service to keep track of service to
 * manifest(s) mapping. Manifests under supported directories are inventoried
 * and a property is added for each file that delivers configuration to the
 * service.  A service that has no corresponding manifest files (deleted) are
 * removed from repository.
 *
 * Unsupported services:
 *
 * A service is considered unsupported if there is no corresponding manifest
 * in the supported directories for that service and the service isn't in the
 * history file list.  The history file, MFSTHISTFILE, contains a list of all
 * services and instances that were delivered by Solaris before the introduction
 * of the SCF_PG_MANIFESTFILES property group.  The history file also contains
 * the path to the manifest file that defined the service or instance.
 *
 * Another type of unsupported services is 'handcrafted' services,
 * programmatically created services or services created by dependent entries
 * in other manifests. A handcrafted service is one where it and all its
 * instances have administrative decorations (SCF_DECORATION_ADMIN).
 *
 * This function sets a flag for unsupported services by setting services'
 * SCF_PG_MANIFESTFILES/support property to false.
 */
static void
upgrade_svc_mfst_connection(scf_service_t *svc, const char *svcname)
{
	scf_decoration_layer_t	layer;
	service_manifest_t	*elem;
	uu_list_walk_t		*mfwalk;
	string_list_t		*mfile;
	uu_list_t		*instances;
	const char		*sname;
	char			*pname;
	int			pgadded;
	int			r;

	/*
	 * Since there's no guarantee manifests under /var are available during
	 * early import, don't perform any upgrade during early import.
	 */
	if (IGNORE_VAR)
		return;

	if (service_manifest_tree == NULL) {
		create_manifest_tree();
	}

	/*
	 * Find service's supporting manifest(s) after
	 * stripping off the svc:/ prefix that is part
	 * of the fmri that is not used in the service
	 * manifest bundle list.
	 */
	sname = svcname + strlen(SCF_FMRI_SVC_PREFIX) +
	    strlen(SCF_FMRI_SERVICE_PREFIX);
	elem = find_add_svc_mfst(sname, NULL);
	if (elem == NULL) {

		/*
		 * A handcrafted service should get unsupported flag.
		 */
		instances = create_instance_list(svc, 1);
		if (instances == NULL) {
			uu_warn(gettext("Unable to create instance list %s\n"),
			    svcname);
			return;
		}

		if (uu_list_numnodes(instances) == 0) {
			svc_add_no_support(svc);
			return;
		}

		/*
		 * If the service is in the history file, and its supporting
		 * manifests are not found, we can safely delete the service
		 * because its manifests are removed from the system.
		 *
		 * Services not found in the history file are not delivered by
		 * Solaris and/or delivered outside supported directories, set
		 * unsupported flag for these services.
		 */
		r = check_mfst_history(svcname);
		if (r == -1)
			return;

		if (r) {
			/* Set unsupported flag for service  */
			svc_add_no_support(svc);
		} else {
			/* Delete the service */
			teardown_service(svc, svcname, 1);
		}

		return;
	}

	/*
	 * Walk through the list of manifests and add them
	 * to the service.
	 *
	 * Create a manifestfiles pg and add the property.
	 */
	mfwalk = uu_list_walk_start(elem->mfstlist, 0);
	if (mfwalk == NULL)
		return;

	cur_svc = svc;

	pgadded = 0;
	layer = SCF_DECORATION_MANIFEST;
	while ((mfile = uu_list_walk_next(mfwalk)) != NULL) {
		if (_scf_set_tx_decoration(g_hndl, mfile->str, layer) != 0)
			return;

		if (pgadded == 0) {
			r = addpg(SCF_PG_MANIFESTFILES, SCF_GROUP_FRAMEWORK);
			if (r != 0) {
				cur_svc = NULL;
				return;
			}
			pgadded = 1;
		}
		pname = uu_msprintf("%s/%s", SCF_PG_MANIFESTFILES,
		    mhash_filename_to_propname(mfile->str, 0));
		if (pname == NULL)
			uu_die(gettext("Out of memory.\n"));

		(void) lscf_addpropvalue(pname, "astring:", mfile->str);
		uu_free(pname);
	}
	uu_list_walk_end(mfwalk);

	if (_scf_unset_tx_decoration(g_hndl) != 0)
		bad_error("_scf_unset_tx_decoration", scf_error());

	cur_svc = NULL;
}

/*
 * checks if the service is handcrafted. We assume a service is handcrafted
 * if it does not have any file backed decoration.
 *
 * returns 1 if there are no file backed decorations
 *         0 if it finds file backed decorations
 *         -1 on errors. Caller is supposed to handle the errors
 */
static int
is_handcrafted(scf_service_t *svc, const char *fmri)
{
	scf_iter_t *iter = scf_iter_create(g_hndl);
	scf_decoration_t *dec = scf_decoration_create(g_hndl);
	int r;
	int ret = -1;

	if (iter == NULL || dec == NULL) {
		uu_warn(gettext("Failed creating libscf entity for %s\n"),
		    fmri);
		goto out;
	}

	if (scf_iter_service_decorations(iter, svc, 0) != 0) {
		uu_warn(gettext("Failed to get service iterator for %s:\n"),
		    fmri);
		goto out;
	}

	while ((r = scf_iter_next_decoration(iter, dec)) == 1) {
		scf_decoration_layer_t layer;

		if (scf_decoration_get_layer(dec, &layer) != 0) {
			(void) uu_warn(gettext("Unable to get decoration for "
			    "%s\n"), fmri);
			scfdie();
		}

		if (IS_LAYER_BACKED_BY_FILE(layer)) {
			ret = 0;
			goto out;
		}
	}
	if (r == -1) {
		(void) uu_warn(gettext("Unable to iterate through "
		    "decorations of %s\n"), fmri);
		goto out;
	}

	ret = 1;	/* didn't find any file backed decoration */
out:
	scf_decoration_destroy(dec);
	scf_iter_destroy(iter);

	return (ret);
}

/*
 * Take a service and process the manifest file entires to see if
 * there is continued support for the service and instances.  If
 * not cleanup as appropriate.
 *
 * If a service does not have a manifest files entry flag it for
 * upgrade and return.
 *
 * For each manifestfiles property check if the manifest file is
 * under the supported /lib/svc/manifest or /var/svc/manifest path
 * and if not then return immediately as this service is not supported
 * by the cleanup mechanism and should be ignored.
 *
 * For each manifest file that is supported, check to see if the
 * file exists.  If not then remove the manifest file property
 * from the service and the smf/manifest hash table.  If the manifest
 * file exists then verify that it supports the instances that are
 * part of the service.
 *
 * Once all manifest files have been accounted for remove any instances
 * that are no longer supported in the service.
 *
 * Return values :
 * 0 - Successfully processed the service
 * non-zero - failed to process the service
 *
 * On most errors, will just return to wait and get the next service,
 * unless in case of unable to create the needed structures which is
 * most likely a fatal error that is not going to be recoverable.
 */
int
lscf_service_cleanup(void *act, scf_walkinfo_t *wip)
{
	struct mpg_mfile	 *mpntov = NULL;
	struct mpg_mfile	**mpvarry = NULL;
	scf_service_t		 *svc;
	scf_propertygroup_t	 *mpg;
	scf_property_t		 *mp;
	scf_value_t		 *mv;
	scf_iter_t		 *mi;
	scf_instance_t		 *instance;
	uu_list_walk_t		 *insts;
	uu_list_t		 *instances = NULL;
	boolean_t		  activity = (boolean_t)act;
	char			 *mpnbuf = NULL;
	char			 *mpvbuf = NULL;
	char			 *pgpropbuf;
	int			  mfstcnt, rminstct, instct, mfstmax;
	int			  index;
	int			  r = 0;
	int			  br_done;
	int			  has_support;
	boolean_t		  svc_supported = B_TRUE;

	assert(g_hndl != NULL);
	assert(wip->svc != NULL);
	assert(wip->fmri != NULL);

	svc = wip->svc;

	mpg = scf_pg_create(g_hndl);
	if (mpg == NULL)
		scfdie();

	mp = scf_property_create(g_hndl);
	if (mp == NULL)
		scfdie();

	mi = scf_iter_create(g_hndl);
	if (mi == NULL)
		scfdie();

	mv = scf_value_create(g_hndl);
	if (mv == NULL)
		scfdie();

	instance = scf_instance_create(g_hndl);
	if (instance == NULL)
		scfdie();

	/*
	 * Get the manifestfiles property group to be parsed for
	 * files existence.
	 */
	if (scf_service_get_pg(svc, SCF_PG_MANIFESTFILES, mpg) != SCF_SUCCESS) {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			uu_warn(gettext("No manifestfiles PG for %s\n"),
			    wip->fmri);
			upgrade_svc_mfst_connection(svc, wip->fmri);
			break;
		case SCF_ERROR_DELETED:
		case SCF_ERROR_CONNECTION_BROKEN:
			log_cleanup_error(wip->fmri);
			break;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_iter_pg_properties",
			    scf_error());
		}

		goto out;
	}

	if (scf_pg_get_property(mpg, "support", mp) == 0) {
		uint8_t	support;

		if (scf_property_get_value(mp, mv) != 0 ||
		    scf_value_get_boolean(mv, &support) != 0) {
			uu_warn(gettext("Unable to get the manifest "
			    "support value: %s\n"),
			    scf_strerror(scf_error()));
			switch (scf_error()) {
			case SCF_ERROR_CONSTRAINT_VIOLATED:
				break;
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				r = scferror2errno(scf_error());
				log_cleanup_error(wip->fmri);
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_property_get_value",
				    scf_error());
			}
		}
		svc_supported = support;
		has_support = 1;
	} else {
		switch (scf_error()) {
		case SCF_ERROR_NOT_FOUND:
			has_support = 0;
			break;
		case SCF_ERROR_DELETED:
		case SCF_ERROR_CONNECTION_BROKEN:
			r = scferror2errno(scf_error());
			log_cleanup_error(wip->fmri);
			goto out;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_pg_get_property",
			    scf_error());
		}
	}

	/*
	 * Iterate through each of the manifestfiles properties
	 * to determine what manifestfiles are available.
	 *
	 * If a manifest file is supported then increment the
	 * count and therefore the service is safe.
	 */
	if (scf_iter_pg_properties(mi, mpg) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_DELETED:
		case SCF_ERROR_CONNECTION_BROKEN:
			log_cleanup_error(wip->fmri);
			uu_warn(gettext("exiting cleanup for %s: %s\n"),
			    wip->fmri, scf_strerror(scf_error()));
			goto out;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_NOT_SET:
		default:
			bad_error("scf_iter_pg_properties",
			    scf_error());
		}
	}

	mfstcnt = 0;
	mfstmax = MFSTFILE_MAX;
	mpvarry = safe_malloc(sizeof (struct mpg_file *) * MFSTFILE_MAX);
	while ((r = scf_iter_next_property(mi, mp)) != 0) {
		if (r == -1)
			bad_error(gettext("Unable to iterate through "
			    "manifestfiles properties : %s"),
			    scf_error());

		mpntov = safe_malloc(sizeof (struct mpg_mfile));
		mpnbuf = safe_malloc(max_scf_name_len + 1);
		mpvbuf = safe_malloc(max_scf_value_len + 1);
		mpntov->mpg = mpnbuf;
		mpntov->mfile = mpvbuf;
		mpntov->access = 1;
		mpntov->nosvcsupport = 0;
		if (scf_property_get_name(mp, mpnbuf,
		    max_scf_name_len + 1) < 0) {
			uu_warn(gettext("Unable to get manifest file "
			    "property : %s\n"),
			    scf_strerror(scf_error()));

			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				r = scferror2errno(scf_error());
				log_cleanup_error(wip->fmri);
				uu_warn(gettext("exiting cleanup for %s: %s"),
				    wip->fmri, scf_strerror(scf_error()));
				goto out_free;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_iter_pg_properties",
				    scf_error());
			}
		}

		/*
		 * The support property is a boolean value that indicates
		 * if the service is supported for manifest file deletion.
		 * This currently means that "the service was
		 * administratively created," with a corresponding
		 * value of B_FALSE.
		 *
		 * It is valid for this property to be set AND for the
		 * service to have valid, supported manifest files.
		 */
		if (strcmp(mpnbuf, SUPPORTPROP) == 0) {
			free(mpntov);
			free(mpnbuf);
			free(mpvbuf);
			mpntov = NULL;
			mpnbuf = mpvbuf = NULL;
			continue;
		}

		/*
		 * Get the value of the manifest file for this entry
		 * for access verification and instance support
		 * verification if it still exists.
		 *
		 * During Early Manifest Import if the manifest is in
		 * /var/svc then it may not yet be available for checking
		 * so we must determine if /var/svc is available.  If not
		 * then defer until Late Manifest Import to cleanup.
		 */
		if (scf_property_get_value(mp, mv) != 0) {
			uu_warn(gettext("Unable to get the manifest file "
			    "value: %s\n"),
			    scf_strerror(scf_error()));

			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				r = scferror2errno(scf_error());
				log_cleanup_error(wip->fmri);
				goto out_free;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_property_get_value",
				    scf_error());
			}
		}

		if (scf_value_get_astring(mv, mpvbuf,
		    max_scf_value_len + 1) < 0) {
			uu_warn(gettext("Unable to get the manifest "
			    "file : %s\n"),
			    scf_strerror(scf_error()));

			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONNECTION_BROKEN:
				r = scferror2errno(scf_error());
				log_cleanup_error(wip->fmri);
				goto out_free;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_value_get_astring",
				    scf_error());
			}
		}

		/*
		 * Anything with a manifest outside of the supported
		 * directories, immediately bail out because that makes
		 * this service non-supported.  We don't even want
		 * to do instance processing in this case because the
		 * instances could be part of the non-supported manifest.
		 */
		if (strncmp(mpvbuf, LIBSVC_DIR, strlen(LIBSVC_DIR)) != 0 &&
		    strncmp(mpvbuf, ETCSVCPROFILE_DIR,
		    strlen(ETCSVCPROFILE_DIR)) != 0) {
			if (strncmp(mpvbuf, VARSVC_DIR,
			    strlen(VARSVC_DIR)) != 0) {
				int ret;

				/*
				 * The manifestfiles entry points to a bundle
				 * in a non-standard location. Such bundles are
				 * toxic to the clean up process. Log, delete
				 * the entry, check for handcrafted service
				 * and continue.
				 */
				ret = is_handcrafted(svc, wip->fmri);
				if (ret == -1) {
					switch (scf_error()) {
					case SCF_ERROR_DELETED:
					case SCF_ERROR_NO_RESOURCES:
						log_cleanup_error(wip->fmri);
						break;
					case SCF_ERROR_NOT_SET:
					case SCF_ERROR_NOT_BOUND:
					default:
						bad_error("is_handcrafted",
						    scf_error());
					}
					log_cleanup_error(wip->fmri);
					goto out_free;
				} else if (ret == 1) {
					if (!has_support) {
						svc_add_no_support(svc);
						has_support = 1;
					}
					svc_supported = B_FALSE;
					continue;
				} else {
					assert(ret == 0);
					safe_printf("Deleting non-standard "
					    "location bundle %s in %s\n",
					    mpvbuf, wip->fmri);
					cur_svc = svc;
					pgpropbuf = uu_msprintf("%s/%s",
					    SCF_PG_MANIFESTFILES, mpnbuf);
					if (pgpropbuf == NULL)
						uu_die(gettext(
						    "Out of memory.\n"));

					lscf_delprop(pgpropbuf, 1);
					cur_svc = NULL;
				}
				uu_free(pgpropbuf);
				free(mpntov);
				free(mpnbuf);
				free(mpvbuf);
				mpntov = NULL;
				mpnbuf = mpvbuf = NULL;
				continue;
			}
			/*
			 * Manifest is not in /lib/svc, so we need to
			 * consider the /var/svc case.
			 */
			if (IGNORE_VAR) {
				/*
				 * /var is not mounted yet, we defer clean up
				 * until /var can be accessed.
				 */
				safe_printf("Deferring cleanup for %s "
				    "until /var can be accessed: "
				    "caused by bundle %s\n", wip->fmri, mpvbuf);
				goto out_free;
			}
		}

		mpvarry[mfstcnt] = mpntov;
		mfstcnt++;

		/*
		 * Check for the need to reallocate array
		 */
		if (mfstcnt >= (mfstmax - 1)) {
			struct mpg_mfile **newmpvarry;

			mfstmax = mfstmax * 2;
			newmpvarry = realloc(mpvarry,
			    sizeof (struct mpg_mfile *) * mfstmax);

			if (newmpvarry == NULL)
				uu_die(gettext("Out of memory.\n"));

			mpvarry = newmpvarry;
		}

		mpvarry[mfstcnt] = NULL;
	}

	/*
	 * If the service is explicitly marked as unsupported AND it
	 * previously had no manifest files in supported locations,
	 * there's nothing for us to do here.
	 */
	if (!svc_supported && mfstcnt == 0) {
		safe_printf(gettext(
		    "Exiting cleanup for %s: unsupported manifestfiles\n"),
		    wip->fmri);
		goto out_free;
	}

	for (index = 0; mpvarry[index]; index++) {
		mpntov = mpvarry[index];

		/*
		 * Check to see if the manifestfile is accessible, if so hand
		 * this service and manifestfile off to be processed for
		 * instance support.
		 */
		mpvbuf = mpntov->mfile;
		if (access(mpvbuf, F_OK) != 0) {
			mpntov->access = 0;
			activity = B_TRUE;
			mfstcnt--;
		}
	}

	/*
	 * If mfstcnt is 0, none of the manifests that supported the service
	 * existed, so tear down the service.  All previously-file-backed
	 * instances will be removed, and the service itself and any
	 * administratively-created instances will be removed only if
	 * the service itself was not originally created administratively.
	 */
	if (mfstcnt == 0) {
		teardown_service(svc, wip->fmri, 1);

		goto out_free;
	}

	if (activity) {
		int	nosvcsupport = 0;

		/*
		 * If the list of service instances is NULL then
		 * create the list.
		 */
		instances = create_instance_list(svc, 1);
		if (instances == NULL) {
			uu_warn(gettext("Unable to create instance list %s\n"),
			    wip->fmri);
			goto out_free;
		}

		rminstct = uu_list_numnodes(instances);
		instct = rminstct;

		for (index = 0; mpvarry[index]; index++) {
			mpntov = mpvarry[index];
			if (mpntov->access == 0)
				continue;

			mpnbuf = mpntov->mpg;
			mpvbuf = mpntov->mfile;
			r = check_instance_support(svc, mpvbuf, wip->fmri,
			    instances);
			if (r == -1) {
				/* Mark nosvcsupport and increment total */
				mpntov->nosvcsupport = 1;
				nosvcsupport++;
			} else {
				rminstct -= r;
			}
		}

		if (instct > 0 && instct == rminstct &&
		    nosvcsupport == mfstcnt) {
			teardown_service(svc, wip->fmri, 1);

			goto out_free;
		}
	}

	/*
	 * At this point, we know that the service is still
	 * file-backed, and we have a list of all instances for which
	 * file backing has been removed.
	 */

	if (instances != NULL && uu_list_numnodes(instances) > 0) {
		string_list_t *sp;

		insts = uu_list_walk_start(instances, 0);
		while ((sp = uu_list_walk_next(insts)) != NULL) {
			safe_printf(gettext("Delete instance %s from "
			    "service %s\n"), sp->str, wip->fmri);
			if (scf_service_get_instance(svc, sp->str,
			    instance) != SCF_SUCCESS) {
				(void) uu_warn("scf_error - %s\n",
				    scf_strerror(scf_error()));

				continue;
			}

			(void) disable_instance(instance);
			(void) lscf_instance_delete(instance, 1, 1);
		}
		uu_list_walk_end(insts);
	}

	/*
	 * At this point we've removed all instances and services for
	 * which file backing has been removed.
	 *
	 * Now clear any property groups and properties that were associated
	 * with that bundle but may have been part of services and instances
	 * that are still supported by other files.
	 *
	 * For each bundle that was removed, send a bundle remove request.
	 */
	br_done = 0;
	for (index = 0; mpvarry[index]; index++) {
		mpntov = mpvarry[index];

		/* skip if manifest still supports service and is accessible */
		if (mpntov->nosvcsupport == 0 && mpntov->access != 0)
			continue;

		safe_printf(gettext("%s: removing old configuration "
		    "delivered from bundle: %s\n"), wip->fmri, mpntov->mfile);

		br_done++;
		r = scf_service_bundle_remove(svc, mpntov->mfile);
		if (r != SCF_SUCCESS) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
				break;

			case SCF_ERROR_CONNECTION_BROKEN:
				r = scferror2errno(scf_error());
				log_cleanup_error(wip->fmri);
				goto out_free;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			case SCF_ERROR_NOT_SET:
			default:
				bad_error("scf_service_bundle_remove",
				    scf_error());
			}
		}
		/*
		 * Now that we removed support for the removed manifest, we can
		 * safely remove its corresponding manifestfiles entry
		 */
		cur_svc = svc;
		pgpropbuf = uu_msprintf("%s/%s", SCF_PG_MANIFESTFILES,
		    mpntov->mpg);
		if (pgpropbuf == NULL)
			uu_die(gettext("Out of memory.\n"));

		safe_printf(gettext("Removing %s entry for %s\n"), pgpropbuf,
		    wip->fmri);
		lscf_delprop(pgpropbuf, 1);
		cur_svc = NULL;

		uu_free(pgpropbuf);
	}

	if (br_done > 0) {
		/*
		 * Now we need to refresh the service and instances.
		 */
		r = refresh_entity(1, svc, wip->fmri, instance, mi, NULL);
		switch (r) {
		case 0:
			break;

		case ECONNABORTED:
			warn(gettext("Could not refresh %s "
			    "(repository connection broken).\n"), wip->fmri);
			break;

		case ECANCELED:
			warn(emsg_deleted);
			break;

		case EPERM:
			warn(gettext("Could not refresh %s "
			    "(permission denied).\n"), wip->fmri);
			break;

		case ENOSPC:
			warn(gettext("Could not refresh %s "
			    "(repository server out of resources).\n"),
			    wip->fmri);
			break;

		case EACCES:
		default:
			bad_error("refresh_entity", scf_error());
		}
	}

out_free:
	if (mpvarry) {
		struct mpg_mfile *fmpntov;

		for (index = 0; mpvarry[index]; index++) {
			fmpntov  = mpvarry[index];
			if (fmpntov->mpg == mpnbuf)
				mpnbuf = NULL;
			free(fmpntov->mpg);

			if (fmpntov->mfile == mpvbuf)
				mpvbuf = NULL;
			free(fmpntov->mfile);

			if (fmpntov == mpntov)
				mpntov = NULL;
			free(fmpntov);
		}
		if (mpnbuf)
			free(mpnbuf);
		if (mpvbuf)
			free(mpvbuf);
		if (mpntov)
			free(mpntov);

		free(mpvarry);
	}
out:
	scf_pg_destroy(mpg);
	scf_property_destroy(mp);
	scf_iter_destroy(mi);
	scf_value_destroy(mv);
	scf_instance_destroy(instance);

	return (0);
}

/*
 * Take the service and search for the manifestfiles property
 * in each of the property groups.  If the manifest file
 * associated with the property does not exist then remove
 * the property group.
 */
int
lscf_hash_cleanup()
{
	scf_service_t		*svc;
	scf_scope_t		*scope;
	scf_propertygroup_t	*pg;
	scf_property_t		*prop;
	scf_value_t		*val;
	scf_iter_t		*iter;
	char			*pgname = NULL;
	char			*mfile = NULL;
	int			r;

	svc = scf_service_create(g_hndl);
	scope = scf_scope_create(g_hndl);
	pg = scf_pg_create(g_hndl);
	prop = scf_property_create(g_hndl);
	val = scf_value_create(g_hndl);
	iter = scf_iter_create(g_hndl);
	if (pg == NULL || prop == NULL || val == NULL || iter == NULL ||
	    svc == NULL || scope == NULL) {
		uu_warn(gettext("Unable to create a property group, or "
		    "property\n"));
		uu_warn("%s\n", pg == NULL ? "pg is NULL" :
		    "pg is not NULL");
		uu_warn("%s\n", prop == NULL ? "prop is NULL" :
		    "prop is not NULL");
		uu_warn("%s\n", val == NULL ? "val is NULL" :
		    "val is not NULL");
		uu_warn("%s\n", iter == NULL ? "iter is NULL" :
		    "iter is not NULL");
		uu_warn("%s\n", svc == NULL ? "svc is NULL" :
		    "svc is not NULL");
		uu_warn("%s\n", scope == NULL ? "scope is NULL" :
		    "scope is not NULL");
		uu_warn(gettext("scf error is : %s\n"),
		    scf_strerror(scf_error()));
		scfdie();
	}

	if (scf_handle_get_scope(g_hndl, SCF_SCOPE_LOCAL, scope) != 0) {
		switch (scf_error()) {
		case SCF_ERROR_CONNECTION_BROKEN:
		case SCF_ERROR_NOT_FOUND:
			goto out;

		case SCF_ERROR_HANDLE_MISMATCH:
		case SCF_ERROR_NOT_BOUND:
		case SCF_ERROR_INVALID_ARGUMENT:
		default:
			bad_error("scf_handle_get_scope", scf_error());
		}
	}

	if (scf_scope_get_service(scope, HASH_SVC, svc) != 0) {
		uu_warn(gettext("Unable to process the hash service, %s\n"),
		    HASH_SVC);
		goto out;
	}

	pgname = safe_malloc(max_scf_name_len + 1);
	mfile = safe_malloc(max_scf_value_len + 1);

	if (scf_iter_service_pgs(iter, svc) != SCF_SUCCESS) {
		uu_warn(gettext("Unable to cleanup smf hash table : %s\n"),
		    scf_strerror(scf_error()));
		goto out;
	}

	while ((r = scf_iter_next_pg(iter, pg)) != 0) {
		if (r == -1)
			goto out;

		if (scf_pg_get_name(pg, pgname, max_scf_name_len + 1) < 0) {
			switch (scf_error()) {
			case SCF_ERROR_DELETED:
				return (ENODEV);

			case SCF_ERROR_CONNECTION_BROKEN:
				return (ECONNABORTED);

			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error("scf_pg_get_name", scf_error());
			}
		}
		if (IGNORE_VAR) {
			if (strncmp(pgname, VARSVC_PR, strlen(VARSVC_PR)) == 0)
				continue;
		}

		/*
		 * If unable to get the property continue as this is an
		 * entry that has no location to check against.
		 */
		if (scf_pg_get_property(pg, MFSTFILEPR, prop) != SCF_SUCCESS) {
			continue;
		}

		if (scf_property_get_value(prop, val) != SCF_SUCCESS) {
			uu_warn(gettext("Unable to get value from %s\n"),
			    pgname);

			switch (scf_error()) {
			case SCF_ERROR_DELETED:
			case SCF_ERROR_CONSTRAINT_VIOLATED:
			case SCF_ERROR_NOT_FOUND:
			case SCF_ERROR_NOT_SET:
				continue;

			case SCF_ERROR_CONNECTION_BROKEN:
				r = scferror2errno(scf_error());
				goto out;

			case SCF_ERROR_HANDLE_MISMATCH:
			case SCF_ERROR_NOT_BOUND:
			default:
				bad_error("scf_property_get_value",
				    scf_error());
			}
		}

		if (scf_value_get_astring(val, mfile, max_scf_value_len + 1)
		    == -1) {
			uu_warn(gettext("Unable to get astring from %s : %s\n"),
			    pgname, scf_strerror(scf_error()));

			switch (scf_error()) {
			case SCF_ERROR_NOT_SET:
			case SCF_ERROR_TYPE_MISMATCH:
				continue;

			default:
				bad_error("scf_value_get_astring", scf_error());
			}
		}

		if (access(mfile, F_OK) == 0)
			continue;

		(void) scf_pg_delete(pg);
	}

out:
	scf_scope_destroy(scope);
	scf_service_destroy(svc);
	scf_pg_destroy(pg);
	scf_property_destroy(prop);
	scf_value_destroy(val);
	scf_iter_destroy(iter);
	free(pgname);
	free(mfile);

	return (0);
}

#ifndef NATIVE_BUILD
/* ARGSUSED */
CPL_MATCH_FN(complete_select)
{
	const char *arg0, *arg1, *arg1end;
	int word_start, err = 0, r;
	size_t len;
	char *buf;

	if (lscf_prep_hndl(LSCF_HNDL_NONE) == 1)
		return (-1);

	arg0 = line + strspn(line, " \t");
	assert(strncmp(arg0, "select", sizeof ("select") - 1) == 0);

	arg1 = arg0 + sizeof ("select") - 1;
	arg1 += strspn(arg1, " \t");
	word_start = arg1 - line;

	arg1end = arg1 + strcspn(arg1, " \t");
	if (arg1end < line + word_end)
		return (0);

	len = line + word_end - arg1;

	buf = safe_malloc(max_scf_name_len + 1);

	if (cur_snap != NULL) {
		return (0);
	} else if (cur_inst != NULL) {
		return (0);
	} else if (cur_svc != NULL) {
		scf_instance_t *inst;
		scf_iter_t *iter;

		if ((inst = scf_instance_create(g_hndl)) == NULL ||
		    (iter = scf_iter_create(g_hndl)) == NULL)
			scfdie();

		if (scf_iter_service_instances(iter, cur_svc) != 0)
			scfdie();

		for (;;) {
			r = scf_iter_next_instance(iter, inst);
			if (r == 0)
				break;
			if (r != 1)
				scfdie();

			if (scf_instance_get_name(inst, buf,
			    max_scf_name_len + 1) < 0)
				scfdie();

			if (strncmp(buf, arg1, len) == 0) {
				err = cpl_add_completion(cpl, line, word_start,
				    word_end, buf + len, "", " ");
				if (err != 0)
					break;
			}
		}

		scf_iter_destroy(iter);
		scf_instance_destroy(inst);

		return (err);
	} else {
		scf_service_t *svc;
		scf_iter_t *iter;

		assert(cur_scope != NULL);

		if ((svc = scf_service_create(g_hndl)) == NULL ||
		    (iter = scf_iter_create(g_hndl)) == NULL)
			scfdie();

		if (scf_iter_scope_services(iter, cur_scope) != 0)
			scfdie();

		for (;;) {
			r = scf_iter_next_service(iter, svc);
			if (r == 0)
				break;
			if (r != 1)
				scfdie();

			if (scf_service_get_name(svc, buf,
			    max_scf_name_len + 1) < 0)
				scfdie();

			if (strncmp(buf, arg1, len) == 0) {
				err = cpl_add_completion(cpl, line, word_start,
				    word_end, buf + len, "", " ");
				if (err != 0)
					break;
			}
		}

		scf_iter_destroy(iter);
		scf_service_destroy(svc);

		return (err);
	}
}

/* ARGSUSED */
CPL_MATCH_FN(complete_command)
{
	uint32_t scope = 0;

	if (cur_snap != NULL)
		scope = CS_SNAP;
	else if (cur_inst != NULL)
		scope = CS_INST;
	else if (cur_svc != NULL)
		scope = CS_SVC;
	else
		scope = CS_SCOPE;

	return (scope ? add_cmd_matches(cpl, line, word_end, scope) : 0);
}
#endif	/* NATIVE_BUILD */
