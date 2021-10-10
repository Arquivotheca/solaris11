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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libcontract.h>
#include <libcontract_priv.h>
#include <libscf.h>
#include <libscf_priv.h>
#include <libuutil.h>
#include <limits.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <wait.h>

#include <pthread.h>

#include <sqlite.h>
#include <sqlite-misc.h>
#include "configd.h"
#include "repcache_protocol.h"

#define	LATEST_VERSION	8

#define	DB_CALLBACK_CONTINUE	0
#define	DB_CALLBACK_ABORT	1
#define	PRE_AIMM "pre_aimm"
#define	DB_SUFIX ".db"
#define	PRE_AIMM_DB PRE_AIMM DB_SUFIX

#define	SVC_PREFIX "svc:/"

#define	ALT_TMP_REPO	"alt_repo_upgrade.db"
#define	SVCCFG_CMDS	"repo_upgrade_svccfg_cmds"
#define	TMP_REPO	"repo_upgrade.db"
#define	F_SUFFIX	"failed"
#define	TMP_DIR		"/system/volatile/"
#define	TMP_DIR_USR	"/tmp/"

#define	LIB_M "/lib/svc/manifest/"
#define	VAR_M "/var/svc/manifest/"
#define	GEN_PROF_PG	"etc_svc_profile_generic_xml"

const char *profiles[] = {
	"/etc/svc/profile/generic.xml",
	"/etc/svc/profile/platform.xml",
	"/etc/svc/profile/site/",
	"/var/svc/profile/site/",
	NULL};

#define	IS_STD_LOC(l) ((strncmp(l, LIB_M, sizeof (LIB_M) - 1) == 0) || \
			(strncmp(l, VAR_M, sizeof (VAR_M) - 1) == 0))

boolean_t keep_tmp_tables;
boolean_t keep_schema_version;
boolean_t keep_files;

const char *tmp_dir;
const char *tmp_repo;
const char *alt_repo;
const char *svccfg_cmds;

sqlite *g_db;

volatile int rver;
volatile uint32_t notify_cnt;
volatile uint32_t notify_cnt_max;

/*
 * * Upgrade strategy: *
 *     We look at each property in prop_lnk_tbl and decorate it at
 *     MANIFEST_LAYER if it belongs to the initial or last-import snapshots.
 *     Otherwise, we decorate the property at the ADMIN_LAYER.
 *
 *     Next, we have to look at properties that were decorated at the
 *     ADMIN_LAYER but have a different gen_id in the initial or last-import
 *     snapshots. If the values of these properties match their counter-part in
 *     one of the snapshots, we should update (or promote) their
 *     decoration_layer and decoration_bundle_id to match their counter-part in
 *     the snapshot.
 *     We look first for matches in the last-import snapshot because it may
 *     contain a more recent manifestfiles value and we need the decoration
 *     entry to point to the bundle entry with the most recent manifest file.
 *
 *     Next we have to address services that have no initial or last-import
 *     snapshots (because they have no instance) and properties defined by
 *     profiles. We get all manifestfiles recorded in smf/manifests that are
 *     present in the current BE and are located at standard location and load
 *     them on an alternate repo. The properties in the alternate repo will be
 *     properly decorated. We then look for properties matching and update the
 *     decorations in the repository according according with the alt repo one.
 *
 *     Next, we have to look at the dependents property groups that are
 *     decorated at the MANIFEST_LAYER and find the generated correspondent
 *     dependencies. Once we find these dependencies, we update their
 *     decoration entries with the decoration_layer and decoration_bundle_id
 *     from the property in the dependents property group.
 */

enum moat_src {
	svc_inst_pg_prop = 1,
	svc_pg_prop,
	snap_inst_lvl,
	snap_svc_lvl
};

typedef struct two_bundles {
	uint32_t bad_bundle_id;
	uint32_t good_bundle_id;
} two_bundles_t;

typedef struct aux4 {
	const char *id;
	const char *column;
	const char *table;
	decoration_type_t type;
	uint32_t dec_id;
} aux4_t;

typedef struct db_bundle_val {
	sqlite *db;
	uint32_t alt_prop_id;
	const char *bundle_name;
	uint32_t val_id;
	rep_protocol_decoration_layer_t dec_layer;
} db_bundle_val_t;

typedef struct file_lc {
	FILE *file;
	uint32_t lc;
} file_lc_t;

typedef struct cmp_val {
	uint32_t val_id;
	uint32_t val_order;
	uint32_t val_ret;
} cmp_val_t;

typedef struct dec_update {
	uint32_t dec_key; /* decoration_key of the entry to be modified */
	uint32_t bundle_id;
	rep_protocol_decoration_layer_t layer;
} dec_update_t;

typedef struct prop_at_snap {
	uint32_t prop_id;
	dec_update_t *dec_update;
	uint32_t match;
} prop_at_snap_t;

typedef struct decoration_entry {
	uint32_t decoration_key;
	uint32_t decoration_id;
	char	 *decoration_entity_type;
	uint32_t decoration_value_id;
	uint32_t decoration_gen_id;
	rep_protocol_decoration_layer_t decoration_layer;
	uint32_t decoration_bundle_id;
	decoration_type_t decoration_type;
	uint32_t decoration_flags;
} decoration_entry_t;

typedef struct prop_dec {
	uint32_t bundle_id;
	uint32_t prop_id;
	char	 *prop_type;
	uint32_t pg_id;
	uint32_t gen_id;
	uint32_t value_id;
	rep_protocol_decoration_layer_t layer;
} prop_dec_t;

const char *
id_space_to_name(enum id_space id)
{
	switch (id) {
	case BACKEND_ID_SERVICE_INSTANCE:
		return ("SI");
	case BACKEND_ID_PROPERTYGRP:
		return ("PG");
	case BACKEND_ID_GENERATION:
		return ("GEN");
	case BACKEND_ID_PROPERTY:
		return ("PROP");
	case BACKEND_ID_VALUE:
		return ("VAL");
	case BACKEND_ID_SNAPNAME:
		return ("SNAME");
	case BACKEND_ID_SNAPSHOT:
		return ("SHOT");
	case BACKEND_ID_SNAPLEVEL:
		return ("SLVL");
	case BACKEND_ID_DECORATION:
		return ("DECOR");
	case BACKEND_KEY_DECORATION:
		return ("DECKEY");
	case BACKEND_ID_BUNDLE:
		return ("BUNDLE");
	default:
		abort();
		/*NOTREACHED*/
	}
}

#define	CNTSTRSZ	256

void
notify_counter(int ver)
{
	char s[6] = {
		'|',
		'/',
		'-',
		'|',
		'-',
		'\\',
	};
	int j = 0;
	int h = -1;

	char cntstr[CNTSTRSZ] = {0};
	uint32_t mynotify_cnt_max;
	uint32_t mynotify_cnt = 0;
	int i, len;

	while (notify_cnt_max == 0 && ver == rver) {
		h = 1;
		(void) fprintf(stdout, "%c\b", s[j]);
		(void) fflush(stdout);
		if (j == 5)
			j = 0;
		else
			j++;
	}

	if (h == 1)
		(void) fprintf(stdout, "\b  ");

	(void) fprintf(stdout, "\n");
	(void) fflush(stdout);

	mynotify_cnt = notify_cnt;
	mynotify_cnt_max = notify_cnt_max;
	while (notify_cnt < notify_cnt_max) {
		if (h == 0) {
			(void) fprintf(stdout, "%c\b", s[j]);
			(void) fflush(stdout);
			if (j == 5)
				j = 0;
			else
				j++;
		}

		if (mynotify_cnt == notify_cnt)
			continue;

		len = strlen(cntstr);
		for (i = 0; i < len; i++)
			(void) fprintf(stdout, "\b");

		(void) snprintf(cntstr, CNTSTRSZ, "      %d of %d rows "
		    "upgraded ",
		    notify_cnt, notify_cnt_max);
		(void) fprintf(stdout, "%s", cntstr);
		(void) fflush(stdout);
		mynotify_cnt = notify_cnt;

		h = 0;
	}

	if (mynotify_cnt_max != 0) {
		len = strlen(cntstr);
		for (i = 0; i < len; i++)
			(void) fprintf(stdout, "\b");

		(void) snprintf(cntstr, CNTSTRSZ, "      %d of %d rows "
		    "upgraded  \n", mynotify_cnt_max, mynotify_cnt_max);
		(void) fprintf(stdout, "%s", cntstr);
	}

	j = 0;
	(void) snprintf(cntstr, CNTSTRSZ, "      Cleaning up %c", s[j]);
	(void) fprintf(stdout, "%s", cntstr);
	(void) fflush(stdout);
	while (ver == rver) {
		(void) fprintf(stdout, "\b%c", s[j]);
		(void) fflush(stdout);
		if (j == 5)
			j = 0;
		else
			j++;

	}

	len = strlen(cntstr);
	for (i = 0; i < len; i++)
		(void) fprintf(stdout, "\b");

	(void) snprintf(cntstr, CNTSTRSZ, "                    \n");
	(void) fprintf(stdout, "%s", cntstr);
	(void) fflush(stdout);
}

/*
 * This is a thread function that will first display the upgrade
 * message, and then depending on the current version of the repository,
 * either display a spinner or some other counter.
 */
/*ARGSUSED*/
void *
upgrade_notify(void *arg)
{
	(void) fprintf(stdout, "Upgrading SMF repository.  "
	    "This may take several minutes.\n");
	(void) fflush(stdout);

	while (rver != LATEST_VERSION) {
		switch (rver) {
		case 7:
			(void) fprintf(stdout, "    Upgrading from Version 7 "
			    "to Version 8 :  ");
			notify_counter(rver);
			break;
		case 6:
			(void) fprintf(stdout, "    Upgrading from Version 6 "
			    "to Version 7 :  ");
			notify_counter(rver);
			break;
		case 5:
			(void) fprintf(stdout, "    Upgrading from Version 5 "
			    "to Version 6 :  ");
			notify_counter(rver);
			break;
		default:
			break;
		}
	}

	return ((void *)0);
}

void
cleanup_file(const char *file)
{
	if (unlink(file) != 0 && errno != ENOENT)
		(void) fprintf(stderr,
		    "Failed to unlink %s: %s\n", file, strerror(errno));
}

const char *
get_fnamep(const char *fname)
{
	const char *p = strrchr(fname, '/');

	return (p == NULL ? fname : ++p);
}

char *
append_fname(const char *fname, const char *sufix)
{
	size_t len;
	char *p, *f;
	char *buf = NULL;
	char *tmp = strdup(fname);

	len = strlen(fname) + strlen(sufix) + 3;

	if (len > PATH_MAX) {
		free(tmp);
		return (NULL);
	}

	if (tmp == NULL || (buf = malloc(len)) == NULL) {
		(void) fprintf(stderr, "failed allocating memory\n");
		free(tmp);
		return (NULL);
	}

	f = (char *)get_fnamep(tmp);
	if ((p = strchr(f, '.')) != NULL) {
		*p++ = '\0';
		(void) snprintf(buf, len, "%s-%s.%s", tmp, sufix, p);
	} else {
		(void) snprintf(buf, len, "%s-%s", tmp, sufix);
	}
	free(tmp);

	return (buf);
}

void
cleanup_and_bail(void)
{
	sqlite_close(g_db);

	if (!keep_files) {
		cleanup_file(tmp_repo);
		cleanup_file(alt_repo);
		cleanup_file(svccfg_cmds);
	}

	exit(1);
}

void
string_to_id(const char *str, uint32_t *output, const char *fieldname)
{
	if (uu_strtouint(str, output, sizeof (*output), 0, 0, 0) == -1) {
		(void) fprintf(stderr, "invalid integer \"%s\" in field \"%s\"",
		    str, fieldname);
		cleanup_and_bail();
	}
}

struct run_single_int_info {
	uint32_t	*rs_out;
	int		rs_result;
};

/*ARGSUSED*/
int
run_single_int_callback(void *arg, int columns, char **vals, char **names)
{
	struct run_single_int_info *info = arg;
	uint32_t val;

	char *endptr = vals[0];

	/*
	 * This will be hit if the query returns more than one row. Assuming
	 * rs_result has been properly initialized to REP_PROTOCOL_NOT_FOUND
	 */
	assert(info->rs_result != REP_PROTOCOL_SUCCESS);
	assert(columns == 1);

	if (vals[0] == NULL)
		return (DB_CALLBACK_CONTINUE);

	errno = 0;
	val = strtoul(vals[0], &endptr, 10);
	if ((val == 0 && endptr == vals[0]) || *endptr != 0 || errno != 0) {
		(void) fprintf(stderr, "malformed integer \"%20s\"", vals[0]);
		cleanup_and_bail();
	}

	*info->rs_out = val;
	info->rs_result = REP_PROTOCOL_SUCCESS;
	return (DB_CALLBACK_CONTINUE);
}

/*
 * Returns a new id or 0 if the id argument is invalid or the query fails.
 */
uint32_t
new_id(enum id_space id)
{
	struct run_single_int_info info;
	uint32_t new_id = 0;
	const char *name = id_space_to_name(id);
	char *emsg;
	int r;

	info.rs_out = &new_id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;

	r = sqlite_exec_printf(g_db,
	    "BEGIN TRANSACTION;"
	    "SELECT id_next FROM id_tbl WHERE (id_name = '%q');"
	    "UPDATE id_tbl SET id_next = id_next + 1 WHERE (id_name = '%q');"
	    "COMMIT TRANSACTION;",
	    run_single_int_callback, &info, &emsg, name, name);
	if (r != 0) {
		(void) fprintf(stderr, "Failed to lookup %s in id_tbl: %s\n",
		    name, emsg);
		cleanup_and_bail();
	}

	return (new_id);
}

/*
 * add value_order column to value_tbl.
 */
int
check_value_order_upgrade(void)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec(g_db, "SELECT value_order FROM value_tbl "
	    "LIMIT 1;", NULL, NULL, &emsg)) == SQLITE_ERROR &&
	    strcmp(emsg, "no such column: value_order") == 0) {
		r = sqlite_exec(g_db,
		    "BEGIN TRANSACTION; "
		    "CREATE TABLE value_tbl_tmp ( "
		    "value_id   INTEGER NOT NULL, "
		    "value_value VARCHAR NOT NULL, "
		    "value_order INTEGER DEFAULT 0); "
		    "INSERT INTO value_tbl_tmp "
		    "(value_id, value_value) "
		    "SELECT value_id, value_value FROM value_tbl; "
		    "DROP TABLE value_tbl; "
		    "CREATE TABLE value_tbl( "
		    "value_id   INTEGER NOT NULL, "
		    "value_value VARCHAR NOT NULL, "
		    "value_order INTEGER DEFAULT 0); "
		    "INSERT INTO value_tbl SELECT * FROM value_tbl_tmp; "
		    "CREATE INDEX value_tbl_id ON value_tbl (value_id); "
		    "DROP TABLE value_tbl_tmp; "
		    "COMMIT TRANSACTION; "
		    "VACUUM; ",
		    NULL, NULL, &emsg);
		if (r != SQLITE_OK) {
			(void) fprintf(stderr,
			    "Failed value_order upgrade: %s\n", emsg);
			return (1);
		}
	}

	return (0);
}

/*
 * verify repository version and suitability of upgrade by this code
 * return schema_version, -1 on error
 */
int
get_repo_version(const char *dbpath)
{
	sqlite *db;
	int r;
	char *emsg;
	uint32_t repo_ver;
	struct run_single_int_info info;

	if ((db = sqlite_open(dbpath, 0600, &emsg)) == NULL) {
		(void) fprintf(stderr, "failed sqlite_open %s: %s\n",
		    dbpath, emsg);
		exit(1);
	}

	/*
	 * Here we check that the schema_version
	 * is one we know how to upgrade from
	 */
	info.rs_out = &repo_ver;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec(db,
	    "SELECT schema_version FROM schema_version",
	    run_single_int_callback, &info, &emsg);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr,
		    "Error verifying schema_version: %d: %s\n", r, emsg);
		sqlite_close(db);
		return (-1);
	}

	if (info.rs_result == REP_PROTOCOL_FAIL_NOT_FOUND) {
		(void) fprintf(stderr,
		    "bad repository: no schema_version\n");
		sqlite_close(db);
		return (-1);
	}

	sqlite_close(db);

	return ((int)repo_ver);
}

/*
 * repo_create_new_tables()
 * create decoration_tbl and bundle_tbl
 * return 0 on success, 1 on failure
 */
int
repo_create_new_tables(void)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec(g_db,
	    "BEGIN TRANSACTION;"
	    "    CREATE TABLE decoration_tbl ("
	    "        decoration_key INTEGER PRIMARY KEY, "
	    "        decoration_id INTEGER NOT NULL, "
	    "        decoration_entity_type CHAR(256), "
	    "        decoration_value_id INTEGER NOT NULL, "
	    "        decoration_gen_id INTEGER NOT NULL, "
	    "        decoration_layer INTEGER NOT NULL, "
	    "        decoration_bundle_id INTEGER NOT NULL, "
	    "        decoration_type INTEGER DEFAULT 0, "
	    "        decoration_flags INTEGER DEFAULT 0, "
	    "        decoration_tv_sec INTEGER DEFAULT 0, "
	    "        decoration_tv_usec INTEGER DEFAULT 0); "
	    "    CREATE INDEX decoration_tbl_base "
	    "        ON decoration_tbl (decoration_id, decoration_value_id);"
	    "    CREATE INDEX decoration_tbl_id "
	    "        ON decoration_tbl (decoration_id);"
	    "    CREATE INDEX decoration_tbl_val ON decoration_tbl ("
	    "        decoration_value_id);"
	    "    CREATE INDEX decoration_tbl_id_gen"
	    "        ON decoration_tbl (decoration_id, decoration_gen_id);"
	    "    CREATE INDEX decoration_tbl_id_gen_lyr"
	    "        ON decoration_tbl (decoration_id, decoration_gen_id,"
	    "        decoration_layer);"
	    "    CREATE TABLE bundle_tbl ("
	    "        bundle_id INTEGER NOT NULL PRIMARY KEY, "
	    "        bundle_name VARCHAR(256) NOT NULL, "
	    "        bundle_timestamp INTEGER DEFAULT 0);"
	    "    CREATE INDEX bundle_tbl_name ON bundle_tbl (bundle_name);"
	    "    INSERT INTO id_tbl (id_name, id_next) VALUES ('DECOR', 1);"
	    "    INSERT INTO id_tbl (id_name, id_next) VALUES ('DECKEY', 1);"
	    "    INSERT INTO id_tbl (id_name, id_next) VALUES ('BUNDLE', 1);"
	    "    CREATE INDEX snaplevel_lnk_tbl_pg_gen"
	    "        ON snaplevel_lnk_tbl (snaplvl_pg_id, snaplvl_gen_id);"
	    "COMMIT TRANSACTION;",
	    NULL, NULL, &emsg)) != 0) {
		(void) fprintf(stderr,
		    "Error creating new tables: %d: %s\n", r, emsg);
		return (1);
	}
	return (0);
}

/*
 * Mother of all tables
 * groups all columns that have a 1 to 1 relationship with
 * property entries coming from pre-aimm
 *
 * Return 0 on success, non-zero on failure
 *
 * Should be called within a transaction
 */
int
create_moat(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE moat ("
	    "        moat_key INTEGER PRIMARY KEY,"
	    "        moat_src_id INTEGER NOT NULL DEFAULT 0,"
	    "        moat_svc_name CHAR(256) NOT NULL,"
	    "        moat_svc_id INTEGER NOT NULL DEFAULT 0,"
	    "        moat_instance_name CHAR(256),"
	    "        moat_instance_id INTEGER NOT NULL DEFAULT 0,"
	    "        moat_pg_name CHAR(256) NOT NULL,"
	    "        moat_pg_type CHAR(256) NOT NULL,"
	    "        moat_prop_name CHAR(256) NOT NULL,"
	    "        moat_prop_id INTEGER NOT NULL,"
	    "        moat_lnk_prop_type CHAR(2) NOT NULL,"
	    "        moat_lnk_pg_id INTEGER NOT NULL,"
	    "        moat_lnk_gen_id INTEGER NOT NULL,"
	    "        moat_lnk_val_id INTEGER,"
	    "        moat_lnk_decoration_id INTEGER,"
	    "        moat_lnk_val_decoration_key INTEGER,"
	    "        moat_bundle_id INTEGER);"
	    "    CREATE INDEX moat_prop_name_idx"
	    "        ON moat (moat_svc_id, moat_instance_id, moat_pg_name,"
	    "            moat_prop_name);"
	    "    CREATE INDEX moat_general_enabled_idx"
	    "        ON moat (moat_pg_name, moat_prop_name);"
	    "    CREATE INDEX moat_pg_name_idx"
	    "        ON moat (moat_pg_name);"
	    "    CREATE INDEX moat_prop_id_idx"
	    "        ON moat (moat_prop_id);"
	    "    CREATE INDEX moat_lnk_decoration_id_idx"
	    "        ON moat (moat_lnk_decoration_id);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Deathrow tables for properties and values
 *
 * We need to delete some properties under manifestfiles pg in initial
 * snapshots. We use these tables as auxiliary tables and to save the
 * properties and values being delete, in case we need them later.
 */
int
create_dr_tables(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE dr_prop_tbl("
	    "        dr_snap_id INTEGER NOT NULL,"
	    "        dr_svc_id  INTEGER NOT NULL,"
	    "        dr_inst_id INTEGER NOT NULL,"
	    "        dr_prop_id INTEGER NOT NULL,"
	    "        dr_pg_id   INTEGER NOT NULL,"
	    "        dr_gen_id  INTEGER NOT NULL,"
	    "        dr_prop_name CHAR(256) NOT NULL,"
	    "        dr_prop_type CHAR(2) NOT NULL,"
	    "        dr_val_id  INTEGER,"
	    "        dr_dec_id  INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX dr_prop_id_idx"
	    "        ON dr_prop_tbl (dr_prop_id);"
	    "    CREATE TABLE dr_value_tbl ("
	    "        dr_value_id INTEGER NOT NULL,"
	    "        dr_value_type CHAR(1) NOT NULL,"
	    "        dr_value_value VARCHAR NOT NULL,"
	    "        dr_value_order INTEGER DEFAULT 0);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Optimization table for looking up which snapshots a property
 * belongs to
 *
 * Return 0 on success, non-zero on failure
 *
 * Should be called within a transaction
 */
int
create_prop_snap_lnk(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE prop_snap_lnk ("
	    "        aux_prop_id INTEGER NOT NULL,"
	    "        aux_inst_id INTEGER NOT NULL,"
	    "        aux_snap_id INTEGER NOT NULL,"
	    "        aux_dec_id INTEGER NOT NULL DEFAULT 0,"
	    "        aux_gen_id INTEGER NOT NULL DEFAULT 0,"
	    "        aux_val_id INTEGER NOT NULL DEFAULT 0,"
	    "        aux_snap_name CHAR(256) NOT NULL);"
	    "    CREATE INDEX prop_snap_lnk_prop"
	    "        ON prop_snap_lnk (aux_prop_id);"
	    "    CREATE INDEX prop_snap_lnk_snap"
	    "        ON prop_snap_lnk (aux_snap_id);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Optimization table for looking up which bundle file is part of
 * a specific snapshot
 *
 * Return 0 on success, non-zero on failure
 *
 * Should be called within a transaction
 */
int
create_snap_bundle_lnk(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE snap_bundle_lnk ("
	    "        aux2_bundle_name CHAR(256) NOT NULL,"
	    "        aux2_bundle_val_id INTEGER NOT NULL,"
	    "        aux2_dec_id INTEGER NOT NULL,"
	    "        aux2_inst_id INTEGER NOT NULL,"
	    "        aux2_snap_id INTEGER NOT NULL,"
	    "        aux2_snap_name CHAR(256) NOT NULL);"
	    "    CREATE INDEX snap_bundle_idx"
	    "        ON snap_bundle_lnk (aux2_snap_id);"
	    "    CREATE INDEX snap_bundle_val_id_bundle_idx"
	    "        ON snap_bundle_lnk (aux2_bundle_val_id);"
	    "    CREATE INDEX snap_bundle_name_idx"
	    "        ON snap_bundle_lnk (aux2_bundle_name);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Auxiliary table used to update the bundle from seed repo in initial snapshot
 * to match bundle in last-import.
 */
int
create_bundle_migration_tbl(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE bundle_migration_tbl ("
	    "        bundle_bad CHAR(256) NOT NULL,"
	    "        bundle_bad_val_id INTEGER NOT NULL,"
	    "        bundle_bad_dec_id INTEGER NOT NULL,"
	    "        bundle_good CHAR(256) NOT NULL,"
	    "        bundle_good_val_id INTEGER NOT NULL,"
	    "        bundle_good_dec_id INTEGER NOT NULL);"
	    "    CREATE INDEX bundle_bad_idx"
	    "        ON bundle_migration_tbl (bundle_bad);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Optimization table for assisting updating a decoration_tbl entry
 * This is used for re-decorating properties.
 *
 * Return 0 on success, non-zero on failure
 *
 * Should be called within a transaction
 */
int
create_prop_dec_tbl(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE prop_dec_tbl ("
	    "        aux3_prop_id INTEGER NOT NULL,"
	    "        aux3_dec_by_name_id INTEGER NOT NULL,"
	    "        aux3_val_dec_key INTEGER NOT NULL);"
	    "    CREATE INDEX prop_dec_prop_idx"
	    "        ON prop_dec_tbl (aux3_prop_id);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Optimization table for assisting decoration of higher entities.
 * This table has to be cleaned up before use for each different
 * higher entity type.
 *
 * Return 0 on success, non-zero on failure
 *
 * Should be called within a transaction
 */
int
create_he_dec_tbl(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE he_dec_tbl ("
	    "        aux4_id INTEGER NOT NULL,"
	    "	     aux4_type char(256),"
	    "        aux4_dec_id INTEGER NOT NULL,"
	    "        aux4_layer INTEGER NOT NULL,"
	    "        aux4_bundle_id INTEGER NOT NULL);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * auxiliary table for processing alt repo
 *
 * Return 0 on success, non-zero on failure
 *
 * Should be called within a transaction
 */
int
create_alt_repo_aux(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE alt_repo_tbl("
	    "         repo_prop_id INTEGER NOT NULL,"
	    "         repo_val_id INTEGER NOT NULL,"
	    "         repo_dec_key INTEGER NOT NULL,"
	    "         alt_repo_prop_id INTEGER NOT NULL,"
	    "         alt_repo_val_id INTEGER NOT NULL,"
	    "         alt_repo_layer INTEGER NOT NULL,"
	    "         alt_repo_bundle_name CHAR(256) NOT NULL);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * Dangling properties in manifest backed snapshots (initial and
 * last-import)
 */
int
create_dangling_prop_snap_bundle_tbl(sqlite *db, char **emsg)
{
	int r;

	r = sqlite_exec(db,
	    "    CREATE TABLE dang_snap_bundle_tbl("
	    "         dang_snap_id INTEGER NOT NULL,"
	    "         dang_bundle_id INTEGER NOT NULL);",
	    NULL, NULL, emsg);

	return (r);
}

/*
 * create_aux_tables()
 * sqlite locks tables used in queries for insert/update so we need a few
 * auxiliary tables.
 *
 * return 0 on success, 1 on error
 */
int
create_aux_tables(sqlite *db)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec(db,
	    "BEGIN TRANSACTION;", NULL, NULL, &emsg)) != 0 ||
	    (r = create_moat(db, &emsg)) != 0 ||
	    (r = create_prop_snap_lnk(db, &emsg)) != 0 ||
	    (r = create_snap_bundle_lnk(db, &emsg)) != 0 ||
	    (r = create_prop_dec_tbl(db, &emsg)) != 0 ||
	    (r = create_he_dec_tbl(db, &emsg)) != 0 ||
	    (r = create_alt_repo_aux(db, &emsg)) != 0 ||
	    (r = create_bundle_migration_tbl(db, &emsg)) != 0 ||
	    (r = create_dangling_prop_snap_bundle_tbl(db, &emsg)) != 0 ||
	    (r = create_dr_tables(db, &emsg)) != 0 ||
	    (r = sqlite_exec(db,
	    "COMMIT TRANSACTION;", NULL, NULL, &emsg)) != 0) {
		(void) fprintf(stderr,
		    "Failed creation of aux tables: %d, %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * create_temporary_tables()
 * sqlite does not support adding a column to an existing so we need a few
 * temporary tables.
 *
 * return 0 on success, 1 on error
 */
int
create_temporary_tables(void)
{
	int r;
	char *emsg;

	/*
	 * Temporary tables to work around sqlite lack of add column
	 */
	if ((r = sqlite_exec(g_db,
	    "BEGIN TRANSACTION;"
	    "    CREATE TABLE service_tbl_tmp ("
	    "        svc_id INTEGER PRIMARY KEY,"
	    "        svc_name CHAR(256) NOT NULL,"
	    "        svc_conflict_cnt INTEGER DEFAULT 0,"
	    "        svc_dec_id INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX svc_tbl_tmp_name ON service_tbl_tmp (svc_name);"
	    "    CREATE INDEX service_tbl_tmp_dec"
	    "        ON service_tbl_tmp (svc_dec_id);"
	    "    INSERT INTO service_tbl_tmp (svc_id, svc_name)"
	    "        SELECT svc_id, svc_name FROM service_tbl;"
	    "    CREATE TABLE instance_tbl_tmp ("
	    "        instance_id INTEGER PRIMARY KEY,"
	    "        instance_name CHAR(256) NOT NULL,"
	    "        instance_svc INTEGER NOT NULL,"
	    "        instance_conflict_cnt INTEGER DEFAULT 0,"
	    "        instance_dec_id INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX instance_tbl_tmp_name"
	    "        ON instance_tbl_tmp (instance_svc, instance_name);"
	    "    CREATE INDEX instance_tbl_tmp_dec"
	    "        ON instance_tbl_tmp (instance_dec_id);"
	    "    INSERT INTO instance_tbl_tmp (instance_id, instance_name,"
	    "        instance_svc)"
	    "        SELECT instance_id, instance_name, instance_svc"
	    "            FROM instance_tbl;"
	    "    CREATE TABLE pg_tbl_tmp ("
	    "        pg_id INTEGER PRIMARY KEY,"
	    "        pg_parent_id INTEGER NOT NULL,"
	    "        pg_name CHAR(256) NOT NULL,"
	    "        pg_type CHAR(256) NOT NULL,"
	    "        pg_flags INTEGER NOT NULL,"
	    "        pg_gen_id INTEGER NOT NULL,"
	    "        pg_dec_id INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX pg_tbl_tmp_name"
	    "        ON pg_tbl_tmp (pg_parent_id, pg_name);"
	    "    CREATE INDEX pg_tbl_tmp_parent ON pg_tbl_tmp (pg_parent_id);"
	    "    CREATE INDEX pg_tbl_tmp_type"
	    "        ON pg_tbl_tmp (pg_parent_id, pg_type);"
	    "    CREATE INDEX pg_tbl_tmp_dec ON pg_tbl_tmp (pg_dec_id);"
	    "    CREATE INDEX pg_tbl_tmp_id_gen"
	    "        ON pg_tbl_tmp (pg_id, pg_gen_id);"
	    "    INSERT INTO pg_tbl_tmp (pg_id, pg_parent_id, pg_name, pg_type,"
	    "        pg_flags, pg_gen_id)"
	    "            SELECT pg_id, pg_parent_id, pg_name, pg_type,"
	    "                pg_flags, pg_gen_id FROM pg_tbl;"
	    "    CREATE TABLE prop_lnk_tbl_tmp ("
	    "        lnk_prop_id INTEGER PRIMARY KEY,"
	    "        lnk_pg_id INTEGER NOT NULL,"
	    "        lnk_gen_id INTEGER NOT NULL,"
	    "        lnk_prop_name CHAR(256) NOT NULL,"
	    "        lnk_prop_type CHAR(2) NOT NULL,"
	    "        lnk_val_id INTEGER,"
	    "        lnk_decoration_id INTEGER NOT NULL DEFAULT 0,"
	    "        lnk_dec_by_name_id INTEGER NOT NULL DEFAULT 0,"
	    "        lnk_val_decoration_key INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX prop_lnk_tbl_tmp_base "
	    "        ON prop_lnk_tbl_tmp (lnk_pg_id, lnk_gen_id);"
	    "    CREATE INDEX prop_lnk_tbl_tmp_gen_id "
	    "        ON prop_lnk_tbl_tmp (lnk_gen_id);"
	    "    CREATE INDEX prop_lnk_tbl_tmp_val "
	    "        ON prop_lnk_tbl_tmp (lnk_val_id);"
	    "    CREATE INDEX prop_lnk_tbl_tmp_dec "
	    "        ON prop_lnk_tbl_tmp (lnk_decoration_id);"
	    "    CREATE INDEX prop_lnk_tbl_tmp_dec_name "
	    "        ON prop_lnk_tbl_tmp (lnk_dec_by_name_id);"
	    "    CREATE INDEX prop_lnk_tbl_tmp_dec_key "
	    "        ON prop_lnk_tbl_tmp (lnk_val_decoration_key);"
	    "    INSERT INTO prop_lnk_tbl_tmp ("
	    "        lnk_prop_id, lnk_pg_id, lnk_gen_id, lnk_prop_name, "
	    "        lnk_prop_type, lnk_val_id, lnk_decoration_id,"
	    "        lnk_val_decoration_key) "
	    "        SELECT lnk_prop_id, lnk_pg_id, lnk_gen_id, lnk_prop_name, "
	    "            lnk_prop_type, lnk_val_id, 0, 0 FROM prop_lnk_tbl;"
	    "COMMIT TRANSACTION;",
	    NULL, NULL, &emsg)) != 0) {
		(void) fprintf(stderr,
		    "create_temporary_tables failed: %d: %s\n", r, emsg);
		return (1);
	}
	return (0);
}

/*
 * populate moat
 */
int
populate_moat(sqlite *db)
{
	int r;
	char *emsg;

	/*
	 * We can find properties from 4 different sources:
	 *	1 from svc/instance/pg/prop tables,
	 *	2 from svc/pg/prop tables,
	 *	  from snapshots (when lnk_pg_id is not in pg_tbl)
	 *	3	instance level
	 *	4	service level
	 */
	r = sqlite_exec_printf(db,
	    "INSERT INTO moat (moat_svc_name, moat_svc_id,"
	    "    moat_instance_name, moat_instance_id, moat_pg_name,"
	    "    moat_pg_type, moat_prop_name, moat_lnk_prop_type,"
	    "    moat_prop_id, moat_lnk_pg_id, moat_lnk_gen_id,"
	    "    moat_lnk_val_id, moat_src_id)"
	    "    SELECT svc_name, svc_id, instance_name, instance_id, pg_name,"
	    "        pg_type, lnk_prop_name, lnk_prop_type, lnk_prop_id,"
	    "        lnk_pg_id, lnk_gen_id, lnk_val_id, %d"
	    "    FROM service_tbl"
	    "        INNER JOIN instance_tbl ON svc_id = instance_svc"
	    "        INNER JOIN pg_tbl ON pg_parent_id = instance_id"
	    "        INNER JOIN prop_lnk_tbl ON lnk_pg_id = pg_id;",
	    NULL, NULL, &emsg, svc_inst_pg_prop);

	if (r != 0) {
		(void) fprintf(stderr,
		    "INSERT INTO moat %d failed: %d, %s\n", svc_inst_pg_prop,
		    r, emsg);
		return (1);
	}

	/*
	 * From svc/pg/prop
	 */
	r = sqlite_exec_printf(db,
	    "INSERT INTO moat (moat_svc_name, moat_svc_id, moat_pg_name,"
	    "    moat_pg_type, moat_prop_name, moat_lnk_prop_type,"
	    "    moat_prop_id, moat_lnk_pg_id, moat_lnk_gen_id,"
	    "    moat_lnk_val_id, moat_src_id)"
	    "    SELECT svc_name, svc_id, pg_name, pg_type, lnk_prop_name,"
	    "        lnk_prop_type, lnk_prop_id, lnk_pg_id, lnk_gen_id,"
	    "        lnk_val_id, %d FROM service_tbl"
	    "        INNER JOIN pg_tbl ON pg_parent_id = svc_id"
	    "        INNER JOIN prop_lnk_tbl ON lnk_pg_id = pg_id",
	    NULL, NULL, &emsg, svc_pg_prop);

	if (r != 0) {
		(void) fprintf(stderr,
		    "INSERT INTO moat %d failed: %d, %s\n", svc_pg_prop, r,
		    emsg);
		return (1);
	}

	/*
	 * From snapshots
	 *	instance level
	 */
	r = sqlite_exec_printf(db,
	    "INSERT INTO moat (moat_svc_name, moat_svc_id,"
	    "    moat_instance_name, moat_instance_id, moat_pg_name,"
	    "    moat_pg_type, moat_prop_name, moat_lnk_prop_type,"
	    "    moat_prop_id, moat_lnk_pg_id, moat_lnk_gen_id,"
	    "    moat_lnk_val_id, moat_src_id)"
	    "    SELECT svc_name, svc_id, instance_name, instance_id,"
	    "        snaplvl_pg_name, snaplvl_pg_type, lnk_prop_name,"
	    "        lnk_prop_type, lnk_prop_id, lnk_pg_id, lnk_gen_id,"
	    "        lnk_val_id, %d FROM service_tbl"
	    "        INNER JOIN instance_tbl ON svc_id = instance_svc"
	    "        INNER JOIN snapshot_lnk_tbl ON lnk_inst_id = instance_id"
	    "        INNER JOIN snaplevel_tbl ON lnk_snap_id = snap_id"
	    "        INNER JOIN snaplevel_lnk_tbl"
	    "            ON snap_level_id = snaplvl_level_id"
	    "        INNER JOIN prop_lnk_tbl"
	    "            ON snaplvl_pg_id = lnk_pg_id"
	    "                AND snaplvl_gen_id = lnk_gen_id"
	    "        WHERE snap_level_num = 1"
	    "            AND lnk_pg_id NOT IN (SELECT pg_id FROM pg_tbl);",
	    NULL, NULL, &emsg, snap_inst_lvl);

	if (r != 0) {
		(void) fprintf(stderr,
		    "INSERT INTO moat %d failed: %d, %s\n", snap_inst_lvl, r,
		    emsg);
		return (1);
	}

	/*
	 * From snapshots
	 *	service level
	 *
	 * We use distinct here so that services with multiple instances
	 * do not add repeated rows.
	 */
	r = sqlite_exec_printf(db,
	    "INSERT INTO moat (moat_svc_name, moat_svc_id, moat_pg_name,"
	    "    moat_pg_type, moat_prop_name, moat_lnk_prop_type,"
	    "    moat_prop_id, moat_lnk_pg_id, moat_lnk_gen_id,"
	    "    moat_lnk_val_id, moat_src_id)"
	    "    SELECT DISTINCT svc_name, svc_id, snaplvl_pg_name,"
	    "        snaplvl_pg_type, lnk_prop_name, lnk_prop_type,"
	    "        lnk_prop_id, lnk_pg_id, lnk_gen_id, lnk_val_id, %d"
	    "    FROM service_tbl"
	    "        INNER JOIN instance_tbl ON svc_id = instance_svc"
	    "        INNER JOIN snapshot_lnk_tbl ON lnk_inst_id = instance_id"
	    "        INNER JOIN snaplevel_tbl ON lnk_snap_id = snap_id"
	    "        INNER JOIN snaplevel_lnk_tbl"
	    "            ON snap_level_id = snaplvl_level_id"
	    "        INNER JOIN prop_lnk_tbl"
	    "            ON snaplvl_pg_id = lnk_pg_id"
	    "                AND snaplvl_gen_id = lnk_gen_id"
	    "        WHERE snap_level_num = 2"
	    "            AND lnk_pg_id NOT IN (SELECT pg_id FROM pg_tbl);",
	    NULL, NULL, &emsg, snap_svc_lvl);

	if (r != 0) {
		(void) fprintf(stderr,
		    "INSERT INTO moat %d failed: %d, %s\n", snap_svc_lvl, r,
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * prop_snap_lnk
 */
int
populate_aux_tables(void)
{
	int r;
	char *emsg;

	/*
	 * Populate link table between prop and snapshots.
	 * Note that we only care about initial and last-import snapshots.
	 */
	r = sqlite_exec(g_db,
	    "INSERT INTO prop_snap_lnk"
	    "    (aux_prop_id, aux_inst_id, aux_snap_id, aux_snap_name) "
	    "SELECT lnk_prop_id, lnk_inst_id, lnk_id, lnk_snap_name"
	    "    FROM snapshot_lnk_tbl"
	    "    INNER JOIN snaplevel_tbl ON lnk_snap_id = snap_id"
	    "    INNER JOIN snaplevel_lnk_tbl"
	    "        ON snap_level_id = snaplvl_level_id"
	    "    INNER JOIN prop_lnk_tbl"
	    "        ON snaplvl_pg_id = lnk_pg_id AND"
	    "           snaplvl_gen_id = lnk_gen_id"
	    "    WHERE lnk_snap_name = 'initial'"
	    "        OR lnk_snap_name = 'last-import';",
	    NULL, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed insert_prop_snap_callback: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * Callback function assign decoration id to prop_dec_aux
 *
 * assign lnk_decoration_id according to svc_id/[inst_id/]pg_id/prop_name
 *
 * query columns
 *     0 moat_svc_id
 *     1 moat_inst_id
 *     2 moat_lnk_pg_id
 *     3 moat_prop_name entry
 */
/*ARGSUSED*/
int
assign_dec_id_callback(void *arg, int columns, char **vals,
    char **names)
{
	int r;
	char *emsg;
	uint32_t *dec_id = (uint32_t *)arg;

	assert(columns == 4);

	r = sqlite_exec_printf(g_db,
	    "UPDATE prop_lnk_tbl_tmp SET lnk_decoration_id = %u"
	    "    WHERE lnk_prop_id IN (SELECT moat_prop_id"
	    "        FROM moat"
	    "            WHERE moat_svc_id = '%q' AND moat_instance_id = '%q'"
	    "                AND moat_lnk_pg_id = '%q'"
	    "                AND moat_prop_name = '%q');",
	    NULL, NULL, &emsg, (*dec_id)++, vals[0], vals[1], vals[2], vals[3]);

	if (r != 0) {
		(void) fprintf(stderr,
		    "UPDATE prop_lnk_tbl_tmp with dec_id failed: %s\n", emsg);
		return (1);
	}

	return (0);
}

/*
 * Callback function assign unique identifier for properties based on
 * pg_name so that promote_properties() will check against properties in
 * the last-import snapshot which have a different pg_id because of how
 * services are imported before aimm.
 *
 * assign lnk_dec_by_name_id according to svc_id/[inst_id/]pg_name/prop_name
 *
 * query columns
 *     0 moat_svc_id
 *     1 moat_inst_id
 *     2 moat_pg_name
 *     3 moat_prop_name
 */
/*ARGSUSED*/
int
assign_dec_by_name_id_cb(void *arg, int columns, char **vals,
    char **names)
{
	int r;
	char *emsg;
	uint32_t *dec_by_name_id = (uint32_t *)arg;

	assert(columns == 4);

	r = sqlite_exec_printf(g_db,
	    "UPDATE prop_lnk_tbl_tmp SET lnk_dec_by_name_id = %u"
	    "    WHERE lnk_prop_id IN (SELECT moat_prop_id"
	    "        FROM moat"
	    "            WHERE moat_svc_id = '%q' AND moat_instance_id = '%q'"
	    "                AND moat_pg_name = '%q'"
	    "                AND moat_prop_name = '%q');",
	    NULL, NULL, &emsg, (*dec_by_name_id)++, vals[0], vals[1], vals[2],
	    vals[3]);

	if (r != 0) {
		(void) fprintf(stderr,
		    "UPDATE prop_lnk_tbl_tmp with dec_by_name_id failed: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * Callback function to populate moat_lnk_decoration_id
 *
 * query columns
 *     lnk_prop_id, lnk_decoration_id
 */
/*ARGSUSED*/
int
decorate_moat_callback(void *arg, int columns, char **vals,
    char **names)
{
	int r;
	char *emsg;

	assert(columns == 2);

	r = sqlite_exec_printf(g_db,
	    "UPDATE moat SET moat_lnk_decoration_id = '%q'"
	    "    WHERE moat_prop_id = '%q';",
	    NULL, NULL, &emsg, vals[1], vals[0]);

	if (r != 0) {
		(void) fprintf(stderr,
		    "UPDATE moat with dec_id failed: %d, %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * update id_tbl
 *
 * return 0 on success, 1 otherwise
 */
int
update_id_tbl(uint32_t id, enum id_space id_space)
{
	int r;
	char *emsg;
	char const *id_name = id_space_to_name(id_space);

	r = sqlite_exec_printf(g_db,
	    "BEGIN TRANSACTION;"
	    "    UPDATE id_tbl SET id_next = %u WHERE id_name = '%q'; "
	    "COMMIT TRANSACTION;",
	    NULL, NULL, &emsg, id, id_name);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed update id_tbl for %s: %d: %s\n", id_name, r, emsg);
		return (1);
	}

	return (0);
}

/*
 * process each svc_id/[instance_id/]pg_id/prop_name.
 * decoration_ids are stored in the temporary table prop_lnk_tbl_tmp. moat is
 * also updated with decoration_id at the end.
 */
int
populate_lnk_decoration_id(void)
{
	int r;
	char *emsg;
	uint32_t dec_id;
	uint32_t dec_by_name_id;

	dec_id = 1;
	r = sqlite_exec(g_db,
	    "SELECT DISTINCT moat_svc_id, moat_instance_id, moat_lnk_pg_id,"
	    "    moat_prop_name FROM moat;",
	    assign_dec_id_callback, &dec_id, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "assign_decoration_id failed: %d, %s\n", r, emsg);
		return (1);
	}

	/*
	 * Once all prop_lnk_tbl_tmp entries have lnk_decoration_id populated
	 * we update the id_tbl entry for DECOR
	 */
	if (update_id_tbl(dec_id, BACKEND_ID_DECORATION) != 0)
		return (1);

	dec_by_name_id = 1;
	r = sqlite_exec(g_db,
	    "SELECT DISTINCT moat_svc_id, moat_instance_id, moat_pg_name,"
	    "    moat_prop_name FROM moat;",
	    assign_dec_by_name_id_cb, &dec_by_name_id, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "assigning dec_by_name_id failed: %s\n", emsg);
		return (1);
	}

	/*
	 * populate moat_lnk_decoration_id from prop_lnk_tbl_tmp
	 */
	r = sqlite_exec(g_db,
	    "SELECT lnk_prop_id, lnk_decoration_id FROM prop_lnk_tbl_tmp",
	    decorate_moat_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed decorate_moat_callback: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * return decoration_id for a given prop_id
 */
uint32_t
get_dec_id(uint32_t prop_id)
{
	int r;
	char *emsg;
	uint32_t id = 0;
	struct run_single_int_info info;

	info.rs_out = &id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;

	r = sqlite_exec_printf(g_db,
	    "SELECT lnk_decoration_id FROM prop_lnk_tbl_tmp"
	    "    WHERE lnk_prop_id = %u;", run_single_int_callback,
	    &info, &emsg, prop_id);

	/*
	 * Should never return not found
	 */
	if (r != 0 || info.rs_result != REP_PROTOCOL_SUCCESS || id == 0) {
		(void) fprintf(stderr,
		    "get_prop_dec_id failed for prop %u: %d: %s\n", prop_id, r,
		    emsg);
		cleanup_and_bail();
	}

	return (id);

}

/*
 * insert_decoration_entry()
 */
int
insert_decoration_entry(decoration_entry_t *e)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec_printf(g_db,
	    "INSERT INTO decoration_tbl (decoration_key, decoration_id,"
	    "    decoration_entity_type, decoration_value_id, "
	    "    decoration_gen_id, decoration_layer, decoration_bundle_id, "
	    "    decoration_type, decoration_flags)"
	    "    VALUES (%u, %u, '%q', %u, %u, %d, %u, %d, %u);",
	    NULL, NULL, &emsg, e->decoration_key, e->decoration_id,
	    e->decoration_entity_type, e->decoration_value_id,
	    e->decoration_gen_id, e->decoration_layer, e->decoration_bundle_id,
	    e->decoration_type, e->decoration_flags)) != 0) {
		(void) fprintf(stderr,
		    "insert_decoration_entry failed: %d: %s\n", r, emsg);
		(void) fprintf(stderr, "%u, %u, %s, %u, %u, %d, %u, %d, %u\n",
		    e->decoration_key, e->decoration_id,
		    e->decoration_entity_type, e->decoration_value_id,
		    e->decoration_gen_id,
		    e->decoration_layer, e->decoration_bundle_id,
		    e->decoration_type, e->decoration_flags);
		return (1);
	}

	notify_cnt++;

	return (0);
}

/*
 * Create property entry and update lnk_val_decoration_key
 */
int
decorate_prop(prop_dec_t *p)
{
	int r;
	char *emsg;
	decoration_entry_t e = {0};

	e.decoration_key = new_id(BACKEND_KEY_DECORATION);
	assert(e.decoration_key != 0);
	e.decoration_id = get_dec_id(p->prop_id);
	e.decoration_entity_type = p->prop_type;
	e.decoration_value_id = p->value_id;
	e.decoration_gen_id = p->gen_id;
	e.decoration_layer = p->layer;
	e.decoration_bundle_id = p->bundle_id;
	e.decoration_type = DECORATION_TYPE_PROP;
	e.decoration_flags = 0;

	if (insert_decoration_entry(&e) != 0) {
		(void) fprintf(stderr, "lnk_prop_id: %u\n", p->prop_id);
		return (1);
	}

	r = sqlite_exec_printf(g_db,
	    "UPDATE prop_lnk_tbl_tmp SET lnk_val_decoration_key = %u"
	    "    WHERE lnk_prop_id = %u;",
	    NULL, NULL, &emsg, e.decoration_key, p->prop_id);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed UPDATE prop_dec_aux: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * return bundle_id for a given prop_id
 * returns 0 on not found
 */
uint32_t
get_bundle(uint32_t prop_id)
{
	int r;
	char *emsg;
	struct run_single_int_info info;
	uint32_t bundle_id = 0;

	info.rs_out = &bundle_id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db,
	    "SELECT DISTINCT bundle_id FROM prop_snap_lnk"
	    "    INNER JOIN snap_bundle_lnk ON aux2_snap_id = aux_snap_id"
	    "    INNER JOIN bundle_tbl ON bundle_name = aux2_bundle_name"
	    "    WHERE aux_prop_id = %u LIMIT 1;",
	    run_single_int_callback, &info, &emsg, prop_id);
	if (r != 0) {
		(void) fprintf(stderr,
		    "failed retrieving bundle_id for prop: %u: %s\n",
		    prop_id, emsg);
		cleanup_and_bail();
	}

	/*
	 * We found a bundle
	 */
	if (bundle_id != 0)
		return (bundle_id);

	/*
	 * We have not found the bundle in *this* snapshot.
	 * Try its sibiling!
	 *   parent is the instance
	 *   sibilings are 'initial' and 'last-import'
	 *
	 * This should be safe because by now, we should have already
	 * sanitized bundles from the build machine (seed.db). We also
	 * do not have bundles from non-standard location in snap_bundle_lnk
	 */
	info.rs_out = &bundle_id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db,
	    "SELECT DISTINCT bundle_id FROM prop_snap_lnk"
	    "    INNER JOIN snap_bundle_lnk ON aux2_inst_id = aux_inst_id"
	    "        AND aux_snap_id != aux2_snap_id"
	    "    INNER JOIN bundle_tbl ON bundle_name = aux2_bundle_name"
	    "    WHERE aux_prop_id = %u ORDER BY bundle_id LIMIT 1;",
	    run_single_int_callback, &info, &emsg, prop_id);
	if (r != 0) {
		(void) fprintf(stderr,
		    "failed retrieving alternate bundle_id for prop: %u: %s\n",
		    prop_id, emsg);
		cleanup_and_bail();
	}

	return (bundle_id);
}

/*
 * Callback function to decorate properties
 *
 * query columns
 *     lnk_prop_id, lnk_prop_type, lnk_val_id, lnk_pg_id, lnk_gen_id
 *
 * from tables
 *     prop_lnk_tbl
 */
/*ARGSUSED*/
int
decorate_prop_callback(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	struct run_single_int_info info;
	uint32_t n;
	prop_dec_t p = {0};

	assert(columns == 5);

	info.rs_out = &n;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db,
	    "SELECT count() FROM prop_snap_lnk"
	    "    WHERE aux_prop_id = '%q';",
	    run_single_int_callback, &info, &emsg, vals[0]);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed SELECT count() FROM prop_snap_lnk: %d: %s\n", r,
		    emsg);
		return (1);
	}

	string_to_id(vals[0], &p.prop_id, names[0]);
	if (vals[2] == NULL)
		p.value_id = 0;
	else
		string_to_id(vals[2], &p.value_id, names[2]);
	string_to_id(vals[3], &p.pg_id, names[3]);
	string_to_id(vals[4], &p.gen_id, names[4]);
	if (n > 0) {
		p.bundle_id = get_bundle(p.prop_id);
		p.layer = REP_PROTOCOL_DEC_MANIFEST;
	} else {
		/*
		 * Some properties in here may belong in the manifest layer
		 * but that will be sorted out when we search for dependents
		 * and promote properties.
		 */
		p.bundle_id = 0;
		p.layer = REP_PROTOCOL_DEC_ADMIN;
	}

	p.prop_type = vals[1];
	if ((r = decorate_prop(&p)) != 0) {
		(void) fprintf(stderr,
		    "failed decorate_prop: %u val: %u pg: %u gen: %u\n",
		    p.prop_id, p.value_id, p.pg_id, p.gen_id);
		return (1);
	}

	return (0);
}

/*
 * Callback function that updates moat_lnk_val_decoration_key with
 * lnk_val_decoration_key in prop_lnk_tbl_tmp
 *
 * query columns
 *     lnk_prop_id, lnk_val_decoration_key
 *
 */
/*ARGSUSED*/
int
update_moat_dec_key_callback(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;

	assert(columns == 2);

	r = sqlite_exec_printf(g_db,
	    "UPDATE moat SET moat_lnk_val_decoration_key = '%q' \n"
	    "    WHERE moat_prop_id = '%q';",
	    NULL, NULL, &emsg, vals[1], vals[0]);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed UPDATE moat SET "
		    "moat_lnk_val_decoration_key: %d: %s\n", r, &emsg);
		return (1);
	}

	return (0);
}

/*
 * decorate properties
 */
int
decorate_properties(void)
{
	int r;
	char *emsg;

	r = sqlite_exec(g_db,
	    "SELECT lnk_prop_id, lnk_prop_type, lnk_val_id, lnk_pg_id,"
	    "    lnk_gen_id FROM prop_lnk_tbl ORDER BY lnk_prop_id",
	    decorate_prop_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed decorate_prop_callback: %d: %s\n", r, emsg);
		return (1);
	}

	/*
	 * Now we update moat_lnk_val_decoration_key from prop_lnk_tbl_tmp
	 */
	r = sqlite_exec(g_db,
	    "SELECT lnk_prop_id, lnk_val_decoration_key FROM prop_lnk_tbl_tmp;",
	    update_moat_dec_key_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed update_moat_dec_key_callback: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

int
prop_get_val_id(uint32_t prop_id, uint32_t *val_id)
{
	int r;
	char *emsg;
	struct run_single_int_info info;

	info.rs_out = val_id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db,
	    "SELECT lnk_val_id FROM prop_lnk_tbl_tmp"
	    "    WHERE lnk_prop_id = %u;",
	    run_single_int_callback, &info, &emsg, prop_id);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed lookup of val_id for prop_id: %u: %s\n", prop_id,
		    emsg);
		return (1);
	} else if (info.rs_result == REP_PROTOCOL_FAIL_NOT_FOUND) {
		/*
		 * This means that lnk_val_id is NULL, so we set *val_id = 0
		 */
		*val_id = 0;
	}

	return (0);
}

/*
 * get number of values from value_tbl for a given value_id
 *
 * return 0 on success, 1 on error
 */
int
get_val_count(sqlite *db, uint32_t value_id, uint32_t *max_order)
{
	int r;
	char *emsg;
	struct run_single_int_info info;

	info.rs_out = max_order;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(db,
	    "SELECT count() FROM value_tbl WHERE value_id = %u",
	    run_single_int_callback, &info, &emsg, value_id);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed select value_id count(): %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * compare by value, not value_id
 *
 * return 0 on match, 1 on not match, -1 on error
 */
int
cmp_by_val(uint32_t l_val, uint32_t r_val)
{
	int r;
	char *emsg;
	uint32_t l_count, r_count, match_count;
	struct run_single_int_info info;

	if (get_val_count(g_db, l_val, &l_count) != 0 ||
	    get_val_count(g_db, r_val, &r_count) != 0)
		return (-1);

	if (l_count != r_count)
		return (1);

	info.rs_out = &match_count;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	/*
	 * Tell me how many values are equal
	 */
	r = sqlite_exec_printf(g_db,
	    "SELECT count() FROM value_tbl AS A INNER JOIN value_tbl AS B"
	    "    ON A.value_order = B.value_order"
	    "        AND A.value_value = B.value_value"
	    "    WHERE A.value_id = %u AND B.value_id = %u;",
	    run_single_int_callback, &info, &emsg, l_val, r_val);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed compare by value query: %s\n"
		    "val_id(L): %u, val_id(R): %u\n", emsg, l_val, r_val);
		return (-1);
	}

	if (match_count != l_count)
		return (1);

	return (0);
}

/*
 * compares the values of two prop_ids
 *
 * return 0 if values match, 1 otherwise, -1 on error
 */
int
val_cmp(uint32_t l_prop, uint32_t r_prop)
{
	uint32_t l_val, r_val;

	if (prop_get_val_id(l_prop, &l_val) == 1 ||
	    prop_get_val_id(r_prop, &r_val) == 1)
		return (-1);

	if (l_val == r_val) /* shortcut for match */
		return (0);

	return (cmp_by_val(l_val, r_val));
}

/*
 * populate fields bundle_id and layer of dec_update_t with values
 * in the columns from the query
 *
 * columns:
 *     decoration_bundle_id, decoration_layer, lnk_prop_id
 *
 * arg:
 *     dec_update_t *
 *
 */
/*ARGSUSED*/
int
bundle_and_layer_callback(void *arg, int columns, char **vals, char **names)
{
	uint32_t layer;
	dec_update_t *p = (dec_update_t *)arg;

	assert(columns == 3);

	string_to_id(vals[0], &p->bundle_id, names[0]);
	string_to_id(vals[1], &layer, names[1]);

	p->layer = (rep_protocol_decoration_layer_t)layer;

	return (0);
}

/*
 * get bundle id and layer from decoration_tbl for a given prop_id
 *
 * return 0 on success or 1 on failure
 */
int
get_bundle_and_layer(dec_update_t *p, uint32_t prop_id)
{
	int r;
	char *emsg;

	r = sqlite_exec_printf(g_db,
	    "SELECT decoration_bundle_id, decoration_layer, lnk_prop_id"
	    "    FROM prop_lnk_tbl_tmp"
	    "    INNER JOIN decoration_tbl"
	    "        ON lnk_val_decoration_key = decoration_key"
	    "    WHERE lnk_prop_id = %u;",
	    bundle_and_layer_callback, p, &emsg, prop_id);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed lookup bundle and layer: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * update the decoration entry pointed by dec_key
 *
 * return 0 on success, 1 on failure
 */
int
update_decoration_entry(dec_update_t *p)
{
	int r;
	char *emsg;

	r = sqlite_exec_printf(g_db,
	    "UPDATE decoration_tbl SET"
	    "    decoration_bundle_id = %u, decoration_layer = %d"
	    "    WHERE"
	    "        decoration_key = %u;",
	    NULL, NULL, &emsg, p->bundle_id, p->layer,
	    p->dec_key);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed update decoration_tbl entry: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * Compare the values associated with two properties. One from the query
 * (lnk_prop_id in vals[0]) and one from arg (p->prop_id).
 *
 * Sets p->match = 1 if we find a match.
 *
 * query columns
 *    0 lnk_prop_id for the property that may have our layer and bundle
 *    1 decoration_bundle_id
 *    2 decoration_layer
 *
 * arg
 *    snap_at_prop
 */
/*ARGSUSED*/
int
compare_and_update_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	uint32_t prop_id, bundle_id, layer;
	prop_at_snap_t *p = arg;

	if (p-> match != 0) /* we already have a match */
		return (0);

	string_to_id(vals[0], &prop_id, names[0]);
	string_to_id(vals[1], &bundle_id, names[1]);
	string_to_id(vals[2], &layer, names[2]);

	r = val_cmp(p->prop_id, prop_id);
	switch (r) {
	case 0:
		/* we have a match! */
		p->match = 1;
		p->dec_update->bundle_id = bundle_id;
		p->dec_update->layer = (rep_protocol_decoration_layer_t)layer;
		/*FALLTHRU*/
	case 1:
		break;
	default:
		/*
		 * We had an sqlite failure while doing the comparison.
		 * Error message already issued.
		 */
		return (1);

	}

	return (0);
}
/*
 * Callback function that promotes a property decoration to MANIFEST layer
 * if value matches the property value at initial or last-import snapshot.
 *
 * general/enabled properties are special and the query calling this callback
 * function should take care to exclude general/enabled. They are special for
 * two reasons. First because startd looks at the directly attached
 * general/enabled of an instance, not the running snapshot value. Second,
 * because matching the value in the initial or last-import snapshot does not
 * mean the property should be decorated at the manifest layer as the following
 * example shows
 *
 *    manifest: general/enabled = false
 *    system-profile: general/enabled = true
 *    admin has run svcadm disabled <svc>
 *
 * In the case above, the instance would be disable because svcadm disable
 * will not refresh the instance and stard will get the disabled state from
 * the directly attached general/enabled. However,
 *
 *    svccfg -s <inst> listprop -l all
 *
 * would show the system profile as the top most layer property value,
 * giving consumers the wrong impression that the instance should be enabled.
 *
 * query columns
 *     0 aux3_prop_id
 *     1 aux3_dec_by_name_id
 *     2 aux3_val_dec_key
 *
 */
/*ARGSUSED*/
int
promote_properties_callback(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	uint32_t dec_by_name_id;
	prop_at_snap_t p = {0};
	dec_update_t d = {0};

	assert(columns == 3);

	string_to_id(vals[0], &p.prop_id, names[0]);
	string_to_id(vals[1], &dec_by_name_id, names[1]);
	string_to_id(vals[2], &d.dec_key, names[2]);

	p.dec_update = &d;

	/*
	 * Although it can be more efficient to lookup 1st on the initial
	 * snapshot, because comparison has the potential of matching by
	 * value_id instead of matching by value_value, the last-import
	 * snapshot has the latest bundle_name and we want the latest bundle
	 * that delivered the property.
	 *
	 * 1st we look for a property at the last-import snapshot
	 */
	r = sqlite_exec_printf(g_db,
	    "SELECT DISTINCT lnk_prop_id, decoration_bundle_id,"
	    "    decoration_layer FROM prop_lnk_tbl_tmp"
	    "    INNER JOIN prop_snap_lnk ON lnk_prop_id = aux_prop_id"
	    "    INNER JOIN decoration_tbl"
	    "        ON lnk_val_decoration_key = decoration_key"
	    "    WHERE"
	    "        lnk_dec_by_name_id = %u"
	    "        AND aux_snap_name = '%q' ORDER BY lnk_gen_id;",
	    compare_and_update_cb, &p, &emsg, dec_by_name_id, "last-import");
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed last-import snapshot lookup: %s\n", emsg);
		return (1);
	}

	if (p.match == 1) {
		if (update_decoration_entry(&d) != 0)
			return (1);
		return (0);
	}

	/*
	 * 2nd we look for a property at the initial snapshot
	 */
	r = sqlite_exec_printf(g_db,
	    "SELECT DISTINCT lnk_prop_id, decoration_bundle_id,"
	    "    decoration_layer FROM prop_lnk_tbl_tmp"
	    "    INNER JOIN prop_snap_lnk ON lnk_prop_id = aux_prop_id"
	    "    INNER JOIN decoration_tbl"
	    "        ON lnk_val_decoration_key = decoration_key"
	    "    WHERE"
	    "        lnk_dec_by_name_id = %u"
	    "        AND aux_snap_name = '%q' ORDER BY lnk_gen_id;",
	    compare_and_update_cb, &p, &emsg, dec_by_name_id, "initial");
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed initial snapshot lookup: %s\n", emsg);
		return (1);
	}

	if (p.match == 1) {
		if (update_decoration_entry(&d) != 0)
			return (1);
	}

	return (0);
}

/*
 * promote_properties
 * For properties that are at the ADMIN layer, check if they match by value
 * a property on the initial or last-import snapshot.
 * If so, update (promote) layer and bundle_id to match the one in the snapshot.
 */
int
promote_properties(void)
{
	int r;
	char *emsg;

	/*
	 * First we populate prop_dec_tbl with prop_id, dec_id and val_dec_key
	 * for all properties decorated at the ADMIN layer and at the MANIFEST
	 * layer and bundle_id = 0. Since we don't have any property decorated
	 * at a layer different than ADMIN and MANIFEST and all decorations at
	 * ADMIN layer have bundle_id = 0. It is simpler to just query for
	 * bundle_id = 0.
	 * We should exclude general/enabled from this query so we don't
	 * decorate at manifest layer what was an admin customization. E. g.
	 *    manifest: general/enabled = false
	 *    system-profile: general/enabled = true
	 *    admin has run svcadm disabled <svc>
	 *
	 * In the example above, if we don't exclude general/enabled from this
	 * query, we will end up with the admin customization in the manifest
	 * layer.
	 */
	r = sqlite_exec(g_db,
	    "INSERT INTO prop_dec_tbl (aux3_prop_id, aux3_dec_by_name_id,"
	    "    aux3_val_dec_key)"
	    "    SELECT lnk_prop_id, lnk_dec_by_name_id, lnk_val_decoration_key"
	    "        FROM decoration_tbl"
	    "        INNER JOIN prop_lnk_tbl_tmp"
	    "            ON lnk_decoration_id = decoration_id AND"
	    "                lnk_gen_id = decoration_gen_id"
	    "        WHERE decoration_bundle_id = 0 AND"
	    "            lnk_prop_id NOT IN (SELECT moat_prop_id FROM moat"
	    "                WHERE moat_pg_name = 'general' AND"
	    "                    moat_prop_name = 'enabled');",
	    NULL, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed INSERT INTO prop_dec_tbl: %d: %s\n", r, emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "SELECT aux3_prop_id, aux3_dec_by_name_id, aux3_val_dec_key"
	    "    FROM prop_dec_tbl",
	    promote_properties_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed promote_properties_callback: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * callback to populate dec_id and gen_id in prop_snap_lnk
 *
 * query columns
 *     0 moat_prop_id
 *     1 moat_lnk_decoration_id
 *     2 moat_lnk_gen_id
 *     3 moat_lnk_val_id
 */
/*ARGSUSED*/
int
update_prop_snap_lnk_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;

	assert(columns == 4);

	r = sqlite_exec_printf(g_db,
	    "UPDATE prop_snap_lnk SET aux_dec_id = '%q', aux_gen_id = '%q',"
	    "    aux_val_id = '%q' WHERE aux_prop_id = '%q';",
	    NULL, NULL, &emsg, vals[1], vals[2], vals[3], vals[0]);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed to update prop_snap_lnk: prop_id: %s: %s\n",
		    vals[0], emsg);
		return (1);
	}

	return (0);
}

/*
 * Migrate bundle_id for decoration_tbl entry defined by dec_id and gen_id.
 * The property id is used only in the error message.
 */
int
update_bundle_id(uint32_t prop_id, uint32_t good_bundle_id, uint32_t dec_id,
    uint32_t gen_id, uint32_t bad_bundle_id)
{
	int r;
	char *emsg;

	r = sqlite_exec_printf(g_db,
	    "UPDATE decoration_tbl SET decoration_bundle_id = %u"
	    "    WHERE decoration_id = %u"
	    "    AND decoration_gen_id = %u"
	    "    AND decoration_bundle_id = %u;", NULL, NULL, &emsg,
	    good_bundle_id, dec_id, gen_id, bad_bundle_id);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed to migrate prop_id: %u: to bundle %u: %s\n",
		    prop_id, good_bundle_id, emsg);
		return (1);
	}

	return (0);
}

/*
 * callback for migrating properties to a bundle expected to be on the
 * filesystem.
 *
 * query columns
 *     bad bundle
 *     0 prop_id
 *     1 lnk_val_id
 *     2 lnk_gen_id
 *
 *     common
 *     3 dec_id
 *
 *     good bundle
 *     4 prop_id
 *     5 lnk_val_id
 *
 * arg
 *     bad_bundle_id
 *     good_bundle_id
 */
/*ARGSUSED*/
int
prop_migrate_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	two_bundles_t *p = arg;
	uint32_t prop_id, bad_val_id, good_val_id, dec_id, gen_id;

	assert(columns == 6);

	string_to_id(vals[0], &prop_id, names[0]);
	string_to_id(vals[1], &bad_val_id, names[1]);
	string_to_id(vals[2], &gen_id, names[2]);
	string_to_id(vals[3], &dec_id, names[3]);
	string_to_id(vals[5], &good_val_id, names[5]);

	if (bad_val_id == good_val_id ||
	    cmp_by_val(bad_val_id, good_val_id) == 0) {
		if (update_bundle_id(prop_id, p->good_bundle_id, dec_id, gen_id,
		    p->bad_bundle_id) != 0) {
			return (1);
		}
	} else {
		/*
		 * Update decoration_layer to REP_PROTOCOL_DEC_ADMIN
		 */
		r = sqlite_exec_printf(g_db,
		    "UPDATE decoration_tbl"
		    "    SET decoration_layer = %d"
		    "    WHERE decoration_id = %u"
		    "    AND decoration_gen_id = %u"
		    "    AND decoration_bundle_id = %u;", NULL, NULL, &emsg,
		    REP_PROTOCOL_DEC_ADMIN, dec_id, gen_id, p->bad_bundle_id);
		if (r != 0) {
			(void) fprintf(stderr,
			    "Failed to update decoration_layer. prop_id: "
			    "%u: %s\n", prop_id, emsg);
			return (1);
		}
	}

	return (0);
}

/*
 * callback that checks and triggers bundle_id migration based on bundle path
 *
 * query columns
 *     initial snapshot
 *     0 snap_id
 *     1 bundle_id
 *     2 bundle_name
 *
 *     last-import snapshot
 *     3 snap_id
 *     4 bundle_id
 *     5 bundle_name
 *
 */
/*ARGSUSED*/
int
bundle_id_migration_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	int i_stdloc;
	int li_stdloc;
	uint32_t i_bundle_id;
	uint32_t li_bundle_id;
	two_bundles_t p = {0};

	assert(columns == 6);
	assert(vals[2] != NULL && vals[5] != NULL);

	string_to_id(vals[1], &i_bundle_id, names[1]);
	string_to_id(vals[4], &li_bundle_id, names[4]);

	i_stdloc = IS_STD_LOC(vals[2]);
	li_stdloc = IS_STD_LOC(vals[5]);

	/*
	 * The cases where the bundle from the last-import snapshot is from a
	 * standard location have already been handled by code in
	 * populate_bundle_tbl
	 */
	if (i_stdloc && !li_stdloc) {
		/*
		 * The service was first defined from a standard location
		 * manifest and later modified with changes from a manifest in
		 * a non-standard location. We should migrate the properties
		 * with matching values to the initial snapshot bundle.
		 *
		 * bad snapshot: vals[3]
		 * good snapshot: vals[0]
		 *
		 * bad bundle_id: li_bundle_id
		 * good bundle_id: i_bundle_id
		 */
		p.bad_bundle_id = li_bundle_id;
		p.good_bundle_id = i_bundle_id;
		r = sqlite_exec_printf(g_db,
		    "SELECT l.aux_prop_id, l.aux_val_id, l.aux_gen_id,"
		    "    l.aux_dec_id, r.aux_prop_id, r.aux_val_id"
		    "    FROM prop_snap_lnk l"
		    "    INNER JOIN prop_snap_lnk r"
		    "        ON l.aux_dec_id = r.aux_dec_id"
		    "    WHERE l.aux_snap_id = '%q' AND r.aux_snap_id = '%q';",
		    prop_migrate_cb, &p, &emsg, vals[3], vals[0]);
		if (r != 0) {
			(void) fprintf(stderr, "Failed prop_migrate_cb: %s\n",
			    emsg);
			return (1);
		}
	} else {
		/*
		 * Both the initial and last-import snapshots are from
		 * non-standard locations. Update decoration_layer to
		 * REP_PROTOCOL_DEC_ADMIN.
		 */
		r = sqlite_exec_printf(g_db,
		    "UPDATE decoration_tbl"
		    "    SET decoration_layer = %d"
		    "    WHERE decoration_key IN (SELECT decoration_key"
		    "        FROM decoration_tbl INNER JOIN prop_lnk_tbl_tmp"
		    "            ON lnk_decoration_id = decoration_id AND"
		    "               lnk_gen_id = decoration_gen_id"
		    "        INNER JOIN prop_snap_lnk"
		    "            ON lnk_prop_id = aux_prop_id"
		    "        WHERE  aux_snap_id IN ('%q', '%q'));",
		    NULL, NULL, &emsg, REP_PROTOCOL_DEC_ADMIN, vals[1],
		    vals[4]);
		if (r != 0) {
			(void) fprintf(stderr,
			    "Failed to update decoration_layer. prop_id: "
			    "%s: %s\n", vals[0], emsg);
			return (1);
		}
	}

	return (0);
}

/*
 * Migrate bundles
 *
 * The initial snapshot may contain a bundle that no longer exists on the
 * system. For instance, the bundle from the seed repository or if the bundle
 * has been moved (from /var/svc/manifests to /lib/svc/manifests).
 *
 * We identify which bundles are different in the initial and last-import
 * snapshots in the query with bundle_id_migration_cb callback.
 *
 * The bundle migration has the following logic:
 *
 * A bad bundle is a bundle that does not come from a standard location while
 * a good bundle comes from a standard location.
 *
 * We call migrate the update of the decoration_bundle_id in the decoration
 * entry for a property with the bundle_id of the good bundle. We only migrate
 * properties that have the same values in both snapshots, initial and
 * last-import.
 *
 * If bundle from last-import is good
 *     migrate properties from the initial snapshot.
 *     This is actually done in populate_bundle_tbl.
 * else if bundle from initial is good
 *     migrate properties from the last-import snapshot
 * else
 *     update decoration_layer to REP_PROTOCOL_DEC_ADMIN
 *
 */
int
bundle_id_migration(void)
{
	int r;
	char *emsg;

	/*
	 * First populate dec_id and gen_id in prop_snap_lnk
	 */
	r = sqlite_exec(g_db,
	    "SELECT moat_prop_id, moat_lnk_decoration_id, moat_lnk_gen_id,"
	    "    moat_lnk_val_id FROM moat;",
	    update_prop_snap_lnk_cb, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr, "Failed to update prop_snap_lnk: %s\n",
		    emsg);
		return (1);
	}

	/*
	 * Select the service/instances with initial and last-import snapshots
	 * populated by different bundles.
	 */
	r = sqlite_exec(g_db,
	    "SELECT DISTINCT l.aux2_snap_id, lb.bundle_id, l.aux2_bundle_name,"
	    "    r.aux2_snap_id, rb.bundle_id, r.aux2_bundle_name"
	    "    FROM snap_bundle_lnk l"
	    "    INNER JOIN snap_bundle_lnk r"
	    "        ON l.aux2_inst_id = r.aux2_inst_id"
	    "    INNER JOIN bundle_tbl lb"
	    "        ON l.aux2_bundle_name = lb.bundle_name"
	    "    INNER JOIN bundle_tbl rb"
	    "        ON r.aux2_bundle_name = rb.bundle_name"
	    "    WHERE l.aux2_bundle_name <> r.aux2_bundle_name"
	    "        AND l.aux2_snap_name = 'initial';", bundle_id_migration_cb,
	    NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr, "Failed bundle_id_migration: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * Update decoration_layer to REP_PROTOCOL_DEC_ADMIN for all decorations at
 * manifest layer with bundle_id = 0 or with a bundle from a non-standard
 * location.
 */
int
update_decoration_layer(void)
{
	int r;
	char *emsg;

	/*
	 * Update decoration_layer to REP_PROTOCOL_DEC_ADMIN for decoration
	 * entries at manifest layer and a bundle_id that is not at a standard
	 * location
	 */
	r = sqlite_exec_printf(g_db,
	    "UPDATE decoration_tbl"
	    "    SET decoration_layer = %d"
	    "    WHERE decoration_key IN (SELECT decoration_key FROM bundle_tbl"
	    "        INNER JOIN decoration_tbl"
	    "            ON decoration_bundle_id = bundle_id"
	    "        WHERE bundle_name NOT LIKE '%s%%'"
	    "            AND bundle_name NOT LIKE '%s%%'"
	    "            AND decoration_layer = %d);",
	    NULL, NULL, &emsg, REP_PROTOCOL_DEC_ADMIN, LIB_M, VAR_M,
	    REP_PROTOCOL_DEC_MANIFEST);
	if (r != 0) {
		(void) fprintf(stderr, "Failed to flag decoration entry: %s\n",
		    emsg);
		return (1);
	}

	/*
	 * Update decoration_layer to REP_PROTOCOL_DEC_ADMIN for decoration
	 * entries at manifest layer and no bundle (bundle_id = 0).
	 */
	r = sqlite_exec_printf(g_db,
	    "UPDATE decoration_tbl"
	    "    SET decoration_layer = %d"
	    "    WHERE decoration_layer = %d AND decoration_bundle_id = 0;",
	    NULL, NULL, &emsg, REP_PROTOCOL_DEC_ADMIN,
	    REP_PROTOCOL_DEC_MANIFEST);
	if (r != 0) {
		(void) fprintf(stderr, "Failed to flag decoration entry: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * Populate deathrow table with properties that will be deleted.
 *
 * query columns
 *     0 snap_id (matches snapshot_lnk_tbl lnk_id)
 *     1 svc_id
 *     2 inst_id
 *     3 prop_id
 *     4 lnk_pg_id
 *     5 lnk_gen_id
 *     6 prop_name
 *     7 prop_type
 *     8 lnk_val_id
 *     9 lnk_decoration_id
 */
/*ARGSUSED*/
int
dr_dup_bundle_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	uint32_t skip = 0;
	struct run_single_int_info info;

	assert(columns == 10);

	info.rs_out = &skip;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	/*
	 * We have to skip the property which the prop_name matches prop_name
	 * of the manifestfiles property in the last-import snapshot of the
	 * same instance.
	 */
	r = sqlite_exec_printf(g_db,
	    "SELECT count() FROM prop_snap_lnk"
	    "    INNER JOIN moat ON moat_prop_id = aux_prop_id"
	    "    WHERE aux_inst_id = '%q' AND aux_snap_name = 'last-import'"
	    "        AND moat_pg_name = 'manifestfiles'"
	    "        AND moat_prop_name = '%q';",
	    run_single_int_callback, &info, &emsg, vals[2], vals[6]);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed look up last-import snapshot for prop %s: %s\n",
		    vals[6], emsg);
		return (1);
	}

	if (skip > 0)
		return (0);

	r = sqlite_exec_printf(g_db,
	    "INSERT INTO dr_prop_tbl (dr_snap_id, dr_svc_id, dr_inst_id,"
	    "    dr_prop_id, dr_pg_id, dr_gen_id, dr_prop_name, dr_prop_type,"
	    "    dr_val_id, dr_dec_id) values ('%q', '%q', '%q', '%q', '%q',"
	    "    '%q', '%q','%q', '%q', '%q');", NULL, NULL, &emsg, vals[0],
	    vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7],
	    vals[8], vals[9]);
	if (r != 0) {
		(void) fprintf(stderr, "Failed insert into dr_prop_tbl: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * When a second manifest delivers a new instance to an existing service, we
 * get a duplicate bundle property in the manifestfiles pg of the initial
 * snapshot of the new instance.
 *
 * E. g.
 *
 * Manifest A.xml delivers service S and instance I1.
 * The initial and last-import snapshots for I1 has
 *     manifestfiles/A_xml
 *
 * Manifest B.xml (imported after A.xml) delivers instance I2.
 * The initial snapshot for I2 has
 *     manifestfiles/A_xml
 *     manifestfiles/B_xml
 * and the last-import snapshot for I2 has
 *     manifestfiles/B_xml
 *
 * The entry for manifestfiles/A_xml in the snapshot for I2 needs to go away so
 * the properties belonging to I2 are correctly decorated with B.xml bundle.
 */
int
dr_dup_bundles(void)
{
	int r;
	char *emsg;

	/*
	 * This query return all rows in moat for properties in property
	 * group manifestfiles that have more than one property and belong
	 * to an initial snapshot.
	 */
	r = sqlite_exec(g_db,
	    "SELECT aux_snap_id, moat_svc_id, aux_inst_id, moat_prop_id,"
	    "    moat_lnk_pg_id, moat_lnk_gen_id, moat_prop_name,"
	    "    moat_lnk_prop_type, moat_lnk_val_id, moat_lnk_decoration_id"
	    "    FROM moat"
	    "    INNER JOIN prop_snap_lnk ON moat_prop_id = aux_prop_id"
	    "    WHERE moat_pg_name = 'manifestfiles' AND aux_snap_id IN ("
	    "        SELECT aux_snap_id FROM (SELECT aux_snap_id, count() C"
	    "            FROM moat INNER JOIN prop_snap_lnk"
	    "                ON moat_prop_id = aux_prop_id"
	    "            WHERE moat_pg_name = 'manifestfiles'"
	    "                AND aux_snap_name = 'initial'"
	    "                GROUP BY aux_snap_id) WHERE C > 1);",
	    dr_dup_bundle_cb, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed look up of multiple bundles in 'initial' snapshot: "
		    "%s\n", emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "DELETE FROM moat WHERE moat_prop_id IN ("
	    "    SELECT dr_prop_id FROM dr_prop_tbl);"
	    "DELETE FROM prop_lnk_tbl_tmp WHERE lnk_prop_id IN ("
	    "    SELECT dr_prop_id FROM dr_prop_tbl);"
	    "DELETE FROM prop_lnk_tbl WHERE lnk_prop_id IN ("
	    "    SELECT dr_prop_id FROM dr_prop_tbl);",
	    NULL, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed DELETE of duplicate bundle properties: %s\n", emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "INSERT INTO dr_value_tbl SELECT * FROM value_tbl"
	    "    WHERE value_id NOT IN (SELECT lnk_val_id FROM prop_lnk_tbl);"
	    "DELETE FROM value_tbl WHERE value_id NOT IN ("
	    "    SELECT lnk_val_id FROM prop_lnk_tbl);", NULL, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed to cleanup value_tbl: %s\n", emsg);
		return (1);
	}

	return (0);
}

/*
 * When the bundle in the last-import snapshot is different than the bundle in
 * the initial snapshot AND the bundle in the last-import snapshot comes from
 * a standard location, we have either a migration of manifest such as
 * /var/svc/manifest/ -> /lib/svc/manifest/ or the initial bundle comes from
 * the seed repository. There is also the very corner case of someone developing
 * a service and importing from one location during development and deploying
 * in a different (standard) location. The corner case is also handled here.
 *
 * In the cases defined above, we use the bundle from the last-import snapshot
 * to decorate the properties belonging to the initial snapshot so that they
 * can be upgraded correctly when the manifest changes.
 *
 * query columns
 *     initial snapshot
 *     0 snap_id
 *     1 bundle_name
 *     2 bundle_val_id
 *     3 bundle_dec_id
 *
 *     last-import snapshot
 *     4 snap_id
 *     5 bundle_name
 *     6 bundle_val_id
 *     7 bundle_dec_id
 *
 */
/*ARGSUSED*/
int
insert_bundle_migration_tbl_cb(void *arg, int columns, char **vals,
    char **names)
{
	int r;
	char *emsg;
	int i_stdloc, li_stdloc;
	struct stat st;

	assert(columns == 8);
	assert(vals[1] != NULL && vals[5] != NULL);

	i_stdloc = IS_STD_LOC(vals[1]);
	li_stdloc = IS_STD_LOC(vals[5]);

	/*
	 * If the bundle from the initial snapshot is from standard location,
	 * we only migrate if it is no longer on the filesystem.
	 */
	if (i_stdloc && stat(vals[1], &st) == 0)
		return (0);

	if (li_stdloc) {
		/*
		 * initial snapshot bundle is a bad bundle
		 * last-import snapshot bundle is a good bundle
		 */
		r = sqlite_exec_printf(g_db,
		    "INSERT INTO bundle_migration_tbl"
		    "    (bundle_bad, bundle_bad_val_id, bundle_bad_dec_id,"
		    "    bundle_good, bundle_good_val_id, bundle_good_dec_id)"
		    "    VALUES('%q', '%q', '%q', '%q', '%q', '%q');",
		    NULL, NULL, &emsg, vals[1], vals[2], vals[3], vals[5],
		    vals[6], vals[7]);
		if (r != 0) {
			(void) fprintf(stderr,
			    "Failed to insert into budle_migration_tbl: %s\n",
			    emsg);

			return (1);
		}
	}

	return (0);
}

/*
 * Swap bad_bundle with good bundle in auxiliary table snap_bundle_lnk
 *
 * query columns
 *     0 bad bundle name
 *     1 bad bundle val id
 *     2 bad bundle dec id
 *     3 good bundle name
 *     4 good bundle val id
 */
/*ARGSUSED*/
int
update_snap_bundle_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;

	assert(columns == 5);

	/*
	 * update bad_bundle_name with good_bundle_name in snap_bundle_lnk
	 */
	r = sqlite_exec_printf(g_db,
	    "UPDATE snap_bundle_lnk SET aux2_bundle_name = '%q'"
	    "    WHERE aux2_bundle_name = '%q';",
	    NULL, NULL, &emsg, vals[3], vals[0]);
	if (r != 0) {
		(void) fprintf(stderr, "Failed update snap_bundle_lnk: %s\n",
		    emsg);
		return (1);
	}

	/*
	 * Destructive action.
	 * We are replacing the value_tbl row that contains the bad bundle name
	 * with the good bundle name.
	 */
	r = sqlite_exec_printf(g_db,
	    "UPDATE value_tbl SET value_value = '%q'"
	    "    WHERE value_order = 0 AND"
	    "    value_id IN (SELECT moat_lnk_val_id FROM moat"
	    "        WHERE moat_lnk_decoration_id = '%q');",
	    NULL, NULL, &emsg, vals[3], vals[2]);
	if (r != 0) {
		(void) fprintf(stderr, "Failed update snap_bundle_lnk: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * populate bundle_tbl
 */
int
populate_bundle_tbl(void)
{
	int r;
	char *emsg;
	uint32_t bundle_id = 0;
	struct run_single_int_info info;

	r = sqlite_exec(g_db,
	    "INSERT INTO snap_bundle_lnk (aux2_snap_id, aux2_inst_id,"
	    "    aux2_snap_name, aux2_bundle_val_id, aux2_bundle_name,"
	    "    aux2_dec_id)"
	    "    SELECT DISTINCT aux_snap_id, aux_inst_id, aux_snap_name,"
	    "        value_id, value_value, moat_lnk_decoration_id FROM moat"
	    "        INNER JOIN value_tbl ON moat_lnk_val_id = value_id"
	    "        INNER JOIN prop_snap_lnk ON moat_prop_id = aux_prop_id"
	    "    WHERE moat_pg_name = 'manifestfiles';",
	    NULL, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed INSERT INTO snap_bundle_tbl: %d: %s\n", r, emsg);
		return (1);
	}

	/*
	 * Select the service/instances with initial and last-import snapshots
	 * populated by different bundles.
	 */
	r = sqlite_exec(g_db,
	    "SELECT DISTINCT"
	    "    l.aux2_snap_id, l.aux2_bundle_name, l.aux2_bundle_val_id,"
	    "    l.aux2_dec_id,"
	    "    r.aux2_snap_id, r.aux2_bundle_name, r.aux2_bundle_val_id,"
	    "    r.aux2_dec_id"
	    "    FROM snap_bundle_lnk l"
	    "    INNER JOIN snap_bundle_lnk r"
	    "        ON l.aux2_inst_id = r.aux2_inst_id"
	    "    WHERE l.aux2_bundle_name != r.aux2_bundle_name"
	    "        AND l.aux2_snap_name = 'initial'"
	    "        AND r.aux2_snap_name = 'last-import';",
	    insert_bundle_migration_tbl_cb, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed update_inital_snap_bundle_cb: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "SELECT bundle_bad, bundle_bad_val_id, bundle_bad_dec_id,"
	    "    bundle_good, bundle_good_val_id FROM bundle_migration_tbl",
	    update_snap_bundle_cb, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr, "Failed update_snap_bundle_cb: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "INSERT INTO bundle_tbl (bundle_name)"
	    "    SELECT DISTINCT aux2_bundle_name FROM snap_bundle_lnk",
	    NULL, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "failed INSERT INTO bundle_tbl: %d: %s\n", r, emsg);
		return (1);
	}

	info.rs_out = &bundle_id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec(g_db,
	    "SELECT max(bundle_id) FROM bundle_tbl",
	    run_single_int_callback, &info, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "failed to get max(bundle_id); %d: %s\n", r, emsg);
		return (1);
	}
	bundle_id++;  /* we store the _next_ available id */

	if (update_id_tbl(bundle_id, BACKEND_ID_BUNDLE) != 0)
		return (1);

	return (0);
}

/*
 * callback to update a decoration_tbl entry
 *
 * query columns
 *     lnk_val_decoration_key
 *
 * arg
 *     dec_update_t
 */
/*ARGSUSED*/
int
update_decoration_entry_callback(void *arg, int columns, char **vals,
    char **names)
{
	dec_update_t *p = (dec_update_t *)arg;

	assert(columns == 1);

	string_to_id(vals[0], &p->dec_key, names[0]);

	return (update_decoration_entry(p));
}

/*
 * callback to process dependents created by manifests
 *
 * query columns
 *     lnk_prop_id
 *     lnk_prop_name (dependency pg name),
 *     value_value (full fmri of the dependent),
 *
 * arg
 *     dec_update_t
 */
/*ARGSUSED*/
int
link_dependents_callback(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	uint32_t prop_id;
	char *fmri;
	char *inst;
	int prefix_len = strlen(SVC_PREFIX);
	dec_update_t *p = (dec_update_t *)arg;

	assert(columns == 3);

	string_to_id(vals[0], &prop_id, names[0]);
	if (get_bundle_and_layer(p, prop_id) != 0)
		return (1);

	/*
	 * if bundle_id is 0, the service with the dependent element in its
	 * manifest has no instance and therefore no initial or last-import
	 * snapshot
	 */
	if (p->bundle_id == 0)
		return (0);

	fmri = vals[2];		/* fmri of dependent */

	if (strncmp(fmri, SVC_PREFIX, prefix_len) != 0) {
		/* Bail, this is not an fmri we can handle */
		(void) fprintf(stderr, "Skip unknown fmri: %s\n", fmri);
		return (0);
	}

	fmri += prefix_len;
	inst = strchr(fmri, ':');
	if (inst != NULL) {
		*inst++ = '\0';
	}

	if (inst != NULL) {
		if ((r = sqlite_exec_printf(g_db,
		    "SELECT moat_lnk_val_decoration_key FROM moat"
		    "    WHERE"
		    "        moat_svc_name = '%q' AND"
		    "        moat_instance_name = '%q' AND"
		    "        moat_pg_name = '%q' AND"
		    "        moat_pg_type = 'dependency';",
		    update_decoration_entry_callback, p, &emsg, fmri, inst,
		    vals[1])) != 0) {
			(void) fprintf(stderr,
			    "update_deconation_entry_callback failed for "
			    "instance %s: %d: %s\n", vals[1], r, emsg);
			return (1);
		}
	} else {
		if ((r = sqlite_exec_printf(g_db,
		    "SELECT moat_lnk_val_decoration_key FROM moat"
		    "    WHERE"
		    "        moat_svc_name = '%q' AND"
		    "        moat_pg_name = '%q' AND"
		    "        moat_pg_type = 'dependency';",
		    update_decoration_entry_callback, p, &emsg, fmri,
		    vals[1])) != 0) {
			(void) fprintf(stderr,
			    "update_decoration_entry_callback failed for "
			    "service %s: %d: %s\n", vals[1], r, emsg);
			return (1);
		}
	}

	return (0);
}

int
decorate_dependents(void)
{
	int r;
	char *emsg;
	dec_update_t p;

	/*
	 * Now we need to make sure that dependencies created by
	 * dependents pgs are linked to the right bundle_tbl entry.
	 */
	r = sqlite_exec(g_db,
	    "SELECT moat_prop_id, moat_prop_name, value_value"
	    "    FROM moat INNER JOIN value_tbl ON moat_lnk_val_id = value_id"
	    "    WHERE moat_pg_name = 'dependents';",
	    link_dependents_callback, &p, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error query for link_dependents_callback: %d: %s\n",
		    r, emsg);
		return (1);
	}

	return (0);
}

/*
 * callback to insert higher entity dec_id and layer info in he_dec_tbl
 *
 * query columns 4
 *     <higher entity>_id, dec_id, decoration_layer, decoration_bundle_id
 *
 * query columns 5
 *     <higher entity>_id, dec_id, decoration_layer, pg_type,
 *     decoration_bundle_id
 */
/*ARGSUSED*/
int
insert_into_he_dec_tbl_callback(void *arg, int columns, char **vals,
    char **names)
{
	int r;
	char *emsg;

	assert(columns == 4 || columns == 5);

	if (columns == 4) {
		r = sqlite_exec_printf(g_db,
		    "INSERT INTO he_dec_tbl (aux4_id, aux4_dec_id, aux4_layer,"
		    "    aux4_bundle_id) VALUES ('%q', '%q', '%q', '%q');",
		    NULL, NULL, &emsg, vals[0], vals[1], vals[2], vals[3]);
	} else {
		r = sqlite_exec_printf(g_db,
		    "INSERT INTO he_dec_tbl (aux4_id, aux4_type, aux4_dec_id, "
		    "    aux4_layer, aux4_bundle_id)"
		    "        VALUES ('%q', '%q', '%q', '%q', '%q');",
		    NULL, NULL, &emsg, vals[0], vals[3], vals[1], vals[2],
		    vals[4]);
	}

	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed INSERT INTO he_dec_tbl: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * callback to update decoration id of higher entities and insert new
 * decoration entry in decoration_tbl
 *
 * query columns
 *     <higher entity>_id, dec_id, layer, bundle_id
 *
 * from table
 *     he_dec_tbl
 *
 * arg
 *     aux4_t
 *	    id column name
 *	    decoration id column name
 *	    table name of the higher entity
 *	    type of decoration
 */
/*ARGSUSED*/
int
decorate_higher_entities_callback(void *arg, int columns, char **vals,
    char **names)
{
	int r;
	char *emsg;
	aux4_t *p = (aux4_t *)arg;

	assert(columns == 4 || columns == 5);

	if (columns == 4) {
		r = sqlite_exec_printf(g_db,
		    "INSERT INTO decoration_tbl (decoration_id, "
		    "    decoration_layer, decoration_type, "
		    "    decoration_value_id, decoration_gen_id,"
		    "    decoration_bundle_id) "
		    "        VALUES ('%q', '%q', %d, 0, 0, '%q');",
		    NULL, NULL, &emsg, vals[1], vals[2], p->type, vals[3]);
	} else {
		r = sqlite_exec_printf(g_db,
		    "INSERT INTO decoration_tbl (decoration_id, "
		    "    decoration_layer, decoration_entity_type, "
		    "    decoration_type, decoration_value_id, "
		    "    decoration_gen_id, decoration_bundle_id) "
		    "        VALUES ('%q', '%q', '%q', %d, 0, 0, '%q');",
		    NULL, NULL, &emsg, vals[1], vals[2], vals[3], p->type,
		    vals[4]);
	}

	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed to INSERT INTO decoration_tbl: %d: %s\n", r, emsg);
		return (1);
	}

	notify_cnt++;

	return (0);
}

/*
 * Query he_dec_tbl to decorate higher entities.
 *
 * id: column name holding higher entity id (or unique key)
 * column: column name of decoration id of the higher entity
 * table: table name if the higher entity
 * type: decoration type
 */
int
update_he_decoration(const char *id, const char *column, const char *table,
    decoration_type_t t)
{
	int r;
	char *emsg;
	aux4_t p = {0};

	p.id = id;
	p.column = column;
	p.table = table;
	p.type = t;

	if (p.type == DECORATION_TYPE_PG) {
		r = sqlite_exec(g_db,
		    "SELECT aux4_id, aux4_dec_id, aux4_layer, aux4_type, "
		    "    aux4_bundle_id"
		    "    FROM he_dec_tbl",
		    decorate_higher_entities_callback, &p, &emsg);
	} else {
		r = sqlite_exec(g_db,
		    "SELECT aux4_id, aux4_dec_id, aux4_layer, aux4_bundle_id"
		    "    FROM he_dec_tbl",
		    decorate_higher_entities_callback, &p, &emsg);
	}


	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed decorate_higher_entities_callback: %d: %s\n",
		    r, emsg);
		return (1);
	}

	return (0);
}

/*
 * search corner cases where higher entities don't have a property
 * among its dependents, hence dec_id = 0, and decorate at the
 * admin layer.
 */
int
higher_entities_cleanup(void)
{
	int r;
	char *emsg;

	/*
	 * pgs
	 */
	r = sqlite_exec_printf(g_db,
	    "DELETE FROM he_dec_tbl; "
	    "SELECT pg_id, pg_dec_id, %d, pg_type, 0 FROM pg_tbl_tmp"
	    "    WHERE pg_dec_id NOT IN (SELECT DISTINCT decoration_id"
	    "        FROM decoration_tbl);",
	    insert_into_he_dec_tbl_callback, NULL, &emsg,
	    REP_PROTOCOL_DEC_ADMIN);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error cleaning up pg dec_ids: %d: %s\n",
		    r, emsg);
		return (1);
	}

	if (update_he_decoration("pg_id", "pg_dec_id", "pg_tbl_tmp",
	    DECORATION_TYPE_PG) != 0)
		return (1);

	/*
	 * instances
	 */
	r = sqlite_exec_printf(g_db,
	    "DELETE FROM he_dec_tbl; "
	    "SELECT instance_id, instance_dec_id, %d, 0 FROM instance_tbl_tmp"
	    "    WHERE instance_dec_id NOT IN (SELECT DISTINCT decoration_id"
	    "        FROM decoration_tbl);",
	    insert_into_he_dec_tbl_callback, NULL, &emsg,
	    REP_PROTOCOL_DEC_ADMIN);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error cleaning up instance dec_ids: %d: %s\n",
		    r, emsg);
		return (1);
	}

	if (update_he_decoration("instance_id", "instance_dec_id",
	    "instance_tbl_tmp", DECORATION_TYPE_INST) != 0)
		return (1);

	/*
	 * services
	 */
	r = sqlite_exec_printf(g_db,
	    "DELETE FROM he_dec_tbl; "
	    "SELECT svc_id, svc_dec_id, %d, 0 FROM service_tbl_tmp WHERE"
	    "    svc_dec_id NOT IN (SELECT DISTINCT decoration_id FROM"
	    "        decoration_tbl);",
	    insert_into_he_dec_tbl_callback, NULL, &emsg,
	    REP_PROTOCOL_DEC_ADMIN);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error cleaning up service dec_ids: %d: %s\n",
		    r, emsg);
		return (1);
	}

	if (update_he_decoration("svc_id", "svc_dec_id", "service_tbl_tmp",
	    DECORATION_TYPE_SVC) != 0)
		return (1);


	return (0);
}

/*
 * Callback to assign decoration ids to higher entities.
 *
 * columns:
 *     table row id
 *
 * arg:
 *     aux4_t:
 *         column: he dec_id colmun name
 *         table:  he table name
 *         id:     he id column name
 */
/*ARGSUSED*/
int
he_dec_ids_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	aux4_t *p = (aux4_t *)arg;

	/*
	 * we quote table and column names with " instead of ' because
	 * sqlite is not smart and fails silently if we use ' for table and
	 * column names.
	 */
	r = sqlite_exec_printf(g_db,
	    "UPDATE \"%q\" SET \"%q\" = %u WHERE \"%q\" = '%q' AND \"%q\" = 0;",
	    NULL, NULL, &emsg, p->table, p->column, (p->dec_id)++, p->id,
	    vals[0], p->column);

	if (r != 0) {
		(void) fprintf(stderr, "Failed update of %s: %s\n", p->table,
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * Assign a unique decoration ID for each higher entity.
 */
int
assign_he_dec_ids(void)
{
	int r;
	char *emsg;
	aux4_t tbl_info;

	if ((tbl_info.dec_id = new_id(BACKEND_ID_DECORATION)) == 0)
		return (1);

	tbl_info.column = "pg_dec_id";
	tbl_info.table = "pg_tbl_tmp";
	tbl_info.id = "pg_id";
	r = sqlite_exec(g_db,
	    "SELECT pg_id FROM pg_tbl;",
	    he_dec_ids_cb, &tbl_info, &emsg);

	if (r != 0) {
		(void) fprintf(stderr, "Failed query on pg_tbl: %s\n",
		    emsg);
		return (1);
	}

	tbl_info.column = "instance_dec_id";
	tbl_info.table = "instance_tbl_tmp";
	tbl_info.id = "instance_id";
	r = sqlite_exec(g_db,
	    "SELECT instance_id FROM instance_tbl;",
	    he_dec_ids_cb, &tbl_info, &emsg);

	if (r != 0) {
		(void) fprintf(stderr, "Failed query on instance_tbl: %s\n",
		    emsg);
		return (1);
	}

	tbl_info.column = "svc_dec_id";
	tbl_info.table = "service_tbl_tmp";
	tbl_info.id = "svc_id";
	r = sqlite_exec(g_db,
	    "SELECT svc_id FROM service_tbl;",
	    he_dec_ids_cb, &tbl_info, &emsg);

	if (r != 0) {
		(void) fprintf(stderr, "Failed query on service_tbl: %s\n",
		    emsg);
		return (1);
	}

	if (update_id_tbl(tbl_info.dec_id, BACKEND_ID_DECORATION) != 0)
		return (1);

	return (0);
}

/*
 * decorate higher entities (pg, inst and svc)
 *
 * Strategy:
 *    From pg up, we look for the lowest layer in its children.
 *    We use that layer to decorate the higher entity.
 *
 *    Some higher entities don't have any properties among its descendants.
 *    In this case these entities will be left with a *_dec_id = 0.
 *    We call higher_entities_cleanup() to decorate them at the admin layer
 *    later.
 */
int
decorate_higher_entities(void)
{
	int r;
	char *emsg;
	uint32_t dec_key;
	struct run_single_int_info info;

	/*
	 * Populate the dec_id column for the higher entities
	 */
	if (assign_he_dec_ids() != 0)
		return (1);

	/*
	 * decorate the pgs
	 */
	r = sqlite_exec(g_db,
	    "DELETE FROM he_dec_tbl; "
	    "SELECT DISTINCT pg_id, pg_dec_id, decoration_layer, pg_type,"
	    "    decoration_bundle_id FROM decoration_tbl"
	    "    INNER JOIN prop_lnk_tbl_tmp"
	    "    ON lnk_decoration_id = decoration_id"
	    "    INNER JOIN pg_tbl_tmp ON pg_id = lnk_pg_id;",
	    insert_into_he_dec_tbl_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error inserting pg dec layer in he_dec_tbl: %d: %s\n",
		    r, emsg);
		return (1);
	}

	if (update_he_decoration("pg_id", "pg_dec_id", "pg_tbl_tmp",
	    DECORATION_TYPE_PG) != 0)
		return (1);

	/*
	 * decorate the instances
	 */
	r = sqlite_exec(g_db,
	    "DELETE FROM he_dec_tbl; "
	    "SELECT DISTINCT instance_id, instance_dec_id, decoration_layer,"
	    "    decoration_bundle_id FROM decoration_tbl"
	    "    INNER JOIN pg_tbl_tmp ON pg_dec_id = decoration_id"
	    "    INNER JOIN instance_tbl_tmp ON pg_parent_id = instance_id;",
	    insert_into_he_dec_tbl_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error inserting inst dec layer in he_dec_tbl: %d: %s\n",
		    r, emsg);
		return (1);
	}

	if (update_he_decoration("instance_id", "instance_dec_id",
	    "instance_tbl_tmp", DECORATION_TYPE_INST) != 0)
		return (1);

	/*
	 * decorate the services (by pg)
	 */
	r = sqlite_exec(g_db,
	    "DELETE FROM he_dec_tbl; "
	    "SELECT DISTINCT svc_id, svc_dec_id, decoration_layer,"
	    "    decoration_bundle_id FROM decoration_tbl"
	    "    INNER JOIN pg_tbl_tmp ON pg_dec_id = decoration_id"
	    "    INNER JOIN service_tbl_tmp ON pg_parent_id = svc_id;",
	    insert_into_he_dec_tbl_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error inserting service dec layer in he_dec_tbl: %d: %s\n",
		    r, emsg);
		return (1);
	}

	/*
	 * decorate the services (by instance)
	 * for the unlikely case where a service does not define any property
	 * And we don't clean up he_dec_tbl here on purpose...
	 */
	r = sqlite_exec(g_db,
	    "SELECT DISTINCT svc_id, svc_dec_id, decoration_layer,"
	    "    decoration_bundle_id FROM decoration_tbl"
	    "    INNER JOIN instance_tbl_tmp ON instance_dec_id = decoration_id"
	    "    INNER JOIN service_tbl_tmp ON svc_id = instance_svc"
	    "    WHERE svc_id NOT IN (SELECT aux4_id FROM he_dec_tbl);",
	    insert_into_he_dec_tbl_callback, NULL, &emsg);

	if (r != 0) {
		(void) fprintf(stderr,
		    "Error inserting service dec layer in he_dec_tbl: %d: %s\n",
		    r, emsg);
		return (1);
	}

	if (update_he_decoration("svc_id", "svc_dec_id", "service_tbl_tmp",
	    DECORATION_TYPE_SVC) != 0)
		return (1);

	if (higher_entities_cleanup() != 0)
		return (1);

	/*
	 * Since we cheated and let the DB engine populate decoration_key
	 * for all new entries, we have to update "DECKEY".
	 * BTW, it is ok to cheat here since we're single threaded ;-)
	 */
	info.rs_out = &dec_key;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec(g_db,
	    "SELECT max(decoration_key) FROM decoration_tbl",
	    run_single_int_callback, &info, &emsg);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr,
		    "Error getting max(decoration_key): %d: %s\n", r, emsg);
		return (-1);
	}

	/*
	 * increment dec_key so we store the *next* id
	 */
	if (update_id_tbl(++dec_key, BACKEND_KEY_DECORATION) != 0)
		return (1);

	return (0);
}

/*
 * replaces the tables that had columns added.
 *     service_tbl
 *     instance_tbl
 *     pg_tbl
 *     prop_lnk_tbl
 */
int
finalize_tables(void)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec(g_db,
	    "BEGIN TRANSACTION;"
	    "    CREATE TABLE value_tmp_tbl ("
	    "        value_id INTEGER NOT NULL,"
	    "        value_value VARCHAR NOT NULL,"
	    "        value_order INTEGER DEFAULT 0);"
	    "    INSERT INTO value_tmp_tbl"
	    "        SELECT value_id, value_value, value_order"
	    "            FROM value_tbl;"
	    "    DROP TABLE value_tbl;"
	    "    CREATE TABLE value_tbl ("
	    "        value_id INTEGER NOT NULL,"
	    "        value_value VARCHAR NOT NULL,"
	    "        value_order INTEGER DEFAULT 0);"
	    "    CREATE INDEX value_tbl_id ON value_tbl (value_id);"
	    "    INSERT INTO value_tbl"
	    "        SELECT value_id, value_value, value_order"
	    "            FROM value_tmp_tbl;"
	    "    DROP TABLE value_tmp_tbl;"
	    "    DROP TABLE service_tbl;"
	    "    CREATE TABLE service_tbl ("
	    "        svc_id INTEGER PRIMARY KEY,"
	    "        svc_name CHAR(256) NOT NULL,"
	    "        svc_conflict_cnt INTEGER DEFAULT 0,"
	    "        svc_dec_id INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX service_tbl_name ON service_tbl (svc_name);"
	    "    CREATE INDEX service_tbl_dec ON service_tbl (svc_dec_id);"
	    "    INSERT INTO service_tbl (svc_id, svc_name, svc_dec_id)"
	    "        SELECT svc_id, svc_name, svc_dec_id FROM service_tbl_tmp;"
	    "    DROP TABLE instance_tbl;"
	    "    CREATE TABLE instance_tbl ("
	    "        instance_id INTEGER PRIMARY KEY,"
	    "        instance_name CHAR(256) NOT NULL,"
	    "        instance_svc INTEGER NOT NULL,"
	    "        instance_conflict_cnt INTEGER DEFAULT 0,"
	    "        instance_dec_id INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX instance_tbl_name"
	    "        ON instance_tbl (instance_svc, instance_name);"
	    "    CREATE INDEX instance_tbl_dec"
	    "        ON instance_tbl (instance_dec_id);"
	    "    INSERT INTO instance_tbl (instance_id, instance_name,"
	    "        instance_svc, instance_dec_id)"
	    "        SELECT instance_id, instance_name, instance_svc,"
	    "            instance_dec_id FROM instance_tbl_tmp;"
	    "    DROP TABLE pg_tbl;"
	    "    CREATE TABLE pg_tbl ("
	    "        pg_id INTEGER PRIMARY KEY,"
	    "        pg_parent_id INTEGER NOT NULL,"
	    "        pg_name CHAR(256) NOT NULL,"
	    "        pg_type CHAR(256) NOT NULL,"
	    "        pg_flags INTEGER NOT NULL,"
	    "        pg_gen_id INTEGER NOT NULL,"
	    "        pg_dec_id INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX pg_tbl_name ON pg_tbl (pg_parent_id, pg_name);"
	    "    CREATE INDEX pg_tbl_parent ON pg_tbl (pg_parent_id);"
	    "    CREATE INDEX pg_tbl_type ON pg_tbl (pg_parent_id, pg_type);"
	    "    CREATE INDEX pg_tbl_dec ON pg_tbl (pg_dec_id);"
	    "    CREATE INDEX pg_tbl_id_gen ON pg_tbl (pg_id, pg_gen_id);"
	    "    INSERT INTO pg_tbl (pg_id, pg_parent_id, pg_name, pg_type,"
	    "        pg_flags, pg_gen_id, pg_dec_id)"
	    "            SELECT pg_id, pg_parent_id, pg_name, pg_type,"
	    "                pg_flags, pg_gen_id, pg_dec_id FROM pg_tbl_tmp;"
	    "    DROP TABLE prop_lnk_tbl;"
	    "    CREATE TABLE prop_lnk_tbl ("
	    "        lnk_prop_id INTEGER PRIMARY KEY, "
	    "        lnk_pg_id INTEGER NOT NULL, "
	    "        lnk_gen_id INTEGER NOT NULL, "
	    "        lnk_prop_name CHAR(256) NOT NULL, "
	    "        lnk_prop_type CHAR(2) NOT NULL, "
	    "        lnk_val_id INTEGER NOT NULL DEFAULT 0, "
	    "        lnk_decoration_id INTEGER NOT NULL DEFAULT 0,"
	    "        lnk_val_decoration_key INTEGER NOT NULL DEFAULT 0);"
	    "    CREATE INDEX prop_lnk_tbl_base "
	    "        ON prop_lnk_tbl (lnk_pg_id, lnk_gen_id);"
	    "    CREATE INDEX prop_lnk_tbl_val ON prop_lnk_tbl (lnk_val_id);"
	    "    CREATE INDEX prop_lnk_tbl_dec "
	    "        ON prop_lnk_tbl (lnk_decoration_id);"
	    "    CREATE INDEX prop_lnk_tbl_gen_dec"
	    "        ON prop_lnk_tbl (lnk_gen_id, lnk_decoration_id);"
	    "    UPDATE prop_lnk_tbl_tmp SET lnk_val_id = 0"
	    "        WHERE lnk_val_id IS NULL;"
	    "    INSERT INTO prop_lnk_tbl"
	    "        SELECT"
	    "            lnk_prop_id,"
	    "            lnk_pg_id,"
	    "            lnk_gen_id,"
	    "            lnk_prop_name,"
	    "            lnk_prop_type,"
	    "            lnk_val_id,"
	    "            lnk_decoration_id,"
	    "            lnk_val_decoration_key FROM prop_lnk_tbl_tmp;"
	    "COMMIT TRANSACTION;",
	    NULL, NULL, &emsg)) != 0) {
		(void) fprintf(stderr,
		    "finalize_tables failed: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * drop temporary tables
 */
int
drop_temporary_tables(void)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec(g_db,
	    "BEGIN TRANSACTION;"
	    "    DROP TABLE service_tbl_tmp;"
	    "    DROP TABLE instance_tbl_tmp;"
	    "    DROP TABLE pg_tbl_tmp;"
	    "    DROP TABLE prop_lnk_tbl_tmp;"
	    "    DROP TABLE moat;"
	    "    DROP TABLE he_dec_tbl;"
	    "    DROP TABLE prop_dec_tbl;"
	    "    DROP TABLE bundle_migration_tbl;"
	    "    DROP TABLE snap_bundle_lnk;"
	    "    DROP TABLE prop_snap_lnk;"
	    "    DROP TABLE dang_snap_bundle_tbl;"
	    "    DROP TABLE alt_repo_tbl;"
	    "    DROP TABLE dr_prop_tbl;"
	    "    DROP TABLE dr_value_tbl;"
	    "COMMIT TRANSACTION;",
	    NULL, NULL, &emsg)) != 0) {
		(void) fprintf(stderr,
		    "drop_tmp_tables failed: %d: %s\n", r, emsg);
		return (1);
	}

	return (0);
}

/*
 * bump schema_version
 */
int
update_schema_version(int newversion, int oldversion)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec_printf(g_db,
	    "BEGIN TRANSACTION;"
	    "    UPDATE schema_version SET schema_version = %d "
	    "        WHERE schema_version = %d;"
	    "COMMIT TRANSACTION;",
	    NULL, NULL, &emsg, newversion, oldversion)) != 0) {
		(void) fprintf(stderr, "update_schema_version failed: %d: %s\n",
		    r, emsg);
		return (1);
	}

	return (0);
}

/*
 * Callback to insert general/complete properties.
 *
 * Calling query makes sure only general/enabled properties rows are selected
 * We need to check if general/complete already exists for lnk_pg_id/lnk_gen_id
 *
 * columns:
 *     lnk_pg_id, lnk_gen_id
 */
/*ARGSUSED*/
int
add_complete_cb(void *arg, int columns, char **vals, char **names)
{
	char *emsg = NULL;
	uint32_t row_count = 0;
	struct run_single_int_info info;


	info.rs_out = &row_count;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	if (sqlite_exec_printf(g_db,
	    "SELECT count() FROM prop_lnk_tbl_tmp WHERE "
	    "    lnk_pg_id = '%q' AND lnk_gen_id = '%q' AND "
	    "    lnk_prop_name = 'complete';", run_single_int_callback, &info,
	    &emsg, vals[0], vals[1]) != 0) {
		(void) fprintf(stderr, "Unabled to add complete failed: %s\n",
		    emsg);

		return (1);
	}

	if (row_count > 0)
		return (0);

	if (sqlite_exec_printf(g_db,
	    "BEGIN TRANSACTION; "
	    "INSERT INTO prop_lnk_tbl_tmp"
	    "    (lnk_pg_id, lnk_gen_id, lnk_prop_name, lnk_prop_type,"
	    "    lnk_val_id) VALUES ('%q', '%q', 'complete', 's', 0); "
	    "COMMIT TRANSACTION; ",
	    NULL, NULL, &emsg, vals[0], vals[1]) != 0) {
		(void) fprintf(stderr, "Unabled to add complete failed: %s\n",
		    emsg);

		return (1);
	}

	return (0);
}

/*
 * Add a complete property to each general property group
 * that has an enabled property.
 *
 * Needs to run before populating the auxiliary tables.
 * Needs to run after creation and initial population of prop_lnk_tbl_tmp
 *
 */
int
add_complete(void)
{
	char *emsg;
	int r;
	uint32_t max_prop_id = 0;
	struct run_single_int_info info;

	/*
	 * Save the max prop_id of the existing properties
	 */
	info.rs_out = &max_prop_id;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec(g_db,
	    "SELECT max(lnk_prop_id) FROM prop_lnk_tbl;",
	    run_single_int_callback, &info, &emsg);

	if (r != 0) {
		(void) fprintf(stderr, "Failed to get max(lnk_prop_id): %s\n",
		    emsg);
		return (1);
	}

	/*
	 * We need to get gen id from prop_lnk_tbl so that we have all
	 * generations of general/enabled
	 *
	 * The callback will insert new general/complete properties in
	 * prop_lnk_tbl_tmp.
	 */
	if (sqlite_exec(g_db,
	    "SELECT lnk_pg_id, lnk_gen_id FROM prop_lnk_tbl"
	    "    WHERE lnk_prop_name = 'enabled' AND"
	    "    lnk_pg_id IN (SELECT pg_id FROM pg_tbl"
	    "        WHERE pg_name = 'general');",
	    add_complete_cb, NULL, &emsg) != 0) {
		(void) fprintf(stderr, "Failed query on prop_lnk_tbl: %s\n",
		    emsg);
		return (1);
	}

	/*
	 * Now we need to copy the general/complete properties back to
	 * prop_lnk_tbl so we populate the auxiliary tables properly.
	 * Here is where the max_prop_id stored above comes in handy.
	 */
	if (sqlite_exec_printf(g_db,
	    "INSERT INTO prop_lnk_tbl (lnk_prop_id, lnk_pg_id, lnk_gen_id,"
	    "    lnk_prop_name, lnk_prop_type, lnk_val_id)"
	    "    SELECT lnk_prop_id, lnk_pg_id, lnk_gen_id, lnk_prop_name,"
	    "        lnk_prop_type, lnk_val_id FROM prop_lnk_tbl_tmp"
	    "        WHERE lnk_prop_id > '%u';",
	    NULL, NULL, &emsg, max_prop_id) != 0) {
		(void) fprintf(stderr, "Failed INSERT INTO prop_lnk_tbl: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * callback function to populate svccfg commands file with manifests to
 * import and profiles to apply.
 *
 * column:
 *    manifest or profile full path
 *
 * arg:
 *    pointer to stream to output commands (FILE *)
 *
 */
/*ARGSUSED*/
int
populate_svccfg_cmds_cb(void *arg, int columns, char **vals, char **names)
{
	file_lc_t *p = (file_lc_t *)arg;
	FILE *f = (FILE *)p->file;
	int r = 0;
	struct stat st;

	assert(columns == 1);

	if (vals[0] == NULL) {
		return (0);
	} else if (IS_STD_LOC(vals[0])) {
		if (stat(vals[0], &st) == 0) {
			r = fprintf(f, "import %s\n", vals[0]);
			p->lc++;
		}
	} else {
		return (0);
	}

	if (r < 0) {
		(void) fprintf(stderr, "Failed to write %s: %s\n",
		    svccfg_cmds, strerror(errno));
		(void) fprintf(stderr, "\t%s\n", vals[0]);

		return (1);
	}

	return (0);
}

/*
 * Populate svccfg command file with the manifests and profiles to load in the
 * alt repo.
 *
 * Returns the number of bundles to load on success or -1 on failure.
 */
int
populate_svccfg_cmds(const char *alt_db, FILE *f)
{
	int r;
	char *emsg = NULL;
	const char **profile;
	file_lc_t p = {0};

	r = fprintf(f, "repository %s\n", alt_db);

	if (r == -1) {
		(void) fprintf(stderr, "Failed to write to: %s: %s\n",
		    svccfg_cmds, strerror(errno));
		return (-1);
	}

	p.file = f;
	r = sqlite_exec(g_db,
	    "SELECT value_value FROM moat INNER JOIN value_tbl"
	    "    ON moat_lnk_val_id = value_id"
	    "    WHERE moat_svc_name = 'smf/manifest' AND"
	    "    moat_prop_name = 'manifestfile' AND value_value"
	    "    NOT IN (SELECT aux2_bundle_name FROM snap_bundle_lnk);",
	    populate_svccfg_cmds_cb, &p, &emsg);

	if (r != 0) {
		/*
		 * If emsg is NULL the error message was printed in the callback
		 * function
		 */
		if (emsg != NULL)
			(void) fprintf(stderr,
			    "Failed populate_svccfg_cmds_cb,: %s\n", emsg);

		return (-1);
	}

	/*
	 * Add generic.xml and platform.xml profiles as well as any profile
	 * at /etc/svc/profile/site or /var/svc/profile/site
	 */
	for (profile = profiles; *profile; profile++) {
		struct stat st;

		if (stat(*profile, &st) != 0)
			continue;

		r = fprintf(f, "apply %s\n", *profile);
		if (r == -1) {
			(void) fprintf(stderr, "Failed to write to: %s: %s\n",
			    svccfg_cmds, strerror(errno));
			return (-1);
		} else {
			p.lc++;
		}
	}

	return ((int)p.lc);
}

/*
 * populate svccfg cmds file and forks svccfg to run the commands on the
 * alt repo
 *
 * return the number of files (manifests or profiles) processed.
 *        or -1 on error.
 *
 */
int
load_alt_repo(const char *alt_db, int newver)
{
	FILE *f;
	pid_t pid;
	int status, r;

	/*
	 * Open file with commands for svccfg
	 */
	if ((f = fopen(svccfg_cmds, "w")) == NULL) {
		(void) fprintf(stderr,
		    "Failed to create svccfg command file:%s: %s\n",
		    svccfg_cmds, strerror(errno));

		return (-1);
	}

	/*
	 * Populate svccfg commands file
	 */
	if (newver == 6) {
		if ((r = populate_svccfg_cmds(alt_db, f)) < 0) {
			(void) fclose(f);
			return (-1);
		}
		(void) fclose(f);

		if (r == 0) {
			/*
			 * Nothing to import / apply
			 */
			return (0);
		}
	}

	if (newver == 8) {
		char *hashes[4] = {
			"/etc/svc/profile/generic.xml",
			"/etc/svc/profile/generic_open.xml",
			"/etc/svc/profile/generic_limited_net.xml",
			NULL,
		};
		int i;

		if (fprintf(f, "repository %s\n", alt_db) < 0) {
			(void) fclose(f);
			return (-1);
		}

		i = 0;
		while (hashes[i] != NULL) {
			r = fprintf(f, "delhash %s\nunselect\n", hashes[i]);
			if (r < 0) {
				(void) fclose(f);
				return (-1);
			}

			i++;
		}

		(void) fclose(f);
	}

	pid = fork1();
	if (pid == -1) {
		(void) fprintf(stderr, "fork1 failed: %s\n", strerror(errno));
		return (-1);
	} else if (pid == 0) {
		int fd;
		/*
		 * If the new version is 8, then the svccfg command to delete
		 * the hashes may present warnings that are ignorable if the
		 * hash is not present for one or more of the expected entries.
		 */
		if ((fd = open("/dev/null", O_RDWR)) == -1) {
			(void) fprintf(stderr, "Failed to open dev/null: %s\n",
			    strerror(errno));

			return (-1);
		}

		if (dup2(fd, STDOUT_FILENO) == -1 ||
		    dup2(fd, STDERR_FILENO) == -1) {
			(void) fprintf(stderr, "Failed to dup dev/null: %s\n",
			    strerror(errno));

			return (-1);
		}

		(void) close(fd);

		(void) execl("/usr/sbin/svccfg", "svccfg", "-f", svccfg_cmds,
		    NULL);
		(void) fprintf(stderr, "Failed execl: %s\n", strerror(errno));
		return (-1);
	} else {
		(void) waitpid(pid, &status, 0);
	}

	return (r);
}

int
get_or_add_bundle(const char *bundle_name, uint32_t *bundle_id)
{
	int r;
	char *emsg;
	uint32_t id;
	struct run_single_int_info info = {0};

	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	info.rs_out = &id;
	r = sqlite_exec_printf(g_db,
	    "SELECT bundle_id FROM bundle_tbl WHERE bundle_name = '%q';",
	    run_single_int_callback, &info, &emsg, bundle_name);

	if (r != 0) {
		(void) fprintf(stderr, "Failed bundle_id lookup: %s\n", emsg);
		return (1);
	}
	if (info.rs_result == REP_PROTOCOL_FAIL_NOT_FOUND) {
		/*
		 * insert new bundle entry
		 */
		if ((id = new_id(BACKEND_ID_BUNDLE)) == 0) {
			(void) fprintf(stderr,
			    "Failed to get new bundle_id: %s\n", emsg);
			return (1);
		}
		r = sqlite_exec_printf(g_db,
		    "INSERT INTO bundle_tbl (bundle_name, bundle_id)"
		    "    VALUES ('%q', %u);", NULL, NULL, &emsg, bundle_name,
		    id);
		if (r != 0) {
			(void) fprintf(stderr,
			    "Failed to insert new bundle entry: %s\n", emsg);
			return (1);
		}
	} else {
		assert(info.rs_result == REP_PROTOCOL_SUCCESS);
	}
	*bundle_id = id;

	return (0);
}

/*
 * column:
 *    value_value from alt_repo
 *
 * args:
 *    value_id from repo
 *    value_order of value_value
 *    return code: 0 if there is no match. 1 if there is a match.
 *                 if greater than 1 there are multiple matches
 *                 (this is a error)
 */
/*ARGSUSED*/
int
cmp_val_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	cmp_val_t *p = (cmp_val_t *)arg;
	struct run_single_int_info info;

	assert(columns == 1);

	assert(p->val_ret == 0);
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	info.rs_out = &p->val_ret;
	r = sqlite_exec_printf(g_db,
	    "SELECT count() FROM value_tbl WHERE value_id = %u AND"
	    "    value_order = %u AND value_value = '%q';",
	    run_single_int_callback, &info, &emsg, p->val_id, p->val_order,
	    vals[0]);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed lookup value_id %s, value_order %d: %s\n",
		    vals[1], p->val_order, emsg);
		return (1);
	}
	/*
	 * The select count() above should NEVER return more than one row
	 */
	assert(p->val_ret < 2);

	return (0);
}

/*
 * callback to compare values of properties from alt repo and repo
 *
 * columns:
 *    repo_prop_id[0], repo_val_id[1], repo_dec_key[2], alt_repo_val_id[3],
 *    alt_repo_layer[4], alt_repo_bundle_name[5], alt_repo_prop_id[6]
 *
 * args:
 *    alt repo sqlite *db
 *
 */
/*ARGSUSED*/
int
process_alt_repo_aux_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	uint32_t alt_repo_count, repo_count, bundle_id, alt_repo_val_id, layer;
	cmp_val_t val = {0};
	sqlite *db = (sqlite *)arg;
	dec_update_t dec = {0};

	assert(columns == 7);

	string_to_id(vals[1], &val.val_id, names[1]);
	string_to_id(vals[3], &alt_repo_val_id, names[3]);
	if (get_val_count(g_db, val.val_id, &repo_count) != 0 ||
	    get_val_count(db, alt_repo_val_id, &alt_repo_count) != 0) {
		(void) fprintf(stderr, "Failed to lookup # of values:\n");
		return (1);
	}

	if (alt_repo_count != repo_count) {

		return (0);
	}

	for (val.val_order = 0; val.val_order < repo_count; val.val_order++) {
		val.val_ret = 0;

		r = sqlite_exec_printf(db,
		    "SELECT value_value FROM value_tbl WHERE"
		    "    value_id = %u AND value_order = %u;", cmp_val_cb, &val,
		    &emsg, alt_repo_val_id, val.val_order);
		if (r != 0) {
			(void) fprintf(stderr,
			    "Failed alt repo value lookup: %s\n", emsg);
			return (1);
		} else if (val.val_ret != 1) {

			/*
			 * not a match, bail
			 */
			return (0);
		}
	}

	/*
	 * We have a match!
	 * add bundle entry and update decoration layer
	 */
	if (get_or_add_bundle(vals[5], &bundle_id) != 0) {
		return (1);
	}
	dec.bundle_id = bundle_id;
	string_to_id(vals[2], &dec.dec_key, names[2]);
	string_to_id(vals[4], &layer, names[4]);
	dec.layer = (rep_protocol_decoration_layer_t)layer;

	if (update_decoration_entry(&dec) != 0) {
		return (1);
	}

	return (0);
}

/*
 * callback to populate auxiliary table alt_repo_tbl
 *
 * columns: (from repo)
 *    svc_name[0], inst_name[1], pg_name[2], prop_name[3], prop_id[4],
 *    val_id[5], dec_key[6], dec_layer[7]
 *
 * args:
 *    db_bundle_val_t
 *        alt repo sqlite *db
 *        alt_prop_id
 *        bundle_name
 *        alt repo val_id
 *        alt repo dec layer
 *
 */
/*ARGSUSED*/
int
fill_alt_repo_tbl_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;
	db_bundle_val_t *p = (db_bundle_val_t *)arg;

	r = sqlite_exec_printf(g_db,
	    "INSERT INTO alt_repo_tbl (repo_prop_id, repo_val_id, repo_dec_key,"
	    "    alt_repo_prop_id, alt_repo_val_id, alt_repo_layer,"
	    "    alt_repo_bundle_name)"
	    "    VALUES ('%q', '%q', '%q', %u, %u, %d, '%q');", NULL, NULL,
	    &emsg, vals[4], vals[5], vals[6], p->alt_prop_id, p->val_id,
	    p->dec_layer, p->bundle_name);
	if (r != 0) {
		(void) fprintf(stderr, "Failed insert into alt_repo_tbl: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * callback function to process alt repo
 *
 * columns: (from alt repo)
 *    svc_name[0], inst_name[1], pg_name[2], prop_name[3], val_id[4],
 *    dec_layer[5], bundle_name[6], prop_id[7]
 *
 * arg:
 *    sqlite *
 *
 */
/*ARGSUSED*/
int
alt_repo_cb(void *arg, int columns, char **vals, char **names)
{
	int r = 0;
	char *emsg;
	db_bundle_val_t p = {0};

	assert(columns == 8);

	p.db = arg;
	string_to_id(vals[4], &p.val_id, names[4]);
	string_to_id(vals[5], (uint32_t *)&p.dec_layer, names[5]);
	string_to_id(vals[7], &p.alt_prop_id, names[7]);
	p.bundle_name = vals[6];

	r = sqlite_exec_printf(g_db,
	    "SELECT moat_svc_name, moat_instance_name, moat_pg_name,"
	    "    moat_prop_name, moat_prop_id, decoration_value_id,"
	    "    decoration_key, decoration_layer FROM moat"
	    "    INNER JOIN decoration_tbl"
	    "    ON moat_lnk_gen_id = decoration_gen_id AND"
	    "        moat_lnk_decoration_id = decoration_id"
	    "    WHERE moat_svc_name = '%q' AND"
	    "    (moat_instance_name = '%q' OR moat_instance_name IS NULL) AND"
	    "    moat_pg_name = '%q' AND moat_prop_name = '%q';",
	    fill_alt_repo_tbl_cb, &p, &emsg, vals[0], vals[1],
	    vals[2], vals[3]);

	if (r != 0) {
		(void) fprintf(stderr, "failed lookup of moat: %s\n", emsg);
		return (1);
	}

	return (0);
}

/*
 * Process alt repo
 *
 * For each svc_name/inst_name/pg_name/prop_name in alt_repo
 * find counter part in repo, compare values, if match
 *	update repo entry with bundle and layer from alt_repo
 */
int
process_alt_repo(sqlite *db)
{
	int r;
	char *emsg;

	/*
	 * Select all properties with their decoration and bundle info to
	 * populate alt_repo_tbl table.
	 */
	r = sqlite_exec(db,
	    "SELECT DISTINCT moat_svc_name, moat_instance_name, moat_pg_name,"
	    "    moat_prop_name, lnk_val_id, decoration_layer, bundle_name,"
	    "    moat_prop_id"
	    "    FROM moat"
	    "    INNER JOIN prop_lnk_tbl ON moat_prop_id = lnk_prop_id"
	    "    INNER JOIN decoration_tbl"
	    "        ON decoration_id = lnk_decoration_id AND"
	    "           decoration_gen_id = lnk_gen_id"
	    "    INNER JOIN bundle_tbl ON bundle_id = decoration_bundle_id;",
	    alt_repo_cb, db, &emsg);

	if (r != 0) {
		(void) fprintf(stderr, "Failed look up of alt repo: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "SELECT repo_prop_id, repo_val_id, repo_dec_key, alt_repo_val_id,"
	    "    alt_repo_layer, alt_repo_bundle_name, alt_repo_prop_id"
	    "    FROM alt_repo_tbl",
	    process_alt_repo_aux_cb, db, &emsg);
	if (r != 0) {
		(void) fprintf(stderr, "Failed process_alt_repo: %s\n", emsg);
		return (1);
	}

	return (0);
}

/*
 * Update the bundle id in the decoration table entry for the property
 * specified by decoration_id and gen_id with the bundle from the
 * auxiliary table dang_snap_bundle_tbl
 *
 * query columns
 *     lnk_decoration_id
 *     lnk_gen_id
 *     dang_bundle_id
 */
/*ARGSUSED*/
int
update_dec_bundle_id_cb(void *arg, int columns, char **vals, char **names)
{
	int r;
	char *emsg;

	assert(columns == 3);

	r = sqlite_exec_printf(g_db,
	    "UPDATE decoration_tbl SET decoration_bundle_id = '%q'"
	    "    WHERE decoration_id = '%q' AND decoration_gen_id = '%q' AND"
	    "        decoration_layer = %d AND decoration_bundle_id = 0;",
	    NULL, NULL, &emsg, vals[2], vals[0], vals[1],
	    REP_PROTOCOL_DEC_MANIFEST);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed update decoration_table: %s\n", emsg);
		return (1);
	}

	return (0);
}

/*
 * Should run at the end of load_bundles()
 *
 * Some properties in the 'initial' or 'last-import' snapshot are not in the
 * manifests on the current BE (because they were dropped across the upgrade).
 * But they still exist in the repo because of snapshots. In this case, we have
 * to decorate them at the MANIFEST layer and point to the bundle used by the
 * other properties on that snapshot.
 */
int
process_dangling_properties(void)
{
	int r;
	char *emsg;

	r = sqlite_exec_printf(g_db,
	    "INSERT INTO dang_snap_bundle_tbl (dang_snap_id,"
	    "    dang_bundle_id)"
	    "        SELECT DISTINCT aux_snap_id, bundle_id FROM alt_repo_tbl"
	    "            INNER JOIN bundle_tbl"
	    "                ON alt_repo_bundle_name = bundle_name"
	    "            INNER JOIN prop_snap_lnk"
	    "                ON repo_prop_id = aux_prop_id"
	    "            WHERE alt_repo_bundle_name LIKE '%s%%' OR"
	    "                alt_repo_bundle_name LIKE '%s%%';",
	    NULL, NULL, &emsg, LIB_M, VAR_M);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed insert into dang_prop_snap_bundle_tbl: %s\n", emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "SELECT DISTINCT lnk_decoration_id, lnk_gen_id, dang_bundle_id"
	    "    FROM prop_lnk_tbl_tmp"
	    "    INNER JOIN prop_snap_lnk ON lnk_prop_id = aux_prop_id"
	    "    INNER JOIN dang_snap_bundle_tbl ON aux_snap_id = dang_snap_id",
	    update_dec_bundle_id_cb, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed select : %s\n", emsg);
		return (1);
	}

	return (0);
}

/*
 * We rely on the snapshots 'initial' and 'last-import' to figure out the
 * properties that belong in the manifest layer. However, snapshots are
 * taken on instances only and services delivered by manifests that don't
 * have any instances don't have a 'initial' or 'last-import' to hint us.
 * So we fork the aimm aware svccfg and load the manifests for these services.
 *
 * We figure the manifests we need to load by finding the bundles in the
 * manifestfile property of smf/manifest that are not in the bundle_tbl.
 * At this point, we should have all known bundles from the 'initial' and
 * 'last-import' snapshots in bundle_tbl.
 *
 * This approach will pick also the profiles loaded by manifest-import as well
 * as services pre-EMI that don't have the pg manifestfiles with the bundle
 * information. In the case of the pre-EMI services, the properties are already
 * decorated at the manifest layer, we just need to add the bundle entry and
 * update the bundle_id in the decoration entry.
 */
int
load_bundles(void)
{
	int fd, r;
	sqlite *db;
	char *emsg;

	/*
	 * We need to touch a file to be our temporary alternate repository
	 * because svccfg requires the alternate repository to exits, even
	 * though it is empty.
	 */
	if ((fd = open(alt_repo, O_WRONLY | O_CREAT | O_EXCL, 0600)) == -1) {
		(void) fprintf(stderr,
		    "Failed to create temporary alt repo %s: %s\n",
		    alt_repo, strerror(errno));

		return (1);
	}
	(void) close(fd);

	/*
	 * process properties from the alternate repository
	 */
	if ((r = load_alt_repo(alt_repo, 6)) < 0) {
		return (1);
	} else if (r == 0) {
		/*
		 * nothing to process, just return
		 */
		return (0);
	}

	if ((db = sqlite_open(alt_repo, 0600, &emsg)) == NULL) {
		(void) fprintf(stderr, "failed sqlite_open: %s: %s\n",
		    alt_repo, emsg);
		return (1);
	}

	if (sqlite_exec(db,
	    "BEGIN TRANSACTION;", NULL, NULL, &emsg) != 0) {
		(void) fprintf(stderr, "failed BEGIN TRANSACTION: %s\n", emsg);
		return (1);
	}

	if (create_moat(db, &emsg) != 0) {
		(void) fprintf(stderr, "failed to create moat: %s\n", emsg);
		return (1);
	}
	if (sqlite_exec(db,
	    "COMMIT TRANSACTION;", NULL, NULL, &emsg) != 0) {
		(void) fprintf(stderr, "failed COMMIT TRANSACTION: %s\n", emsg);
		return (1);
	}

	if (populate_moat(db) != 0) {
		return (1);
	}

	if (process_alt_repo(db) != 0)
		return (1);

	sqlite_close(db);

	if (process_dangling_properties() != 0)
		return (1);

	return (0);
}

/*
 * Deletes last-import snapshots an all its descendents that ONLY referenced
 * by the last-import snapshots.
 */
int
delete_last_import_snapshots(void)
{
	int r;
	char *emsg;

	r = sqlite_exec(g_db,
	    "DELETE FROM snapshot_lnk_tbl"
	    "    WHERE lnk_snap_name = 'last-import';",
	    NULL, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed delete of last-import snapshot: %s\n", emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "DELETE FROM snaplevel_tbl WHERE snap_id NOT IN"
	    "   (SELECT DISTINCT lnk_snap_id FROM snapshot_lnk_tbl);", NULL,
	    NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed delete of last-import snaplevel: %s\n", emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "DELETE FROM snaplevel_lnk_tbl WHERE snaplvl_level_id NOT IN"
	    "    (SELECT DISTINCT snap_level_id FROM snaplevel_tbl);",
	    NULL, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed delete of last-import snaplevel link entries: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "DELETE FROM prop_lnk_tbl_tmp"
	    "    WHERE lnk_pg_id NOT IN (SELECT pg_id FROM pg_tbl UNION"
	    "        SELECT DISTINCT snaplvl_pg_id FROM snaplevel_lnk_tbl);",
	    NULL, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed to delete last-import property entries: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec_printf(g_db,
	    "DELETE FROM decoration_tbl"
	    "    WHERE decoration_type = '%d' AND decoration_key NOT IN"
	    "       (SELECT decoration_key FROM prop_lnk_tbl_tmp "
	    "        INNER JOIN decoration_tbl "
	    "        ON (decoration_id = lnk_decoration_id AND "
	    "        decoration_gen_id = lnk_gen_id)); ",
	    NULL, NULL, &emsg, DECORATION_TYPE_PROP);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed delete of last-import decoration entries: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "DELETE FROM bundle_tbl WHERE  bundle_id NOT IN"
	    "    (SELECT DISTINCT bundle_id FROM bundle_tbl);", NULL, NULL,
	    &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed delete of last-import bundle entries: %s\n",
		    emsg);
		return (1);
	}

	r = sqlite_exec(g_db,
	    "DELETE FROM value_tbl WHERE value_id NOT IN"
	    "    (SELECT DISTINCT lnk_val_id FROM prop_lnk_tbl_tmp"
	    "        UNION"
	    "     SELECT DISTINCT decoration_value_id FROM decoration_tbl);",
	    NULL, NULL, &emsg);
	if (r != 0) {
		(void) fprintf(stderr,
		    "Failed delete of last-import value entries: %s\n",
		    emsg);
		return (1);
	}

	return (0);
}

/*
 * upgrade SMF repository db to the new format for schema version 6
 *
 * The initial decorations upgrade to add the new decorations table, and
 * other decoration information to the repository.
 *
 * return 6 (new schema version) on success, 1 on error
 */
int
smf_repo_upgrade_to_6(void)
{
	struct run_single_int_info info;
	uint32_t cnt;
	uint32_t cnt_total = 0;
	char *emsg;

	if (check_value_order_upgrade() != 0) {
		(void) fprintf(stderr, "Failed check_value_order_upgrade()\n");
		return (1);
	}
	if (repo_create_new_tables() != 0) {
		(void) fprintf(stderr, "Failed repo_create_new_tables()\n");
		return (1);
	}
	if (create_temporary_tables() != 0) {
		(void) fprintf(stderr, "Failed create_temporary_tables()\n");
		return (1);
	}
	if (add_complete() != 0) {
		(void) fprintf(stderr, "Failed add_complete()\n");
		return (1);
	}
	if (create_aux_tables(g_db) != 0) {
		(void) fprintf(stderr, "Failed create_aux_tables()\n");
		return (1);
	}
	if (populate_moat(g_db) != 0) {
		(void) fprintf(stderr, "Failed populate_moat()\n");
		return (1);
	}
	if (populate_aux_tables() != 0) {
		(void) fprintf(stderr, "Failed populate_aux_tables()\n");
		return (1);
	}

	if (populate_lnk_decoration_id() != 0) {
		(void) fprintf(stderr,
		    "Failed populate_lnk_decoration_id()\n");
		return (1);
	}
	if (dr_dup_bundles() != 0) {
		(void) fprintf(stderr, "Failed dr_dup_bundles()\n");
		return (1);
	}
	if (populate_bundle_tbl() != 0) {
		(void) fprintf(stderr, "Failed populate_bundle_tbl()\n");
		return (1);
	}

	cnt = 0;
	info.rs_out = &cnt;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	if (sqlite_exec(g_db, "SELECT count() FROM service_tbl ",
	    run_single_int_callback, &info, &emsg) != SQLITE_OK)
		return (1);

	cnt_total = cnt;

	cnt = 0;
	info.rs_out = &cnt;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	if (sqlite_exec(g_db, "SELECT count() FROM instance_tbl ",
	    run_single_int_callback, &info, &emsg) != SQLITE_OK)
		return (1);

	cnt_total += cnt;

	cnt = 0;
	info.rs_out = &cnt;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	if (sqlite_exec(g_db, "SELECT count() FROM pg_tbl ",
	    run_single_int_callback, &info, &emsg) != SQLITE_OK)
		return (1);

	cnt_total += cnt;

	cnt = 0;
	info.rs_out = &cnt;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	if (sqlite_exec(g_db, "SELECT count() "
	    "FROM prop_lnk_tbl WHERE lnk_pg_id NOT IN "
	    "    (SELECT pg_id FROM pg_tbl); ",
	    run_single_int_callback, &info, &emsg) != SQLITE_OK)
		return (1);

	cnt_total += cnt;
	notify_cnt_max = cnt_total;

	if (decorate_properties() != 0) {
		(void) fprintf(stderr, "Failed decorate_properties()\n");
		return (1);
	}
	if (promote_properties() != 0) {
		(void) fprintf(stderr, "Failed promote_properties()\n");
		return (1);
	}
	if (bundle_id_migration() != 0) {
		(void) fprintf(stderr, "Failed bundle_id_migration()\n");
		return (1);
	}
	if (load_bundles() != 0) {
		(void) fprintf(stderr, "Failed load_bundles()\n");
		return (1);
	}
	if (decorate_dependents() != 0) {
		(void) fprintf(stderr, "Failed decorate_dependents()\n");
		return (1);
	}
	if (update_decoration_layer() != 0) {
		(void) fprintf(stderr, "Failed update_decoration_layer()\n");
		return (1);
	}
	if (delete_last_import_snapshots() != 0) {
		(void) fprintf(stderr,
		    "Failed delete_last_import_snapshots()\n");
		return (1);
	}
	if (decorate_higher_entities() != 0) {
		(void) fprintf(stderr, "Failed decorate_higher_entities()\n");
		return (1);
	}
	if (finalize_tables() != 0) {
		(void) fprintf(stderr, "Failed finalize_tables()\n");
		return (1);
	}

	if (!keep_tmp_tables && drop_temporary_tables() != 0) {
		(void) fprintf(stderr, "Failed drop_temporary_tables()\n");
		return (1);
	}

	if (!keep_schema_version && update_schema_version(6, 5) != 0) {
		(void) fprintf(stderr, "Failed update_schema_version()\n");
		return (1);
	}

	return (6);
}

typedef struct snplvl_dec_set {
	int	cnt;
	uint32_t layer;
	uint32_t bundle_id;
	uint32_t gen_id;
	uint32_t flags;
} snplvl_dec_set_t;

int decset_pg_or_prop;

/*
 * Using the data that is passed in here to create a list of layer/bundle
 * sets that are associated with the properties of this pg.
 *
 * If an admin layer decoration is passed in, and has a bunlde id associated
 * with it, then add it to the list.
 *
 * If an admin layer is passed in and has no bundle_id associated with it, then
 * throw it out, because we don't know that the pg was touched by this
 * administrative change.
 */
/*ARGSUSED*/
int
build_dec_set(void *arg, int columns, char **vals, char **names)
{
	snplvl_dec_set_t **decset = arg;
	uint32_t l, b, g;
	int set;

	string_to_id(vals[0], &l, names[0]);
	string_to_id(vals[1], &b, names[1]);
	string_to_id(vals[2], &g, names[2]);

	if (decset_pg_or_prop == 0 && l == SCF_DECORATION_ADMIN && b == 0)
		return (DB_CALLBACK_CONTINUE);

	if (decset[0] == NULL)
		set = 0;
	else
		set = decset[0]->cnt;

	decset[set] = (snplvl_dec_set_t *)calloc(1, sizeof (snplvl_dec_set_t));

	decset[set]->layer = l;
	decset[set]->bundle_id = b;
	decset[set]->gen_id = g;
	decset[set]->flags = 0;
	decset[0]->cnt++;

	return (DB_CALLBACK_CONTINUE);
}

/*
 * For each row we ned to create a set of decorations that would have been
 * present at the time this snapshot was taken.
 *
 * In this data we need to store at most one decoration for each layer, and
 * in that decoration store the following information :
 * 	1. layer
 * 	2. entity type : taken from snaplvl_lnk_tbl row we are processing.
 * 	3. bundle_id : taken from a property at this layer (note there could
 * 		be multiple bundle_id entries so this is the exception to the
 * 		rule "at most one per layer".
 * 	4. decoration_flags : This will have to be an approximation of the flags
 * 		from the pg entry if they are available, if not then we will
 * 		have to make an assumption based on property flags.  The main
 * 		thing to collect here is the pg masked or not at this snapshot.
 *
 * 		At this time I'm leaning towards erring on the side of "It's not
 * 		masked" if we cannot determine it's state.  I would rather have
 * 		the pg there, and simply have the user re-mask the pg, as
 * 		opposed to have it disappear for no reason.
 *
 * 	decoration_key, decoration_id will be newly assigned to this set.
 * 	decoration_type is the constant DECORATION_TYPE_PG
 * 	decoration_value_id = 0
 * 	time stamp values are 0 as with other upgrade operations.
 *
 * query columns (from snaplevel_lnk_tbl)
 *     0 snaplvl_level_id
 *     1 snaplvl_pg_id
 *     2 snaplvl_pg_type
 *     3 snaplvl_pg_flags
 *     4 snaplvl_gen_id
 */
/*ARGSUSED*/
int
create_snaplvl_decorations(void *arg, int columns, char **vals, char **names)
{
	struct run_single_int_info info;

	snplvl_dec_set_t **decset;

	uint32_t seen;
	uint32_t pg_gen_id = 0;
	uint32_t snplvl_dec_id = 0;
	char *emsg;
	uint32_t maxdecs;
	int r;
	int i;

	/*
	 * Get a dec_id for this set of decorations.
	 */
	notify_cnt++;
	if ((snplvl_dec_id = new_id(BACKEND_ID_DECORATION)) == 0)
		return (DB_CALLBACK_ABORT);

	info.rs_out = &maxdecs;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db,
	    "SELECT count() FROM decoration_tbl WHERE decoration_id IN "
	    "    (SELECT lnk_decoration_id FROM "
	    "        prop_lnk_tbl WHERE lnk_pg_id = '%q') OR decoration_id IN "
	    "    (SELECT pg_dec_id FROM pg_tbl WHERE pg_id = '%q') ",
	    run_single_int_callback, &info, &emsg, vals[1], vals[1]);

	if (r != SQLITE_OK)
		return (DB_CALLBACK_ABORT);

	if (maxdecs == 0) {
		maxdecs = 1;
		decset = calloc(maxdecs * 2, sizeof (snplvl_dec_set_t *));

		goto admin_dec;
	}

	decset = calloc(maxdecs * 2, sizeof (snplvl_dec_set_t *));

	/*
	 * Does the property group (snaplvl_lnk_tbl pg reference) have a gen_id
	 * of 0?
	 *
	 * If the pg_gen_id is not 0 then, go through the property entries that
	 * are at this generation and less to create a set of decoration for
	 * this pg.  We need to get at most one entry for each layer if it
	 * exists.
	 *
	 */
	string_to_id(vals[4], &pg_gen_id, names[4]);
	if (pg_gen_id != 0) {
		/*
		 * Get layer/bundle sets for this pg based on the properties
		 * that have contributed up to this point.
		 */
		decset_pg_or_prop = 0;
		r = sqlite_exec_printf(g_db,
		    "SELECT DISTINCT decoration_layer, decoration_bundle_id, "
		    "    decoration_gen_id FROM decoration_tbl "
		    "    WHERE decoration_id IN "
		    "    (SELECT lnk_decoration_id FROM prop_lnk_tbl "
		    "        WHERE lnk_pg_id = '%q') AND "
		    "    decoration_gen_id <= %d; ",
		    build_dec_set, decset, &emsg, vals[1], pg_gen_id);
	}

	/*
	 * If the gen_id is 0 or did not find anything in the properties to help
	 * decorate the pg, then we will have to trust what's in the pg's
	 * decorations to create the decoration layer.
	 */
	if (pg_gen_id == 0 || decset[0] == NULL) {
		decset_pg_or_prop = 1;
		r = sqlite_exec_printf(g_db,
		    "SELECT DISTINCT decoration_layer, decoration_bundle_id, "
		    "    decoration_gen_id FROM decoration_tbl "
		    "    WHERE decoration_id = (SELECT pg_dec_id FROM pg_tbl "
		    "        WHERE pg_id = '%q' LIMIT 1) "
		    "    AND decoration_gen_id = 0; ",
		    build_dec_set, decset, &emsg, vals[1]);
	}

	/*
	 * Now see if the pg is masked at this generation or less, to see if
	 * we need to mask this set of decorations, by simply adding an
	 * administrative decoration to this set.
	 */
	seen = 0;
	info.rs_out = &seen;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db,
	    "SELECT decoration_gen_id FROM decoration_tbl "
	    "    WHERE decoration_id IN (SELECT pg_dec_id FROM pg_tbl "
	    "        WHERE pg_id = '%q') AND decoration_gen_id <= %d AND "
	    "        (decoration_flags & %d) != 0 "
	    "        ORDER BY decoration_gen_id DESC LIMIT 1",
	    run_single_int_callback, &info, &emsg, vals[1], pg_gen_id,
	    DECORATION_MASK);

	/*
	 * If a masked flag is seen then mark it as such at the generation
	 * it was seen at.
	 */
	if (info.rs_result == REP_PROTOCOL_SUCCESS && seen > 0) {
		int set;

		if (decset[0] == NULL)
			set = 0;
		else
			set = decset[0]->cnt;

		decset[set] =
		    (snplvl_dec_set_t *)calloc(1, sizeof (snplvl_dec_set_t));
		decset[set]->layer = SCF_DECORATION_ADMIN;
		decset[set]->bundle_id = 0;
		decset[set]->gen_id = seen;
		decset[set]->flags = DECORATION_MASK;
		decset[0]->cnt++;
	}

	/*
	 * If there is still no determined decoration for this snapshot of
	 * the pg, we simply do not have the data to know where this pg was
	 * at the time the snapshot was taken.  So create a simple admin
	 * decoration as a best guess.
	 */
admin_dec:
	if (decset[0] == NULL) {
		decset[0] =
		    (snplvl_dec_set_t *)calloc(1, sizeof (snplvl_dec_set_t));
		decset[0]->layer = SCF_DECORATION_ADMIN;
		decset[0]->bundle_id = 0;
		decset[0]->flags = 0;
		decset[0]->cnt = 0;
	}

	r = sqlite_exec_printf(g_db,
	    "BEGIN TRANSACTION; "
	    "UPDATE snaplevel_lnk_tbl_tmp SET snaplvl_dec_id = %d "
	    "    WHERE snaplvl_level_id = '%q' AND snaplvl_pg_id = '%q' "
	    "    AND snaplvl_gen_id = '%q'; ",
	    NULL, NULL, &emsg, snplvl_dec_id, vals[0], vals[1], vals[4]);


	i = 0;
	while (decset[i] != NULL) {
		if (r == SQLITE_OK) {
			r = sqlite_exec_printf(g_db,
			    "INSERT INTO decoration_tbl ( "
			    "    decoration_id, decoration_entity_type, "
			    "    decoration_value_id, decoration_gen_id, "
			    "    decoration_layer, decoration_bundle_id, "
			    "    decoration_type, decoration_flags) "
			    "VALUES (%d, %Q, 0, %d, %d, %d, %d, %d); ",
			    NULL, NULL, &emsg, snplvl_dec_id, vals[2],
			    decset[i]->gen_id, decset[i]->layer,
			    decset[i]->bundle_id, DECORATION_TYPE_PG,
			    decset[i]->flags);
		}

		free(decset[i]);
		decset[i] = NULL;
		i++;
	}

	if (r != SQLITE_OK) {
		(void) printf("Failed to add the decoration set for "
		    "snaplevel_lnk_tbl id %s : %s\n", vals[0], emsg);

		free(decset);
		return (DB_CALLBACK_ABORT);
	}

	free(decset);
	r = sqlite_exec(g_db, "COMMIT TRANSACTION; ", NULL, NULL, &emsg);

	if (r != SQLITE_OK) {
		(void) printf("Failed to add the decoration set for "
		    "snaplevel_lnk_tbl id %s : %s\n", vals[0], emsg);

		return (DB_CALLBACK_ABORT);
	}

	return (DB_CALLBACK_CONTINUE);
}

/*
 * upgrade SMF repository db to the new format for schema version 7
 *
 * Add decorations of property groups to snapshots.  This will add a
 * column to the snaplevel_lnk_tbl for a decoration id that will be filled
 * based on data available at the generation in which the snapshot is
 * taken.
 *
 * return 7 (new schema version) on success, 1 on error
 */
int
smf_repo_upgrade_to_7(void)
{
	struct run_single_int_info info;
	char *emsg;
	int r;

	info.rs_out = (uint32_t *)&notify_cnt_max;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	if (sqlite_exec(g_db, "SELECT count() FROM snaplevel_lnk_tbl ",
	    run_single_int_callback, &info, &emsg) != SQLITE_OK)
		return (1);

	r = sqlite_exec(g_db,
	    "BEGIN TRANSACTION; "
	    "CREATE TABLE snaplevel_lnk_tbl_tmp ( "
	    "    snaplvl_level_id INTEGER NOT NULL, "
	    "    snaplvl_pg_id INTEGER NOT NULL, "
	    "    snaplvl_pg_name CHAR(256) NOT NULL, "
	    "    snaplvl_pg_type CHAR(256) NOT NULL, "
	    "    snaplvl_pg_flags INTEGER NOT NULL, "
	    "    snaplvl_gen_id INTEGER NOT NULL, "
	    "    snaplvl_dec_id INTEGER NOT NULL DEFAULT 0); "
	    "INSERT INTO snaplevel_lnk_tbl_tmp ( "
	    "    snaplvl_level_id, snaplvl_pg_id, snaplvl_pg_name, "
	    "    snaplvl_pg_type, snaplvl_pg_flags, snaplvl_gen_id) "
	    "    SELECT snaplvl_level_id, snaplvl_pg_id, snaplvl_pg_name, "
	    "         snaplvl_pg_type, snaplvl_pg_flags, snaplvl_gen_id "
	    "         FROM snaplevel_lnk_tbl; "
	    "COMMIT TRANSACTION; ",
	    NULL, NULL, &emsg);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr,
		    "Failed create the new snaplevel_lnk_tbl column "
		    "snaplvl_pg_deckey: %s\n", emsg);
		return (1);
	}

	if ((r = sqlite_exec(g_db, "SELECT snaplvl_level_id, snaplvl_pg_id, "
	    "snaplvl_pg_type, snaplvl_pg_flags, snaplvl_gen_id "
	    "FROM snaplevel_lnk_tbl ",
	    create_snaplvl_decorations, NULL, &emsg)) != 0) {
		(void) fprintf(stderr, "Failed to assign snaplevel decoration "
		    "keys : %s\n", emsg);
	}

	r = sqlite_exec_printf(g_db,
	    "BEGIN TRANSACTION; "
	    "DROP TABLE snaplevel_lnk_tbl; "
	    "CREATE TABLE snaplevel_lnk_tbl ( "
	    "    snaplvl_level_id INTEGER NOT NULL, "
	    "    snaplvl_pg_id INTEGER NOT NULL, "
	    "    snaplvl_pg_name CHAR(256) NOT NULL, "
	    "    snaplvl_pg_type CHAR(256) NOT NULL, "
	    "    snaplvl_pg_flags INTEGER NOT NULL, "
	    "    snaplvl_gen_id INTEGER NOT NULL, "
	    "    snaplvl_dec_id INTEGER NOT NULL DEFAULT 0); "
	    "INSERT INTO snaplevel_lnk_tbl ( "
	    "    snaplvl_level_id, snaplvl_pg_id, snaplvl_pg_name, "
	    "    snaplvl_pg_type, snaplvl_pg_flags, snaplvl_gen_id, "
	    "    snaplvl_dec_id) "
	    "    SELECT snaplvl_level_id, snaplvl_pg_id, snaplvl_pg_name, "
	    "         snaplvl_pg_type, snaplvl_pg_flags, snaplvl_gen_id, "
	    "         snaplvl_dec_id FROM snaplevel_lnk_tbl_tmp; "
	    "CREATE INDEX snaplevel_lnk_tbl_id "
	    "    ON snaplevel_lnk_tbl (snaplvl_pg_id); "
	    "CREATE INDEX snaplevel_lnk_tbl_level "
	    "    ON snaplevel_lnk_tbl (snaplvl_level_id); "
	    "CREATE INDEX snaplevel_lnk_tbl_pg_gen "
	    "    ON snaplevel_lnk_tbl (snaplvl_pg_id, snaplvl_gen_id); "
	    "UPDATE id_tbl SET id_next = (SELECT decoration_key "
	    "        FROM decoration_tbl ORDER BY decoration_key "
	    "        DESC LIMIT 1) + 1 "
	    "    WHERE (id_name = '%q');"
	    "COMMIT TRANSACTION; "
	    "VACUUM; ", NULL, NULL, &emsg,
	    id_space_to_name(BACKEND_KEY_DECORATION));


	if (!keep_schema_version && update_schema_version(7, 6) != 0) {
		(void) fprintf(stderr, "Failed update_schema_version()\n");
		return (1);
	}

	return (7);
}

typedef struct fill_id_set {
	uint32_t *ids;
	uint32_t *genids;
	int offset;
} fill_id_set_t;

/*
 * Take a decoration id and possibly a generation id and add these to
 * a preallocated array as uint32_t values to be used later by the caller.
 *
 * query columns
 * 	0 decoration id
 * 	1 generation id (if genidarray is set in fill_id_set_t passed in as
 * 		arg)
 */
/*ARGSUSED*/
int
fill_id_array_cb(void *arg, int columns, char **vals, char **names)
{
	fill_id_set_t *idset = arg;
	uint32_t *idarry = idset->ids;
	uint32_t *genidarry = idset->genids;
	uint32_t myid;

	string_to_id(vals[0], &myid, names[0]);
	idarry[idset->offset] = myid;

	if (genidarry != NULL) {
		string_to_id(vals[1], &myid, names[1]);
		genidarry[idset->offset] = myid;
	}

	idset->offset++;

	return (DB_CALLBACK_CONTINUE);
}

/*
 * For each pg id given clear any duplicate property entries with a
 * prop_name = etc_svc_profile_generic_xml.  This is used to cleanup
 * the manifestfiles property groups for each service after the physical
 * file name properties were replaced with their link names.
 *
 * query columns
 * 	0 pg_id
 */
/*ARGSUSED*/
int
clear_dup_props_cb(void *arg, int columns, char **vals, char **names)
{
	struct run_single_int_info info;
	fill_id_set_t idset;

	uint32_t *decidarray;
	uint32_t pg_id;
	uint32_t total_cnt, distinct_cnt;
	char *emsg;
	int i, r;

	string_to_id(vals[0], &pg_id,  names[0]);

	info.rs_out = &total_cnt;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db, "SELECT count() FROM prop_lnk_tbl "
	    "WHERE lnk_pg_id = %d AND "
	    "    lnk_prop_name = '%q'; ",
	    run_single_int_callback, &info, &emsg, pg_id, GEN_PROF_PG);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (DB_CALLBACK_ABORT);
	}

	if (total_cnt == 0)
		return (DB_CALLBACK_CONTINUE);

	info.rs_out = &distinct_cnt;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db, "SELECT count() FROM (SELECT DISTINCT "
	    "lnk_gen_id  FROM prop_lnk_tbl WHERE lnk_pg_id = %d AND "
	    "    lnk_prop_name = '%q'); ",
	    run_single_int_callback, &info, &emsg, pg_id, GEN_PROF_PG);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (DB_CALLBACK_ABORT);
	}

	/*
	 * If the total count does not equal the distinct count then
	 * there is a duplicate property at one of the generations.
	 */
	if (total_cnt == distinct_cnt)
		return (DB_CALLBACK_CONTINUE);

	/*
	 * Get a list of the decoration_ids that do not match the first
	 * decoration id which are a list of decoration ids' and associated
	 * properties that must be gotten rid of.
	 */
	decidarray = (uint32_t *)malloc(total_cnt * sizeof (uint32_t));
	idset.ids = decidarray;
	idset.genids = NULL;
	idset.offset = 0;
	r = sqlite_exec_printf(g_db, "SELECT lnk_decoration_id "
	    "FROM prop_lnk_tbl WHERE lnk_decoration_id != "
	    "    (SELECT lnk_decoration_id FROM prop_lnk_tbl "
	    "        WHERE lnk_pg_id = %d AND "
	    "        lnk_prop_name = '%q' "
	    "        ORDER BY lnk_gen_id LIMIT 1) AND lnk_pg_id = %d AND "
	    "    lnk_prop_name = '%q'",
	    fill_id_array_cb, &idset, &emsg, pg_id, GEN_PROF_PG, pg_id,
	    GEN_PROF_PG);

	for (i = 0; i < idset.offset; i++) {
		r = sqlite_exec_printf(g_db, "DELETE FROM value_tbl "
		    "    WHERE value_id = (SELECT lnk_val_id FROM prop_lnk_tbl "
		    "        WHERE lnk_decoration_id = %d LIMIT 1); "
		    "DELETE FROM prop_lnk_tbl WHERE lnk_decoration_id = %d; "
		    "DELETE FROM decoration_tbl WHERE decoration_id = %d; ",
		    NULL, NULL, &emsg, decidarray[i], decidarray[i],
		    decidarray[i]);

		if (r != SQLITE_OK) {
			(void) fprintf(stderr, "%s\n", emsg);
			return (DB_CALLBACK_ABORT);
		}
	}

	free(decidarray);

	return (DB_CALLBACK_CONTINUE);
}

/*
 * upgrade SMF repository db to the new format for schema version 8
 *
 * Repair damage caused by the generic.xml profile being a symbolic link
 * to different files, depending on the argument given to netservices(1m).
 *
 * Replace the physical file names with the link name, and clear remnents
 * of the old data from the repository, clearing conflicts that may have
 * been created as well.
 *
 * return 8 (new schema version) on success, 1 on error
 */
int
smf_repo_upgrade_to_8(void)
{
	struct run_single_int_info info;

	fill_id_set_t idset;

	uint32_t generic_bid = 0;
	uint32_t limited_bid = 0;
	uint32_t open_bid = 0;
	uint32_t *decidarray;
	uint32_t *genidarray;
	uint32_t is_did_arry_sz;
	uint32_t p_did_arry_sz;
	char *emsg;
	int i, r;

	/*
	 * Check to see if there is a bundle_tbl entry for
	 * /etc/svc/profile/generic.xml and if so get the bundle id.
	 *
	 * If not create a new entry with a new bundle_id.
	 */
	if (get_or_add_bundle("/etc/svc/profile/generic.xml",
	    &generic_bid) != 0)
		return (1);

	/*
	 * Get the bundle_id for generic_limited_net.xml and for
	 * generic_open.xml (do not want to add these if they don't
	 * exist).
	 */
	info.rs_out = &limited_bid;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec(g_db, "SELECT bundle_id FROM bundle_tbl WHERE "
	    "bundle_name = '/etc/svc/profile/generic_limited_net.xml'; ",
	    run_single_int_callback, &info, &emsg);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	info.rs_out = &open_bid;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec(g_db, "SELECT bundle_id FROM bundle_tbl WHERE "
	    "bundle_name = '/etc/svc/profile/generic_open.xml'; ",
	    run_single_int_callback, &info, &emsg);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	/*
	 * Update the decorations to point to the right bundle_ids.
	 *
	 * For services and instances just add a new row for
	 * each decoration_id that has an entry for either of the old
	 * bundle_ids.  (make sure that an old bundle_id of 0 is accounted
	 * for).
	 *
	 * 1. First count the possible entries
	 * 2. Now allocate an array of integers the size of the count
	 * 3. fill in the array with the decoration_ids
	 * 4. insert a new row into the decoration_tbl for each entry in
	 * the array that represents the link file name.
	 * 5. delete the old entries from the decoration table that represent
	 * the physical file names.
	 *
	 * 6. Now handle the pgs and properties (this is almost the same except
	 * we must do one for each generation).
	 */
	info.rs_out = &is_did_arry_sz;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db, "SELECT count(decoration_id) FROM "
	    "(SELECT DISTINCT decoration_id FROM decoration_tbl "
	    "    WHERE (decoration_type = %d OR decoration_type = %d OR "
	    "        decoration_type = %d) AND "
	    "        (decoration_bundle_id = %d OR decoration_bundle_id = %d) "
	    "        AND decoration_bundle_id != 0); ",
	    run_single_int_callback, &info, &emsg, DECORATION_TYPE_SVC,
	    DECORATION_TYPE_INST, DECORATION_TYPE_PG, limited_bid, open_bid);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	info.rs_out = &p_did_arry_sz;
	info.rs_result = REP_PROTOCOL_FAIL_NOT_FOUND;
	r = sqlite_exec_printf(g_db, "SELECT count() FROM "
	    "(SELECT DISTINCT decoration_id, decoration_gen_id "
	    "    FROM decoration_tbl WHERE decoration_type = %d AND "
	    "        (decoration_bundle_id = %d OR decoration_bundle_id = %d) "
	    "        AND decoration_bundle_id != 0); ",
	    run_single_int_callback, &info, &emsg, DECORATION_TYPE_PROP,
	    limited_bid, open_bid);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	notify_cnt_max = is_did_arry_sz + p_did_arry_sz;
	decidarray = (uint32_t *)malloc(is_did_arry_sz * sizeof (uint32_t));

	idset.ids = decidarray;
	idset.genids = NULL;
	idset.offset = 0;

	r = sqlite_exec_printf(g_db, "SELECT DISTINCT decoration_id "
	    "FROM decoration_tbl WHERE (decoration_type = %d OR "
	    "    decoration_type = %d OR decoration_type = %d) AND "
	    "    (decoration_bundle_id = %d OR decoration_bundle_id = %d) AND "
	    "    decoration_bundle_id != 0; ",
	    fill_id_array_cb, &idset, &emsg, DECORATION_TYPE_SVC,
	    DECORATION_TYPE_INST, DECORATION_TYPE_PG, limited_bid, open_bid);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	for (i = 0; i < is_did_arry_sz; i++) {
		r = sqlite_exec_printf(g_db, "INSERT INTO decoration_tbl "
		    "(decoration_id, decoration_entity_type, "
		    "decoration_value_id, decoration_gen_id, decoration_layer, "
		    "decoration_bundle_id, decoration_type, decoration_flags, "
		    "decoration_tv_sec, decoration_tv_usec) "
		    "SELECT decoration_id, decoration_entity_type, "
		    "decoration_value_id, decoration_gen_id, decoration_layer, "
		    "%d, decoration_type, decoration_flags, decoration_tv_sec, "
		    "decoration_tv_usec FROM decoration_tbl "
		    "    WHERE decoration_id = %d AND "
		    "    (decoration_bundle_id = %d OR "
		    "    decoration_bundle_id = %d) AND "
		    "    decoration_bundle_id != 0 ORDER BY decoration_tv_sec "
		    "    DESC LIMIT 1; ",
		    NULL, NULL, &emsg, generic_bid, decidarray[i], limited_bid,
		    open_bid);

		if (r != SQLITE_OK) {
			(void) fprintf(stderr, "%s\n", emsg);
			return (1);
		}

		/*
		 * Remove the old decroation entries from the decoration table
		 * that represent the physical file names.
		 */
		r = sqlite_exec_printf(g_db, "DELETE FROM decoration_tbl "
		    "WHERE decoration_id = %d AND (decoration_bundle_id = %d "
		    "    OR decoration_bundle_id = %d) AND "
		    "    decoration_bundle_id != 0; ",
		    NULL, NULL, &emsg, decidarray[i], limited_bid, open_bid);

		if (r != SQLITE_OK) {
			(void) fprintf(stderr, "%s\n", emsg);
			return (1);
		}

		notify_cnt++;
	}

	free(decidarray);


	decidarray = (uint32_t *)malloc(p_did_arry_sz * sizeof (uint32_t));
	genidarray = (uint32_t *)malloc(p_did_arry_sz * sizeof (uint32_t));
	idset.ids = decidarray;
	idset.genids = genidarray;
	idset.offset = 0;

	r = sqlite_exec_printf(g_db,
	    "SELECT DISTINCT decoration_id, decoration_gen_id "
	    "    FROM decoration_tbl WHERE decoration_type = %d AND "
	    "        (decoration_bundle_id = %d OR decoration_bundle_id = %d) "
	    "        AND decoration_bundle_id != 0; ",
	    fill_id_array_cb, &idset, &emsg, DECORATION_TYPE_PROP, limited_bid,
	    open_bid);

	for (i = 0; i < p_did_arry_sz; i++) {
		r = sqlite_exec_printf(g_db, "INSERT INTO decoration_tbl "
		    "(decoration_id, decoration_entity_type, "
		    "decoration_value_id, decoration_gen_id, decoration_layer, "
		    "decoration_bundle_id, decoration_type, decoration_flags, "
		    "decoration_tv_sec, decoration_tv_usec) "
		    "SELECT decoration_id, decoration_entity_type, "
		    "decoration_value_id, decoration_gen_id, decoration_layer, "
		    "%d, decoration_type, decoration_flags, decoration_tv_sec, "
		    "decoration_tv_usec FROM decoration_tbl "
		    "    WHERE decoration_id = %d AND "
		    "    decoration_gen_id = %d AND "
		    "    (decoration_bundle_id = %d OR "
		    "    decoration_bundle_id = %d) AND "
		    "    decoration_bundle_id != 0 ORDER BY decoration_tv_sec "
		    "    DESC LIMIT 1; ",
		    NULL, NULL, &emsg, generic_bid, decidarray[i],
		    genidarray[i], limited_bid, open_bid);

		if (r != SQLITE_OK) {
			(void) fprintf(stderr, "%s\n", emsg);
			return (1);
		}

		r = sqlite_exec_printf(g_db, "DELETE FROM decoration_tbl "
		    "WHERE decoration_id = %d AND decoration_gen_id = %d AND "
		    "    (decoration_bundle_id = %d "
		    "    OR decoration_bundle_id = %d) AND "
		    "    decoration_bundle_id != 0; ",
		    NULL, NULL, &emsg, decidarray[i], genidarray[i],
		    limited_bid, open_bid);

		if (r != SQLITE_OK) {
			(void) fprintf(stderr, "%s\n", emsg);
			return (1);
		}

		notify_cnt++;
	}

	free(decidarray);
	free(genidarray);

	/*
	 * Reset the manifestfiles property group property names to correctly
	 * represent the symlink profile.
	 *
	 * Update the value tbl entry so that it points to the right name of
	 * the applied profile link (generic.xml).
	 */
	r = sqlite_exec_printf(g_db, "UPDATE prop_lnk_tbl "
	    "    SET lnk_prop_name = '%q' "
	    "    WHERE ((lnk_prop_name = '%q' OR "
	    "        lnk_prop_name = '%q') AND "
	    "        lnk_pg_id IN (SELECT pg_id FROM pg_tbl "
	    "            WHERE pg_name = 'manifestfiles')); "
	    "UPDATE value_tbl SET value_value = '/etc/svc/profile/generic.xml' "
	    "    WHERE value_id IN (SELECT lnk_val_id FROM prop_lnk_tbl "
	    "        WHERE lnk_prop_name = '%q'); ",
	    NULL, NULL, &emsg, GEN_PROF_PG, GEN_PROF_PG, GEN_PROF_PG,
	    GEN_PROF_PG);

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	/*
	 * Finally we need to make sure that none of the manifestfiles pg's
	 * have multiple entries for generic.xml at the same generation.
	 */
	r = sqlite_exec(g_db, "SELECT pg_id FROM pg_tbl "
	    "WHERE pg_name = 'manifestfiles'; ",
	    clear_dup_props_cb, NULL, &emsg);

	r = sqlite_exec_printf(g_db, "UPDATE id_tbl SET id_next = "
	    "(SELECT decoration_key FROM decoration_tbl "
	    "    ORDER BY decoration_key DESC LIMIT 1) + 1 "
	    "    WHERE (id_name = '%q'); ", NULL, NULL, &emsg,
	    id_space_to_name(BACKEND_KEY_DECORATION));

	if (r != SQLITE_OK) {
		(void) fprintf(stderr, "%s\n", emsg);
		return (1);
	}

	if (!keep_schema_version && update_schema_version(8, 7) != 0) {
		(void) fprintf(stderr, "Failed update_schema_version()\n");
		return (1);
	}

	if (load_alt_repo(tmp_repo, 8) < 0)
		return (1);

	return (8);
}

void
vacuum_clean(void)
{
	int r;
	char *emsg;

	if ((r = sqlite_exec(g_db,
	    "VACUUM",
	    NULL, NULL, &emsg)) != 0) {
		(void) fprintf(stderr, "vacuum failed: %d: %s\n", r, emsg);
	}
}

void
check_integrity(const char *dbpath)
{
	sqlite *db;
	char *emsg;

	/*
	 * We check the integrity of the db after we copied it back
	 */
	if ((db = sqlite_open(dbpath, 0600, &emsg)) == NULL) {
		(void) fprintf(stderr, "failed sqlite_open %s: %s\n",
		    dbpath, emsg);
		exit(1);
	}

	if (sqlite_exec(db,
	    "PRAGMA integrity_check;", NULL, NULL, &emsg) != 0) {
		(void) fprintf(stderr, "integrity_check failed: %s\n",
		    emsg);
	}

	sqlite_close(db);

}

int
repo_do_copy(int srcfd, int dstfd, size_t *sz)
{
	char *buf;
	off_t nrd, nwr, n, r_off = 0, w_off = 0;

	if ((buf = malloc(8192)) == NULL)
		return (-1);

	while ((nrd = read(srcfd, buf, 8192)) != 0) {
		if (nrd < 0) {
			if (errno == EINTR)
				continue;

			free(buf);
			return (-1);
		}

		r_off += nrd;

		nwr = 0;
		do {
			if ((n = write(dstfd, &buf[nwr], nrd - nwr)) < 0) {
				if (errno == EINTR)
					continue;

				free(buf);
				return (-1);
			}

			nwr += n;
			w_off += n;

		} while (nwr < nrd);
	}

	if (sz)
		*sz = w_off;

	free(buf);
	return (0);
}

int
repo_copy(const char *src, const char *dst, int remove_src)
{
	int srcfd, dstfd;
	char *tmppath = malloc(PATH_MAX);
	int res = 0;
	struct stat s_buf;
	size_t cpsz, sz;

	if (tmppath == NULL) {
		res = -1;
		goto out;
	}

	/*
	 * Create and open the related db files
	 */
	(void) strlcpy(tmppath, dst, PATH_MAX);
	sz = strlcat(tmppath, "-XXXXXX", PATH_MAX);
	assert(sz < PATH_MAX);
	if (sz >= PATH_MAX) {
		(void) fprintf(stderr,
		    "repository copy failed: strlcat %s: overflow\n", tmppath);
		abort();
	}

	if ((dstfd = mkstemp(tmppath)) < 0) {
		(void) fprintf(stderr,
		    "repository copy failed: mkstemp %s: %s\n",
		    tmppath, strerror(errno));
		res = -1;
		goto out;
	}

	if ((srcfd = open(src, O_RDONLY)) < 0) {
		(void) fprintf(stderr,
		    "repository copy failed: opening %s: %s\n",
		    src, strerror(errno));
		res = -1;
		goto errexit;
	}

	/*
	 * fstat the repository before copy for sanity check.
	 */
	if (fstat(srcfd, &s_buf) < 0) {
		(void) fprintf(stderr, "repository copy failed: fstat %s: %s\n",
		    src, strerror(errno));
		res = -1;
		goto errexit;
	}

	if ((res = repo_do_copy(srcfd, dstfd, &cpsz)) != 0) {
		(void) fprintf(stderr, "repo_do_copy failed %s -> %s: %s\n",
		    src, dst, strerror(errno));
		goto errexit;
	}

	if (cpsz != s_buf.st_size) {
		(void) fprintf(stderr,
		    "repository copy failed: incomplete copy\n");
		res = -1;
		goto errexit;
	}

	/*
	 * Rename tmppath to dst
	 */
	if (rename(tmppath, dst) < 0) {
		(void) fprintf(stderr,
		    "repository copy failed: rename %s to %s: %s\n",
		    tmppath, dst, strerror(errno));
		res = -1;
	}

errexit:
	if (res != 0 && unlink(tmppath) < 0)
		(void) fprintf(stderr,
		    "repository copy failed: remove %s: %s\n",
		    tmppath, strerror(errno));

	(void) close(srcfd);
	(void) close(dstfd);

out:
	free(tmppath);
	if (remove_src) {
		if (unlink(src) < 0)
			(void) fprintf(stderr,
			    "repository copy failed: remove %s: %s\n",
			    src, strerror(errno));
	}

	return (res);
}

const char *
build_tmp_basename(const char *dir, const char *filename)
{
	size_t len;
	char *buf;
	char *p;

	len = strlen(dir) + strlen(filename) + 1;

	if (len > PATH_MAX)
		return (NULL);

	if ((buf = malloc(len)) == NULL) {
		(void) fprintf(stderr, "failed allocating memory\n");
		exit(1);
	}

	(void) strlcpy(buf, dir, len);
	(void) strlcat(buf, filename, len);
	if ((p = strchr(get_fnamep(buf), '.')) != NULL)
		*p = '\0';

	return (buf);
}

/*
 * we check the existence of a concurrent upgrade by stating what should be
 * the temporary repository file on which the upgrade is ran. If it exists,
 * we bail claiming there is an upgrade already running.
 *
 * return 0 if we are alone, 1 otherwise
 */
int
check_concurrent_upgrade(const char *file)
{
	int r;
	struct stat stat_buf = {0};

	if ((r = stat(file, &stat_buf)) != 0 && errno == ENOENT) {
		/*
		 * file doesn't exist, return 0
		 */
		return (0);
	} else if (r != 0) {
		/*
		 * can't stat(2) file, bail.
		 */
		(void) fprintf(stderr, "stat(2) of %s failed: %s\n", file,
		    strerror(errno));
		exit(1);
	}

	return (1);
}

void
print_usage(const char *progname, FILE *f, boolean_t do_exit)
{
	(void) fprintf(f,
	    "Usage: %s [-hntud] <repository>\n\n"
	    "\t-h display this help\n"
	    "\t-n keep temporary tables\n"
	    "\t-u Do NOT update schema_version\n"
	    "\t-d keep auxiliary files\n\n", progname);

	if (do_exit)
		exit(UU_EXIT_USAGE);
}

/*
 * make a backup of the old repository and append old version to
 * its name
 */
int
backup_repo(const char *dbpath, int oldver)
{
	const char *pre_upgrade = NULL;
	char oldverstr[4];

	assert(oldver > 0 && oldver < 1000);

	if (sprintf((char *)&oldverstr, "%d", oldver) < 0)
		return (1);

	if ((pre_upgrade = append_fname(dbpath, oldverstr)) == NULL) {
		(void) fprintf(stderr,
		    "do_upgrade cannot save old repo: strlcat %s: overflow\n",
		    pre_upgrade);
	} else if (rename(dbpath, pre_upgrade) < 0) {
		(void) fprintf(stderr,
		    "do_upgrade failed: rename %s -> %s: %s\n",
		    dbpath, pre_upgrade, strerror(errno));
		return (1);
	}

	return (0);
}

#define	argserr(progname)	print_usage(progname, stderr, B_TRUE)

int
main(int argc, char **argv)
{
	pthread_t thread;

	char opt;
	const char *dbpath;
	char *emsg;
	const char *tmp_prefix;
	const char *progname;
	int oldver;
	int r;

	const char * const options = "nuhd";

	progname = uu_setpname(argv[0]);

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'n':
			keep_tmp_tables = B_TRUE;
			break;
		case 'u':
			keep_schema_version = B_TRUE;
			break;
		case 'd':
			keep_files = B_TRUE;
			break;
		case 'h':
		case '?':
		default:
			argserr(progname);
			/*NOTREACHED*/
		}
	}

	/*
	 * Process contract decorations can let us know if we are running in
	 * the context of svc:/system/repository:default, svccfg repository
	 * subcommand or as a standalone program.
	 *
	 * If so, we use /system/volatile as tmpfs, otherwise we use /tmp
	 */
	if ((r = contract_pr_check_decorations(SCF_SERVICE_CONFIGD, -1,
	    CT_CREATOR_CONFIGD, CT_AUX_REPO_UPGRADE)) == 0) {
		/*
		 * If no argument was given AND we are in
		 * svc:/system/repository:default context
		 * get the default repository location.
		 */
		dbpath = (argc > optind) ? argv[optind] : REPOSITORY_DB;

		tmp_dir = TMP_DIR;
	} else if (r == -1) {
		(void) fprintf(stderr,
		    "failed to get process contract decorations: %s\n",
		    strerror(errno));
		exit(1);
	} else {
		if (argc <= optind)
			argserr(progname);

		dbpath = argv[optind];
		tmp_dir = TMP_DIR_USR;
	}

	/*
	 * Check the schema_version of the repo passed as argument.
	 * 5: go ahead and do the upgrade.
	 * 6: do nothing, just exit
	 * -1: error, exit
	 * else: don't know how to upgrade, exit
	 */
	switch (rver = get_repo_version(dbpath)) {
	case 8:
		return (0);

	case 7:
	case 6:
	case 5:
		if ((errno = pthread_create(&thread, NULL, upgrade_notify,
		    NULL)) != 0) {
			(void) fprintf(stdout, "Upgrading SMF repository.  "
			    "This may take up to an hour on slow systems.\n");
			(void) fprintf(stderr, "Failed to create progress "
			    "report thread: %d\n", errno);
			(void) fflush(stdout);
		}
		break;

	case -1:
		/* message printed by the call */
		exit(1);

	default:
		(void) fprintf(stderr,
		    "Unknown schema_version = %d, expected 5 or 6\n", rver);
		exit(1);
	}

	oldver = rver;

	if ((tmp_prefix = build_tmp_basename(tmp_dir, get_fnamep(dbpath))) ==
	    NULL || (tmp_repo = append_fname(tmp_prefix, TMP_REPO)) == NULL ||
	    (alt_repo = append_fname(tmp_prefix, ALT_TMP_REPO)) == NULL ||
	    (svccfg_cmds = append_fname(tmp_prefix, SVCCFG_CMDS)) == NULL) {
		(void) fprintf(stderr, "failed memory allocation\n");
		exit(1);
	}

	if (check_concurrent_upgrade(tmp_repo) != 0) {
		/*
		 * Print a nice error message before leaving...
		 */
		(void) fprintf(stderr,
		    "\nThe file\n\n\t%s\n\nalready exists. It is likely that"
		    "\n\n\t%s\n\nis already being upgraded by another process."
		    "\nIf you are sure that's not the case, remove"
		    "\n\n\t%s\n\nto permit the upgrade to run.\n\n",
		    tmp_repo, dbpath, tmp_repo);

		exit(1);
	}

	/*
	 * copy repository to tmpfs.
	 */
	if (repo_copy(dbpath, tmp_repo, 0) != 0) {
		(void) fprintf(stderr,
		    "Failed to coping repository to tmpfs %s -> %s: %s\n",
		    dbpath, tmp_repo, strerror(errno));
		exit(1);
	}

	/*
	 * Up to here we can bail without any cleanup.
	 * From now on, we need to make sure we cleanup tmp_repo from the
	 * filesystem or we'll have false positives for concurrent upgrades
	 */

	/*
	 * open db at tmpfs
	 */
	if ((g_db = sqlite_open(tmp_repo, 0600, &emsg)) == NULL) {
		(void) fprintf(stderr, "failed sqlite_open %s: %s\n",
		    dbpath, emsg);
		cleanup_and_bail();
	}

	notify_cnt = 0;
	notify_cnt_max = 0;
	if (rver == 5 && (rver = smf_repo_upgrade_to_6()) != 6) {
		(void) fprintf(stderr, "\nRepository upgrade to version 6 "
		    "failed!\n");
		/*
		 * Cleanup temporary files and exit.
		 */
		cleanup_and_bail();
	}

	notify_cnt = 0;
	notify_cnt_max = 0;
	if (rver == 6 && (rver = smf_repo_upgrade_to_7()) != 7) {
		(void) fprintf(stderr, "\nRepository upgrade to version 7 "
		    "failed!\n");
		/*
		 * Cleanup temporary files and exit.
		 */
		cleanup_and_bail();
	}

	/*
	 * This is to fix a bug in the way the generic.xml sysmlinked profile
	 * was applied.  The repository.db needs to be cleared of references
	 * to the actual file and use the symlink name, and it's a one time
	 * operation that is better suited to this format.
	 */
	notify_cnt = 0;
	notify_cnt_max = 0;
	if (rver == 7 && (rver = smf_repo_upgrade_to_8()) != 8) {
		(void) fprintf(stderr, "\nRepository upgrade to version 8 "
		    "failed!\n");
		/*
		 * Cleanup temporary files and exit.
		 */
		cleanup_and_bail();
	}

	vacuum_clean();

	sqlite_close(g_db);

	if (backup_repo(dbpath, oldver) != 0)
		exit(1);

	/*
	 * copy repository back from tmpfs.
	 */
	if (repo_copy(tmp_repo, dbpath, 1) != 0) {
		(void) fprintf(stderr,
		    "Failed copying repository from tmpfs %s -> %s: %s\n",
		    dbpath, tmp_repo, strerror(errno));
		exit(1);
	}

	check_integrity(dbpath);

	if (!keep_files) {
		cleanup_file(alt_repo);
		cleanup_file(svccfg_cmds);
	}

	free((void *)tmp_prefix);
	free((void *)tmp_repo);
	free((void *)alt_repo);
	free((void *)svccfg_cmds);

	/*
	 * Put one new line just to make sure that any following output is
	 * not appended onto the upgrading messages or counters.
	 */
	(void) fprintf(stdout, "SMF repository upgrade complete\n");

	return (0);
}
