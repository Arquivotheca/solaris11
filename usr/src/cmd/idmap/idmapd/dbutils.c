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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Database related utility routines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <rpc/rpc.h>
#include <sys/sid.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <assert.h>
#include <sys/u8_textprep.h>
#include <alloca.h>
#include <libuutil.h>
#include <note.h>

#include "idmapd.h"
#include "adutils.h"
#include "string.h"
#include "idmap_priv.h"
#include "schema.h"
#include "nldaputils.h"
#include "idmap_lsa.h"


static idmap_retcode sql_compile_n_step_once(sqlite *, char *,
		sqlite_vm **, int *, int, const char ***);
static idmap_retcode lookup_localsid2pid(idmap_request_t *);
static idmap_retcode lookup_cache_name2sid(sqlite *, const char *,
	    const char *, char **, char **, idmap_rid_t *, idmap_id_type *);
static void		sid2pid_first_pass(lookup_state_t *,
				idmap_request_t *);
static void		sid2pid_second_pass(lookup_state_t *,
				idmap_request_t *);
static void		pid2sid_first_pass(lookup_state_t *,
				idmap_request_t *);
static void		pid2sid_second_pass(lookup_state_t *,
				idmap_request_t *);
static void		update_cache_sid2pid(lookup_state_t *,
				idmap_request_t *);
static void		update_cache_pid2sid(lookup_state_t *,
				idmap_request_t *);
static idmap_retcode	load_cfg_in_state(lookup_state_t *);
static void		cleanup_lookup_state(lookup_state_t *);

extern idmap_retcode	ad_lookup_batch(lookup_state_t *);

#define	EMPTY_NAME(name)	(*name == 0 || strcmp(name, "\"\"") == 0)

typedef enum init_db_option {
	FAIL_IF_CORRUPT = 0,
	REMOVE_IF_CORRUPT = 1
} init_db_option_t;

/*
 * Thread specific data to hold the database handles so that the
 * databases are not opened and closed for every request. It also
 * contains the sqlite busy handler structure.
 */

struct idmap_busy {
	const char *name;
	const int *delays;
	int delay_size;
	int total;
	int sec;
};


typedef struct idmap_tsd {
	sqlite *db_db;
	sqlite *cache_db;
	struct idmap_busy cache_busy;
	struct idmap_busy db_busy;
} idmap_tsd_t;

/*
 * Flags to indicate how local the directory we're consulting is.
 * If neither is set, it means the directory belongs to a remote forest.
 */
#define	DOMAIN_IS_LOCAL	0x01
#define	FOREST_IS_LOCAL	0x02

static const int cache_delay_table[] =
		{ 1, 2, 5, 10, 15, 20, 25, 30,  35,  40,
		50,  50, 60, 70, 80, 90, 100};

static const int db_delay_table[] =
		{ 5, 10, 15, 20, 30,  40,  55,  70, 100};


static pthread_key_t	idmap_tsd_key;

void
idmap_tsd_destroy(void *key)
{

	idmap_tsd_t	*tsd = (idmap_tsd_t *)key;
	if (tsd) {
		if (tsd->db_db)
			(void) sqlite_close(tsd->db_db);
		if (tsd->cache_db)
			(void) sqlite_close(tsd->cache_db);
		free(tsd);
	}
}

void
idmap_init_tsd_key(void)
{
	int rc;

	rc = pthread_key_create(&idmap_tsd_key, idmap_tsd_destroy);
	assert(rc == 0);
}



idmap_tsd_t *
idmap_get_tsd(void)
{
	idmap_tsd_t	*tsd;

	if ((tsd = pthread_getspecific(idmap_tsd_key)) == NULL) {
		/* No thread specific data so create it */
		if ((tsd = malloc(sizeof (*tsd))) != NULL) {
			/* Initialize thread specific data */
			(void) memset(tsd, 0, sizeof (*tsd));
			/* save the trhread specific data */
			if (pthread_setspecific(idmap_tsd_key, tsd) != 0) {
				/* Can't store key */
				free(tsd);
				tsd = NULL;
			}
		} else {
			tsd = NULL;
		}
	}

	return (tsd);
}

static
void
validate_get_mapped_ids_sid2pid(idmap_request_t *rq)
{
	int e;

	if (rq->from.name != NULL &&
	    u8_validate(rq->from.name, strlen(rq->from.name),
	    NULL, U8_VALIDATE_ENTIRE, &e) < 0) {
		rq->retcode = IDMAP_ERR_BAD_UTF8;
		return;
	}
	if (rq->from.domain != NULL &&
	    u8_validate(rq->from.domain, strlen(rq->from.domain),
	    NULL, U8_VALIDATE_ENTIRE, &e) < 0) {
		rq->retcode = IDMAP_ERR_BAD_UTF8;
		return;
	}

	if (!IS_ID_POSIX(rq->to)) {
		/*
		 * Starting point is Windows, but target is
		 * not UNIX.
		 */
		rq->retcode = IDMAP_ERR_ARG;
		return;
	}

	rq->retcode = IDMAP_SUCCESS;
}

static
void
validate_get_mapped_ids_pid2sid(idmap_request_t *rq)
{
	if (!IS_ID_SID(rq->to)) {
		/*
		 * Starting point is UNIX, but target is
		 * not Windows.
		 */
		rq->retcode = IDMAP_ERR_ARG;
		return;
	}

	rq->retcode = IDMAP_SUCCESS;
}

/*
 * A simple wrapper around u8_textprep_str() that returns the Unicode
 * lower-case version of some string.  The result must be freed.
 */
char *
tolower_u8(const char *s)
{
	char *res = NULL;
	char *outs;
	size_t inlen, outlen, inbytesleft, outbytesleft;
	int rc, err;

	/*
	 * u8_textprep_str() does not allocate memory.  The input and
	 * output buffers may differ in size (though that would be more
	 * likely when normalization is done).  We have to loop over it...
	 *
	 * To improve the chances that we can avoid looping we add 10
	 * bytes of output buffer room the first go around.
	 */
	inlen = inbytesleft = strlen(s);
	outlen = outbytesleft = inlen + 10;
	if ((res = malloc(outlen)) == NULL)
		return (NULL);
	outs = res;

	while ((rc = u8_textprep_str((char *)s, &inbytesleft, outs,
	    &outbytesleft, U8_TEXTPREP_TOLOWER, U8_UNICODE_LATEST, &err)) < 0 &&
	    err == E2BIG) {
		if ((res = realloc(res, outlen + inbytesleft)) == NULL)
			return (NULL);
		/* adjust input/output buffer pointers */
		s += (inlen - inbytesleft);
		outs = res + outlen - outbytesleft;
		/* adjust outbytesleft and outlen */
		outlen += inbytesleft;
		outbytesleft += inbytesleft;
	}

	if (rc < 0) {
		free(res);
		res = NULL;
		return (NULL);
	}

	res[outlen - outbytesleft] = '\0';

	return (res);
}

static int sql_exec_tran_no_cb(sqlite *db, char *sql, const char *dbname,
	const char *while_doing);


/*
 * Initialize 'dbname' using 'sql'
 */
static
int
init_db_instance(const char *dbname, int version,
	const char *detect_version_sql, char * const *sql,
	init_db_option_t opt, int *created, int *upgraded)
{
	int rc, curr_version;
	int tries = 1;
	int prio = LOG_NOTICE;
	sqlite *db = NULL;
	char *errmsg = NULL;

	*created = 0;
	*upgraded = 0;

	if (opt == REMOVE_IF_CORRUPT)
		tries = 3;

rinse_repeat:
	if (tries == 0) {
		idmapdlog(LOG_ERR, "Failed to initialize db %s", dbname);
		return (-1);
	}
	if (tries-- == 1)
		/* Last try, log errors */
		prio = LOG_ERR;

	db = sqlite_open(dbname, 0600, &errmsg);
	if (db == NULL) {
		idmapdlog(prio, "Error creating database %s (%s)",
		    dbname, CHECK_NULL(errmsg));
		sqlite_freemem(errmsg);
		if (opt == REMOVE_IF_CORRUPT)
			(void) unlink(dbname);
		goto rinse_repeat;
	}

	sqlite_busy_timeout(db, 3000);

	/* Detect current version of schema in the db, if any */
	curr_version = 0;
	if (detect_version_sql != NULL) {
		char **results;
		int nrow;

#ifdef	IDMAPD_DEBUG
		(void) fprintf(stderr, "Schema version detection SQL: %s\n",
		    detect_version_sql);
#endif	/* IDMAPD_DEBUG */
		rc = sqlite_get_table(db, detect_version_sql, &results,
		    &nrow, NULL, &errmsg);
		if (rc != SQLITE_OK) {
			idmapdlog(prio,
			    "Error detecting schema version of db %s (%s)",
			    dbname, errmsg);
			sqlite_freemem(errmsg);
			sqlite_free_table(results);
			sqlite_close(db);
			return (-1);
		}
		if (nrow != 1) {
			idmapdlog(prio,
			    "Error detecting schema version of db %s", dbname);
			sqlite_close(db);
			sqlite_free_table(results);
			return (-1);
		}
		curr_version = atol(results[1]);
		sqlite_free_table(results);
	}

	if (curr_version < 0) {
		if (opt == REMOVE_IF_CORRUPT)
			(void) unlink(dbname);
		goto rinse_repeat;
	}

	if (curr_version == version)
		goto done;

	/* Install or upgrade schema */
#ifdef	IDMAPD_DEBUG
	(void) fprintf(stderr, "Schema init/upgrade SQL: %s\n",
	    sql[curr_version]);
#endif	/* IDMAPD_DEBUG */
	rc = sql_exec_tran_no_cb(db, sql[curr_version], dbname,
	    (curr_version == 0) ? "installing schema" : "upgrading schema");
	if (rc != 0) {
		idmapdlog(prio, "Error %s schema for db %s", dbname,
		    (curr_version == 0) ? "installing schema" :
		    "upgrading schema");
		if (opt == REMOVE_IF_CORRUPT)
			(void) unlink(dbname);
		goto rinse_repeat;
	}

	*upgraded = (curr_version > 0);
	*created = (curr_version == 0);

done:
	(void) sqlite_close(db);
	return (0);
}


/*
 * This is the SQLite database busy handler that retries the SQL
 * operation until it is successful.
 */
int
/* LINTED E_FUNC_ARG_UNUSED */
idmap_sqlite_busy_handler(void *arg, const char *table_name, int count)
{
	struct idmap_busy	*busy = arg;
	int			delay;
	struct timespec		rqtp;

	if (count == 1)  {
		busy->total = 0;
		busy->sec = 2;
	}
	if (busy->total > 1000 * busy->sec) {
		idmapdlog(LOG_DEBUG,
		    "Thread %d waited %d sec for the %s database",
		    pthread_self(), busy->sec, busy->name);
		busy->sec++;
	}

	if (count <= busy->delay_size) {
		delay = busy->delays[count-1];
	} else {
		delay = busy->delays[busy->delay_size - 1];
	}
	busy->total += delay;
	rqtp.tv_sec = 0;
	rqtp.tv_nsec = delay * (NANOSEC / MILLISEC);
	(void) nanosleep(&rqtp, NULL);
	return (1);
}


/*
 * Get the database handle
 */
idmap_retcode
get_db_handle(sqlite **db)
{
	char		*errmsg;
	idmap_tsd_t	*tsd;

	/*
	 * Retrieve the db handle from thread-specific storage
	 * If none exists, open and store in thread-specific storage.
	 */
	if ((tsd = idmap_get_tsd()) == NULL) {
		idmapdlog(LOG_ERR,
		    "Error getting thread specific data for %s", IDMAP_DBNAME);
		return (IDMAP_ERR_MEMORY);
	}

	if (tsd->db_db == NULL) {
		tsd->db_db = sqlite_open(IDMAP_DBNAME, 0, &errmsg);
		if (tsd->db_db == NULL) {
			idmapdlog(LOG_ERR, "Error opening database %s (%s)",
			    IDMAP_DBNAME, CHECK_NULL(errmsg));
			sqlite_freemem(errmsg);
			return (IDMAP_ERR_DB);
		}

		tsd->db_busy.name = IDMAP_DBNAME;
		tsd->db_busy.delays = db_delay_table;
		tsd->db_busy.delay_size = sizeof (db_delay_table) /
		    sizeof (int);
		sqlite_busy_handler(tsd->db_db, idmap_sqlite_busy_handler,
		    &tsd->db_busy);
	}
	*db = tsd->db_db;
	return (IDMAP_SUCCESS);
}

/*
 * Get the cache handle
 */
idmap_retcode
get_cache_handle(sqlite **cache)
{
	char		*errmsg;
	idmap_tsd_t	*tsd;

	/*
	 * Retrieve the db handle from thread-specific storage
	 * If none exists, open and store in thread-specific storage.
	 */
	if ((tsd = idmap_get_tsd()) == NULL) {
		idmapdlog(LOG_ERR, "Error getting thread specific data for %s",
		    IDMAP_DBNAME);
		return (IDMAP_ERR_MEMORY);
	}

	if (tsd->cache_db == NULL) {
		tsd->cache_db = sqlite_open(IDMAP_CACHENAME, 0, &errmsg);
		if (tsd->cache_db == NULL) {
			idmapdlog(LOG_ERR, "Error opening database %s (%s)",
			    IDMAP_CACHENAME, CHECK_NULL(errmsg));
			sqlite_freemem(errmsg);
			return (IDMAP_ERR_DB);
		}

		tsd->cache_busy.name = IDMAP_CACHENAME;
		tsd->cache_busy.delays = cache_delay_table;
		tsd->cache_busy.delay_size = sizeof (cache_delay_table) /
		    sizeof (int);
		sqlite_busy_handler(tsd->cache_db, idmap_sqlite_busy_handler,
		    &tsd->cache_busy);
	}
	*cache = tsd->cache_db;
	return (IDMAP_SUCCESS);
}

/*
 * Initialize cache and db
 */
int
init_dbs()
{
	char *sql[4];
	int created, upgraded;

	/* name-based mappings; probably OK to blow away in a pinch(?) */
	sql[0] = DB_INSTALL_SQL;
	sql[1] = DB_UPGRADE_FROM_v1_SQL;
	sql[2] = NULL;

	if (init_db_instance(IDMAP_DBNAME, DB_VERSION, DB_VERSION_SQL, sql,
	    FAIL_IF_CORRUPT, &created, &upgraded) < 0)
		return (-1);

	/* mappings, name/SID lookup cache + ephemeral IDs; OK to blow away */
	sql[0] = CACHE_INSTALL_SQL;
	sql[1] = CACHE_UPGRADE_FROM_v1_SQL;
	sql[2] = CACHE_UPGRADE_FROM_v2_SQL;
	sql[3] = NULL;

	if (init_db_instance(IDMAP_CACHENAME, CACHE_VERSION, CACHE_VERSION_SQL,
	    sql, REMOVE_IF_CORRUPT, &created, &upgraded) < 0)
		return (-1);

	_idmapdstate.new_eph_db = (created || upgraded) ? 1 : 0;

	return (0);
}

/*
 * Finalize databases
 */
void
fini_dbs()
{
}

/*
 * This table is a listing of status codes that will be returned to the
 * client when a SQL command fails with the corresponding error message.
 */
static msg_table_t sqlmsgtable[] = {
	{IDMAP_ERR_U2W_NAMERULE_CONFLICT,
	"columns unixname, is_user, u2w_order are not unique"},
	{IDMAP_ERR_W2U_NAMERULE_CONFLICT,
	"columns winname, windomain, is_user, is_wuser, w2u_order are not"
	" unique"},
	{IDMAP_ERR_W2U_NAMERULE_CONFLICT, "Conflicting w2u namerules"},
	{-1, NULL}
};

/*
 * idmapd's version of string2stat to map SQLite messages to
 * status codes
 */
idmap_retcode
idmapd_string2stat(const char *msg)
{
	int i;
	for (i = 0; sqlmsgtable[i].msg; i++) {
		if (strcasecmp(sqlmsgtable[i].msg, msg) == 0)
			return (sqlmsgtable[i].retcode);
	}
	return (IDMAP_ERR_OTHER);
}

/*
 * Executes some SQL in a transaction.
 *
 * Returns 0 on success, -1 if it failed but the rollback succeeded, -2
 * if the rollback failed.
 */
static
int
sql_exec_tran_no_cb(sqlite *db, char *sql, const char *dbname,
	const char *while_doing)
{
	char		*errmsg = NULL;
	int		rc;

	rc = sqlite_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		idmapdlog(LOG_ERR, "Begin transaction failed (%s) "
		    "while %s (%s)", errmsg, while_doing, dbname);
		sqlite_freemem(errmsg);
		return (-1);
	}

	rc = sqlite_exec(db, sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		idmapdlog(LOG_ERR, "Database error (%s) while %s (%s)", errmsg,
		    while_doing, dbname);
		sqlite_freemem(errmsg);
		errmsg = NULL;
		goto rollback;
	}

	rc = sqlite_exec(db, "COMMIT TRANSACTION", NULL, NULL, &errmsg);
	if (rc == SQLITE_OK) {
		sqlite_freemem(errmsg);
		return (0);
	}

	idmapdlog(LOG_ERR, "Database commit error (%s) while s (%s)",
	    errmsg, while_doing, dbname);
	sqlite_freemem(errmsg);
	errmsg = NULL;

rollback:
	rc = sqlite_exec(db, "ROLLBACK TRANSACTION", NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		idmapdlog(LOG_ERR, "Rollback failed (%s) while %s (%s)",
		    errmsg, while_doing, dbname);
		sqlite_freemem(errmsg);
		return (-2);
	}
	sqlite_freemem(errmsg);

	return (-1);
}

/*
 * Execute the given SQL statment without using any callbacks
 */
idmap_retcode
sql_exec_no_cb(sqlite *db, const char *dbname, char *sql)
{
	char		*errmsg = NULL;
	int		r;
	idmap_retcode	retcode;

	r = sqlite_exec(db, sql, NULL, NULL, &errmsg);
	assert(r != SQLITE_LOCKED && r != SQLITE_BUSY);

	if (r != SQLITE_OK) {
		idmapdlog(LOG_ERR, "Database error on %s while executing %s "
		    "(%s)", dbname, sql, CHECK_NULL(errmsg));
		retcode = idmapd_string2stat(errmsg);
		if (errmsg != NULL)
			sqlite_freemem(errmsg);
		return (retcode);
	}

	return (IDMAP_SUCCESS);
}

/*
 * Generate expression that can be used in WHERE statements.
 * Examples:
 * <prefix> <col>      <op> <value>   <suffix>
 * ""       "unixuser" "="  "foo" "AND"
 */
idmap_retcode
gen_sql_expr_from_rule(idmap_namerule *rule, char **out)
{
	char	*s_windomain = NULL, *s_winname = NULL;
	char	*s_unixname = NULL;
	char	*dir;
	char	*lower_winname;
	int	retcode = IDMAP_SUCCESS;

	if (out == NULL)
		return (IDMAP_ERR_ARG);


	if (!EMPTY_STRING(rule->windomain)) {
		s_windomain =  sqlite_mprintf("AND windomain = %Q ",
		    rule->windomain);
		if (s_windomain == NULL) {
			retcode = IDMAP_ERR_MEMORY;
			goto out;
		}
	}

	if (!EMPTY_STRING(rule->winname)) {
		if ((lower_winname = tolower_u8(rule->winname)) == NULL)
			lower_winname = rule->winname;
		s_winname = sqlite_mprintf(
		    "AND winname = %Q AND is_wuser = %d ",
		    lower_winname, rule->is_wuser ? 1 : 0);
		if (lower_winname != rule->winname)
			free(lower_winname);
		if (s_winname == NULL) {
			retcode = IDMAP_ERR_MEMORY;
			goto out;
		}
	}

	if (!EMPTY_STRING(rule->unixname)) {
		s_unixname = sqlite_mprintf(
		    "AND unixname = %Q AND is_user = %d ",
		    rule->unixname, rule->is_user ? 1 : 0);
		if (s_unixname == NULL) {
			retcode = IDMAP_ERR_MEMORY;
			goto out;
		}
	}

	switch (rule->direction) {
	case IDMAP_DIRECTION_BI:
		dir = "AND w2u_order > 0 AND u2w_order > 0";
		break;
	case IDMAP_DIRECTION_W2U:
		dir = "AND w2u_order > 0"
		    " AND (u2w_order = 0 OR u2w_order ISNULL)";
		break;
	case IDMAP_DIRECTION_U2W:
		dir = "AND u2w_order > 0"
		    " AND (w2u_order = 0 OR w2u_order ISNULL)";
		break;
	default:
		dir = "";
		break;
	}

	*out = sqlite_mprintf("%s %s %s %s",
	    s_windomain ? s_windomain : "",
	    s_winname ? s_winname : "",
	    s_unixname ? s_unixname : "",
	    dir);

	if (*out == NULL) {
		retcode = IDMAP_ERR_MEMORY;
		idmapdlog(LOG_ERR, "Out of memory");
		goto out;
	}

out:
	if (s_windomain != NULL)
		sqlite_freemem(s_windomain);
	if (s_winname != NULL)
		sqlite_freemem(s_winname);
	if (s_unixname != NULL)
		sqlite_freemem(s_unixname);

	return (retcode);
}



/*
 * Generate and execute SQL statement for LIST RPC calls
 */
idmap_retcode
process_list_svc_sql(sqlite *db, const char *dbname, char *sql, uint64_t limit,
		int flag, list_svc_cb cb, void *result)
{
	list_cb_data_t	cb_data;
	char		*errmsg = NULL;
	int		r;
	idmap_retcode	retcode = IDMAP_ERR_INTERNAL;

	(void) memset(&cb_data, 0, sizeof (cb_data));
	cb_data.result = result;
	cb_data.limit = limit;
	cb_data.flag = flag;


	r = sqlite_exec(db, sql, cb, &cb_data, &errmsg);
	assert(r != SQLITE_LOCKED && r != SQLITE_BUSY);
	switch (r) {
	case SQLITE_OK:
		retcode = IDMAP_SUCCESS;
		break;

	default:
		retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Database error on %s while executing "
		    "%s (%s)", dbname, sql, CHECK_NULL(errmsg));
		break;
	}
	if (errmsg != NULL)
		sqlite_freemem(errmsg);
	return (retcode);
}

/*
 * This routine is called by callbacks that process the results of
 * LIST RPC calls to validate data and to allocate memory for
 * the result array.
 */
idmap_retcode
validate_list_cb_data(list_cb_data_t *cb_data, int argc, char **argv,
		int ncol, uchar_t **list, size_t valsize)
{
	size_t	nsize;
	void	*tmplist;

	if (cb_data->limit > 0 && cb_data->next == cb_data->limit)
		return (IDMAP_NEXT);

	if (argc < ncol || argv == NULL) {
		idmapdlog(LOG_ERR, "Invalid data");
		return (IDMAP_ERR_INTERNAL);
	}

	/* alloc in bulk to reduce number of reallocs */
	if (cb_data->next >= cb_data->len) {
		nsize = (cb_data->len + SIZE_INCR) * valsize;
		tmplist = realloc(*list, nsize);
		if (tmplist == NULL) {
			idmapdlog(LOG_ERR, "Out of memory");
			return (IDMAP_ERR_MEMORY);
		}
		*list = tmplist;
		(void) memset(*list + (cb_data->len * valsize), 0,
		    SIZE_INCR * valsize);
		cb_data->len += SIZE_INCR;
	}
	return (IDMAP_SUCCESS);
}

static
idmap_retcode
get_namerule_order(char *winname, char *windomain, char *unixname,
	int direction, int is_diagonal, int *w2u_order, int *u2w_order)
{
	*w2u_order = 0;
	*u2w_order = 0;

	/*
	 * Windows to UNIX lookup order:
	 *  1. winname@domain (or winname) to ""
	 *  2. winname@domain (or winname) to unixname
	 *  3. winname@* to ""
	 *  4. winname@* to unixname
	 *  5. *@domain (or *) to *
	 *  6. *@domain (or *) to ""
	 *  7. *@domain (or *) to unixname
	 *  8. *@* to *
	 *  9. *@* to ""
	 * 10. *@* to unixname
	 *
	 * winname is a special case of winname@domain when domain is the
	 * default domain. Similarly * is a special case of *@domain when
	 * domain is the default domain.
	 *
	 * Note that "" has priority over specific names because "" inhibits
	 * mappings and traditionally deny rules always had higher priority.
	 */
	if (direction != IDMAP_DIRECTION_U2W) {
		/* bi-directional or from windows to unix */
		if (winname == NULL)
			return (IDMAP_ERR_W2U_NAMERULE);
		else if (unixname == NULL)
			return (IDMAP_ERR_W2U_NAMERULE);
		else if (EMPTY_NAME(winname))
			return (IDMAP_ERR_W2U_NAMERULE);
		else if (*winname == '*' && windomain && *windomain == '*') {
			if (*unixname == '*')
				*w2u_order = 8;
			else if (EMPTY_NAME(unixname))
				*w2u_order = 9;
			else /* unixname == name */
				*w2u_order = 10;
		} else if (*winname == '*') {
			if (*unixname == '*')
				*w2u_order = 5;
			else if (EMPTY_NAME(unixname))
				*w2u_order = 6;
			else /* name */
				*w2u_order = 7;
		} else if (windomain != NULL && *windomain == '*') {
			/* winname == name */
			if (*unixname == '*')
				return (IDMAP_ERR_W2U_NAMERULE);
			else if (EMPTY_NAME(unixname))
				*w2u_order = 3;
			else /* name */
				*w2u_order = 4;
		} else  {
			/* winname == name && windomain == null or name */
			if (*unixname == '*')
				return (IDMAP_ERR_W2U_NAMERULE);
			else if (EMPTY_NAME(unixname))
				*w2u_order = 1;
			else /* name */
				*w2u_order = 2;
		}

	}

	/*
	 * 1. unixname to "", non-diagonal
	 * 2. unixname to winname@domain (or winname), non-diagonal
	 * 3. unixname to "", diagonal
	 * 4. unixname to winname@domain (or winname), diagonal
	 * 5. * to *@domain (or *), non-diagonal
	 * 5. * to *@domain (or *), diagonal
	 * 7. * to ""
	 * 8. * to winname@domain (or winname)
	 * 9. * to "", non-diagonal
	 * 10. * to winname@domain (or winname), diagonal
	 */
	if (direction != IDMAP_DIRECTION_W2U) {
		int diagonal = is_diagonal ? 1 : 0;

		/* bi-directional or from unix to windows */
		if (unixname == NULL || EMPTY_NAME(unixname))
			return (IDMAP_ERR_U2W_NAMERULE);
		else if (winname == NULL)
			return (IDMAP_ERR_U2W_NAMERULE);
		else if (windomain != NULL && *windomain == '*')
			return (IDMAP_ERR_U2W_NAMERULE);
		else if (*unixname == '*') {
			if (*winname == '*')
				*u2w_order = 5 + diagonal;
			else if (EMPTY_NAME(winname))
				*u2w_order = 7 + 2 * diagonal;
			else
				*u2w_order = 8 + 2 * diagonal;
		} else {
			if (*winname == '*')
				return (IDMAP_ERR_U2W_NAMERULE);
			else if (EMPTY_NAME(winname))
				*u2w_order = 1 + 2 * diagonal;
			else
				*u2w_order = 2 + 2 * diagonal;
		}
	}
	return (IDMAP_SUCCESS);
}

/*
 * Generate and execute SQL statement to add name-based mapping rule
 */
idmap_retcode
add_namerule(sqlite *db, idmap_namerule *rule)
{
	char		*sql = NULL;
	idmap_stat	retcode;
	char		*dom = NULL;
	char		*name;
	int		w2u_order, u2w_order;
	char		w2ubuf[11], u2wbuf[11];
	char		*canonname = NULL;
	char		*canondomain = NULL;

	retcode = get_namerule_order(rule->winname, rule->windomain,
	    rule->unixname, rule->direction,
	    rule->is_user == rule->is_wuser ? 0 : 1, &w2u_order, &u2w_order);
	if (retcode != IDMAP_SUCCESS)
		goto out;

	if (w2u_order)
		(void) snprintf(w2ubuf, sizeof (w2ubuf), "%d", w2u_order);
	if (u2w_order)
		(void) snprintf(u2wbuf, sizeof (u2wbuf), "%d", u2w_order);

	/*
	 * For the triggers on namerules table to work correctly:
	 * 1) Use NULL instead of 0 for w2u_order and u2w_order
	 * 2) Use "" instead of NULL for "no domain"
	 */

	name = rule->winname;
	dom = rule->windomain;

	RDLOCK_CONFIG();
	if (lookup_wksids_name2sid(name, dom,
	    &canonname, &canondomain,
	    NULL, NULL, NULL) == IDMAP_SUCCESS) {
		name = canonname;
		dom = canondomain;
	} else if (EMPTY_STRING(dom)) {
		if (_idmapdstate.cfg->pgcfg.default_domain)
			dom = _idmapdstate.cfg->pgcfg.default_domain;
		else
			dom = "";
	}
	sql = sqlite_mprintf("INSERT into namerules "
	    "(is_user, is_wuser, windomain, winname_display, is_nt4, "
	    "unixname, w2u_order, u2w_order) "
	    "VALUES(%d, %d, %Q, %Q, %d, %Q, %q, %q);",
	    rule->is_user ? 1 : 0, rule->is_wuser ? 1 : 0, dom,
	    name, rule->is_nt4 ? 1 : 0, rule->unixname,
	    w2u_order ? w2ubuf : NULL, u2w_order ? u2wbuf : NULL);
	UNLOCK_CONFIG();

	if (sql == NULL) {
		retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Out of memory");
		goto out;
	}

	retcode = sql_exec_no_cb(db, IDMAP_DBNAME, sql);

	if (retcode == IDMAP_ERR_OTHER)
		retcode = IDMAP_ERR_CFG;

out:
	free(canonname);
	free(canondomain);
	if (sql != NULL)
		sqlite_freemem(sql);
	return (retcode);
}

/*
 * Flush name-based mapping rules
 */
idmap_retcode
flush_namerules(sqlite *db)
{
	idmap_stat	retcode;

	retcode = sql_exec_no_cb(db, IDMAP_DBNAME, "DELETE FROM namerules;");

	return (retcode);
}

/*
 * Generate and execute SQL statement to remove a name-based mapping rule
 */
idmap_retcode
rm_namerule(sqlite *db, idmap_namerule *rule)
{
	char		*sql = NULL;
	idmap_stat	retcode;
	char		*expr = NULL;

	if (rule->direction < 0 && EMPTY_STRING(rule->windomain) &&
	    EMPTY_STRING(rule->winname) && EMPTY_STRING(rule->unixname))
		return (IDMAP_SUCCESS);

	retcode = gen_sql_expr_from_rule(rule, &expr);
	if (retcode != IDMAP_SUCCESS)
		goto out;

	sql = sqlite_mprintf("DELETE FROM namerules WHERE 1 %s;", expr);

	if (sql == NULL) {
		retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Out of memory");
		goto out;
	}


	retcode = sql_exec_no_cb(db, IDMAP_DBNAME, sql);

out:
	if (expr != NULL)
		sqlite_freemem(expr);
	if (sql != NULL)
		sqlite_freemem(sql);
	return (retcode);
}

/*
 * Compile the given SQL query and step just once.
 *
 * Input:
 * db  - db handle
 * sql - SQL statement
 *
 * Output:
 * vm     -  virtual SQL machine
 * ncol   - number of columns in the result
 * values - column values
 *
 * Return values:
 * IDMAP_SUCCESS
 * IDMAP_ERR_NOTFOUND
 * IDMAP_ERR_INTERNAL
 */

static
idmap_retcode
sql_compile_n_step_once(sqlite *db, char *sql, sqlite_vm **vm, int *ncol,
		int reqcol, const char ***values)
{
	char		*errmsg = NULL;
	int		r;

	if ((r = sqlite_compile(db, sql, NULL, vm, &errmsg)) != SQLITE_OK) {
		idmapdlog(LOG_ERR, "Database error during %s (%s)", sql,
		    CHECK_NULL(errmsg));
		sqlite_freemem(errmsg);
		return (IDMAP_ERR_INTERNAL);
	}

	r = sqlite_step(*vm, ncol, values, NULL);
	assert(r != SQLITE_LOCKED && r != SQLITE_BUSY);

	if (r == SQLITE_ROW) {
		if (ncol != NULL && *ncol < reqcol) {
			(void) sqlite_finalize(*vm, NULL);
			*vm = NULL;
			return (IDMAP_ERR_INTERNAL);
		}
		/* Caller will call finalize after using the results */
		return (IDMAP_SUCCESS);
	} else if (r == SQLITE_DONE) {
		(void) sqlite_finalize(*vm, NULL);
		*vm = NULL;
		return (IDMAP_ERR_NOTFOUND);
	}

	(void) sqlite_finalize(*vm, &errmsg);
	*vm = NULL;
	idmapdlog(LOG_ERR, "Database error during %s (%s)", sql,
	    CHECK_NULL(errmsg));
	sqlite_freemem(errmsg);
	return (IDMAP_ERR_INTERNAL);
}

idmap_retcode
process_mapping(idmap_request_t *requests, int nreq)
{
	idmap_retcode rc;
	sqlite		*cache = NULL, *db = NULL;
	lookup_state_t	state;
	int		i;
	bool_t		any_tracing;

	(void) memset(&state, 0, sizeof (state));

	state.nreq = nreq;
	state.requests = requests;

	/* Get cache handle */
	rc = get_cache_handle(&cache);
	if (rc != IDMAP_SUCCESS)
		goto out;
	state.cache = cache;

	/* Get db handle */
	rc = get_db_handle(&db);
	if (rc != IDMAP_SUCCESS)
		goto out;
	state.db = db;

	/* Allocate hash table to check for duplicate sids in a single batch */
	state.sid_history_size = nreq;
	state.sid_history = calloc(state.sid_history_size,
	    sizeof (*state.sid_history));
	if (state.sid_history == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		rc = IDMAP_ERR_MEMORY;
		goto out;
	}

	/* Get directory-based name mapping info */
	rc = load_cfg_in_state(&state);
	if (rc != IDMAP_SUCCESS)
		goto out;

	state.done = TRUE;

	any_tracing = FALSE;

	/* First stage */
	for (i = 0; i < state.nreq; i++) {
		idmap_request_t *rq = &state.requests[i];

		/* Zap this early so tracing doesn't show it. */
		if (IS_ID_POSIX(rq->to))
			rq->to.u.pid = IDMAP_SENTINEL_PID;

		if (TRACING(rq))
			any_tracing = TRUE;

		TRACE(rq, "Start mapping");
		if (IS_ID_SID(rq->from)) {
			sid2pid_first_pass(&state, rq);
		} else if (IS_ID_POSIX(rq->from)) {
			pid2sid_first_pass(&state, rq);
		} else {
			rq->retcode = IDMAP_ERR_ARG;
			TRACE(rq, "Bad \"from\" type");
			continue;
		}
		if (IDMAP_FATAL_ERROR(rq->retcode)) {
			rc = rq->retcode;
			goto out;
		}
	}

	/* Check if we are done */
	if (state.done)
		goto out;

	/*
	 * native LDAP lookups:
	 *  pid2sid:
	 *	- nldap or mixed mode. Lookup nldap by pid or unixname to get
	 *	  winname.
	 *  sid2pid:
	 *	- nldap mode. Got winname and sid (either given or found in
	 *	  name_cache). Lookup nldap by winname to get pid and
	 *	  unixname.
	 */
	if (state.nldap_nqueries > 0) {
		rc = nldap_lookup_batch(&state);
		if (IDMAP_FATAL_ERROR(rc)) {
			for (i = 0; i < state.nreq; i++) {
				idmap_request_t *rq = &state.requests[i];
				TRACE(rq, "Native LDAP lookup error=%d", rc);
			}
			goto out;
		}

		if (any_tracing) {
			for (i = 0; i < state.nreq; i++) {
				idmap_request_t *rq = &state.requests[i];
				TRACE(rq, "Native LDAP lookup");
			}
		}
	}

	/*
	 * AD lookups:
	 *  pid2sid:
	 *	- nldap or mixed mode. Got winname from nldap lookup.
	 *	  winname2sid could not be resolved locally. Lookup AD
	 *	  by winname to get sid.
	 *	- ad mode. Got unixname. Lookup AD by unixname to get
	 *	  winname and sid.
	 *  sid2pid:
	 *	- ad or mixed mode. Lookup AD by sid or winname to get
	 *	  winname, sid and unixname.
	 *	- any mode. Got either sid or winname but not both. Lookup
	 *	  AD by sid or winname to get winname, sid.
	 */
	if (state.ad_nqueries > 0) {
		rc = ad_lookup_batch(&state);
		if (IDMAP_FATAL_ERROR(rc)) {
			for (i = 0; i < state.nreq; i++) {
				idmap_request_t *const rq = &state.requests[i];
				TRACE(rq, "AD lookup error=%d", rc);
			}
			goto out;
		}
		for (i = 0; i < state.nreq; i++) {
			idmap_request_t *rq = &state.requests[i];
			if (IS_ID_SID(rq->from) &&
			    rq->retcode == IDMAP_ERR_DOMAIN_NOTFOUND &&
			    rq->from.u.sid.prefix != NULL &&
			    rq->from.name != NULL) {
				/*
				 * If AD lookup failed Domain Not Found but
				 * we have a winname and SID, it means that
				 * - LSA succeeded
				 * - it's a request a cross-forest trust
				 * and
				 * - we were looking for directory-based
				 *   mapping information.
				 * In that case, we're OK, just go on.
				 *
				 * If this seems more convoluted than it
				 * should be, it is - really, we probably
				 * shouldn't even be attempting AD lookups
				 * in this situation, but that's a more
				 * intricate cleanup that will have to wait
				 * for later.
				 */
				rq->retcode = IDMAP_SUCCESS;
				TRACE(rq,
				    "AD lookup - domain not found (ignored)");
				continue;
			}
		}
	}

	/*
	 * native LDAP lookups:
	 *  sid2pid:
	 *	- nldap mode. Got winname and sid from AD lookup. Lookup nldap
	 *	  by winname to get pid and unixname.
	 */
	if (state.nldap_nqueries > 0) {
		rc = nldap_lookup_batch(&state);
		if (IDMAP_FATAL_ERROR(rc)) {
			for (i = 0; i < state.nreq; i++) {
				idmap_request_t *rq = &state.requests[i];
				TRACE(rq, "Native LDAP lookup error=%d", rc);
			}
			goto out;
		}

		if (any_tracing) {
			for (i = 0; i < state.nreq; i++) {
				idmap_request_t *rq = &state.requests[i];
				TRACE(rq, "Native LDAP lookup");
			}
		}
	}

	/* Reset 'done' flags */
	state.done = TRUE;

	/* Second stage */
	for (i = 0; i < state.nreq; i++) {
		idmap_request_t *rq = &state.requests[i];
		if (IS_ID_SID(rq->from)) {
			sid2pid_second_pass(&state, rq);
		} else if (IS_ID_POSIX(rq->from)) {
			pid2sid_second_pass(&state, rq);
		} else {
			assert(0);
		}
		if (IDMAP_FATAL_ERROR(rq->retcode)) {
			rc = rq->retcode;
			goto out;
		}
	}

	/* Check if we are done */
	if (state.done)
		goto out;

	/* Update cache in a single transaction */
	if (sql_exec_no_cb(cache, IDMAP_CACHENAME, "BEGIN TRANSACTION;")
	    != IDMAP_SUCCESS) {
		/*
		 * NEEDSWORK:
		 * It seems wrong to discard the result code from the
		 * SQL operation, but at the same time it perhaps
		 * shouldn't be fatal to the mapping operations.
		 */
		goto out;
	}

	for (i = 0; i < state.nreq; i++) {
		idmap_request_t *rq = &state.requests[i];
		if (IS_ID_SID(rq->from)) {
			update_cache_sid2pid(&state, rq);
		} else if (IS_ID_POSIX(rq->from)) {
			update_cache_pid2sid(&state, rq);
		} else {
			assert(0);
		}
	}

	(void) sql_exec_no_cb(cache, IDMAP_CACHENAME,
	    "COMMIT TRANSACTION;");

out:
	cleanup_lookup_state(&state);

	for (i = 0; i < state.nreq; i++) {
		idmap_request_t *rq = &state.requests[i];

		if (!rq->do_how && rq->retcode == IDMAP_SUCCESS)
			idmap_how_clear(&rq->how);

		if (IDMAP_ERROR(rc))
			TRACE(rq, "Failure code %d", rc);
		else
			TRACE(rq, "Done");
	}

	return (rc);
}

/*
 * Load config in the state.
 *
 * nm_siduid and nm_sidgid fields:
 * state->nm_siduid represents mode used by sid2uid and uid2sid
 * requests for directory-based name mappings. Similarly,
 * state->nm_sidgid represents mode used by sid2gid and gid2sid
 * requests.
 *
 * sid2uid/uid2sid:
 * none       -> directory_based_mapping != DIRECTORY_MAPPING_NAME
 * AD-mode    -> !nldap_winname_attr && ad_unixuser_attr
 * nldap-mode -> nldap_winname_attr && !ad_unixuser_attr
 * mixed-mode -> nldap_winname_attr && ad_unixuser_attr
 *
 * sid2gid/gid2sid:
 * none       -> directory_based_mapping != DIRECTORY_MAPPING_NAME
 * AD-mode    -> !nldap_winname_attr && ad_unixgroup_attr
 * nldap-mode -> nldap_winname_attr && !ad_unixgroup_attr
 * mixed-mode -> nldap_winname_attr && ad_unixgroup_attr
 */
idmap_retcode
load_cfg_in_state(lookup_state_t *state)
{
	state->nm_siduid = IDMAP_NM_NONE;
	state->nm_sidgid = IDMAP_NM_NONE;
	RDLOCK_CONFIG();

	state->eph_map_unres_sids = 0;
	if (_idmapdstate.cfg->pgcfg.eph_map_unres_sids)
		state->eph_map_unres_sids = 1;

	state->directory_based_mapping =
	    _idmapdstate.cfg->pgcfg.directory_based_mapping;

	/* If we're not in domain mode, the rest is moot. */
	if (_idmapdstate.cfg->pgcfg.domain_name == NULL) {
		UNLOCK_CONFIG();
		return (IDMAP_SUCCESS);
	}

	state->domain_name = strdup(_idmapdstate.cfg->pgcfg.domain_name);
	if (state->domain_name == NULL) {
		UNLOCK_CONFIG();
		return (IDMAP_ERR_MEMORY);
	}

	if (_idmapdstate.cfg->pgcfg.default_domain != NULL) {
		state->defdom =
		    strdup(_idmapdstate.cfg->pgcfg.default_domain);
		if (state->defdom == NULL) {
			UNLOCK_CONFIG();
			return (IDMAP_ERR_MEMORY);
		}
	}

	if (_idmapdstate.cfg->pgcfg.directory_based_mapping !=
	    DIRECTORY_MAPPING_NAME) {
		UNLOCK_CONFIG();
		return (IDMAP_SUCCESS);
	}

	if (_idmapdstate.cfg->pgcfg.nldap_winname_attr != NULL) {
		state->nm_siduid =
		    (_idmapdstate.cfg->pgcfg.ad_unixuser_attr != NULL)
		    ? IDMAP_NM_MIXED : IDMAP_NM_NLDAP;
		state->nm_sidgid =
		    (_idmapdstate.cfg->pgcfg.ad_unixgroup_attr != NULL)
		    ? IDMAP_NM_MIXED : IDMAP_NM_NLDAP;
	} else {
		state->nm_siduid =
		    (_idmapdstate.cfg->pgcfg.ad_unixuser_attr != NULL)
		    ? IDMAP_NM_AD : IDMAP_NM_NONE;
		state->nm_sidgid =
		    (_idmapdstate.cfg->pgcfg.ad_unixgroup_attr != NULL)
		    ? IDMAP_NM_AD : IDMAP_NM_NONE;
	}
	if (_idmapdstate.cfg->pgcfg.ad_unixuser_attr != NULL) {
		state->ad_unixuser_attr =
		    strdup(_idmapdstate.cfg->pgcfg.ad_unixuser_attr);
		if (state->ad_unixuser_attr == NULL) {
			UNLOCK_CONFIG();
			return (IDMAP_ERR_MEMORY);
		}
	}
	if (_idmapdstate.cfg->pgcfg.ad_unixgroup_attr != NULL) {
		state->ad_unixgroup_attr =
		    strdup(_idmapdstate.cfg->pgcfg.ad_unixgroup_attr);
		if (state->ad_unixgroup_attr == NULL) {
			UNLOCK_CONFIG();
			return (IDMAP_ERR_MEMORY);
		}
	}
	if (_idmapdstate.cfg->pgcfg.nldap_winname_attr != NULL) {
		state->nldap_winname_attr =
		    strdup(_idmapdstate.cfg->pgcfg.nldap_winname_attr);
		if (state->nldap_winname_attr == NULL) {
			UNLOCK_CONFIG();
			return (IDMAP_ERR_MEMORY);
		}
	}
	UNLOCK_CONFIG();
	return (IDMAP_SUCCESS);
}

/*
 * Set the rule with specified values.
 * All the strings are copied.
 */
static void
idmap_namerule_set(idmap_namerule *rule, const char *windomain,
		const char *winname, const char *unixname, boolean_t is_user,
		boolean_t is_wuser, boolean_t is_nt4, int direction)
{
	/*
	 * Only update if they differ because we have to free
	 * and duplicate the strings
	 */
	if (rule->windomain == NULL || windomain == NULL ||
	    strcmp(rule->windomain, windomain) != 0) {
		if (rule->windomain != NULL) {
			free(rule->windomain);
			rule->windomain = NULL;
		}
		if (windomain != NULL)
			rule->windomain = strdup(windomain);
	}

	if (rule->winname == NULL || winname == NULL ||
	    strcmp(rule->winname, winname) != 0) {
		if (rule->winname != NULL) {
			free(rule->winname);
			rule->winname = NULL;
		}
		if (winname != NULL)
			rule->winname = strdup(winname);
	}

	if (rule->unixname == NULL || unixname == NULL ||
	    strcmp(rule->unixname, unixname) != 0) {
		if (rule->unixname != NULL) {
			free(rule->unixname);
			rule->unixname = NULL;
		}
		if (unixname != NULL)
			rule->unixname = strdup(unixname);
	}

	rule->is_user = is_user;
	rule->is_wuser = is_wuser;
	rule->is_nt4 = is_nt4;
	rule->direction = direction;
}

/*
 * Lookup well-known SIDs table either by winname or by SID.
 *
 * If the given winname or SID is a well-known SID then we set is_wksid
 * variable and then proceed to see if the SID has a hard mapping to
 * a particular UID/GID (Ex: Creator Owner/Creator Group mapped to
 * fixed ephemeral ids). The direction flag indicates whether we have
 * a mapping; UNDEF indicates that we do not.
 *
 * If we find a mapping then we return success, except for the
 * special case of IDMAP_SENTINEL_PID which indicates an inhibited mapping.
 *
 * If we find a matching entry, but no mapping, we supply SID, name, and type
 * information and return "not found".  Higher layers will probably
 * do ephemeral mapping.
 *
 * If we do not find a match, we return "not found" and leave the question
 * to higher layers.
 */
static
idmap_retcode
lookup_wksids_sid2pid(idmap_request_t *rq, int *is_wksid)
{
	const wksids_table_t *wksid;

	*is_wksid = 0;

	assert(rq->from.u.sid.prefix != NULL ||
	    rq->from.name != NULL);

	if (rq->from.u.sid.prefix != NULL) {
		wksid = find_wksid_by_sid(rq->from.u.sid.prefix,
		    rq->from.u.sid.rid, rq->to.type);
	} else {
		wksid = find_wksid_by_name(rq->from.name, rq->from.domain,
		    rq->to.type);
	}
	if (wksid == NULL)
		return (IDMAP_ERR_NOTFOUND);

	/* Found matching entry. */

	/* Fill in name if it was not already there. */
	if (rq->from.name == NULL) {
		rq->from.name = strdup(wksid->winname);
		if (rq->from.name == NULL)
			return (IDMAP_ERR_MEMORY);
	}

	/* Fill in SID if it was not already there */
	if (rq->from.u.sid.prefix == NULL) {
		if (wksid->sidprefix != NULL) {
			rq->from.u.sid.prefix =
			    strdup(wksid->sidprefix);
		} else {
			RDLOCK_CONFIG();
			rq->from.u.sid.prefix =
			    strdup(_idmapdstate.cfg->pgcfg.machine_sid);
			UNLOCK_CONFIG();
		}
		if (rq->from.u.sid.prefix == NULL)
			return (IDMAP_ERR_MEMORY);
		rq->from.u.sid.rid = wksid->rid;
	}

	/* Fill in the canonical domain if not already there */
	if (rq->from.domain == NULL) {
		const char *dom;

		RDLOCK_CONFIG();
		if (wksid->domain != NULL)
			dom = wksid->domain;
		else
			dom = _idmapdstate.hostname;
		rq->from.domain = strdup(dom);
		UNLOCK_CONFIG();
		if (rq->from.domain == NULL)
			return (IDMAP_ERR_MEMORY);
	}

	*is_wksid = 1;
	rq->dont_update_namecache = TRUE;

	rq->from.type = wksid->is_wuser ? IDMAP_USID : IDMAP_GSID;

	if (rq->to.type == IDMAP_POSIXID)
		rq->to.type = wksid->is_wuser ? IDMAP_UID : IDMAP_GID;

	if (wksid->direction == IDMAP_DIRECTION_UNDEF) {
		/*
		 * We don't have a mapping
		 * (But note that we may have supplied SID, name, or type
		 * information.)
		 */
		return (IDMAP_ERR_NOTFOUND);
	}

	/*
	 * We have an explicit mapping.
	 */
	if (wksid->pid == IDMAP_SENTINEL_PID) {
		/*
		 * ... which is that mapping is inhibited.
		 */
		return (IDMAP_ERR_NOMAPPING);
	}

	rq->to.u.pid = wksid->pid;

	rq->direction = wksid->direction;
	rq->how.map_type = IDMAP_MAP_TYPE_KNOWN_SID;
	rq->src = IDMAP_MAP_SRC_HARD_CODED;
	return (IDMAP_SUCCESS);
}


/*
 * Look for an entry mapping a PID to a SID.
 *
 * Note that direction=UNDEF entries do not specify a mapping,
 * and that IDMAP_SENTINEL_PID entries represent either an inhibited
 * mapping or an ephemeral mapping.  We don't handle either here;
 * they are filtered out by find_wksid_by_pid.
 */
static
idmap_retcode
lookup_wksids_pid2sid(idmap_request_t *rq)
{
	const wksids_table_t *wksid;

	wksid = find_wksid_by_pid(rq->from.u.pid, rq->from.type == IDMAP_UID);
	if (wksid == NULL)
		return (IDMAP_ERR_NOTFOUND);

	if (rq->to.type == IDMAP_SID) {
		rq->to.type = wksid->is_wuser ? IDMAP_USID : IDMAP_GSID;
	}
	rq->to.u.sid.rid = wksid->rid;

	if (wksid->sidprefix != NULL) {
		rq->to.u.sid.prefix =
		    strdup(wksid->sidprefix);
	} else {
		RDLOCK_CONFIG();
		rq->to.u.sid.prefix =
		    strdup(_idmapdstate.cfg->pgcfg.machine_sid);
		UNLOCK_CONFIG();
	}

	if (rq->to.u.sid.prefix == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		return (IDMAP_ERR_MEMORY);
	}

	/* Fill in name if it was not already there. */
	if (rq->to.name == NULL) {
		rq->to.name = strdup(wksid->winname);
		if (rq->to.name == NULL)
			return (IDMAP_ERR_MEMORY);
	}

	/* Fill in the canonical domain if not already there */
	if (rq->to.domain == NULL) {
		const char *dom;

		RDLOCK_CONFIG();
		if (wksid->domain != NULL)
			dom = wksid->domain;
		else
			dom = _idmapdstate.hostname;
		rq->to.domain = strdup(dom);
		UNLOCK_CONFIG();
		if (rq->to.domain == NULL)
			return (IDMAP_ERR_MEMORY);
	}

	rq->direction = wksid->direction;
	rq->how.map_type = IDMAP_MAP_TYPE_KNOWN_SID;
	rq->src = IDMAP_MAP_SRC_HARD_CODED;
	return (IDMAP_SUCCESS);
}

/*
 * Look up a name in the wksids list, matching name and, if supplied, domain,
 * and extract data.
 *
 * Given:
 * name		Windows user name
 * domain	Windows domain name (or NULL)
 *
 * Return:  Error code
 *
 * *canonname	canonical name (if canonname non-NULL) [1]
 * *canondomain	canonical domain (if canondomain non-NULL) [1]
 * *sidprefix	SID prefix (if sidprefix non-NULL) [1]
 * *rid		RID (if rid non-NULL) [2]
 * *type	Type (if type non-NULL) [2]
 *
 * [1] malloc'ed, NULL on error
 * [2] Undefined on error
 */
idmap_retcode
lookup_wksids_name2sid(
    const char *name,
    const char *domain,
    char **canonname,
    char **canondomain,
    char **sidprefix,
    idmap_rid_t *rid,
    idmap_id_type *type)
{
	const wksids_table_t *wksid;

	if (sidprefix != NULL)
		*sidprefix = NULL;
	if (canonname != NULL)
		*canonname = NULL;
	if (canondomain != NULL)
		*canondomain = NULL;

	wksid = find_wksid_by_name(name, domain, IDMAP_POSIXID);
	if (wksid == NULL)
		return (IDMAP_ERR_NOTFOUND);

	if (sidprefix != NULL) {
		if (wksid->sidprefix != NULL) {
			*sidprefix = strdup(wksid->sidprefix);
		} else {
			RDLOCK_CONFIG();
			*sidprefix = strdup(
			    _idmapdstate.cfg->pgcfg.machine_sid);
			UNLOCK_CONFIG();
		}
		if (*sidprefix == NULL)
			goto nomem;
	}

	if (rid != NULL)
		*rid = wksid->rid;

	if (canonname != NULL) {
		*canonname = strdup(wksid->winname);
		if (*canonname == NULL)
			goto nomem;
	}

	if (canondomain != NULL) {
		if (wksid->domain != NULL) {
			*canondomain = strdup(wksid->domain);
		} else {
			RDLOCK_CONFIG();
			*canondomain = strdup(_idmapdstate.hostname);
			UNLOCK_CONFIG();
		}
		if (*canondomain == NULL)
			goto nomem;
	}

	if (type != NULL)
		*type = (wksid->is_wuser) ?
		    IDMAP_USID : IDMAP_GSID;

	return (IDMAP_SUCCESS);

nomem:
	idmapdlog(LOG_ERR, "Out of memory");

	if (sidprefix != NULL) {
		free(*sidprefix);
		*sidprefix = NULL;
	}

	if (canonname != NULL) {
		free(*canonname);
		*canonname = NULL;
	}

	if (canondomain != NULL) {
		free(*canondomain);
		*canondomain = NULL;
	}

	return (IDMAP_ERR_MEMORY);
}

static
idmap_retcode
lookup_cache_sid2pid(sqlite *cache, idmap_request_t *rq)
{
	char		*end;
	char		*sql = NULL;
	const char	**values;
	sqlite_vm	*vm = NULL;
	int		ncol;
	posix_id_t	pid;
	time_t		curtime, exp;
	idmap_retcode	retcode;
	char		*is_user_string, *lower_name;
	int		direction;
	idmap_id_type	type;

	/* Current time */
	curtime = time(NULL);
	assert(curtime != (time_t)-1);

	switch (rq->to.type) {
	case IDMAP_UID:
		is_user_string = "1";
		break;
	case IDMAP_GID:
		is_user_string = "0";
		break;
	case IDMAP_POSIXID:
		/* the non-diagonal mapping */
		is_user_string = "is_wuser";
		break;
	default:
		assert(FALSE);
	}

	/* SQL to lookup the cache */

	if (rq->from.u.sid.prefix != NULL) {
		sql = sqlite_mprintf("SELECT pid, is_user, expiration, "
		    "unixname, u2w, is_wuser, "
		    "map_type, map_dn, map_attr, map_value, "
		    "map_windomain, map_winname, map_unixname, map_is_nt4 "
		    "FROM idmap_cache WHERE is_user = %s AND "
		    "sidprefix = %Q AND rid = %u AND w2u = 1 AND "
		    "(pid >= 2147483648 OR "
		    "(expiration = 0 OR expiration ISNULL OR "
		    "expiration > %d));",
		    is_user_string, rq->from.u.sid.prefix,
		    rq->from.u.sid.rid, curtime);
	} else {
		assert(rq->from.name != NULL);
		if ((lower_name = tolower_u8(rq->from.name)) == NULL)
			lower_name = rq->from.name;
		sql = sqlite_mprintf("SELECT pid, is_user, expiration, "
		    "unixname, u2w, is_wuser, "
		    "map_type, map_dn, map_attr, map_value, "
		    "map_windomain, map_winname, map_unixname, map_is_nt4 "
		    "FROM idmap_cache WHERE is_user = %s AND "
		    "winname = %Q AND windomain = %Q AND w2u = 1 AND "
		    "(pid >= 2147483648 OR "
		    "(expiration = 0 OR expiration ISNULL OR "
		    "expiration > %d));",
		    is_user_string, lower_name, rq->from.domain,
		    curtime);
		if (lower_name != rq->from.name)
			free(lower_name);
	}
	if (sql == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		retcode = IDMAP_ERR_MEMORY;
		goto out;
	}
	retcode = sql_compile_n_step_once(cache, sql, &vm, &ncol,
	    14, &values);
	sqlite_freemem(sql);

	if (retcode == IDMAP_ERR_NOTFOUND) {
		TRACE(rq, "Not found in mapping cache");
		goto out;
	}
	if (retcode != IDMAP_SUCCESS) {
		TRACE(rq, "Mapping cache lookup error=%d", retcode);
		goto out;
	}

	/* sanity checks */
	if (values[0] == NULL || values[1] == NULL) {
		retcode = IDMAP_ERR_CACHE;
		goto out;
	}

	pid = strtoul(values[0], &end, 10);
	type = uu_streq(values[1], "0") ? IDMAP_GID : IDMAP_UID;

	if (values[4] != NULL && atol(values[4]) != 0)
		direction = IDMAP_DIRECTION_BI;
	else
		direction = IDMAP_DIRECTION_W2U;


	/*
	 * We may have an expired ephemeral mapping. Consider
	 * the expired entry as valid if we are not going to
	 * perform further mapping. But do not renew the
	 * expiration.
	 * If we will be doing further mapping then store the
	 * ephemeral pid in the result so that we can use it
	 * if we end up doing dynamic mapping again.
	 */
	if (!rq->no_new_id_alloc &&
	    !rq->no_nameservice &&
	    IDMAP_ID_IS_EPHEMERAL(pid) && values[2] != NULL) {
		exp = atoll(values[2]);
		if (exp && exp <= curtime) {
			TRACE(rq, "Found expired mapping");
			/* Store the ephemeral pid */
			rq->expired.pid = pid;
			rq->expired.direction = direction;
			rq->expired.type = type;
			retcode = IDMAP_ERR_NOTFOUND;
			goto out;
		}
	}

	rq->to.type = type;
	rq->to.u.pid = pid;
	rq->direction = direction;

	if (values[3] != NULL) {
		free(rq->to.name);
		rq->to.name = strdup(values[3]);
		if (rq->to.name == NULL) {
			idmapdlog(LOG_ERR, "Out of memory");
			retcode = IDMAP_ERR_MEMORY;
		}
	}

	rq->from.type = uu_streq(values[5], "0") ? IDMAP_GSID : IDMAP_USID;

	if (rq->do_how) {
		rq->src = IDMAP_MAP_SRC_CACHE;
		rq->how.map_type = atol(values[6]);
		switch (rq->how.map_type) {
		case IDMAP_MAP_TYPE_DS_AD:
			rq->how.idmap_how_u.ad.dn = strdup(values[7]);
			rq->how.idmap_how_u.ad.attr = strdup(values[8]);
			rq->how.idmap_how_u.ad.value = strdup(values[9]);
			break;

		case IDMAP_MAP_TYPE_DS_NLDAP:
			rq->how.idmap_how_u.nldap.dn = strdup(values[7]);
			rq->how.idmap_how_u.nldap.attr = strdup(values[8]);
			rq->how.idmap_how_u.nldap.value = strdup(values[9]);
			break;

		case IDMAP_MAP_TYPE_RULE_BASED:
			rq->how.idmap_how_u.rule.windomain = strdup(values[10]);
			rq->how.idmap_how_u.rule.winname = strdup(values[11]);
			rq->how.idmap_how_u.rule.unixname = strdup(values[12]);
			rq->how.idmap_how_u.rule.is_nt4 = atol(values[13]);
			rq->how.idmap_how_u.rule.is_user = type == IDMAP_UID;
			rq->how.idmap_how_u.rule.is_wuser = atol(values[5]);
			break;

		case IDMAP_MAP_TYPE_EPHEMERAL:
			break;

		case IDMAP_MAP_TYPE_LOCAL_SID:
			break;

		case IDMAP_MAP_TYPE_KNOWN_SID:
			break;

		case IDMAP_MAP_TYPE_IDMU:
			rq->how.idmap_how_u.idmu.dn = strdup(values[7]);
			rq->how.idmap_how_u.idmu.attr = strdup(values[8]);
			rq->how.idmap_how_u.idmu.value = strdup(values[9]);
			break;

		default:
			/* Unknown mapping type */
			assert(FALSE);
		}
	}

	TRACE(rq, "Found in mapping cache");

out:
	if (vm != NULL)
		(void) sqlite_finalize(vm, NULL);
	return (retcode);
}

/*
 * Previous versions used two enumerations for representing types.
 * One of those has largely been eliminated, but was used in the
 * name cache table and so during an upgrade might still be visible.
 * In addition, the test suite prepopulates the cache with these values.
 *
 * This function translates those old values into the new values.
 *
 * This code deliberately does not use symbolic values for the legacy
 * values.  This is the *only* place where they should be used.
 */
static
idmap_id_type
xlate_legacy_type(int type)
{
	switch (type) {
	case -1004:	/* _IDMAP_T_USER */
		return (IDMAP_USID);
	case -1005:	/* _IDMAP_T_GROUP */
		return (IDMAP_GSID);
	default:
		return (type);
	}
	NOTE(NOTREACHED)
}

static
idmap_retcode
lookup_cache_sid2name(sqlite *cache, const char *sidprefix, idmap_rid_t rid,
		char **canonname, char **canondomain, idmap_id_type *type)
{
	char		*sql;
	const char	**values;
	sqlite_vm	*vm = NULL;
	int		ncol;
	time_t		curtime;
	idmap_retcode	retcode = IDMAP_SUCCESS;

	/* Get current time */
	curtime = time(NULL);
	assert(curtime != (time_t)-1);

	/* SQL to lookup the cache */
	sql = sqlite_mprintf("SELECT canon_name, domain, type "
	    "FROM name_cache WHERE "
	    "sidprefix = %Q AND rid = %u AND "
	    "(expiration = 0 OR expiration ISNULL OR "
	    "expiration > %d);",
	    sidprefix, rid, curtime);
	if (sql == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		retcode = IDMAP_ERR_MEMORY;
		goto out;
	}
	retcode = sql_compile_n_step_once(cache, sql, &vm, &ncol, 3, &values);
	sqlite_freemem(sql);

	if (retcode == IDMAP_SUCCESS) {
		if (type != NULL) {
			if (values[2] == NULL) {
				retcode = IDMAP_ERR_CACHE;
				goto out;
			}
			*type = xlate_legacy_type(atol(values[2]));
		}

		if (canonname != NULL && values[0] != NULL) {
			if ((*canonname = strdup(values[0])) == NULL) {
				idmapdlog(LOG_ERR, "Out of memory");
				retcode = IDMAP_ERR_MEMORY;
				goto out;
			}
		}

		if (canondomain != NULL && values[1] != NULL) {
			if ((*canondomain = strdup(values[1])) == NULL) {
				if (canonname != NULL) {
					free(*canonname);
					*canonname = NULL;
				}
				idmapdlog(LOG_ERR, "Out of memory");
				retcode = IDMAP_ERR_MEMORY;
				goto out;
			}
		}
	}

out:
	if (vm != NULL)
		(void) sqlite_finalize(vm, NULL);
	return (retcode);
}

/*
 * Given SID, find winname using name_cache OR
 * Given winname, find SID using name_cache.
 * Used when mapping win to unix i.e. rq->from is windows id and
 * rq->to is unix id
 */
static
idmap_retcode
lookup_name_cache(sqlite *cache, idmap_request_t *rq)
{
	idmap_id_type	type = -1;
	idmap_retcode	retcode;
	char		*sidprefix = NULL;
	idmap_rid_t	rid;
	char		*name = NULL, *domain = NULL;

	/* Done if we've both sid and winname */
	if (rq->from.u.sid.prefix != NULL && rq->from.name != NULL) {
		/* Don't bother TRACE()ing, too boring */
		return (IDMAP_SUCCESS);
	}

	if (rq->from.u.sid.prefix != NULL) {
		/* Lookup sid to winname */
		retcode = lookup_cache_sid2name(cache,
		    rq->from.u.sid.prefix,
		    rq->from.u.sid.rid, &name, &domain, &type);
	} else {
		/* Lookup winame to sid */
		retcode = lookup_cache_name2sid(cache, rq->from.name,
		    rq->from.domain, &name, &sidprefix, &rid, &type);
	}

	if (retcode != IDMAP_SUCCESS) {
		if (retcode == IDMAP_ERR_NOTFOUND) {
			TRACE(rq, "Not found in name cache");
		} else {
			TRACE(rq, "Name cache lookup error=%d", retcode);
		}
		free(name);
		free(domain);
		free(sidprefix);
		return (retcode);
	}

	rq->from.type = type;

	rq->dont_update_namecache = TRUE;

	/*
	 * If we found canonical names or domain, use them instead of
	 * the existing values.
	 */
	if (name != NULL) {
		free(rq->from.name);
		rq->from.name = name;
	}
	if (domain != NULL) {
		free(rq->from.domain);
		rq->from.domain = domain;
	}

	if (rq->from.u.sid.prefix == NULL) {
		rq->from.u.sid.prefix = sidprefix;
		rq->from.u.sid.rid = rid;
	}

	TRACE(rq, "Found in name cache");
	return (retcode);
}



static int
ad_lookup_batch_int(lookup_state_t *state,
		adutils_ad_t *dir, int how_local,
		int *num_processed)
{
	idmap_retcode	retcode;
	int		i,  num_queued;
	int		next_request;
	int		retries = 0;
	char		**unixname;
	idmap_query_state_t	*qs = NULL;
	idmap_how	*how;
	char		**dn, **attr, **value;

	*num_processed = 0;

retry:
	retcode = idmap_lookup_batch_start(dir, state->ad_nqueries,
	    state->directory_based_mapping,
	    state->defdom,
	    &qs);
	if (retcode != IDMAP_SUCCESS) {
		if (retcode == IDMAP_ERR_RETRIABLE_NET_ERR &&
		    retries++ < ADUTILS_DEF_NUM_RETRIES)
			goto retry;
		degrade_svc(1, "failed to create batch for AD lookup");
			goto out;
	}
	num_queued = 0;

	restore_svc();

	if (how_local & FOREST_IS_LOCAL) {
		/*
		 * Directory based name mapping is only performed within the
		 * joined forest.  We don't trust other "trusted"
		 * forests to provide DS-based name mapping information because
		 * AD's definition of "cross-forest trust" does not encompass
		 * this sort of behavior.
		 */
		idmap_lookup_batch_set_unixattr(qs,
		    state->ad_unixuser_attr, state->ad_unixgroup_attr);
	}

	for (i = 0; i < state->nreq; i++) {
		idmap_request_t *rq = &state->requests[i];
		how = &rq->how;

		retcode = IDMAP_SUCCESS;

		/* Skip if no AD lookup required */
		if (!rq->do_ad_lookup)
			continue;

		/* Skip if we've already tried and gotten a "not found" */
		if (rq->not_found_this_ad)
			continue;

		/* Skip if we've already either succeeded or failed */
		if (rq->retcode != IDMAP_ERR_RETRIABLE_NET_ERR)
			continue;

		if (IS_ID_SID(rq->from)) {

			/* win2unix request: */

			posix_id_t *pid = NULL;
			unixname = dn = attr = value = NULL;
			switch (state->directory_based_mapping) {
			case DIRECTORY_MAPPING_NAME:
				if (rq->to.name == NULL &&
				    AD_OR_MIXED_MODE(rq->to.type, state)) {
					unixname = &rq->to.name;
					idmap_how_clear(&rq->how);
					rq->src = IDMAP_MAP_SRC_NEW;
					how->map_type = IDMAP_MAP_TYPE_DS_AD;
					dn = &how->idmap_how_u.ad.dn;
					attr = &how->idmap_how_u.ad.attr;
					value = &how->idmap_how_u.ad.value;
				}
				break;
			case DIRECTORY_MAPPING_IDMU:
				if (how_local & DOMAIN_IS_LOCAL) {
					/*
					 * Ensure that we only do IDMU
					 * processing when querying the domain
					 * we've joined.
					 */
					pid = &rq->to.u.pid;
					/*
					 * Get how info for IDMU based mapping.
					 */
					idmap_how_clear(&rq->how);
					rq->src = IDMAP_MAP_SRC_NEW;
					how->map_type = IDMAP_MAP_TYPE_IDMU;
					dn = &how->idmap_how_u.idmu.dn;
					attr = &how->idmap_how_u.idmu.attr;
					value = &how->idmap_how_u.idmu.value;
				}
				break;
			}

			if (rq->from.u.sid.prefix != NULL) {
				/* Lookup AD by SID */
				retcode = idmap_sid2name_batch_add1(
				    qs, rq->from.u.sid.prefix,
				    &rq->from.u.sid.rid, rq->to.type,
				    dn, attr, value,
				    (rq->from.name == NULL) ?
				    &rq->from.name : NULL,
				    (rq->from.domain == NULL) ?
				    &rq->from.domain : NULL,
				    &rq->from.type, unixname,
				    pid,
				    &rq->retcode);
				if (retcode == IDMAP_SUCCESS)
					num_queued++;
			} else {
				/* Lookup AD by winname */
				assert(rq->from.name != NULL);
				retcode = idmap_name2sid_batch_add1(
				    qs, rq->from.name, rq->from.domain,
				    rq->to.type,
				    dn, attr, value,
				    &rq->from.name,
				    &rq->from.u.sid.prefix,
				    &rq->from.u.sid.rid,
				    &rq->from.type, unixname,
				    pid,
				    &rq->retcode);
				if (retcode == IDMAP_SUCCESS)
					num_queued++;
			}

		} else if (IS_ID_UID(rq->from) || IS_ID_GID(rq->from)) {

			/* unix2win request: */

			if (rq->to.u.sid.prefix != NULL &&
			    rq->to.name != NULL) {
				/* Already have SID and winname. done */
				rq->retcode = IDMAP_SUCCESS;
				continue;
			}

			if (rq->to.u.sid.prefix != NULL) {
				/*
				 * SID but no winname -- lookup AD by
				 * SID to get winname.
				 * how info is not needed here because
				 * we are not retrieving unixname from
				 * AD.
				 */

				retcode = idmap_sid2name_batch_add1(
				    qs, rq->to.u.sid.prefix,
				    &rq->to.u.sid.rid,
				    IDMAP_POSIXID,
				    NULL, NULL, NULL,
				    &rq->to.name,
				    &rq->to.domain, &rq->to.type,
				    NULL, NULL, &rq->retcode);
				if (retcode == IDMAP_SUCCESS)
					num_queued++;
			} else if (rq->to.name != NULL) {
				/*
				 * winname but no SID -- lookup AD by
				 * winname to get SID.
				 * how info is not needed here because
				 * we are not retrieving unixname from
				 * AD.
				 */
				retcode = idmap_name2sid_batch_add1(
				    qs, rq->to.name, rq->to.domain,
				    IDMAP_POSIXID,
				    NULL, NULL, NULL, NULL,
				    &rq->to.u.sid.prefix,
				    &rq->to.u.sid.rid,
				    &rq->to.type, NULL,
				    NULL,
				    &rq->retcode);
				if (retcode == IDMAP_SUCCESS)
					num_queued++;
			} else if (state->directory_based_mapping ==
			    DIRECTORY_MAPPING_IDMU &&
			    (how_local & DOMAIN_IS_LOCAL)) {
				bool_t is_user;

				assert(rq->from.u.pid != IDMAP_SENTINEL_PID);
				is_user = IS_ID_UID(rq->from);
				/* IDMU can't do diagonal mappings */
				if (is_user) {
					if (rq->to.type == IDMAP_GSID)
						continue;
				} else {
					if (rq->to.type == IDMAP_USID)
						continue;
				}

				idmap_how_clear(&rq->how);
				rq->src = IDMAP_MAP_SRC_NEW;
				how->map_type = IDMAP_MAP_TYPE_IDMU;
				retcode = idmap_pid2sid_batch_add1(
				    qs, rq->from.u.pid, is_user,
				    &how->idmap_how_u.ad.dn,
				    &how->idmap_how_u.ad.attr,
				    &how->idmap_how_u.ad.value,
				    &rq->to.u.sid.prefix,
				    &rq->to.u.sid.rid,
				    &rq->to.name, &rq->to.domain,
				    &rq->to.type, &rq->retcode);
				if (retcode == IDMAP_SUCCESS)
					num_queued++;
			} else if (state->directory_based_mapping ==
			    DIRECTORY_MAPPING_NAME &&
			    rq->from.name != NULL) {
				/*
				 * No SID and no winname but we've unixname.
				 * Lookup AD by unixname to get SID.
				 */
				bool_t is_user;
				bool_t is_wuser;

				is_user = IS_ID_UID(rq->from);
				if (rq->to.type == IDMAP_USID)
					is_wuser = TRUE;
				else if (rq->to.type == IDMAP_GSID)
					is_wuser = FALSE;
				else
					is_wuser = is_user;

				idmap_how_clear(&rq->how);
				rq->src = IDMAP_MAP_SRC_NEW;
				how->map_type = IDMAP_MAP_TYPE_DS_AD;
				retcode = idmap_unixname2sid_batch_add1(
				    qs, rq->from.name, is_user, is_wuser,
				    &how->idmap_how_u.ad.dn,
				    &how->idmap_how_u.ad.attr,
				    &how->idmap_how_u.ad.value,
				    &rq->to.u.sid.prefix,
				    &rq->to.u.sid.rid,
				    &rq->to.name, &rq->to.domain,
				    &rq->to.type, &rq->retcode);
				if (retcode == IDMAP_SUCCESS)
					num_queued++;
			}
		}

		if (retcode == IDMAP_ERR_DOMAIN_NOTFOUND) {
			rq->not_found_this_ad = TRUE;
			retcode = IDMAP_SUCCESS;
		} else if (retcode != IDMAP_SUCCESS) {
			break;
		}
	} /* End of for loop */

	if (retcode == IDMAP_SUCCESS) {
		/* add keeps track if we added an entry to the batch */
		if (num_queued > 0)
			retcode = idmap_lookup_batch_end(&qs);
		else
			idmap_lookup_release_batch(&qs);
	} else {
		idmap_lookup_release_batch(&qs);
		num_queued = 0;
		next_request = i + 1;
	}

	if (retcode == IDMAP_ERR_RETRIABLE_NET_ERR &&
	    retries++ < ADUTILS_DEF_NUM_RETRIES)
		goto retry;
	else if (retcode == IDMAP_ERR_RETRIABLE_NET_ERR)
		degrade_svc(1, "some AD lookups timed out repeatedly");

	if (retcode != IDMAP_SUCCESS) {
		idmapdlog(LOG_NOTICE, "Failed to batch AD lookup requests");

		/* Mark any unproccessed requests for an other AD */
		for (i = next_request; i < state->nreq; i++) {
			idmap_request_t *rq = &state->requests[i];
			rq->not_found_this_ad = TRUE;
		}
	}

out:
	/*
	 * This loop does the following:
	 * 1. Reset do_ad_lookup flag from the request.
	 * 2. If batch_start or batch_add failed then set the status
	 *    of each request marked for AD lookup to that error.
	 * 3. Evaluate the type of the AD object (i.e. user or group)
	 *    and update the type in request.
	 */
	for (i = 0; i < state->nreq; i++) {
		idmap_request_t *rq = &state->requests[i];

		/*
		 * If it didn't need AD lookup, ignore it.
		 */
		if (!rq->do_ad_lookup)
			continue;

		/*
		 * If we deferred it this time, reset for the next
		 * AD server.
		 */
		if (rq->not_found_this_ad) {
			rq->not_found_this_ad = FALSE;
			continue;
		}

		/* Count number processed */
		(*num_processed)++;

		/* Reset AD lookup flag */
		rq->do_ad_lookup = FALSE;

		/*
		 * If batch_start or batch_add failed then set the
		 * status of each request marked for AD lookup to
		 * that error.
		 */
		if (retcode != IDMAP_SUCCESS) {
			rq->retcode = retcode;
			TRACE(rq, "AD batch failed");
			continue;
		}

		if (rq->retcode == IDMAP_ERR_NOTFOUND) {
			/* Nothing found - remove the preset info */
			idmap_how_clear(&rq->how);
		}

		if (IS_ID_SID(rq->from)) {
			if (rq->retcode == IDMAP_ERR_NOTFOUND) {
				TRACE(rq, "Not found in AD");
				continue;
			}
			if (rq->retcode != IDMAP_SUCCESS) {
				TRACE(rq, "AD lookup error");
				continue;
			}
			/* Evaluate result type */
			switch (rq->from.type) {
			case IDMAP_USID:
				if (rq->to.type == IDMAP_POSIXID)
					rq->to.type = IDMAP_UID;
				/*
				 * We found a user.  If we were expecting a
				 * group, ignore any information from IDMU.
				 * If we got information from IDMU and we
				 * were expecting a user, use it.
				 */
				if (rq->to.type != IDMAP_UID) {
					rq->to.u.pid = IDMAP_SENTINEL_PID;
				} else if (rq->to.u.pid != IDMAP_SENTINEL_PID) {
					rq->direction = IDMAP_DIRECTION_BI;
					rq->how.map_type = IDMAP_MAP_TYPE_IDMU;
					rq->src = IDMAP_MAP_SRC_NEW;
				}
				break;

			case IDMAP_GSID:
				if (rq->to.type == IDMAP_POSIXID)
					rq->to.type = IDMAP_GID;
				/*
				 * We found a group.  If we were expecting a
				 * user, ignore any information from IDMU.
				 * If we got information from IDMU and we
				 * were expecting a group, use it.
				 */
				if (rq->to.type != IDMAP_GID) {
					rq->to.u.pid = IDMAP_SENTINEL_PID;
				} else if (rq->to.u.pid != IDMAP_SENTINEL_PID) {
					rq->direction = IDMAP_DIRECTION_BI;
					rq->how.map_type = IDMAP_MAP_TYPE_IDMU;
					rq->src = IDMAP_MAP_SRC_NEW;
				}
				break;

			default:
				rq->retcode = IDMAP_ERR_SID;
				break;
			}
			TRACE(rq, "Found in AD");
			if (rq->retcode == IDMAP_SUCCESS &&
			    rq->from.name != NULL &&
			    (rq->to.name == NULL ||
			    rq->to.u.pid == IDMAP_SENTINEL_PID) &&
			    NLDAP_MODE(rq->to.type, state)) {
				rq->do_nldap_lookup = TRUE;
				state->nldap_nqueries++;
			}
		} else if (IS_ID_UID(rq->from) || IS_ID_GID(rq->from)) {
			if (rq->retcode == IDMAP_ERR_NOTFOUND) {
				/* This is not an error */
				rq->retcode = IDMAP_SUCCESS;
				TRACE(rq, "Not found in AD");
				continue;
			} else if (rq->retcode != IDMAP_SUCCESS) {
				TRACE(rq, "AD lookup error");
				continue;
			}
			/* Evaluate result type */
			if (rq->to.type != IDMAP_USID &&
			    rq->to.type != IDMAP_GSID)
				rq->retcode = IDMAP_ERR_SID;
			TRACE(rq, "Found in AD");
		}
	}

	return (retcode);
}



/*
 * Batch AD lookups
 */
idmap_retcode
ad_lookup_batch(lookup_state_t *state)
{
	idmap_retcode	retcode;
	int		i;
	int		num_queries;
	int		num_processed;

	if (state->ad_nqueries == 0)
		return (IDMAP_SUCCESS);

	for (i = 0; i < state->nreq; i++) {
		idmap_request_t *rq = &state->requests[i];

		/* Skip if not marked for AD lookup or already in error. */
		if (!rq->do_ad_lookup ||
		    rq->retcode != IDMAP_SUCCESS)
			continue;

		/* Init status */
		rq->retcode = IDMAP_ERR_RETRIABLE_NET_ERR;
	}

	RDLOCK_CONFIG();
	num_queries = state->ad_nqueries;

	if (_idmapdstate.num_gcs == 0 && _idmapdstate.num_dcs == 0) {
		/* Case of no ADs */
		for (i = 0; i < state->nreq; i++) {
			idmap_request_t *rq = &state->requests[i];
			if (!rq->do_ad_lookup)
				continue;
			rq->do_ad_lookup = FALSE;
			rq->retcode = IDMAP_ERR_NO_ACTIVEDIRECTORY;
			TRACE(rq, "No AD servers");
		}
		retcode = IDMAP_SUCCESS;
		goto out;
	}

	if (state->directory_based_mapping == DIRECTORY_MAPPING_IDMU) {
		for (i = 0; i < _idmapdstate.num_dcs && num_queries > 0; i++) {

			retcode = ad_lookup_batch_int(state,
			    _idmapdstate.dcs[i],
			    i == 0 ? DOMAIN_IS_LOCAL|FOREST_IS_LOCAL : 0,
			    &num_processed);
			num_queries -= num_processed;

		}
	}

	for (i = 0; i < _idmapdstate.num_gcs && num_queries > 0; i++) {

		retcode = ad_lookup_batch_int(state,
		    _idmapdstate.gcs[i],
		    i == 0 ? FOREST_IS_LOCAL : 0,
		    &num_processed);
		num_queries -= num_processed;

	}

	/*
	 * There are no more ADs to try.  Return errors for any
	 * remaining requests.
	 */
	if (num_queries > 0) {
		for (i = 0; i < state->nreq; i++) {
			idmap_request_t *rq = &state->requests[i];
			if (!rq->do_ad_lookup)
				continue;
			rq->do_ad_lookup = FALSE;
			rq->retcode = IDMAP_ERR_DOMAIN_NOTFOUND;
			TRACE(rq, "No servers for this domain");
		}
	}

out:
	UNLOCK_CONFIG();

	/* AD lookups done. Reset state->ad_nqueries and return */
	state->ad_nqueries = 0;
	return (retcode);
}

/*
 * Convention when processing win2unix requests:
 *
 * Windows identity:
 * rq->from.name =
 *              winname if given otherwise winname found will be placed
 *              here.
 * rq->from.domain =
 *              windomain if given otherwise windomain found will be
 *              placed here.
 * rq->from.type =
 *              Either IDMAP_SID/USID/GSID. If this is IDMAP_SID then it'll
 *              be set to IDMAP_USID/GSID depending upon whether the
 *              given SID is user or group respectively. The user/group-ness
 *              is determined either when looking up well-known SIDs table OR
 *              if the SID is found in namecache OR by ad_lookup_batch().
 *		The type of an unresolvable SID is inferred from the desired
 *		UNIX ID type.  When the desired UNIX ID type is not specified,
 *		unresolvable SIDs are assumed to be users.
 * rq->from.u.sid =
 *              SID if given otherwise SID found will be placed here.
 *
 * Unix identity:
 * rq->to.name =
 *              unixname found will be placed here.
 * rq->to.domain =
 *              NOT USED
 * rq->to.type =
 *              Target type. If the request specified IDMAP_POSIXID then the
 *              actual type found (IDMAP_UID/GID) found will be placed here.
 * rq->to.u.pid =
 *              UID/GID found will be placed here.
 *
 * Others:
 * rq->retcode =
 *              Return status for this request will be placed here.
 * rq->direction =
 *              Direction found will be placed here. Direction
 *              meaning whether the resultant mapping is valid
 *              only from win2unix or bi-directional.
 */

/*
 * This function does the following:
 * 1. Lookup well-known SIDs table.
 * 2. Check if the given SID is a local-SID and if so extract UID/GID from it.
 * 3. Lookup cache.
 * 4. Check if the client does not want new mapping to be allocated
 *    in which case this pass is the final pass.
 * 5. Set AD lookup flag if it determines that the next stage needs
 *    to do AD lookup.
 */
static
void
sid2pid_first_pass(lookup_state_t *state, idmap_request_t *rq)
{
	idmap_retcode	retcode;
	int		wksid;

	validate_get_mapped_ids_sid2pid(rq);
	if (rq->retcode != IDMAP_SUCCESS)
		return;

	/* Initialize result */
	rq->to.u.pid = IDMAP_SENTINEL_PID;
	rq->direction = IDMAP_DIRECTION_UNDEF;
	wksid = 0;

	if (rq->from.u.sid.prefix == NULL) {
		/* They have to give us *something* to work with! */
		if (rq->from.name == NULL) {
			retcode = IDMAP_ERR_ARG;
			goto out;
		}

		/* Allow for a fully-qualified name in the "name" parameter */
		if (rq->from.domain == NULL) {
			char *p;
			p = strchr(rq->from.name, '@');
			if (p != NULL) {
				char *q;
				q = rq->from.name;
				rq->from.name =
				    uu_strndup(q, p - rq->from.name);
				rq->from.domain = strdup(p+1);
				free(q);
				if (rq->from.name == NULL ||
				    rq->from.domain == NULL) {
					retcode = IDMAP_ERR_MEMORY;
					goto out;
				}
			}
		}
	}

	/* Lookup well-known SIDs table */
	retcode = lookup_wksids_sid2pid(rq, &wksid);
	if (retcode == IDMAP_SUCCESS) {
		/* Found a well-known account with a hardwired mapping */
		TRACE(rq, "Hardwired mapping");
		goto out;
	} else if (retcode != IDMAP_ERR_NOTFOUND) {
		TRACE(rq,
		    "Well-known account lookup failed, code %d", retcode);
		goto out;
	}

	if (wksid) {
		/* Found a well-known account, but no mapping */
		TRACE(rq, "Well-known account");
	} else {
		TRACE(rq, "Not a well-known account");

		/* Check if this is a localsid */
		retcode = lookup_localsid2pid(rq);
		if (retcode == IDMAP_SUCCESS) {
			TRACE(rq, "Local SID");
			goto out;
		} else if (retcode != IDMAP_ERR_NOTFOUND) {
			TRACE(rq,
			    "Local SID lookup error=%d", retcode);
			goto out;
		}
		TRACE(rq, "Not a local SID");

		if (rq->local_only) {
			retcode = IDMAP_ERR_NONE_GENERATED;
			goto out;
		}
	}

	/*
	 * If this is a name-based request and we don't have a domain,
	 * use the default domain.  Note that the well-known identity
	 * cases will have supplied a SID prefix already, and that we
	 * don't (yet?) support looking up a local user through a Windows
	 * style name.
	 */
	if (rq->from.u.sid.prefix == NULL &&
	    rq->from.name != NULL && rq->from.domain == NULL) {
		if (state->defdom == NULL) {
			retcode = IDMAP_ERR_DOMAIN_NOTFOUND;
			goto out;
		}
		rq->from.domain = strdup(state->defdom);
		if (rq->from.domain == NULL) {
			retcode = IDMAP_ERR_MEMORY;
			goto out;
		}
		TRACE(rq, "Added default domain");
	}

	/* Lookup cache */
	retcode = lookup_cache_sid2pid(state->cache, rq);
	if (retcode != IDMAP_ERR_NOTFOUND)
		goto out;

	if (rq->no_new_id_alloc || rq->no_nameservice) {
		retcode = IDMAP_ERR_NONE_GENERATED;
		goto out;
	}

	/*
	 * Failed to find non-expired entry in cache. Next step is
	 * to determine if this request needs to be batched for AD lookup.
	 *
	 * At this point we have either sid or winname or both. If we don't
	 * have both then lookup name_cache for the sid or winname
	 * whichever is missing. If not found then this request will be
	 * batched for AD lookup.
	 */
	retcode = lookup_name_cache(state->cache, rq);
	if (retcode == IDMAP_SUCCESS) {
		if (rq->to.type == IDMAP_POSIXID) {
			if (rq->from.type == IDMAP_USID)
				rq->to.type = IDMAP_UID;
			else
				rq->to.type = IDMAP_GID;
		}
	} else if (retcode != IDMAP_ERR_NOTFOUND)
		goto out;

	if (state->domain_name != NULL) {
		/*
		 * If we don't have both name and SID, try looking up the
		 * entry with LSA.
		 */
		if (rq->from.u.sid.prefix != NULL &&
		    rq->from.name == NULL) {

			retcode = lookup_lsa_by_sid(
			    rq->from.u.sid.prefix,
			    rq->from.u.sid.rid,
			    &rq->from.name, &rq->from.domain, &rq->from.type);
			if (retcode == IDMAP_SUCCESS) {
				TRACE(rq, "Found with LSA");
			} else if (retcode == IDMAP_ERR_NOTFOUND) {
				TRACE(rq, "Not found with LSA");
			} else {
				TRACE(rq, "LSA error %d", retcode);
				goto out;
			}

		} else  if (rq->from.name != NULL &&
		    rq->from.u.sid.prefix == NULL) {
			char *canonname;
			char *canondomain;

			retcode = lookup_lsa_by_name(
			    rq->from.name, rq->from.domain,
			    &rq->from.u.sid.prefix,
			    &rq->from.u.sid.rid,
			    &canonname, &canondomain,
			    &rq->from.type);
			if (retcode == IDMAP_SUCCESS) {
				free(rq->from.name);
				rq->from.name = canonname;
				free(rq->from.domain);
				rq->from.domain = canondomain;
				TRACE(rq, "Found with LSA");
			} else if (retcode == IDMAP_ERR_NOTFOUND) {
				TRACE(rq, "Not found with LSA");
			} else {
				TRACE(rq, "LSA error %d", retcode);
				goto out;
			}
		}
	}

	/*
	 * Set the flag to indicate that we are not done yet so that
	 * subsequent passes considers this request for name-based
	 * mapping and ephemeral mapping.
	 */
	state->done = B_FALSE;
	rq->done = FALSE;

	/*
	 * If we couldn't find the name or SID, give up.  Maybe fall back to
	 * ephemeral mapping later.
	 */
	if (retcode != IDMAP_SUCCESS)
		goto out;

	/*
	 * Even if we have both sid and winname, we still may need to batch
	 * this request for AD lookup to support directory-based mapping.
	 *
	 * We avoid AD lookup for well-known SIDs because they don't have
	 * regular AD objects.
	 */
	if (AD_OR_MIXED_MODE(rq->to.type, state)) {
		if (!wksid && rq->to.name == NULL) {
			rq->do_ad_lookup = TRUE;
			state->ad_nqueries++;
		}
	} else if (state->directory_based_mapping == DIRECTORY_MAPPING_IDMU) {
		if (!wksid && rq->to.u.pid == IDMAP_SENTINEL_PID) {
			rq->do_ad_lookup = TRUE;
			state->ad_nqueries++;
		}
	} else if (NLDAP_MODE(rq->to.type, state)) {
		rq->do_nldap_lookup = TRUE;
		state->nldap_nqueries++;
	}

out:
	rq->retcode = retcode;
	/*
	 * If we are done and there was an error then set fallback pid
	 * in the result.
	 */
	if (rq->done && rq->retcode != IDMAP_SUCCESS)
		rq->to.u.pid = UID_NOBODY;
}

/*
 * Generate SID using the following convention
 * 	<machine-sid-prefix>-<1000 + uid>
 * 	<machine-sid-prefix>-<2^31 + gid>
 */
static
idmap_retcode
generate_localsid(idmap_request_t *rq, int fallback)
{
	assert(!IDMAP_ID_IS_EPHEMERAL(rq->from.u.pid));

	free(rq->to.u.sid.prefix);
	rq->to.u.sid.prefix = NULL;

	/*
	 * Diagonal mapping for localSIDs not supported because of the
	 * way we generate localSIDs.
	 */
	if (rq->from.type != IDMAP_GID && rq->to.type == IDMAP_GSID)
		return (IDMAP_ERR_NOTGROUP);
	if (rq->from.type != IDMAP_UID && rq->to.type == IDMAP_USID)
		return (IDMAP_ERR_NOTUSER);

	/* Skip 1000 UIDs */
	if (rq->from.type == IDMAP_UID &&
	    rq->from.u.pid + LOCALRID_UID_MIN > LOCALRID_UID_MAX)
		return (IDMAP_ERR_NOMAPPING);

	RDLOCK_CONFIG();
	rq->to.u.sid.prefix =
	    strdup(_idmapdstate.cfg->pgcfg.machine_sid);
	if (rq->to.u.sid.prefix == NULL) {
		UNLOCK_CONFIG();
		idmapdlog(LOG_ERR, "Out of memory");
		return (IDMAP_ERR_MEMORY);
	}
	UNLOCK_CONFIG();
	if (rq->from.type == IDMAP_UID) {
		rq->to.u.sid.rid = rq->from.u.pid + LOCALRID_UID_MIN;
		rq->to.type = IDMAP_USID;
	} else {
		rq->to.u.sid.rid = rq->from.u.pid + LOCALRID_GID_MIN;
		rq->to.type = IDMAP_GSID;
	}
	rq->direction = IDMAP_DIRECTION_BI;

	if (!fallback) {
		rq->how.map_type = IDMAP_MAP_TYPE_LOCAL_SID;
		rq->src = IDMAP_MAP_SRC_ALGORITHMIC;
	}

	/*
	 * Don't update name_cache because local sids don't have
	 * valid windows names.
	 */
	rq->dont_update_namecache = TRUE;
	return (IDMAP_SUCCESS);
}

static
idmap_retcode
lookup_localsid2pid(idmap_request_t *rq)
{
	char		*sidprefix;
	uint32_t	rid;
	int		s;

	/*
	 * If the sidprefix == localsid then UID = last RID - 1000 or
	 * GID = last RID - 2^31.
	 */
	if ((sidprefix = rq->from.u.sid.prefix) == NULL)
		/* This means we are looking up by winname */
		return (IDMAP_ERR_NOTFOUND);
	rid = rq->from.u.sid.rid;

	RDLOCK_CONFIG();
	s = (_idmapdstate.cfg->pgcfg.machine_sid) ?
	    strcasecmp(sidprefix, _idmapdstate.cfg->pgcfg.machine_sid) : 1;
	UNLOCK_CONFIG();

	/*
	 * If the given sidprefix does not match machine_sid then this is
	 * not a local SID.
	 */
	if (s != 0)
		return (IDMAP_ERR_NOTFOUND);

	switch (rq->to.type) {
	case IDMAP_UID:
		if (rid < LOCALRID_UID_MIN || rid > LOCALRID_UID_MAX)
			return (IDMAP_ERR_ARG);
		rq->to.u.pid = rid - LOCALRID_UID_MIN;
		break;
	case IDMAP_GID:
		if (rid < LOCALRID_GID_MIN)
			return (IDMAP_ERR_ARG);
		rq->to.u.pid = rid - LOCALRID_GID_MIN;
		break;
	case IDMAP_POSIXID:
		if (rid >= LOCALRID_GID_MIN) {
			rq->to.u.pid = rid - LOCALRID_GID_MIN;
			rq->to.type = IDMAP_GID;
		} else if (rid >= LOCALRID_UID_MIN) {
			rq->to.u.pid = rid - LOCALRID_UID_MIN;
			rq->to.type = IDMAP_UID;
		} else {
			return (IDMAP_ERR_ARG);
		}
		break;
	default:
		return (IDMAP_ERR_NOTSUPPORTED);
	}
	rq->how.map_type = IDMAP_MAP_TYPE_LOCAL_SID;
	rq->src = IDMAP_MAP_SRC_ALGORITHMIC;
	return (IDMAP_SUCCESS);
}

/*
 * Name service lookup by unixname to get pid
 */
static
idmap_retcode
ns_lookup_byname(const char *name, const char *lower_name, idmap_identity_t *id)
{
	struct passwd	pwd, *pwdp;
	struct group	grp, *grpp;
	char		*buf;
	static size_t	pwdbufsiz = 0;
	static size_t	grpbufsiz = 0;

	switch (id->type) {
	case IDMAP_UID:
		if (pwdbufsiz == 0)
			pwdbufsiz = sysconf(_SC_GETPW_R_SIZE_MAX);
		buf = alloca(pwdbufsiz);
		pwdp = getpwnam_r(name, &pwd, buf, pwdbufsiz);
		if (pwdp == NULL && errno == 0 && lower_name != NULL &&
		    name != lower_name && strcmp(name, lower_name) != 0)
			pwdp = getpwnam_r(lower_name, &pwd, buf, pwdbufsiz);
		if (pwdp == NULL) {
			if (errno == 0)
				return (IDMAP_ERR_NOTFOUND);
			else
				return (IDMAP_ERR_INTERNAL);
		}
		id->u.pid = pwd.pw_uid;
		break;
	case IDMAP_GID:
		if (grpbufsiz == 0)
			grpbufsiz = sysconf(_SC_GETGR_R_SIZE_MAX);
		buf = alloca(grpbufsiz);
		grpp = getgrnam_r(name, &grp, buf, grpbufsiz);
		if (grpp == NULL && errno == 0 && lower_name != NULL &&
		    name != lower_name && strcmp(name, lower_name) != 0)
			grpp = getgrnam_r(lower_name, &grp, buf, grpbufsiz);
		if (grpp == NULL) {
			if (errno == 0)
				return (IDMAP_ERR_NOTFOUND);
			else
				return (IDMAP_ERR_INTERNAL);
		}
		id->u.pid = grp.gr_gid;
		break;
	default:
		return (IDMAP_ERR_ARG);
	}
	return (IDMAP_SUCCESS);
}


/*
 * Name service lookup by pid to get unixname
 */
static
idmap_retcode
ns_lookup_bypid(posix_id_t pid, int is_user, char **unixname)
{
	struct passwd	pwd;
	struct group	grp;
	char		*buf;
	static size_t	pwdbufsiz = 0;
	static size_t	grpbufsiz = 0;

	if (is_user) {
		if (pwdbufsiz == 0)
			pwdbufsiz = sysconf(_SC_GETPW_R_SIZE_MAX);
		buf = alloca(pwdbufsiz);
		errno = 0;
		if (getpwuid_r(pid, &pwd, buf, pwdbufsiz) == NULL) {
			if (errno == 0)
				return (IDMAP_ERR_NOTFOUND);
			else
				return (IDMAP_ERR_INTERNAL);
		}
		*unixname = strdup(pwd.pw_name);
	} else {
		if (grpbufsiz == 0)
			grpbufsiz = sysconf(_SC_GETGR_R_SIZE_MAX);
		buf = alloca(grpbufsiz);
		errno = 0;
		if (getgrgid_r(pid, &grp, buf, grpbufsiz) == NULL) {
			if (errno == 0)
				return (IDMAP_ERR_NOTFOUND);
			else
				return (IDMAP_ERR_INTERNAL);
		}
		*unixname = strdup(grp.gr_name);
	}
	if (*unixname == NULL)
		return (IDMAP_ERR_MEMORY);
	return (IDMAP_SUCCESS);
}

/*
 * Name-based mapping
 *
 * Case 1: If no rule matches do ephemeral
 *
 * Case 2: If rule matches and unixname is "" then return no mapping.
 *
 * Case 3: If rule matches and unixname is specified then lookup name
 *  service using the unixname. If unixname not found then return no mapping.
 *
 * Case 4: If rule matches and unixname is * then lookup name service
 *  using winname as the unixname. If unixname not found then process
 *  other rules using the lookup order. If no other rule matches then do
 *  ephemeral. Otherwise, based on the matched rule do Case 2 or 3 or 4.
 *  This allows us to specify a fallback unixname per _domain_ or no mapping
 *  instead of the default behaviour of doing ephemeral mapping.
 *
 * Example 1:
 * *@sfbay == *
 * If looking up windows users foo@sfbay and foo does not exists in
 * the name service then foo@sfbay will be mapped to an ephemeral id.
 *
 * Example 2:
 * *@sfbay == *
 * *@sfbay => guest
 * If looking up windows users foo@sfbay and foo does not exists in
 * the name service then foo@sfbay will be mapped to guest.
 *
 * Example 3:
 * *@sfbay == *
 * *@sfbay => ""
 * If looking up windows users foo@sfbay and foo does not exists in
 * the name service then we will return no mapping for foo@sfbay.
 *
 */
static
void
name_based_mapping_sid2pid(lookup_state_t *state, idmap_request_t *rq)
{
	const char	*windomain;
	char		*sql, *errmsg = NULL, *lower_winname = NULL;
	char		*lower_unixname, *winname;
	const char	**values;
	sqlite_vm	*vm = NULL;
	int		ncol, r, is_user, is_wuser;
	idmap_namerule	*rule = &rq->how.idmap_how_u.rule;
	const char	*me = "name_based_mapping_sid2pid";

	assert(rq->from.name != NULL); /* We have winname */
	assert(rq->to.name == NULL); /* We don't have unixname */

	winname = rq->from.name;
	windomain = rq->from.domain;

	is_wuser = (rq->from.type == IDMAP_USID);

	switch (rq->to.type) {
	case IDMAP_UID:
		is_user = 1;
		break;
	case IDMAP_GID:
		is_user = 0;
		break;
	case IDMAP_POSIXID:
		is_user = is_wuser;
		rq->to.type = is_user ? IDMAP_UID : IDMAP_GID;
		break;
	}

	if (windomain == NULL)
		windomain = "";

	if ((lower_winname = tolower_u8(winname)) == NULL)
		lower_winname = winname;    /* hope for the best */

	sql = sqlite_mprintf(
	    "SELECT unixname, u2w_order, winname_display, windomain, is_nt4 "
	    "FROM namerules WHERE "
	    "w2u_order > 0 AND is_user = %d AND is_wuser = %d AND "
	    "(winname = %Q OR winname = '*') AND "
	    "(windomain = %Q OR windomain = '*') "
	    "ORDER BY w2u_order ASC;",
	    is_user, is_wuser, lower_winname, windomain);
	if (sql == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		rq->retcode = IDMAP_ERR_MEMORY;
		goto out;
	}

	r = sqlite_compile(state->db, sql, NULL, &vm, &errmsg);
	sqlite_freemem(sql);
	sql = NULL;

	if (r != SQLITE_OK) {
		rq->retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "%s: database error (%s)", me,
		    CHECK_NULL(errmsg));
		sqlite_freemem(errmsg);
		goto out;
	}

	for (;;) {
		r = sqlite_step(vm, &ncol, &values, NULL);
		assert(r != SQLITE_LOCKED && r != SQLITE_BUSY);

		if (r == SQLITE_ROW) {
			if (ncol < 5) {
				rq->retcode = IDMAP_ERR_INTERNAL;
				goto out;
			}

			TRACE(rq, "Matching rule: %s@%s -> %s",
			    values[2] == NULL ? "(null)" : values[2],
			    values[3] == NULL ? "(null)" : values[3],
			    values[0] == NULL ? "(null)" : values[0]);

			if (values[0] == NULL) {
				rq->retcode = IDMAP_ERR_INTERNAL;
				goto out;
			}

			if (EMPTY_NAME(values[0])) {
				rq->retcode = IDMAP_ERR_NOMAPPING;
				TRACE(rq, "Mapping inhibited");
				break;
			}

			if (values[0][0] == '*') {
				rq->to.name = strdup(winname);
				lower_unixname = lower_winname;
			} else {
				rq->to.name = strdup(values[0]);
				lower_unixname = NULL;
			}
			if (rq->to.name == NULL) {
				rq->retcode = IDMAP_ERR_MEMORY;
				goto out;
			}

			rq->retcode = ns_lookup_byname(
			    rq->to.name, lower_unixname,
			    &rq->to);
			switch (rq->retcode) {
			case IDMAP_SUCCESS:
				TRACE(rq, "UNIX name found");
				break;
			case IDMAP_ERR_NOTFOUND:
				if (values[0][0] == '*') {
					/*
					 * Case 4
					 * "Not found" on a wild card is not
					 * an error.  Just forget we tried,
					 * and go on.
					 */
					rq->retcode = IDMAP_SUCCESS;
					TRACE(rq, "Not found, continuing");
					free(rq->to.name);
					rq->to.name = NULL;
					continue;
				}
				/*
				 * Case 3
				 * A rule gave an explicit UNIX name as its
				 * result, and that name does not exist.
				 * This is an administrative error.
				 */
				rq->retcode = IDMAP_ERR_NOMAPPING;
				TRACE(rq, "Not found");
				break;
			default:
				TRACE(rq, "UNIX name lookup failed");
				break;
			}
			break;

		} else if (r == SQLITE_DONE) {
			TRACE(rq, "No matching rule");
			goto out;
		} else {
			(void) sqlite_finalize(vm, &errmsg);
			vm = NULL;
			idmapdlog(LOG_ERR, "%s: database error (%s)", me,
			    CHECK_NULL(errmsg));
			sqlite_freemem(errmsg);
			rq->retcode = IDMAP_ERR_INTERNAL;
			goto out;
		}
	}

	/* Found (with or without error) */

	if (values[1] != NULL && atol(values[1]) != 0)
		rq->direction = IDMAP_DIRECTION_BI;
	else
		rq->direction = IDMAP_DIRECTION_W2U;

	idmap_namerule_set(rule, values[3], values[2],
	    values[0], is_user, is_wuser, atol(values[4]),
	    rq->direction);

	rq->how.map_type = IDMAP_MAP_TYPE_RULE_BASED;
	rq->src = IDMAP_MAP_SRC_NEW;

out:
	if (lower_winname != NULL && lower_winname != winname)
		free(lower_winname);
	if (vm != NULL)
		(void) sqlite_finalize(vm, NULL);
}

static
int
get_next_eph_uid(uid_t *next_uid)
{
	uid_t uid;
	gid_t gid;
	int err;

	*next_uid = (uid_t)-1;
	uid = _idmapdstate.next_uid++;
	if (uid >= _idmapdstate.limit_uid) {
		if ((err = allocids(0, 8192, &uid, 0, &gid)) != 0)
			return (err);

		_idmapdstate.limit_uid = uid + 8192;
		_idmapdstate.next_uid = uid;
	}
	*next_uid = uid;

	return (0);
}

static
int
get_next_eph_gid(gid_t *next_gid)
{
	uid_t uid;
	gid_t gid;
	int err;

	*next_gid = (gid_t)-1;
	gid = _idmapdstate.next_gid++;
	if (gid >= _idmapdstate.limit_gid) {
		if ((err = allocids(0, 0, &uid, 8192, &gid)) != 0)
			return (err);

		_idmapdstate.limit_gid = gid + 8192;
		_idmapdstate.next_gid = gid;
	}
	*next_gid = gid;

	return (0);
}

static
int
gethash(const char *str, uint32_t num, idmap_id_type type, uint_t htsize)
{
	uint_t  hval, i, len;

	if (str == NULL)
		return (0);
	for (len = strlen(str), hval = 0, i = 0; i < len; i++) {
		hval += str[i];
		hval += (hval << 10);
		hval ^= (hval >> 6);
	}
	for (str = (const char *)&num, i = 0; i < sizeof (num); i++) {
		hval += str[i];
		hval += (hval << 10);
		hval ^= (hval >> 6);
	}
	for (str = (const char *)&type, i = 0; i < sizeof (type); i++) {
		hval += str[i];
		hval += (hval << 10);
		hval ^= (hval >> 6);
	}
	hval += (hval << 3);
	hval ^= (hval >> 11);
	hval += (hval << 15);
	return (hval % htsize);
}

static
bool_t
get_from_sid_history(
    lookup_state_t *state,
    idmap_request_t *rq)
{
	uint_t		hnum;
	idmap_request_t *earlier_rq;

	hnum = gethash(rq->from.u.sid.prefix, rq->from.u.sid.rid,
	    rq->to.type, state->sid_history_size);

	for (earlier_rq = state->sid_history[hnum];
	    earlier_rq != NULL;
	    earlier_rq = earlier_rq->next_same_hash) {

		if (earlier_rq->from.u.sid.rid == rq->from.u.sid.rid &&
		    uu_streq(earlier_rq->from.u.sid.prefix,
		    rq->from.u.sid.prefix) &&
		    earlier_rq->to.type == rq->to.type) {
			rq->to.u.pid = earlier_rq->to.u.pid;
			return (TRUE);
		}
	}
	return (FALSE);
}

static
void
add_to_sid_history(
    lookup_state_t *state,
    idmap_request_t *rq)
{
	uint_t		hnum;

	hnum = gethash(rq->from.u.sid.prefix, rq->from.u.sid.rid, rq->to.type,
	    state->sid_history_size);

	rq->next_same_hash = state->sid_history[hnum];
	state->sid_history[hnum] = rq;
}

void
cleanup_lookup_state(lookup_state_t *state)
{
	free(state->sid_history);
	free(state->ad_unixuser_attr);
	free(state->ad_unixgroup_attr);
	free(state->nldap_winname_attr);
	free(state->defdom);
	free(state->domain_name);
}

/* ARGSUSED */
static
void
dynamic_ephemeral_mapping(lookup_state_t *state, idmap_request_t *rq)
{
	rq->direction = IDMAP_DIRECTION_BI;

	if (rq->expired.type != IDMAP_NONE) {
		if (rq->to.type == rq->expired.type ||
		    rq->to.type == IDMAP_POSIXID) {
			rq->how.map_type = IDMAP_MAP_TYPE_EPHEMERAL;
			rq->src = IDMAP_MAP_SRC_CACHE;
			rq->to.type = rq->expired.type;
			rq->to.u.pid = rq->expired.pid;
			rq->direction = rq->expired.direction;
			TRACE(rq, "Reuse expired mapping");
			return;
		} else {
			TRACE(rq, "Expired mapping is wrong type");
		}
	}

	rq->how.map_type = IDMAP_MAP_TYPE_EPHEMERAL;
	rq->src = IDMAP_MAP_SRC_NEW;

	if (get_from_sid_history(state, rq)) {
		TRACE(rq, "Reuse mapping from earlier entry in this batch");
		return;
	}

	if (rq->to.type == IDMAP_UID) {
		if (get_next_eph_uid(&rq->to.u.pid) != 0) {
			rq->retcode = IDMAP_ERR_INTERNAL;
			TRACE(rq, "Could not allocate ephemeral UID");
			return;
		}
	} else {
		if (get_next_eph_gid(&rq->to.u.pid) != 0) {
			rq->retcode = IDMAP_ERR_INTERNAL;
			TRACE(rq, "Could not allocate ephemeral GID");
			return;
		}
	}

	TRACE(rq, "New ephemeral mapping");
	add_to_sid_history(state, rq);
}

static
void
sid2pid_second_pass(lookup_state_t *state, idmap_request_t *rq)
{
	/* Check if second pass is needed */
	if (rq->done)
		return;

	if (rq->retcode != IDMAP_SUCCESS && state->eph_map_unres_sids &&
	    rq->from.u.sid.prefix != NULL &&
	    rq->from.name == NULL) {
		/* We don't consider this to be an error condition. */
		rq->retcode = IDMAP_SUCCESS;
		/*
		 * We are asked to map an unresolvable SID to a UID or
		 * GID, but, which?  We'll treat all unresolvable SIDs
		 * as users unless the caller specified which of a UID
		 * or GID they want.
		 */
		if (rq->from.type == IDMAP_SID)
			rq->from.type = IDMAP_USID;
		if (rq->to.type == IDMAP_POSIXID) {
			rq->to.type = IDMAP_UID;
			TRACE(rq, "Assume unresolvable SID is user");
		} else if (rq->to.type == IDMAP_UID) {
			TRACE(rq, "Must map unresolvable SID to user");
		} else if (rq->to.type == IDMAP_GID) {
			TRACE(rq, "Must map unresolvable SID to group");
		}
		goto do_eph;
	}
	if (rq->retcode != IDMAP_SUCCESS)
		goto out;
	assert(rq->from.name != NULL);

	/*
	 * If we're here with a POSIX ID, it's from IDMU and
	 * we need to look up the name, for name-based
	 * requests and the cache.
	 */
	if (rq->to.u.pid != IDMAP_SENTINEL_PID) {
		idmap_retcode	rc;
		assert(rq->to.name == NULL);

		rc = ns_lookup_bypid(rq->to.u.pid,
		    rq->to.type == IDMAP_UID, &rq->to.name);
		switch (rc) {
		case IDMAP_SUCCESS:
			TRACE(rq, "Found UNIX name");
			break;
		case IDMAP_ERR_NOTFOUND:
			/*
			 * If the lookup fails, go ahead anyway.
			 * The general UNIX rule is that it's OK to
			 * have a UID or GID that isn't in the
			 * name service.
			 */
			TRACE(rq, "UNIX name not found");
			break;
		default:
			rq->retcode = rc;
			TRACE(rq, "Getting UNIX name");
			break;
		}

		goto out;
	}
	assert(rq->to.u.pid == IDMAP_SENTINEL_PID);

	/*
	 * If directory-based name mapping is enabled then the unixname
	 * may already have been retrieved from the AD object (AD-mode or
	 * mixed-mode) or from native LDAP object (nldap-mode) -- done.
	 */
	if (rq->to.name != NULL) {
		assert(rq->to.type != IDMAP_POSIXID);

		/*
		 * Special case: (1) If the ad_unixuser_attr and
		 * ad_unixgroup_attr uses the same attribute
		 * name and (2) if this is a diagonal mapping
		 * request and (3) the unixname has been retrieved
		 * from the AD object -- then we ignore it and fallback
		 * to name-based mapping rules and ephemeral mapping
		 *
		 * Example:
		 *  Properties:
		 *    config/ad_unixuser_attr = "unixname"
		 *    config/ad_unixgroup_attr = "unixname"
		 *  AD user object:
		 *    dn: cn=bob ...
		 *    objectclass: user
		 *    sam: bob
		 *    unixname: bob1234
		 *  AD group object:
		 *    dn: cn=winadmins ...
		 *    objectclass: group
		 *    sam: winadmins
		 *    unixname: unixadmins
		 *
		 *  In this example whether "unixname" refers to a unixuser
		 *  or unixgroup depends upon the AD object.
		 *
		 * $idmap show -c winname:bob gid
		 *    AD lookup by "samAccountName=bob" for
		 *    "ad_unixgroup_attr (i.e unixname)" for directory-based
		 *    mapping would get "bob1234" which is not what we want.
		 *    Now why not getgrnam_r("bob1234") and use it if it
		 *    is indeed a unixgroup? That's because Unix can have
		 *    users and groups with the same name and we clearly
		 *    don't know the intention of the admin here.
		 *    Therefore we ignore this and fallback to name-based
		 *    mapping rules or ephemeral mapping.
		 */
		if ((AD_MODE(rq->to.type, state) ||
		    MIXED_MODE(rq->to.type, state)) &&
		    state->ad_unixuser_attr != NULL &&
		    state->ad_unixgroup_attr != NULL &&
		    strcasecmp(state->ad_unixuser_attr,
		    state->ad_unixgroup_attr) == 0 &&
		    ((rq->from.type == IDMAP_USID &&
		    rq->to.type == IDMAP_GID) ||
		    (rq->from.type == IDMAP_GSID &&
		    rq->to.type == IDMAP_UID))) {
			TRACE(rq, "Ignoring UNIX name found in AD");
			free(rq->to.name);
			rq->to.name = NULL;
			/* fallback */
		} else {
			if (AD_MODE(rq->to.type, state))
				rq->direction = IDMAP_DIRECTION_BI;
			else if (NLDAP_MODE(rq->to.type, state))
				rq->direction = IDMAP_DIRECTION_BI;
			else if (MIXED_MODE(rq->to.type, state))
				rq->direction = IDMAP_DIRECTION_W2U;

			rq->retcode = ns_lookup_byname(rq->to.name,
			    NULL, &rq->to);
			switch (rq->retcode) {
			case IDMAP_SUCCESS:
				TRACE(rq, "UNIX lookup");
				break;
			case IDMAP_ERR_NOTFOUND:
				/*
				 * If ns_lookup_byname() fails that
				 * means the unixname (rq->to.name),
				 * which was obtained from the AD
				 * object by directory-based mapping,
				 * is not a valid Unix user/group and
				 * therefore we return the error to the
				 * client instead of doing rule-based
				 * mapping or ephemeral mapping. This
				 * way the client can detect the issue.
				 */
				TRACE(rq, "UNIX name not found");
				break;
			default:
				TRACE(rq, "UNIX lookup failed");
				break;
			}
			goto out;
		}
	}
	assert(rq->to.name == NULL);
	assert(rq->to.u.pid == IDMAP_SENTINEL_PID);

	/* Free any mapping info from Directory based mapping */
	if (rq->how.map_type != IDMAP_MAP_TYPE_UNKNOWN)
		idmap_how_clear(&rq->how);

	/*
	 * We don't have anything yet, so evaluate local name-based
	 * mapping rules.
	 */
	name_based_mapping_sid2pid(state, rq);
	if (rq->retcode != IDMAP_SUCCESS)
		goto out;

	if (rq->to.u.pid != IDMAP_SENTINEL_PID)
		goto out;

do_eph:
	/* If not found, do ephemeral mapping */
	dynamic_ephemeral_mapping(state, rq);

out:
	if (rq->retcode != IDMAP_SUCCESS) {
		rq->done = TRUE;
		rq->to.u.pid = UID_NOBODY;
	} else {
		/* Success so far, so there's more to do... */
		state->done = FALSE;
	}
}

static
void
update_cache_pid2sid(lookup_state_t *state, idmap_request_t *rq)
{
	char		*sql = NULL;
	char		*map_dn = NULL;
	char		*map_attr = NULL;
	char		*map_value = NULL;
	char 		*map_windomain = NULL;
	char		*map_winname = NULL;
	char		*map_unixname = NULL;
	int		map_is_nt4 = FALSE;

	/* Check if we need to cache anything */
	if (rq->done)
		return;

	/* We don't cache negative entries */
	if (rq->retcode != IDMAP_SUCCESS)
		return;

	assert(rq->direction != IDMAP_DIRECTION_UNDEF);
	assert(rq->from.u.pid != IDMAP_SENTINEL_PID);
	assert(rq->to.type != IDMAP_SID);

	assert(rq->how.map_type != IDMAP_MAP_TYPE_UNKNOWN);
	switch (rq->how.map_type) {
	case IDMAP_MAP_TYPE_DS_AD:
		map_dn = rq->how.idmap_how_u.ad.dn;
		map_attr = rq->how.idmap_how_u.ad.attr;
		map_value = rq->how.idmap_how_u.ad.value;
		break;

	case IDMAP_MAP_TYPE_DS_NLDAP:
		map_dn = rq->how.idmap_how_u.nldap.dn;
		map_attr = rq->how.idmap_how_u.nldap.attr;
		map_value = rq->how.idmap_how_u.nldap.value;
		break;

	case IDMAP_MAP_TYPE_RULE_BASED:
		map_windomain = rq->how.idmap_how_u.rule.windomain;
		map_winname = rq->how.idmap_how_u.rule.winname;
		map_unixname = rq->how.idmap_how_u.rule.unixname;
		map_is_nt4 = rq->how.idmap_how_u.rule.is_nt4;
		break;

	case IDMAP_MAP_TYPE_EPHEMERAL:
		break;

	case IDMAP_MAP_TYPE_LOCAL_SID:
		break;

	case IDMAP_MAP_TYPE_IDMU:
		map_dn = rq->how.idmap_how_u.idmu.dn;
		map_attr = rq->how.idmap_how_u.idmu.attr;
		map_value = rq->how.idmap_how_u.idmu.value;
		break;

	default:
		/* Don't cache other mapping types */
		assert(FALSE);
	}

	/*
	 * Using NULL for u2w instead of 0 so that our trigger allows
	 * the same pid to be the destination in multiple entries
	 */
	sql = sqlite_mprintf("INSERT OR REPLACE into idmap_cache "
	    "(sidprefix, rid, windomain, canon_winname, pid, unixname, "
	    "is_user, is_wuser, expiration, w2u, u2w, "
	    "map_type, map_dn, map_attr, map_value, map_windomain, "
	    "map_winname, map_unixname, map_is_nt4) "
	    "VALUES(%Q, %u, %Q, %Q, %u, %Q, %d, %d, "
	    "strftime('%%s','now') + 600, %q, 1, "
	    "%d, %Q, %Q, %Q, %Q, %Q, %Q, %d); ",
	    rq->to.u.sid.prefix, rq->to.u.sid.rid,
	    rq->to.domain, rq->to.name, rq->from.u.pid,
	    rq->from.name, (rq->from.type == IDMAP_UID) ? 1 : 0,
	    (rq->to.type == IDMAP_USID) ? 1 : 0,
	    (rq->direction == IDMAP_DIRECTION_BI) ? "1" : NULL,
	    rq->how.map_type, map_dn, map_attr, map_value,
	    map_windomain, map_winname, map_unixname, map_is_nt4);

	if (sql == NULL) {
		rq->retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Out of memory");
		return;
	}

	rq->retcode = sql_exec_no_cb(state->cache, IDMAP_CACHENAME, sql);
	sqlite_freemem(sql);
	sql = NULL;
	if (rq->retcode != IDMAP_SUCCESS)
		return;

	/* Check if we need to update namecache */
	if (rq->dont_update_namecache || rq->to.name == NULL)
		return;

	sql = sqlite_mprintf("INSERT OR REPLACE into name_cache "
	    "(sidprefix, rid, canon_name, domain, type, expiration) "
	    "VALUES(%Q, %u, %Q, %Q, %d, strftime('%%s','now') + 3600); ",
	    rq->to.u.sid.prefix, rq->to.u.sid.rid,
	    rq->to.name, rq->to.domain,
	    rq->to.type);

	if (sql == NULL) {
		rq->retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Out of memory");
		return;
	}

	rq->retcode = sql_exec_no_cb(state->cache, IDMAP_CACHENAME, sql);
	sqlite_freemem(sql);
	sql = NULL;
}

static
void
update_cache_sid2pid(lookup_state_t *state, idmap_request_t *rq)
{
	char		*sql = NULL;
	char		*map_dn = NULL;
	char		*map_attr = NULL;
	char		*map_value = NULL;
	char 		*map_windomain = NULL;
	char		*map_winname = NULL;
	char		*map_unixname = NULL;
	int		map_is_nt4 = FALSE;

	/* Check if we need to cache anything */
	if (rq->done)
		return;

	/* We don't cache negative entries */
	if (rq->retcode != IDMAP_SUCCESS)
		return;

	/*
	 * If there's an expired ephemeral mapping and we're replacing it
	 * with a real ID, invalidate the W->U part of the expired mapping.
	 */
	if (rq->expired.type != IDMAP_NONE &&
	    !IDMAP_ID_IS_EPHEMERAL(rq->to.u.pid)) {
		sql = sqlite_mprintf("UPDATE idmap_cache "
		    "SET w2u = 0 WHERE "
		    "sidprefix = %Q AND rid = %u AND w2u = 1 AND "
		    "pid >= 2147483648 AND is_user = %d;",
		    rq->from.u.sid.prefix,
		    rq->from.u.sid.rid,
		    rq->expired.type == IDMAP_UID);
		if (sql == NULL) {
			rq->retcode = IDMAP_ERR_INTERNAL;
			idmapdlog(LOG_ERR, "Out of memory");
			return;
		}

		rq->retcode =
		    sql_exec_no_cb(state->cache, IDMAP_CACHENAME, sql);
		sqlite_freemem(sql);
		sql = NULL;
		if (rq->retcode != IDMAP_SUCCESS)
			return;

	}

	assert(rq->direction != IDMAP_DIRECTION_UNDEF);
	assert(rq->to.u.pid != IDMAP_SENTINEL_PID);

	switch (rq->how.map_type) {
	case IDMAP_MAP_TYPE_DS_AD:
		map_dn = rq->how.idmap_how_u.ad.dn;
		map_attr = rq->how.idmap_how_u.ad.attr;
		map_value = rq->how.idmap_how_u.ad.value;
		break;

	case IDMAP_MAP_TYPE_DS_NLDAP:
		map_dn = rq->how.idmap_how_u.nldap.dn;
		map_attr = rq->how.idmap_how_u.ad.attr;
		map_value = rq->how.idmap_how_u.nldap.value;
		break;

	case IDMAP_MAP_TYPE_RULE_BASED:
		map_windomain = rq->how.idmap_how_u.rule.windomain;
		map_winname = rq->how.idmap_how_u.rule.winname;
		map_unixname = rq->how.idmap_how_u.rule.unixname;
		map_is_nt4 = rq->how.idmap_how_u.rule.is_nt4;
		break;

	case IDMAP_MAP_TYPE_EPHEMERAL:
		break;

	case IDMAP_MAP_TYPE_IDMU:
		map_dn = rq->how.idmap_how_u.idmu.dn;
		map_attr = rq->how.idmap_how_u.idmu.attr;
		map_value = rq->how.idmap_how_u.idmu.value;
		break;

	default:
		/* Don't cache other mapping types */
		assert(FALSE);
	}

	sql = sqlite_mprintf("INSERT OR REPLACE into idmap_cache "
	    "(sidprefix, rid, windomain, canon_winname, pid, unixname, "
	    "is_user, is_wuser, expiration, w2u, u2w, "
	    "map_type, map_dn, map_attr, map_value, map_windomain, "
	    "map_winname, map_unixname, map_is_nt4) "
	    "VALUES(%Q, %u, %Q, %Q, %u, %Q, %d, %d, "
	    "strftime('%%s','now') + 600, 1, %q, "
	    "%d, %Q, %Q, %Q, %Q, %Q, %Q, %d);",
	    rq->from.u.sid.prefix, rq->from.u.sid.rid,
	    (rq->from.domain != NULL) ? rq->from.domain : "", rq->from.name,
	    rq->to.u.pid, rq->to.name,
	    (rq->to.type == IDMAP_UID) ? 1 : 0,
	    (rq->from.type == IDMAP_USID) ? 1 : 0,
	    (rq->direction == IDMAP_DIRECTION_BI) ? "1" : NULL,
	    rq->how.map_type, map_dn, map_attr, map_value,
	    map_windomain, map_winname, map_unixname, map_is_nt4);

	if (sql == NULL) {
		rq->retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Out of memory");
		return;
	}

	rq->retcode = sql_exec_no_cb(state->cache, IDMAP_CACHENAME, sql);
	sqlite_freemem(sql);
	sql = NULL;
	if (rq->retcode != IDMAP_SUCCESS)
		return;


	/* Check if we need to update namecache */
	if (rq->dont_update_namecache || rq->from.name == NULL)
		return;

	sql = sqlite_mprintf("INSERT OR REPLACE into name_cache "
	    "(sidprefix, rid, canon_name, domain, type, expiration) "
	    "VALUES(%Q, %u, %Q, %Q, %d, strftime('%%s','now') + 3600); ",
	    rq->from.u.sid.prefix, rq->from.u.sid.rid,
	    rq->from.name, rq->from.domain,
	    rq->from.type);

	if (sql == NULL) {
		rq->retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "Out of memory");
		return;
	}

	rq->retcode = sql_exec_no_cb(state->cache, IDMAP_CACHENAME, sql);
	sqlite_freemem(sql);
	sql = NULL;
}

static
idmap_retcode
lookup_cache_pid2sid(sqlite *cache, idmap_request_t *rq)
{
	char		*end;
	char		*sql = NULL;
	const char	**values;
	sqlite_vm	*vm = NULL;
	int		ncol;
	idmap_retcode	retcode = IDMAP_SUCCESS;
	time_t		curtime;

	/* Current time */
	curtime = time(NULL);
	assert(curtime != (time_t)-1);

	/* SQL to lookup the cache by pid or by unixname */
	if (rq->from.u.pid != IDMAP_SENTINEL_PID) {
		sql = sqlite_mprintf("SELECT sidprefix, rid, "
		    "canon_winname, windomain, w2u, is_wuser, "
		    "map_type, map_dn, map_attr, map_value, map_windomain, "
		    "map_winname, map_unixname, map_is_nt4 "
		    "FROM idmap_cache WHERE "
		    "pid = %u AND u2w = 1 AND is_user = %d AND "
		    "(pid >= 2147483648 OR "
		    "(expiration = 0 OR expiration ISNULL OR "
		    "expiration > %d));",
		    rq->from.u.pid, rq->from.type == IDMAP_UID, curtime);
	} else if (rq->from.name != NULL) {
		sql = sqlite_mprintf("SELECT sidprefix, rid, "
		    "canon_winname, windomain, w2u, is_wuser, "
		    "map_type, map_dn, map_attr, map_value, map_windomain, "
		    "map_winname, map_unixname, map_is_nt4 "
		    "FROM idmap_cache WHERE "
		    "unixname = %Q AND u2w = 1 AND is_user = %d AND "
		    "(pid >= 2147483648 OR "
		    "(expiration = 0 OR expiration ISNULL OR "
		    "expiration > %d));",
		    rq->from.name, rq->from.type == IDMAP_UID, curtime);
	} else {
		retcode = IDMAP_ERR_ARG;
		goto out;
	}

	if (sql == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		retcode = IDMAP_ERR_MEMORY;
		goto out;
	}
	retcode = sql_compile_n_step_once(
	    cache, sql, &vm, &ncol, 14, &values);
	sqlite_freemem(sql);

	if (retcode == IDMAP_ERR_NOTFOUND)
		goto out;
	else if (retcode == IDMAP_SUCCESS) {
		idmap_id_type	idtype;

		/* sanity checks */
		if (values[0] == NULL || values[1] == NULL) {
			retcode = IDMAP_ERR_CACHE;
			goto out;
		}

		idtype = atol(values[5]) != 0 ? IDMAP_USID : IDMAP_GSID;
		if (rq->to.type == IDMAP_USID && idtype != IDMAP_USID) {
			retcode = IDMAP_ERR_NOTUSER;
			goto out;
		} else if (rq->to.type == IDMAP_GSID && idtype != IDMAP_GSID) {
			retcode = IDMAP_ERR_NOTGROUP;
			goto out;
		}
		rq->to.type = idtype;

		rq->to.u.sid.rid = strtoul(values[1], &end, 10);
		rq->to.u.sid.prefix = strdup(values[0]);
		if (rq->to.u.sid.prefix == NULL) {
			idmapdlog(LOG_ERR, "Out of memory");
			retcode = IDMAP_ERR_MEMORY;
			goto out;
		}

		if (values[4] != NULL && atol(values[4]) != 0)
			rq->direction = IDMAP_DIRECTION_BI;
		else
			rq->direction = IDMAP_DIRECTION_U2W;

		if (values[2] != NULL) {
			rq->to.name = strdup(values[2]);
			if (rq->to.name == NULL) {
				idmapdlog(LOG_ERR, "Out of memory");
				retcode = IDMAP_ERR_MEMORY;
				goto out;
			}
		}

		if (values[3] != NULL) {
			rq->to.domain = strdup(values[3]);
			if (rq->to.domain == NULL) {
				idmapdlog(LOG_ERR, "Out of memory");
				retcode = IDMAP_ERR_MEMORY;
				goto out;
			}
		}

		if (rq->do_how) {
			rq->src = IDMAP_MAP_SRC_CACHE;
			rq->how.map_type = atol(values[6]);
			switch (rq->how.map_type) {
			case IDMAP_MAP_TYPE_DS_AD:
				rq->how.idmap_how_u.ad.dn =
				    strdup(values[7]);
				rq->how.idmap_how_u.ad.attr =
				    strdup(values[8]);
				rq->how.idmap_how_u.ad.value =
				    strdup(values[9]);
				break;

			case IDMAP_MAP_TYPE_DS_NLDAP:
				rq->how.idmap_how_u.nldap.dn =
				    strdup(values[7]);
				rq->how.idmap_how_u.nldap.attr =
				    strdup(values[8]);
				rq->how.idmap_how_u.nldap.value =
				    strdup(values[9]);
				break;

			case IDMAP_MAP_TYPE_RULE_BASED:
				rq->how.idmap_how_u.rule.windomain =
				    strdup(values[10]);
				rq->how.idmap_how_u.rule.winname =
				    strdup(values[11]);
				rq->how.idmap_how_u.rule.unixname =
				    strdup(values[12]);
				rq->how.idmap_how_u.rule.is_nt4 =
				    atol(values[13]);
				rq->how.idmap_how_u.rule.is_user =
				    rq->from.type == IDMAP_UID;
				rq->how.idmap_how_u.rule.is_wuser =
				    atol(values[5]);
				break;

			case IDMAP_MAP_TYPE_EPHEMERAL:
				break;

			case IDMAP_MAP_TYPE_LOCAL_SID:
				break;

			case IDMAP_MAP_TYPE_KNOWN_SID:
				break;

			case IDMAP_MAP_TYPE_IDMU:
				rq->how.idmap_how_u.idmu.dn =
				    strdup(values[7]);
				rq->how.idmap_how_u.idmu.attr =
				    strdup(values[8]);
				rq->how.idmap_how_u.idmu.value =
				    strdup(values[9]);
				break;

			default:
				/* Unknown mapping type */
				assert(FALSE);
			}
		}
	}

out:
	if (vm != NULL)
		(void) sqlite_finalize(vm, NULL);
	return (retcode);
}

/*
 * Given:
 * cache	sqlite handle
 * name		Windows user name
 * domain	Windows domain name
 *
 * Return:  Error code
 *
 * *canonname	Canonical name (if canonname is non-NULL) [1]
 * *sidprefix	SID prefix [1]
 * *rid		RID
 * *type	Type of name
 *
 * [1] malloc'ed, NULL on error
 */
static
idmap_retcode
lookup_cache_name2sid(
    sqlite *cache,
    const char *name,
    const char *domain,
    char **canonname,
    char **sidprefix,
    idmap_rid_t *rid,
    idmap_id_type *type)
{
	char		*end, *lower_name;
	char		*sql;
	const char	**values;
	sqlite_vm	*vm = NULL;
	int		ncol;
	time_t		curtime;
	idmap_retcode	retcode;

	*sidprefix = NULL;
	if (canonname != NULL)
		*canonname = NULL;

	/* Get current time */
	errno = 0;
	if ((curtime = time(NULL)) == (time_t)-1) {
		idmapdlog(LOG_ERR, "Failed to get current time (%s)",
		    strerror(errno));
		retcode = IDMAP_ERR_INTERNAL;
		goto out;
	}

	/* SQL to lookup the cache */
	if ((lower_name = tolower_u8(name)) == NULL)
		lower_name = (char *)name;
	sql = sqlite_mprintf("SELECT sidprefix, rid, type, canon_name "
	    "FROM name_cache WHERE name = %Q AND domain = %Q AND "
	    "(expiration = 0 OR expiration ISNULL OR "
	    "expiration > %d);", lower_name, domain, curtime);
	if (lower_name != name)
		free(lower_name);
	if (sql == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		retcode = IDMAP_ERR_MEMORY;
		goto out;
	}
	retcode = sql_compile_n_step_once(cache, sql, &vm, &ncol, 4, &values);

	sqlite_freemem(sql);

	if (retcode != IDMAP_SUCCESS)
		goto out;

	if (type != NULL) {
		if (values[2] == NULL) {
			retcode = IDMAP_ERR_CACHE;
			goto out;
		}
		*type = xlate_legacy_type(atol(values[2]));
	}

	if (values[0] == NULL || values[1] == NULL) {
		retcode = IDMAP_ERR_CACHE;
		goto out;
	}

	if (canonname != NULL) {
		assert(values[3] != NULL);
		*canonname = strdup(values[3]);
		if (*canonname == NULL) {
			idmapdlog(LOG_ERR, "Out of memory");
			retcode = IDMAP_ERR_MEMORY;
			goto out;
		}
	}

	*sidprefix = strdup(values[0]);
	if (*sidprefix == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		retcode = IDMAP_ERR_MEMORY;
		goto out;
	}
	*rid = strtoul(values[1], &end, 10);

	retcode = IDMAP_SUCCESS;

out:
	if (vm != NULL)
		(void) sqlite_finalize(vm, NULL);

	if (retcode != IDMAP_SUCCESS) {
		free(*sidprefix);
		*sidprefix = NULL;
		if (canonname != NULL) {
			free(*canonname);
			*canonname = NULL;
		}
	}
	return (retcode);
}

/*
 * Given:
 * cache	sqlite handle to cache
 * name		Windows user name
 * domain	Windows domain name
 * local_only	if true, don't try AD lookups
 *
 * Returns: Error code
 *
 * *canonname	Canonical name (if non-NULL) [1]
 * *canondomain	Canonical domain (if non-NULL) [1]
 * *sidprefix	SID prefix [1]
 * *rid		RID
 * *req		Request (direction is updated)
 *
 * [1] malloc'ed, NULL on error
 */
idmap_retcode
lookup_name2sid(
    lookup_state_t *state,
    const char *name,
    const char *domain,
    idmap_id_type want_type,
    char **canonname,
    char **canondomain,
    char **sidprefix,
    idmap_rid_t *rid,
    idmap_id_type *type,
    idmap_request_t *rq,
    int local_only)
{
	idmap_retcode	retcode;

	*sidprefix = NULL;
	if (canonname != NULL)
		*canonname = NULL;
	if (canondomain != NULL)
		*canondomain = NULL;

	/* Lookup well-known SIDs table */
	retcode = lookup_wksids_name2sid(name, domain, canonname, canondomain,
	    sidprefix, rid, type);
	if (retcode == IDMAP_SUCCESS) {
		rq->dont_update_namecache = TRUE;
		goto out;
	} else if (retcode != IDMAP_ERR_NOTFOUND) {
		return (retcode);
	}

	/* Lookup cache */
	retcode = lookup_cache_name2sid(state->cache, name, domain, canonname,
	    sidprefix, rid, type);
	if (retcode == IDMAP_SUCCESS) {
		rq->dont_update_namecache = TRUE;
		goto out;
	} else if (retcode != IDMAP_ERR_NOTFOUND) {
		return (retcode);
	}

	/*
	 * The caller may be using this function to determine if this
	 * request needs to be marked for AD lookup or not
	 * (i.e. _IDMAP_F_LOOKUP_AD) and therefore may not want this
	 * function to AD lookup now.
	 */
	if (local_only)
		return (retcode);

	if (state->domain_name == NULL)
		return (IDMAP_ERR_NOTFOUND);

	retcode = lookup_lsa_by_name(name, domain,
	    sidprefix, rid,
	    canonname, canondomain,
	    type);
	if (retcode != IDMAP_SUCCESS)
		return (retcode);

out:
	/*
	 * Entry found (cache or Windows lookup)
	 */
	if (want_type == IDMAP_USID && *type != IDMAP_USID)
		retcode = IDMAP_ERR_NOTUSER;
	else if (want_type == IDMAP_GSID && *type != IDMAP_GSID)
		retcode = IDMAP_ERR_NOTGROUP;
	else if (want_type == IDMAP_SID) {
		/*
		 * Caller wants to know if its user or group
		 * Verify that it's one or the other.
		 */
		if (*type != IDMAP_USID && *type != IDMAP_GSID)
			retcode = IDMAP_ERR_SID;
	}

	if (retcode == IDMAP_SUCCESS) {
		/*
		 * If we were asked for a canonical domain and none
		 * of the searches have provided one, assume it's the
		 * supplied domain.
		 */
		if (canondomain != NULL && *canondomain == NULL) {
			*canondomain = strdup(domain);
			if (*canondomain == NULL)
				retcode = IDMAP_ERR_MEMORY;
		}
	}
	if (retcode != IDMAP_SUCCESS) {
		free(*sidprefix);
		*sidprefix = NULL;
		if (canonname != NULL) {
			free(*canonname);
			*canonname = NULL;
		}
		if (canondomain != NULL) {
			free(*canondomain);
			*canondomain = NULL;
		}
	}
	return (retcode);
}

static
void
name_based_mapping_pid2sid(lookup_state_t *state, idmap_request_t *rq)
{
	char		*sql = NULL, *errmsg = NULL;
	const char	**values;
	sqlite_vm	*vm = NULL;
	int		ncol, r;
	const char	*me = "name_based_mapping_pid2sid";
	idmap_namerule	*rule = &rq->how.idmap_how_u.rule;

	assert(rq->from.name != NULL); /* We have unixname */
	assert(rq->to.name == NULL); /* We don't have winname */
	assert(rq->to.u.sid.prefix == NULL); /* No SID either */

	sql = sqlite_mprintf(
	    "SELECT winname_display, windomain, w2u_order, "
	    "is_wuser, unixname, is_nt4 "
	    "FROM namerules WHERE "
	    "u2w_order > 0 AND is_user = %d AND "
	    "(unixname = %Q OR unixname = '*') "
	    "ORDER BY u2w_order ASC;",
	    rq->from.type == IDMAP_UID, rq->from.name);
	if (sql == NULL) {
		idmapdlog(LOG_ERR, "Out of memory");
		rq->retcode = IDMAP_ERR_MEMORY;
		goto out;
	}

	r = sqlite_compile(state->db, sql, NULL, &vm, &errmsg);
	sqlite_freemem(sql);
	sql = NULL;
	if (r != SQLITE_OK) {
		rq->retcode = IDMAP_ERR_INTERNAL;
		idmapdlog(LOG_ERR, "%s: database error (%s)", me,
		    CHECK_NULL(errmsg));
		sqlite_freemem(errmsg);
		goto out;
	}

	for (;;) {
		r = sqlite_step(vm, &ncol, &values, NULL);
		assert(r != SQLITE_LOCKED && r != SQLITE_BUSY);

		if (r == SQLITE_ROW) {
			char *canonname;
			char *canondomain;

			if (ncol < 6) {
				rq->retcode = IDMAP_ERR_INTERNAL;
				goto out;
			}

			TRACE(rq, "Matching rule: %s -> %s@%s",
			    values[4] == NULL ? "(null)" : values[4],
			    values[0] == NULL ? "(null)" : values[0],
			    values[1] == NULL ? "(null)" : values[1]);

			if (values[0] == NULL) {
				/* values [1] and [2] can be null */
				rq->retcode = IDMAP_ERR_INTERNAL;
				goto out;
			}

			if (EMPTY_NAME(values[0])) {
				rq->retcode = IDMAP_ERR_NOMAPPING;
				TRACE(rq, "Mapping inhibited");
				break;
			}

			if (values[0][0] == '*') {
				rq->to.name = strdup(rq->from.name);
			} else {
				rq->to.name = strdup(values[0]);
			}
			if (rq->to.name == NULL) {
				rq->retcode = IDMAP_ERR_MEMORY;
				break;
			}

			if (values[1] != NULL)
				rq->to.domain = strdup(values[1]);
			else if (state->defdom != NULL) {
				rq->to.domain = strdup(state->defdom);
				TRACE(rq,
				    "Added default domain to rule");
			} else {
				idmapdlog(LOG_ERR, "%s: no domain", me);
				TRACE(rq,
				    "No domain in rule, and no default domain");
				rq->retcode = IDMAP_ERR_DOMAIN_NOTFOUND;
				break;
			}
			if (rq->to.domain == NULL) {
				rq->retcode = IDMAP_ERR_MEMORY;
				break;
			}

			rq->retcode = lookup_name2sid(state,
			    rq->to.name, rq->to.domain,
			    rq->to.type,
			    &canonname, &canondomain,
			    &rq->to.u.sid.prefix, &rq->to.u.sid.rid,
			    &rq->to.type, rq, 0);

			if (rq->retcode == IDMAP_SUCCESS) {
				free(rq->to.name);
				rq->to.name = canonname;
				canonname = NULL;

				free(rq->to.domain);
				rq->to.domain = canondomain;
				canondomain = NULL;

				TRACE(rq, "Windows name found");
			} else if (rq->retcode == IDMAP_ERR_NOTFOUND) {
				if (values[0][0] == '*') {
					/*
					 * "Not found" on a wild card is not
					 * an error.  Just forget we tried,
					 * and go on.
					 */
					rq->retcode = IDMAP_SUCCESS;
					TRACE(rq, "Not found, continuing");
					free(rq->to.name);
					rq->to.name = NULL;
					free(rq->to.domain);
					rq->to.domain = NULL;
					continue;
				}
				rq->retcode = IDMAP_ERR_NOMAPPING;
				TRACE(rq, "Not found");
			} else {
				TRACE(rq, "Windows name lookup failed");
			}
			break;

		} else if (r == SQLITE_DONE) {
			TRACE(rq, "No matching rule");
			goto out;
		} else {
			(void) sqlite_finalize(vm, &errmsg);
			vm = NULL;
			idmapdlog(LOG_ERR, "%s: database error (%s)", me,
			    CHECK_NULL(errmsg));
			sqlite_freemem(errmsg);
			rq->retcode = IDMAP_ERR_INTERNAL;
			goto out;
		}
	}

	/* Found (with or without error) */

	if (values[2] != NULL && atol(values[2]) != 0)
		rq->direction = IDMAP_DIRECTION_BI;
	else
		rq->direction = IDMAP_DIRECTION_U2W;

	idmap_namerule_set(rule, values[1], values[0], values[4],
	    rq->from.type == IDMAP_UID, atol(values[3]),
	    atol(values[5]),
	    rq->direction);

	rq->how.map_type = IDMAP_MAP_TYPE_RULE_BASED;
	rq->src = IDMAP_MAP_SRC_NEW;

out:
	if (vm != NULL)
		(void) sqlite_finalize(vm, NULL);
}

/*
 * Convention when processing unix2win requests:
 *
 * Unix identity:
 * rq->from.name =
 *              unixname if given otherwise unixname found will be placed
 *              here.
 * rq->from.domain =
 *              NOT USED
 * rq->from.idtype =
 *              Given type (IDMAP_UID or IDMAP_GID)
 * rq->from.u.pid =
 *              UID/GID if given otherwise UID/GID found will be placed here.
 *
 * Windows identity:
 * rq->to.name =
 *              winname found will be placed here.
 * rq->to.domain =
 *              windomain found will be placed here.
 * rq->to.type =
 *              Target type. If the request specified IDMAP_SID then the
 *              actual type (IDMAP_USID/GSID) found will be placed here.
 * req->to.u.sid =
 *              SID found will be placed here.
 *
 * Others:
 * rq->retcode =
 *              Return status for this request will be placed here.
 * rq->direction =
 *              Direction found will be placed here. Direction
 *              meaning whether the resultant mapping is valid
 *              only from unix2win or bi-directional.
 */

/*
 * This function does the following:
 * 1. Lookup well-known SIDs table.
 * 2. Lookup cache.
 * 3. Check if the client does not want new mapping to be allocated
 *    in which case this pass is the final pass.
 * 4. Set AD/NLDAP lookup flags if it determines that the next stage needs
 *    to do AD/NLDAP lookup.
 */
static
void
pid2sid_first_pass(lookup_state_t *state, idmap_request_t *rq)
{
	idmap_retcode	retcode;
	idmap_retcode	retcode2;
	bool_t		gen_localsid_on_err = FALSE;

	validate_get_mapped_ids_pid2sid(rq);
	if (rq->retcode != IDMAP_SUCCESS)
		return;

	/* Initialize result */
	rq->direction = IDMAP_DIRECTION_UNDEF;

	if (rq->to.u.sid.prefix != NULL) {
		/* sanitize sidprefix */
		free(rq->to.u.sid.prefix);
		rq->to.u.sid.prefix = NULL;
	}

	/* Find pid */
	if (rq->from.u.pid == IDMAP_SENTINEL_PID) {
		if (rq->from.name == NULL) {
			retcode = IDMAP_ERR_ARG;
			goto out;
		}

		retcode = ns_lookup_byname(rq->from.name, NULL, &rq->from);
		if (retcode != IDMAP_SUCCESS) {
			TRACE(rq, "Getting UNIX ID error=%d", retcode);
			retcode = IDMAP_ERR_NOMAPPING;
			goto out;
		}
		TRACE(rq, "Found UNIX ID");
	}

	/* Lookup in well-known SIDs table */
	retcode = lookup_wksids_pid2sid(rq);
	if (retcode == IDMAP_SUCCESS) {
		TRACE(rq, "Hardwired mapping");
		goto out;
	} else if (retcode != IDMAP_ERR_NOTFOUND) {
		TRACE(rq,
		    "Well-known account lookup error=%d", retcode);
		goto out;
	}

	/* Lookup in cache */
	retcode = lookup_cache_pid2sid(state->cache, rq);
	if (retcode == IDMAP_SUCCESS) {
		TRACE(rq, "Found in mapping cache");
		goto out;
	} else if (retcode != IDMAP_ERR_NOTFOUND) {
		TRACE(rq,
		    "Mapping cache lookup error=%d", retcode);
		goto out;
	}
	TRACE(rq, "Not found in mapping cache");

	/* Ephemeral ids cannot be allocated during pid2sid */
	if (IDMAP_ID_IS_EPHEMERAL(rq->from.u.pid)) {
		retcode = IDMAP_ERR_NOMAPPING;
		TRACE(rq, "Unallocated ephemeral ID");
		goto out;
	}

	if (rq->no_new_id_alloc) {
		retcode = IDMAP_ERR_NONE_GENERATED;
		goto out;
	}

	if (rq->no_nameservice) {
		gen_localsid_on_err = TRUE;
		retcode = IDMAP_ERR_NOMAPPING;
		goto out;
	}

	if (rq->from.name == NULL) {
		/* Get unixname if only pid is given. */
		assert(rq->from.u.pid != IDMAP_SENTINEL_PID);
		retcode = ns_lookup_bypid(rq->from.u.pid,
		    rq->from.type == IDMAP_UID, &rq->from.name);
		switch (retcode) {
		case IDMAP_SUCCESS:
			TRACE(rq, "Found UNIX name");
			break;
		case IDMAP_ERR_NOTFOUND:
			TRACE(rq, "UNIX name not found");
			break;
		default:
			TRACE(rq,
			    "Getting UNIX name error=%d", retcode);
			gen_localsid_on_err = TRUE;
			goto out;
		}
	}

	/* Set flags for the next stage */
	if (state->directory_based_mapping == DIRECTORY_MAPPING_IDMU) {
		rq->do_ad_lookup = TRUE;
		state->ad_nqueries++;
	} else if (AD_MODE(rq->from.type, state)) {
		if (rq->from.name != NULL) {
			rq->do_ad_lookup = TRUE;
			state->ad_nqueries++;
		}
	} else if (NLDAP_OR_MIXED_MODE(rq->from.type, state)) {
		/*
		 * If native LDAP or mixed mode is enabled for name mapping
		 * then the next stage will need to lookup native LDAP using
		 * unixname/pid to get the corresponding winname.
		 */
		rq->do_nldap_lookup = TRUE;
		state->nldap_nqueries++;
	}

	/*
	 * Failed to find non-expired entry in cache. Set the flag to
	 * indicate that we are not done yet.
	 */
	state->done = FALSE;
	rq->done = FALSE;
	retcode = IDMAP_SUCCESS;

out:
	rq->retcode = retcode;
	if (rq->done && rq->retcode != IDMAP_SUCCESS) {
		if (gen_localsid_on_err == TRUE) {
			retcode2 = generate_localsid(rq, TRUE);
			if (retcode2 == IDMAP_SUCCESS)
				TRACE(rq, "Generate local SID");
			else
				TRACE(rq,
				    "Generate local SID error=%d", retcode2);
		}
	}
}

static
void
pid2sid_second_pass(lookup_state_t *state, idmap_request_t *rq)
{
	bool_t		gen_localsid_on_err = TRUE;

	/* Check if second pass is needed */
	if (rq->done)
		return;

	/* Get status from previous pass */
	if (rq->retcode != IDMAP_SUCCESS)
		goto out;

	/*
	 * If directory-based name mapping is enabled then the winname
	 * may already have been retrieved from the AD object (AD-mode)
	 * or from native LDAP object (nldap-mode or mixed-mode).
	 * Note that if we have winname but no SID then it's an error
	 * because this implies that the Native LDAP entry contains
	 * winname which does not exist and it's better that we return
	 * an error instead of doing rule-based mapping so that the user
	 * can detect the issue and take appropriate action.
	 */
	if (rq->to.name != NULL) {
		/* Return notfound if we've winname but no SID. */
		if (rq->to.u.sid.prefix == NULL) {
			TRACE(rq, "Windows name but no SID");
			rq->retcode = IDMAP_ERR_NOTFOUND;
			goto out;
		}
		if (state->directory_based_mapping == DIRECTORY_MAPPING_IDMU)
			rq->direction = IDMAP_DIRECTION_BI;
		else if (AD_MODE(rq->from.type, state))
			rq->direction = IDMAP_DIRECTION_BI;
		else if (NLDAP_MODE(rq->from.type, state))
			rq->direction = IDMAP_DIRECTION_BI;
		else if (MIXED_MODE(rq->from.type, state))
			rq->direction = IDMAP_DIRECTION_U2W;
		goto out;
	} else if (rq->to.u.sid.prefix != NULL) {
		/*
		 * We've SID but no winname. This is fine because
		 * the caller may have only requested SID.
		 */
		goto out;
	}

	/* Free any mapping info from Directory based mapping */
	if (rq->how.map_type != IDMAP_MAP_TYPE_UNKNOWN)
		idmap_how_clear(&rq->how);

	if (rq->from.name != NULL) {
		/* Use unixname to evaluate local name-based mapping rules */
		name_based_mapping_pid2sid(state, rq);
		if (rq->retcode != IDMAP_SUCCESS)
			goto out;
	}

	if (rq->to.u.sid.prefix == NULL) {
		rq->retcode = generate_localsid(rq, FALSE);
		if (rq->retcode == IDMAP_SUCCESS)
			TRACE(rq, "Generated local SID");
		else
			TRACE(rq, "Failed to generate local SID");
		gen_localsid_on_err = FALSE;
	}

out:
	if (rq->retcode != IDMAP_SUCCESS) {
		rq->done = TRUE;
		free(rq->to.name);
		rq->to.name = NULL;
		free(rq->to.domain);
		rq->to.domain = NULL;
		if (gen_localsid_on_err == TRUE) {
			idmap_retcode retcode2 = generate_localsid(rq, TRUE);
			if (retcode2 == IDMAP_SUCCESS)
				TRACE(rq, "Generate local SID");
			else
				TRACE(rq,
				    "Generate local SID error=%d", retcode2);
		} else {
			if (rq->from.type == IDMAP_UID)
				rq->to.type = IDMAP_USID;
			else
				rq->to.type = IDMAP_GSID;
		}
	} else {
		/* Success so far, so we still have more to do. */
		state->done = FALSE;
	}
}

idmap_retcode
idmap_cache_flush(idmap_flush_op op)
{
	idmap_retcode	rc;
	sqlite *cache = NULL;
	char *sql1;
	char *sql2;

	switch (op) {
	case IDMAP_FLUSH_EXPIRE:
		sql1 =
		    "UPDATE idmap_cache SET expiration=1 WHERE expiration>0;";
		sql2 =
		    "UPDATE name_cache SET expiration=1 WHERE expiration>0;";
		break;

	case IDMAP_FLUSH_DELETE:
		sql1 = "DELETE FROM idmap_cache;";
		sql2 = "DELETE FROM name_cache;";
		break;

	default:
		return (IDMAP_ERR_INTERNAL);
	}

	rc = get_cache_handle(&cache);
	if (rc != IDMAP_SUCCESS)
		return (rc);

	/*
	 * Note that we flush the idmapd cache first, before the kernel
	 * cache.  If we did it the other way 'round, a request could come
	 * in after the kernel cache flush and pull a soon-to-be-flushed
	 * idmapd cache entry back into the kernel cache.  This way the
	 * worst that will happen is that a new entry will be added to
	 * the kernel cache and then immediately flushed.
	 */

	rc = sql_exec_no_cb(cache, IDMAP_CACHENAME, sql1);
	if (rc != IDMAP_SUCCESS)
		return (rc);

	rc = sql_exec_no_cb(cache, IDMAP_CACHENAME, sql2);

	(void) __idmap_flush_kcache();
	return (rc);
}
